"""Host-only experimental loading signature; never chip identity or DAC health."""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import uuid

import probe

ROOT = Path(__file__).resolve().parents[2]
PROFILE_PATH = Path(__file__).with_name('loading_profile.json')
PROFILE = json.loads(PROFILE_PATH.read_text(encoding='utf-8'))
FIRMWARE_ID = 'a9179c5f093605fd51845dbfa8c064604089083de9e3930fcb4805c6504fb805'
CLASSES = ('PT8211_LIKE', 'UNLOADED')
FEATURES = tuple(f'pin{p}_target{t}' for p in (7, 20, 21) for t in (0, 1))


def unknown(reason):
    return {'prediction': 'UNKNOWN', 'reasons': [reason], 'features': {},
            'profile_id': PROFILE['profile_id'], 'integrity_verified': False}


def classify_records(records):
    """Pure measurement decision. Use classify_run for validated on-disk captures.

    No manual attachment label is accepted or consulted here. Margins are cycle
    distances to inclusive acceptance boundaries, not confidence probabilities.
    """
    result = unknown('')
    result['reasons'] = []
    expected = PROFILE['firmware']
    if (PROFILE['profile_version'] != 1 or expected['cpu_hz'] != 600000000
            or expected['source_sha256'] != FIRMWARE_ID or expected['schema'] != 2):
        return unknown('Incompatible calibration profile')
    if not records or records[0] != expected:
        return unknown('Incompatible firmware identity or measurement parameters')
    if len(records) > 4582 or len(records) < 487:
        return unknown('Missing/extra measurement records')
    try:
        if type(records[-1]['intervals']) is not int or not 0 <= records[-1]['intervals'] <= 4096:
            return unknown('Invalid cadence count')
        probe.validate(records)
        for r in records:
            if r['type'] != 'pin':
                continue
            if any(type(r[field]) is not int for field in ('pin','target','initial','final','bits','cycles')):
                return unknown('Invalid pin measurement type')
            if type(r['timeout']) is not bool:
                return unknown('Invalid timeout flag')
            if r['timeout']:
                return unknown('Timed-out pin trial')
            if (r['initial'] != 1-r['target'] or r['final'] != r['target']
                    or r['bits'] != (255 if r['target'] else 0)):
                return unknown('Inconsistent digital observations or transition initial/final state')
            if not 0 <= r['cycles'] < 60000:
                return unknown('Out-of-profile transition timing')
    except (KeyError, ValueError, TypeError, IndexError):
        return unknown('Incomplete or malformed measurements')

    votes = {}
    for pin in (7, 20, 21):
        for target in (0, 1):
            key = f'pin{pin}_target{target}'
            values = [r['cycles'] for r in records if r['type']=='pin' and r['pin']==pin and r['target']==target]
            if len(values) != 64:
                return unknown('Missing/extra pin trials')
            stats = probe.distribution(values)
            matches = []
            margins = {}
            for label in CLASSES:
                band = PROFILE['bands'][key][label]
                constraints = {
                    'median': min(stats['median']-band['median'][0], band['median'][1]-stats['median']),
                    'central': min(stats['p05']-band['central'][0], band['central'][1]-stats['p95']),
                    'trial': min(stats['min']-band['trial'][0], band['trial'][1]-stats['max'])}
                constraints['minimum'] = min(constraints.values())
                margins[label] = constraints
                if constraints['minimum'] >= 0:
                    matches.append(label)
            votes[key] = matches[0] if len(matches)==1 else 'UNKNOWN'
            result['features'][key] = {'role': 'primary' if target==0 else 'cross-check',
                'statistics_cycles': stats, 'evidence': votes[key], 'class_margins_cycles': margins}

    primary = [votes[f'pin{p}_target0'] for p in (7,20,21)]
    if len(set(primary)) != 1 or primary[0] not in CLASSES:
        result['reasons'] = ['Pull-down primary gates disagree or are outside calibrated bands']
        return result
    candidate = primary[0]
    if any(votes[f'pin{p}_target1'] != candidate for p in (7,20,21)):
        result['reasons'] = ['Pull-up cross-checks do not unanimously confirm pull-down evidence']
        return result
    result['prediction'] = candidate
    for role, keys in (('primary', [f'pin{p}_target0' for p in (7,20,21)]),
                       ('cross_check', [f'pin{p}_target1' for p in (7,20,21)])):
        result[role+'_margin_cycles'] = min(result['features'][k]['class_margins_cycles'][candidate]['minimum'] for k in keys)
    result['minimum_margin_cycles'] = min(result['primary_margin_cycles'], result['cross_check_margin_cycles'])
    return result


def classify_run(path):
    """Require exactly one integrity-validated protocol-2 capture directory."""
    path = Path(path)
    if not (path/'manifest.json').is_file():
        return unknown('Expected one explicit capture directory')
    try:
        _, manifest, _, _ = probe.load_runs(path)[0]
        if manifest.get('wire_protocol') != 2:
            return unknown('Incompatible wire protocol')
        if manifest.get('manual_usb_isolator') not in PROFILE['supported_isolator_labels']:
            return unknown('USB topology outside calibration')
        decoder = probe.Decoder()
        records = [decoder.decode(line)[0] for line in (path/'raw.jsonl').read_bytes().splitlines(keepends=True)]
        result = classify_records(records)
        result['integrity_verified'] = True
        return result
    except (OSError, ValueError, KeyError, TypeError, IndexError) as error:
        return unknown('Capture integrity/format failure: '+str(error))


def evaluate(output_dir):
    """In-sample evaluation ONLY of the frozen profile's 13 calibration paths."""
    report_path = ROOT/PROFILE['calibration_report']
    raw = report_path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != PROFILE['calibration_report_sha256']:
        raise ValueError('Calibration report fingerprint mismatch')
    source = json.loads(raw)
    if [r['directory'] for r in source['runs']] != PROFILE['selected_run_paths']:
        raise ValueError('Selected calibration runs changed')
    evaluations = []
    for selected in source['runs']:
        path = ROOT/selected['directory']
        result = classify_run(path)
        manifest = json.loads((path/'manifest.json').read_text())
        if manifest['manual_hardware_label'] != selected['condition'] or manifest['manual_usb_isolator'] != selected['isolator']:
            raise ValueError('Selected manual labels changed')
        if manifest['raw_sha256'] != selected['manifest']['raw_sha256']:
            raise ValueError('Selected calibration capture fingerprint changed')
        expected = {'attached':'PT8211_LIKE', 'detached':'UNLOADED'}[selected['condition']]
        evaluations.append({'directory': selected['directory'], 'manual_label': selected['condition'],
            'isolator': selected['isolator'], 'boot': selected['boot'], 'expected_signature': expected,
            'matches_manual_label': result['prediction']==expected, **result})
    output = {'created_utc': datetime.now(timezone.utc).isoformat(),
        'profile': PROFILE, 'profile_sha256': hashlib.sha256(PROFILE_PATH.read_bytes()).hexdigest(),
        'evaluation_scope': 'In-sample calibration replay, not independent accuracy validation. Manual labels used only for scoring after classification.',
        'predictions': dict(Counter(r['prediction'] for r in evaluations)),
        'matched': sum(r['matches_manual_label'] for r in evaluations), 'run_count':len(evaluations),
        'runs': evaluations,
        'limits': ['Experimental electrical loading signature of this exact Teensy/module/wiring and firmware, not PT8211 chip identity, power or DAC health.',
                   'Unknown third-party loads, other boards, wiring lengths, temperature/supply drift and other firmware versions are not validated.',
                   'Eight-cycle engineering padding is not a confidence interval; false-positive/false-negative rates outside these calibration runs are unknown.',
                   'Audio Shield recognition remains pending physical hardware. Manual override and production integration remain future work.']}
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    name = 'pt8211_loading_evaluation_'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+uuid.uuid4().hex[:8]
    target = output_dir/name
    probe.write_json(target.with_suffix('.json'), output)
    lines = ['# Experimental PT8211 loading classifier evaluation', '', output['evaluation_scope'], '',
             '| Run | Manual | Predicted | Primary / cross-check / minimum margin, cycles |', '|---|---|---|---|']
    for r in evaluations:
        margins = ' / '.join(str(r.get(k, 'n/a')) for k in ('primary_margin_cycles','cross_check_margin_cycles','minimum_margin_cycles'))
        lines.append(f"| {Path(r['directory']).name} | {r['manual_label']} ({r['isolator']}) | {r['prediction']} | {margins} |")
    lines += ['', f"Matched {output['matched']}/{output['run_count']} manual labels in calibration replay.", '',
              '## Proposed decision bands', '', PROFILE['padding_derivation'], '',
              'M = median band; C = allowed p05/p95 envelope; H = hard envelope for every raw trial. All bounds inclusive, in cycles.', '',
              '| Feature | UNLOADED: M; C; H | PT8211_LIKE: M; C; H |', '|---|---|---|']
    for key in FEATURES:
        cells = []
        for label in ('UNLOADED','PT8211_LIKE'):
            b = PROFILE['bands'][key][label]
            cells.append('; '.join(f"{b[name][0]:g}..{b[name][1]:g}" for name in ('median','central','trial')))
        lines.append(f"| {key} | {cells[0]} | {cells[1]} |")
    lines += ['', PROFILE['decision'], '',
              'Margin = minimum distance to any applicable M/C/H boundary. A boundary value has zero margin and is accepted; a negative margin rejects that signature. Margins are not probabilities. UNKNOWN covers gaps, outside bands, disagreement and quality/profile failures.', '',
              'Calibration identity: '+PROFILE['firmware']['source_sha256']+' at 600000000 Hz; complete metadata must match.', '', '## Limits', '']
    lines += ['- '+s for s in output['limits']]
    target.with_suffix('.md').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    return target.with_suffix('.json'), target.with_suffix('.md')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    one = commands.add_parser('classify'); one.add_argument('run', type=Path)
    batch = commands.add_parser('evaluate')
    batch.add_argument('--output-dir', type=Path, default=ROOT/'assets/hardware_diagnostics')
    args = parser.parse_args()
    if args.command == 'classify':
        print(json.dumps(classify_run(args.run), indent=2))
    else:
        for path in evaluate(args.output_dir): print(path)


if __name__ == '__main__':
    main()

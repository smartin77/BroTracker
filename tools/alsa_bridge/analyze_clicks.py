#!/usr/bin/env python3
"""Offline evidence validation; transfer continuity is not an audible-click detector."""
import argparse
import collections
import csv
import hashlib
import json
import struct
import time
from pathlib import Path

# Evidence originates on Linux (including ARM64), even when analyzed on Windows.
LINUX_ERRNOS = {4: 'EINTR', 5: 'EIO', 11: 'EAGAIN', 19: 'ENODEV',
                32: 'EPIPE', 77: 'EBADFD', 86: 'ESTRPIPE'}
FIELDS = 'seq ns kind requested result source captured played discarded queued avail delay state step'.split()

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def distribution(values):
    values = sorted(values)
    if not values:
        return {'n': 0}
    def q(p):
        return values[round((len(values)-1)*p)]
    return {'n': len(values), 'min': values[0], 'p50': q(.5),
            'p95': q(.95), 'p99': q(.99), 'max': values[-1]}

def analyze(directory):
    directory = Path(directory)
    errors = []
    required = ['run.json', 'alsa-parameters.txt', 'completion.json',
                'transfers.tsv', 'received.s16le', 'accepted.s16le']
    hashes = {name: digest(directory/name) for name in required if (directory/name).is_file()}
    missing = [name for name in required if name not in hashes]
    if missing:
        return {'complete': False, 'errors': ['missing: '+', '.join(missing)], 'sha256': hashes}
    meta = json.loads((directory/'run.json').read_text())
    reserve=meta.get('capture_holdback_frames',0)
    if reserve not in [0,45]:errors.append('unsupported experimental capture reserve')
    completion = json.loads((directory/'completion.json').read_text())
    if meta.get('schema') != 1 or meta.get('rate') != 44100 or meta.get('format') != 'S16_LE' or meta.get('frame_bytes') != 4 or meta.get('channels') != 2:
        errors.append('incompatible PCM schema')
    received = (directory/'received.s16le').read_bytes()
    accepted = (directory/'accepted.s16le').read_bytes()
    for name, data in [('received', received), ('accepted', accepted)]:
        if len(data) % 4:
            errors.append(name+' has a partial frame')
    captured = played = discarded = count = 0
    pcm_bounds = {"capture": [], "playback": []}
    previous_ns = previous_service = first_service = None
    gaps, queue_levels, cap_avail, play_avail, cap_delay, play_delay = [], [], [], [], [], []
    gap_events = []
    recovery_counts = collections.Counter()
    transfers = {side: {'calls': 0, 'short_positive': 0, 'zero': 0, 'errors': collections.Counter()}
                 for side in ['capture', 'playback']}
    discontinuities, mismatches, end = [], [], False
    start_ok = False
    statuses = {'capture': [], 'playback': []}
    with (directory/'transfers.tsv').open(newline='') as f:
        reader = csv.DictReader(f, delimiter='\t')
        if reader.fieldnames != FIELDS:
            raise ValueError('invalid transfer header')
        for raw in reader:
            if None in raw or any(raw.get(k) is None for k in FIELDS):
                raise ValueError('unfinished/malformed transfer record')
            r = {k: int(raw[k]) for k in FIELDS}
            count += 1
            if r['seq'] != count:
                errors.append('missing/duplicate sequence at record '+str(count))
            if previous_ns is not None and r['ns'] < previous_ns:
                errors.append('nonmonotonic timestamp at record '+str(count))
            previous_ns = r['ns']
            if end:
                errors.append('record after end')
            if (r['captured'], r['played'], r['discarded'], r['queued']) != (captured, played, discarded, captured-played-discarded):
                errors.append('frame accounting mismatch at record '+str(count))
            if not 0 <= r['queued'] <= meta.get('queue_frames', 8192):
                errors.append('queue out of bounds')
            kind, n = r['kind'], r['result']
            if kind == 1:
                queue_levels.append(r['queued'])
                if previous_service is not None:
                    gaps.append(r['ns']-previous_service)
                    gap_events.append({'from_ns': previous_service, 'to_ns': r['ns'],
                                       'gap_ns': r['ns']-previous_service, 'seq': count})
                if first_service is None:
                    first_service = r['ns']
                previous_service = r['ns']
            elif kind in [2, 3]:
                if kind==2 and reserve and (r['avail']<=reserve or r['requested']>r['avail']-reserve):
                    errors.append('capture read violated declared holdback snapshot')
                side = 'capture' if kind == 2 else 'playback'
                stats = transfers[side]
                stats['calls'] += 1
                if not 0 < r['requested'] <= 256 or n > r['requested']:
                    errors.append('invalid transfer size')
                if n < 0:
                    stats['errors'][str(n)] += 1
                elif n == 0:
                    stats['zero'] += 1
                else:
                    bounds=pcm_bounds[side]
                    if not bounds: bounds.extend([r['ns'],r['ns']])
                    else: bounds[1]=r['ns']
                    stats['short_positive'] += n < r['requested']
                    if r['source'] != (captured if kind == 2 else played+discarded):
                        errors.append('source mapping mismatch')
                    if kind == 2:
                        captured += n
                    else:
                        source = r['source']*4
                        a, b = received[source:source+n*4], accepted[played*4:(played+n)*4]
                        if len(a) != n*4 or len(b) != n*4 or a != b:
                            mismatches.append({'seq': count, 'source_frame': r['source'], 'frames': n})
                        if n > r['queued']:
                            errors.append('playback exceeds queued PCM')
                        played += n
            elif kind in [4, 5]:
                side = 'capture' if kind == 4 else 'playback'
                statuses[side].append({'ns': r['ns'], 'avail': r['avail'], 'delay': r['delay'],
                                       'result': n, 'state': r['state'], 'queued': r['queued']})
                if n == 0:
                    (cap_avail if kind == 4 else play_avail).append(r['avail'])
                    (cap_delay if kind == 4 else play_delay).append(r['delay'])
            elif kind in [6, 7]:
                discontinuities.append(r)
                if kind == 6 and r['step'] == 0:
                    recovery_counts['initiated'] += 1
                    recovery_counts['capture_xrun'] += (r['avail'] == 4 or (r['state'] == 1 and n == -32))
                    recovery_counts['playback_xrun'] += (r['delay'] == 4 or (r['state'] == 0 and n == -32))
                    recovery_counts['suspend'] += n == -86
                elif kind == 6 and n < 0:
                    recovery_counts['failed_operations'] += 1
                if kind == 7:
                    if n != r['queued']:
                        errors.append('discard does not equal queued frames')
                    discarded += n
            elif kind == 8:
                if start_ok or count != 1 or n < 0:
                    errors.append('capture start failed/duplicated/out of order')
                start_ok = n == 0
            elif kind == 9:
                end = True
                if n != 0:
                    errors.append('aborted audio service')
            else:
                errors.append('unknown record kind')
    if not start_ok or not end:
        errors.append('missing successful start or orderly end')
    if len(received) != captured*4 or len(accepted) != played*4:
        errors.append('PCM byte counts do not match transfer records')
    if (completion.get('schema') != 1 or completion.get('incomplete') is not False
            or completion.get('writer_failed') is not False or completion.get('lost_records') != 0
            or completion.get('attempted_records') != count or completion.get('queued_records') != count):
        errors.append('diagnostic loss/failure or completion count mismatch')
    # Musical edges are content observations only; never a fault verdict.
    previous = None
    max_step = 0
    for frame in struct.iter_unpack('<hh', received[:len(received)//4*4]):
        if previous is not None:
            max_step = max(max_step, *(abs(a-b) for a, b in zip(frame, previous)))
        previous = frame
    duration_ns = (previous_service-first_service) if first_service is not None and previous_service is not None else 0
    def trend(side):
        valid = [s for s in statuses[side] if s['result'] == 0]
        if len(valid) < 2 or valid[-1]['ns'] == valid[0]['ns']:
            return None
        # Endpoint slope only: resets/scheduling/ALSA plugins can dominate it.
        seconds = (valid[-1]['ns']-valid[0]['ns'])/1e9
        return (valid[-1]['delay']-valid[0]['delay'])/seconds
    return {'schema': 1, 'directory': str(directory), 'complete': not errors,
            'errors': errors, 'sha256': hashes, 'metadata': meta,
            'pcm_transfer_bounds_ns': pcm_bounds,
            'records': count, 'captured_frames': captured, 'accepted_frames': played,
            'discarded_application_frames': discarded, 'remaining_frames': captured-played-discarded,
            'duration_seconds': duration_ns/1e9, 'transfers': transfers,
            'accepted_equals_mapped_received': not mismatches, 'mismatches': mismatches,
            'service_gap_ns': distribution(gaps),
            'largest_service_gaps': sorted(gap_events, key=lambda g: g['gap_ns'], reverse=True)[:20],
            'application_queue_frames': distribution(queue_levels),
            'xrun_recovery_counts': recovery_counts,
            'errno_names': {code: LINUX_ERRNOS.get(abs(int(code)), 'UNKNOWN_LINUX_ERRNO')
                            for stats in transfers.values() for code in stats['errors']},
            'capture_avail_frames': distribution(cap_avail), 'playback_avail_frames': distribution(play_avail),
            'capture_delay_frames': distribution(cap_delay), 'playback_delay_frames': distribution(play_delay),
            'delay_endpoint_slope_frames_per_second': {s: trend(s) for s in statuses},
            'status_samples': statuses, 'recovery_and_discard_events': discontinuities,
            'max_same_channel_adjacent_step': max_step,
            'interpretation': 'Sharp edges may be musical content. Accepted PCM is not DAC output. '
            'Recovery may drop already accepted kernel PCM; unknown hardware losses cannot be reconstructed. '
            'Clean PCM/no XRUN does not establish hardware limits or exclude scheduling/analog faults. '
            'Endpoint slopes are not independent clock measurements.'}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    p.add_argument('--output', type=Path, help='new JSON report; refuses overwrite')
    p.add_argument('--usbmon', type=Path, help='optional USB evidence; PCM validation remains independent')
    p.add_argument('--read-integrity',action='store_true',help='require independent read-return evidence')
    p.add_argument('--marker', help='append an approximate user-observation marker, not an audio timestamp')
    args = p.parse_args()
    if args.marker is not None:
        record = {'monotonic_ns': time.monotonic_ns(), 'wall_ns': time.time_ns(),
                  'note': args.marker, 'source': 'user-observation; reaction/command latency unknown'}
        with (args.directory/'user-markers.jsonl').open('a', encoding='utf-8') as f:
            f.write(json.dumps(record)+'\n')
        return 0
    try:
        report = analyze(args.directory)
    except (ValueError, OSError, KeyError) as e:
        report = {'complete': False, 'errors': [str(e)], 'directory': str(args.directory)}
    if args.usbmon is not None:
        try:
            from analyze_usbmon import analyze as analyze_usb
            report['usb_comparison']=analyze_usb(args.usbmon,args.directory)
            if not report.get('complete'):
                report['usb_comparison']['usb_comparison_valid']=False
                report['usb_comparison']['errors'].append('PCM evidence incomplete; comparison invalid')
        except (OSError,ValueError,KeyError) as e:
            report['usb_comparison']=dict(usb_comparison_valid=False,errors=[str(e)])
    if args.read_integrity:
        from analyze_read_integrity import analyze as analyze_reads
        report['read_integrity']=analyze_reads(args.directory,report,report.get('usb_comparison'))
    text = json.dumps(report, indent=2)+'\n'
    if args.output:
        with args.output.open('x', encoding='utf-8') as f:
            f.write(text)
    else:
        print(text, end='')
    return 0 if report['complete'] and report.get('accepted_equals_mapped_received') else 1

if __name__ == '__main__':
    raise SystemExit(main())

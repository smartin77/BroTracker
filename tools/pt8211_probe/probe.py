"""Capture manually labelled empirical runs; compare without inferring attachment."""
import argparse
from collections import Counter, defaultdict
from datetime import datetime, timezone
import hashlib
import json
import re
from pathlib import Path
import statistics
import time
import uuid
import zlib

ROOT = Path(__file__).resolve().parents[2]


class Decoder:
    """Verify wire bytes before acknowledging or accepting a payload."""
    def __init__(self):
        self.protocol = None
        self.sequence = 0
        self.stream = 0
        self.ended = False

    def decode(self, line):
        if self.ended:
            raise ValueError('Record after end')
        if not line.endswith(b'\n'):
            raise ValueError('Unfinished trailing record without newline')
        if line.lstrip().startswith(b'!ABORT'):
            raise ValueError('Firmware dump aborted: '+line.decode(errors='replace').strip())
        envelope = json.loads(line)
        protocol = envelope.get('protocol', 1)
        if self.protocol is None:
            self.protocol = protocol
        if protocol != self.protocol:
            raise ValueError('Mixed wire protocols')
        ack = None
        if protocol == 2:
            match = re.fullmatch(rb'\{"protocol":2,"seq":(\d+),"crc32":"([0-9a-f]{8})","payload":(.*)\}\n', line)
            if not match:
                raise ValueError('Malformed protocol-2 framing')
            seq, checksum, payload = int(match[1]), match[2], match[3]
            if seq != self.sequence:
                raise ValueError(f'Sequence mismatch: expected {self.sequence}, received {seq}')
            if zlib.crc32(payload) != int(checksum, 16):
                raise ValueError(f'CRC mismatch at sequence {seq}')
            record = json.loads(payload)
            if record['type'] == 'end':
                if record['records'] != seq+1 or record['stream_crc32'] != f'{self.stream:08x}':
                    raise ValueError('End count/stream CRC mismatch')
            else:
                self.stream = zlib.crc32(payload, self.stream)
            ack = f'ACK {seq} {checksum.decode()}\n'.encode()
        elif protocol == 1:
            record = envelope
        else:
            raise ValueError('Unsupported wire protocol')
        if self.sequence == 0 and (record.get('type') != 'meta' or record.get('schema') != protocol):
            raise ValueError('Metadata schema/wire protocol mismatch')
        self.sequence += 1
        self.ended = record.get('type') == 'end'
        return record, ack


def send_command(device, command, deadline, log):
    offset = 0
    while offset < len(command):
        if time.monotonic() >= deadline:
            raise TimeoutError('Capture deadline while writing command/ACK')
        count = device.write(command[offset:])
        if not isinstance(count, int) or not 0 <= count <= len(command)-offset:
            raise ValueError('Invalid serial write count')
        log.write(json.dumps({'event': 'write', 'command': command.decode().strip(),
                              'offset': offset, 'accepted': count})+'\n')
        log.flush()
        offset += count
        if not count:
            time.sleep(.001)


def validate(records):
    if not records or records[0].get('type') != 'meta' or records[-1].get('type') != 'end':
        raise ValueError('Incomplete capture: meta/end records required')
    meta = records[0]
    if meta.get('schema') not in (1, 2):
        raise ValueError('Unsupported schema')
    kinds = Counter(r['type'] for r in records)
    if kinds['meta'] != 1 or kinds['end'] != 1 or kinds['control'] != 100:
        raise ValueError('Unexpected record counts')
    groups = Counter((r['pin'], r['target']) for r in records if r['type'] == 'pin')
    if groups != Counter({(p, t): meta['trials'] for p in (7, 20, 21) for t in (0, 1)}):
        raise ValueError('Missing/extra pin trials')
    if kinds['cadence'] != records[-1]['intervals']:
        raise ValueError('Cadence record count mismatch')
    if records[-1]['cadence_overflow']:
        raise ValueError('Cadence storage overflow')
    if records[-1]['callbacks'] != kinds['cadence'] + 1:
        raise ValueError('Missing cadence intervals')
    if any(r['type'] not in {'meta', 'pin', 'control', 'cadence', 'end'} for r in records):
        raise ValueError('Unknown record type')
    if meta['schema'] == 2:
        expected = [('meta', None)]
        expected += [('pin', (p,t)) for p in (7,20,21) for t in (0,1) for _ in range(meta['trials'])]
        expected += [('control', (i+1)*100) for i in range(100)]
        expected += [('cadence', None)]*records[-1]['intervals']+[('end', None)]
        actual = [(r['type'], (r['pin'],r['target']) if r['type']=='pin' else r['elapsed_ms'] if r['type']=='control' else None) for r in records]
        if actual != expected:
            raise ValueError('Measurement record order/timestamps mismatch')
        if records[-1]['records'] != len(records):
            raise ValueError('End record count mismatch')
    return meta


def distribution(values):
    if not values:
        return {'n': 0}
    ordered = sorted(values)
    return {'n': len(values), 'min': ordered[0], 'p05': ordered[int((len(values)-1)*.05)],
            'median': statistics.median(values), 'p95': ordered[int((len(values)-1)*.95)],
            'max': ordered[-1], 'mean': statistics.mean(values),
            'stdev': statistics.pstdev(values)}


def summarize(records):
    meta = validate(records)
    groups = defaultdict(list)
    for r in records:
        if r['type'] == 'pin':
            groups[f"pin{r['pin']}_target{r['target']}"].append(r)
    result = {'pins': {}}
    for key, rows in groups.items():
        # Initial already at target is unresolved at this method's time resolution.
        usable = [r for r in rows if not r['timeout'] and r['initial'] != r['target']]
        result['pins'][key] = {
            'timeouts': sum(r['timeout'] for r in rows),
            'initial_already_target': sum(r['initial'] == r['target'] for r in rows),
            'raw_elapsed_cycles': distribution([r['cycles'] for r in rows]),
            'transition_cycles': distribution([r['cycles'] for r in usable]),
            'transition_us': distribution([r['cycles']*1e6/meta['cpu_hz'] for r in usable]),
            'observation_bitmasks': dict(Counter(str(r['bits']) for r in rows)),
            'high_observations': sum(r['bits'].bit_count() for r in rows),
            'total_observations': len(rows)*meta['observations']}
    result['control'] = {
        field: distribution([r[field] for r in records if r['type'] == 'control'])
        for field in ('cpu', 'cpu_peak', 'memory', 'memory_peak')}
    result['control']['cadence_cycles'] = distribution([r['cycles'] for r in records if r['type'] == 'cadence'])
    result['control']['cadence_us'] = distribution([r['cycles']*1e6/meta['cpu_hz'] for r in records if r['type'] == 'cadence'])
    result['control']['callbacks'] = records[-1]['callbacks']
    return result


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def capture(args):
    import serial  # pyserial required only for physical capture
    directory = ROOT / 'assets/hardware_diagnostics' / f'pt8211_{args.label}' / (
        datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ') + '-' + uuid.uuid4().hex[:8])
    directory.mkdir(parents=True, exist_ok=False)
    manifest = {'manual_hardware_label': args.label, 'manual_usb_isolator': args.isolator,
                'port': args.port, 'started_utc': datetime.now(timezone.utc).isoformat(),
                'complete': False, 'note': args.note}
    write_json(directory / 'manifest.json', manifest)
    records = []
    try:
        with serial.Serial(args.port, 115200, timeout=1, write_timeout=2) as device, (directory / 'raw.jsonl').open('wb') as raw, (directory / 'transport.jsonl').open('w', encoding='utf-8') as log:
            if hasattr(device, 'set_buffer_size'):
                device.set_buffer_size(rx_size=1048576)
                log.write(json.dumps({'event': 'requested_rx_buffer', 'bytes':1048576})+'\n')
            deadline = time.monotonic() + args.timeout
            send_command(device, b'GO\n', deadline, log)
            decoder = Decoder()
            pending = bytearray()
            finished = False
            while not finished:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                device.timeout = min(1, remaining)
                chunk = device.read(min(max(device.in_waiting, 1), 16384))
                if not chunk:
                    continue
                raw.write(chunk)
                raw.flush()
                pending.extend(chunk)
                while b'\n' in pending:
                    line, _, tail = pending.partition(b'\n')
                    pending = bytearray(tail)
                    record, ack = decoder.decode(bytes(line)+b'\n')
                    records.append(record)
                    if record.get('type') == 'end':
                        validate(records)
                        finished = True
                    if ack is not None:
                        send_command(device, ack, deadline, log)
            if pending:
                raise ValueError(f'Unfinished trailing record: {len(pending)} bytes without newline; '
                                 'all received bytes preserved in raw.jsonl')
            meta = validate(records)
        write_json(directory / 'results.json', records)
        write_json(directory / 'summary.json', summarize(records))
        manifest.update(complete=True, firmware=meta,
                        wire_protocol=decoder.protocol, record_retries=0,
                        raw_sha256=hashlib.sha256((directory / 'raw.jsonl').read_bytes()).hexdigest())
    except Exception as error:
        manifest['error'] = str(error)
        raise
    finally:
        write_json(directory / 'manifest.json', manifest)
        print(directory)


def load_runs(path):
    manifests = [path / 'manifest.json'] if (path / 'manifest.json').exists() else sorted(path.glob('*/manifest.json'))
    runs = []
    for file in manifests:
        manifest = json.loads(file.read_text(encoding='utf-8'))
        if not manifest['complete']:
            raise ValueError(f'Incomplete run: {file.parent}')
        raw = (file.parent / 'raw.jsonl').read_bytes()
        if hashlib.sha256(raw).hexdigest() != manifest['raw_sha256']:
            raise ValueError(f'Raw capture hash mismatch: {file.parent}')
        decoder = Decoder()
        records = [decoder.decode(line)[0] for line in raw.splitlines(keepends=True)]
        if json.loads((file.parent / 'results.json').read_text()) != records:
            raise ValueError(f'Raw/structured mismatch: {file.parent}')
        runs.append((file.parent, manifest, validate(records), summarize(records)))
    if not runs:
        raise ValueError(f'No runs in {path}')
    return runs


def compare(args):
    attached, detached = load_runs(args.attached), load_runs(args.detached)
    runs = attached + detached
    for group, label in ((attached, 'attached'), (detached, 'detached')):
        if any(run[1]['manual_hardware_label'] != label for run in group):
            raise ValueError('Manual hardware label does not match comparison side')
    if any(r[2] != runs[0][2] for r in runs[1:]):
        raise ValueError('Firmware identity or measurement parameters differ; compare the SAME build')
    if len({r[1]['manual_usb_isolator'] for r in runs}) != 1:
        raise ValueError('USB isolator labels differ; compare the same USB setup')
    report = {'interpretation': 'Empirical comparison only; no attachment classification or DAC-health inference.',
              'runs': [{'directory': str(p), 'label': m['manual_hardware_label'], 'summary': s}
                       for p, m, _, s in runs], 'repeatability': {}}
    for group, label in ((attached, 'attached'), (detached, 'detached')):
        report['repeatability'][label] = {
            'run_count': len(group),
            'note': 'One run cannot establish between-boot repeatability.' if len(group) == 1 else 'Between-run medians and timeout counts below.',
            'pins': {key: {
                'transition_medians_cycles': distribution([r[3]['pins'][key]['transition_cycles']['median']
                    for r in group if r[3]['pins'][key]['transition_cycles']['n']]),
                'timeouts_per_run': distribution([r[3]['pins'][key]['timeouts'] for r in group])}
                for key in group[0][3]['pins']}}
    report['attached_minus_detached_median_cycles'] = {}
    for key in attached[0][3]['pins']:
        a = [r[3]['pins'][key]['transition_cycles']['median'] for r in attached if r[3]['pins'][key]['transition_cycles']['n']]
        d = [r[3]['pins'][key]['transition_cycles']['median'] for r in detached if r[3]['pins'][key]['transition_cycles']['n']]
        report['attached_minus_detached_median_cycles'][key] = statistics.median(a)-statistics.median(d) if a and d else None
    print(json.dumps(report, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    cap = commands.add_parser('capture')
    cap.add_argument('--port', required=True)
    cap.add_argument('--label', choices=['attached', 'detached'], required=True)
    cap.add_argument('--isolator', choices=['adum3160', 'none'], required=True)
    cap.add_argument('--note', default='')
    cap.add_argument('--timeout', type=float, default=90)
    cap.set_defaults(func=capture)
    comp = commands.add_parser('compare')
    comp.add_argument('--attached', type=Path, default=ROOT/'assets/hardware_diagnostics/pt8211_attached')
    comp.add_argument('--detached', type=Path, default=ROOT/'assets/hardware_diagnostics/pt8211_detached')
    comp.set_defaults(func=compare)
    args = parser.parse_args()
    args.func(args)


if __name__ == '__main__':
    main()

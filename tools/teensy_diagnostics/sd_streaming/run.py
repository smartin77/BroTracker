"""Run against already flashed diagnostic; all recordings go under _backyard/.

Baseline must pass before sweep, and all sweeps before stress. Flash a matching
ring-size environment between sweeps. No production or SD-card files are changed.
"""
import argparse
import math
from pathlib import Path
import re
import time
import serial

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / '_backyard' / 'sd_streaming'


def values(line):
    return dict(re.findall(r'(\w+)=([^ ]+)', line.strip()))


class Device:
    def __init__(self, port, label):
        OUT.mkdir(parents=True, exist_ok=True)
        self.log = (OUT / f'{label}-{time.strftime("%Y%m%d-%H%M%S")}.log').open('w', encoding='utf-8')
        self.serial = serial.Serial(port, 115200, timeout=.2)

    def read(self, deadline):
        while time.monotonic() < deadline:
            line = self.serial.readline().decode(errors='replace').strip()
            if line:
                self.log.write(line + '\n'); self.log.flush()
                print(line, flush=True)
                return line
        raise TimeoutError('Device did not finish; inspect retained log')

    def info(self, expected_ring):
        # Discard late boot text, then request a complete foreground rescan.
        self.serial.reset_input_buffer()
        self.serial.write(b'PING\n')
        deadline = time.monotonic() + 10
        while True:
            line = self.read(deadline)
            if line.startswith('PONG '):
                if int(values(line)['ring']) != expected_ring:
                    raise RuntimeError('Flashed ring size differs from --ring')
                break
        self.serial.write(b'INFO\n')
        assets = []
        deadline = time.monotonic() + 180
        while True:
            line = self.read(deadline)
            if line.startswith('ASSET '):
                assets.append(values(line))
            if line == 'NOT_READY':
                raise RuntimeError('SD or required WAV fixtures unavailable')
            if line == 'READY':
                return assets

    def run(self, n, read=4096, threshold=50, duration=8000, stagger=0, retrigger=0, mode=0, eof=False):
        self.serial.write(f'RUN {n} {read} {threshold} {duration} {stagger} {retrigger} {mode}\n'.encode())
        deadline = time.monotonic() + duration / 1000 + 20
        voices, end = [], None
        while True:
            line = self.read(deadline)
            if line.startswith('VOICE '):
                voices.append(values(line))
            if line.startswith('END '):
                end = values(line)
            if line in {'INVALID', 'HARDWARE_NOT_READY', 'NO_SHORT', 'MISSING_ASSETS', 'PREFILL_ERROR', 'RAM_ERROR', 'RAM_CAPACITY_EXCEEDED'}:
                raise RuntimeError(line)
            if line == 'DONE':
                break
        if len(voices) != n or end is None:
            raise RuntimeError('Incomplete measurement')
        if int(end['pool_fail']) or any(int(v['underrun_frames']) or int(v['errors']) or int(v['content_errors']) for v in voices):
            raise RuntimeError('Workload failed; do not promote this configuration')
        if retrigger and any(not int(v['verified_generations']) for v in voices):
            raise RuntimeError('Retrigger run did not verify complete generations')
        if eof and any(int(v['eof']) != 1 or v['hash'] != v['reference'] for v in voices):
            raise RuntimeError('EOF/reference mismatch')
        return voices


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--stage', choices=['baseline', 'sweep', 'stress'], required=True)
    parser.add_argument('--read', type=int, default=4096)
    parser.add_argument('--threshold', type=int, default=50)
    parser.add_argument('--ring', type=int, choices=[8192, 16384, 32768], default=16384)
    args = parser.parse_args()
    device = Device(args.port, f'{args.stage}-ring{args.ring}')
    try:
        assets = device.info(args.ring)
        if args.stage == 'baseline':
            device.run(1, mode=1, duration=10000, eof=True)
            device.run(1, mode=2, duration=10000, eof=True)
            device.run(1, mode=3, duration=30000, eof=True)
            for n in [2, 4, 8]:
                device.run(n)
                device.run(n, stagger=37)
        elif args.stage == 'sweep':
            failures = []
            for read in [2048, 4096, 8192, 16384]:
                if read > args.ring:
                    continue
                for threshold in [50, 75]:
                    for stagger in [0, 37]:
                        try:
                            device.run(8, read, threshold, stagger=stagger)
                        except RuntimeError as exc:
                            failures.append((read, threshold, stagger, str(exc)))
            print('FAILED CONFIGURATIONS', failures)
        else:
            device.run(8, args.read, args.threshold)
            device.run(8, args.read, args.threshold, stagger=37)
            device.run(8, args.read, args.threshold, duration=300000, retrigger=149)
            # Full-file playback to EOF, repeated for >=5 minutes in aggregate.
            duration = math.ceil(max(int(a['frames']) for a in assets[:8]) / 44.1) + 5000
            end = time.monotonic() + 300
            while time.monotonic() < end:
                device.run(8, args.read, args.threshold, duration=duration, stagger=37, eof=True)
        if args.stage != 'sweep':
            device.log.write('RESULT PASS\n'); device.log.flush()
            print('RESULT PASS', flush=True)
    finally:
        device.serial.close()
        device.log.close()


if __name__ == '__main__':
    main()

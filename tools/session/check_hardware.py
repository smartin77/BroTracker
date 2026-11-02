"""Bounded opt-in session/draft hardware checks. Uses discovered Teensy CDC.
Modifies session/draft files and test RAM only; never seeds/replaces development files.
No claim of acoustic continuity or physical failure injection. Close BTX first.
"""
import argparse
import copy
import json
from pathlib import Path
import time


def fnv(values):
    value = 2166136261
    for byte in values:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


class Device:
    def __init__(self, output):
        import serial
        from serial.tools import list_ports
        ports = [p for p in list_ports.comports() if p.vid == 0x16c0 and p.pid == 0x048a]
        if len(ports) != 1:
            raise RuntimeError(f'Expected one discovered Teensy: {ports}')
        self.serial = serial.Serial(ports[0].device, 115200, timeout=.02, write_timeout=2)
        self.log = output.open('x', encoding='utf-8')
        self.identity = 700000

    def close(self):
        self.serial.close()
        self.log.close()

    def send(self, command):
        self.log.write(f'{time.monotonic():.3f} TX {command}\n')
        self.log.flush()
        raw = (command + '\n').encode('ascii')
        if self.serial.write(raw) != len(raw):
            raise RuntimeError('Partial write; outcome unknown; no retry')

    def read(self, seconds=.4):
        until = time.monotonic() + seconds
        lines = []
        while time.monotonic() < until:
            line = self.serial.readline().decode('ascii', errors='replace').strip()
            if line:
                self.log.write(f'{time.monotonic():.3f} RX {line}\n')
                lines.append(line)
        self.log.flush()
        return lines

    def request(self, command, expected, seconds=.5):
        self.send(command)
        lines = self.read(seconds)
        if expected not in lines:
            raise RuntimeError(f'Missing {expected}: {lines[-25:]}')
        return lines

    def hello(self):
        self.send('BTTEST1 HELLO')
        lines = self.read(1)
        assert 'BTSTORE1 SESSIONCAP 2' in lines, lines
        return lines

    def snapshot(self, working=False):
        self.identity += 1
        ident = self.identity
        ns = 'BTPREVIEW1' if working else 'BTPATTERN1'
        self.send(f'BTPREVIEW1 GET 1 {ident} 0' if working else f'BTPATTERN1 GET 2 {ident}')
        lines = self.read(1)
        headers = [s for s in lines if s.startswith(ns + ' BEGIN 2 ')]
        assert len(headers) == 1, lines[-15:]
        identity, rows, channels, tempo, count = map(int, headers[0].split()[3:])
        assert ident == identity and count == rows * channels
        cells = [list(map(int, s.split()[3:])) for s in lines if s.startswith(ns + ' CELL 2 ')]
        assert len(cells) == count
        assert [c[1] for c in cells] == list(range(count))
        payload = [rows, channels, *tempo.to_bytes(4, 'little')]
        for cell in cells:
            assert cell[0] == ident
            payload.extend(cell[2:])
        assert f'{ns} END 2 {ident} {count} {fnv(payload)}' in lines
        return dict(rows=rows, channels=channels, tempo=tempo, cells=[c[2:] for c in cells])

    def preview(self, tune):
        self.send('BTTEST1 STATUS')
        lines = self.read()
        ident = [int(s.split()[-1]) for s in lines if s.startswith('BTPREVIEW1 NEXT 1 ')][-1]
        rows, channels, cells = tune['rows'], tune['channels'], tune['cells']
        self.send(f'BTPREVIEW1 BEGIN 1 {ident} {rows} {channels} {len(cells)}')
        self.read(.05)
        for index, (note, instrument, timing) in enumerate(cells):
            self.send(f'BTPREVIEW1 CELL 1 {ident} {index} {note} {instrument} {timing}')
            if index % 32 == 31:
                self.read(.03)
        payload = [rows, channels]
        for cell in cells:
            payload.extend(cell)
        self.send(f'BTPREVIEW1 COMMIT 1 {ident} {len(cells)} {fnv(payload)}')
        lines = self.read(1)
        assert f'BTPREVIEW1 ACCEPTED 1 {ident}' in lines, lines[-20:]
        for _ in range(4):
            if f'BTPREVIEW1 APPLIED 1 {ident}' in lines:
                break
            lines += self.read(.5)
        assert f'BTPREVIEW1 APPLIED 1 {ident}' in lines, lines[-20:]
        assert self.snapshot(True) == tune


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hardware', action='store_true', required=True)
    parser.add_argument('--phase', choices=['exercise', 'verify-reboot', 'inspect'], required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    device = Device(args.output / (args.phase + '.log'))
    try:
        hello = device.hello()
        if args.phase == 'verify-reboot':
            assert 'BTTEST1 STATE IDLE' in hello, hello
            device.request('BTAUDIO1 SET 1 pt8211', 'BTAUDIO1 SELECTED 1 pt8211')
            expected = json.loads((args.output / 'expected-reboot.json').read_text())
            assert device.snapshot() == expected['published']
            assert device.snapshot(True) == expected['working']
            print('Power-restart: published and unpublished content restored, playback stopped')
            return
        if args.phase == 'inspect':
            print('\n'.join(s for s in hello if 'STATE ' in s or 'BTSTORE1' in s))
            print('Published first cells:', device.snapshot()['cells'][:4])
            print('Working first cells:', device.snapshot(True)['cells'][:4])
            return
        assert 'BTTEST1 STATE IDLE' in hello, 'STOP before running this test'
        device.request('BTAUDIO1 SET 1 pt8211', 'BTAUDIO1 SELECTED 1 pt8211')
        device.request('BTMIDI1 SET 1 12', 'BTMIDI1 SELECTED 1 12')
        device.request('BTTEST1 STOP', 'BTTEST1 STOPPED')
        baseline = device.snapshot()
        initial = device.snapshot(True)
        (args.output / 'initial.json').write_text(json.dumps(dict(published=baseline, working=initial), indent=2))
        device.request('BTSTORE1 LOAD 1 missing_session_test', 'BTSTORE1 RESULT 1 load-missing_session_test missing 0')
        assert device.snapshot() == baseline and device.snapshot(True) == initial
        a = copy.deepcopy(initial)
        assert 24 <= a['cells'][0][0] <= 124
        a['cells'][0][0] += 1
        device.preview(a)
        device.request('BTSTORE1 DRAFTSAVE 1', 'BTSTORE1 RESULT 1 draft-save ok 0')
        assert device.snapshot() == baseline  # Saving did not publish.
        b = copy.deepcopy(a); b['cells'][0][0] += 1
        device.preview(b)
        device.request('BTTEST1 STOP', 'BTTEST1 STOPPED')
        device.send('BTSTORE1 DRAFTLOAD 1')
        lines = device.read(1)
        assert any(s.startswith('BTSTORE1 RESULT 1 draft-load ok ') for s in lines), lines
        assert device.snapshot() == a and device.snapshot(True) == a
        device.preview(b)
        device.request('BTTEST1 START', 'BTTEST1 STARTED')
        device.request('BTSTORE1 DRAFTSAVE 1', 'BTSTORE1 RESULT 1 draft-save deferred-playing 0')
        c = copy.deepcopy(b); c['cells'][0][0] += 1
        device.preview(c)
        lines = device.read(.5)
        ticks = [int(s.split()[5]) for s in lines if s.startswith('BTPATTERN1 POS 2 1 1 ')]
        assert len(ticks) > 2 and ticks[-1] > ticks[0], ticks
        device.serial.close()
        time.sleep(.5)
        device.serial.open()
        hello = device.hello()
        assert 'BTTEST1 STATE PLAYING' in hello, hello
        assert device.snapshot(True) == c and device.snapshot() == a
        device.send('BTTEST1 PAUSE'); lines = device.read(1)
        assert 'BTTEST1 PAUSED' in lines, lines
        assert 'BTSTORE1 RESULT 1 draft-save ok 0' in lines, lines
        assert 'BTSTORE1 RESULT 1 session-auto ok 0' in lines, lines
        device.request('BTTEST1 STOP', 'BTTEST1 STOPPED')
        device.send('BTSTORE1 DRAFTLOAD 1'); lines = device.read(1)
        assert any(s.startswith('BTSTORE1 RESULT 1 draft-load ok ') for s in lines), lines
        assert device.snapshot() == b and device.snapshot(True) == b  # Frozen Save B, later edits C.
        device.preview(c)
        device.request('BTSTORE1 SAVE 1', 'BTSTORE1 RESULT 1 save ok 0')
        expected = dict(published=b, working=c)
        (args.output / 'expected-reboot.json').write_text(json.dumps(expected, indent=2))
        print('PASS: missing-file preservation, unpublished Save, draft Load, frozen playing Save, powered CDC disconnect, Pause/Stop session persistence')
        print('Prepared distinct published/working content for power-restart test; stopped')
    finally:
        device.close()


if __name__ == '__main__':
    main()

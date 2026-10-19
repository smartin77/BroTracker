import argparse
import contextlib
import copy
import io
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
import zlib
import subprocess
from unittest.mock import patch

import probe


def fixture():
    rows = [dict(type='meta', schema=1, trials=64, observations=8, cpu_hz=600000000,
                 source_sha256='synthetic-offline-fixture')]
    for pin in (7, 20, 21):
        for target in (0, 1):
            for i in range(64):
                rows.append(dict(type='pin', pin=pin, target=target, initial=1-target,
                                 final=1-target if i == 0 else target, bits=255 if target else 0,
                                 cycles=60000 if i == 0 else 120, timeout=i == 0))
    rows.extend(dict(type='control', elapsed_ms=(i+1)*100, callbacks=i+1,
                     cpu=1., cpu_peak=2., memory=4, memory_peak=5) for i in range(100))
    rows.extend(dict(type='cadence', cycles=1741496) for _ in range(99))
    rows.append(dict(type='end', intervals=99, callbacks=100, cadence_overflow=False))
    return rows


class FakeSerial:
    payload = []
    def __init__(self, *args, **kwargs): self.lines = iter(self.payload)
    def __enter__(self): return self
    def __exit__(self, *args): pass
    def reset_input_buffer(self): pass
    def write(self, data):
        assert data == b'GO\n' or data.startswith(b'ACK ')
        return len(data)
    @property
    def in_waiting(self): return 4096
    def read(self, size): return next(self.lines, b'')


class ProbeTest(unittest.TestCase):
    def test_protocol2_capture_and_corruption(self):
        rows = fixture()
        rows[0]['schema'] = 2
        stream = 0
        frames = []
        for seq, row in enumerate(rows):
            if row['type'] == 'end':
                row.update(records=len(rows), stream_crc32=f'{stream:08x}')
            payload = json.dumps(row, separators=(',', ':')).encode()
            frames.append(b'{"protocol":2,"seq":'+str(seq).encode()+b',"crc32":"'+f'{zlib.crc32(payload):08x}'.encode()+b'","payload":'+payload+b'}\n')
            if row['type'] != 'end': stream = zlib.crc32(payload, stream)
        FakeSerial.payload = [part for f in frames for part in (f[:17],b'',f[17:])]
        with tempfile.TemporaryDirectory() as tmp, patch.object(probe, 'ROOT', Path(tmp)), \
             patch.dict(sys.modules, {'serial': types.SimpleNamespace(Serial=FakeSerial)}), \
             contextlib.redirect_stdout(io.StringIO()):
            probe.capture(argparse.Namespace(port='FAKE', label='attached', isolator='none', note='schema2 test', timeout=5))
            manifest = next(Path(tmp).rglob('manifest.json'))
            run = probe.load_runs(manifest.parent)[0]
            self.assertEqual(run[2]['schema'], 2)
            self.assertEqual((manifest.parent/'raw.jsonl').read_bytes(), b''.join(frames))
            self.assertIn('ACK 0', (manifest.parent/'transport.jsonl').read_text())
        for bad in (frames[:1]+frames[2:], frames[:2]+frames[1:],
                    [frames[0].replace(b'600000000',b'600000001')]+frames[1:]):
            with self.subTest(bad=bad[:2]), self.assertRaises(ValueError):
                decoder = probe.Decoder()
                for line in bad: decoder.decode(line)
        decoder = probe.Decoder()
        for line in frames[:-1]: decoder.decode(line)
        with self.assertRaisesRegex(ValueError, 'Unfinished'):
            decoder.decode(frames[-1][:-1])

    def test_command_short_zero_writes_and_deadline(self):
        class Writer:
            def __init__(self): self.output=b''; self.calls=0
            def write(self, data):
                self.calls+=1
                n=0 if self.calls==1 else min(2,len(data))
                self.output+=data[:n]
                return n
        writer=Writer()
        probe.send_command(writer,b'ACK 123 deadbeef\n',probe.time.monotonic()+1,io.StringIO())
        self.assertEqual(writer.output,b'ACK 123 deadbeef\n')
        with self.assertRaises(TimeoutError):
            probe.send_command(writer,b'GO\n',0,io.StringIO())

    def test_native_firmware_transport(self):
        root=Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as tmp:
            binary=Path(tmp)/'transport_test.exe'
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',
                str(root/'tools/pt8211_probe/transport_test.cpp'),'-o',str(binary)],check=True,capture_output=True)
            subprocess.run([str(binary)],check=True,capture_output=True)

    def test_capture_bulk_chunks_with_multiple_records(self):
        expected = fixture()
        raw = b''.join((json.dumps(r)+'\n').encode() for r in expected)
        FakeSerial.payload = [raw[i:i+4096] for i in range(0,len(raw),4096)]
        with tempfile.TemporaryDirectory() as tmp, patch.object(probe, 'ROOT', Path(tmp)), \
             patch.dict(sys.modules, {'serial': types.SimpleNamespace(Serial=FakeSerial)}), \
             contextlib.redirect_stdout(io.StringIO()):
            probe.capture(argparse.Namespace(port='FAKE', label='attached', isolator='none', note='', timeout=5))
            manifest = next(Path(tmp).rglob('manifest.json'))
            self.assertTrue(json.loads(manifest.read_text())['complete'])
            self.assertEqual((manifest.parent/'raw.jsonl').read_bytes(), raw)
            self.assertEqual(json.loads((manifest.parent/'results.json').read_text()), expected)

    def test_capture_fragmented_records_across_timeouts(self):
        expected = fixture()
        lines = [(json.dumps(r)+'\n').encode() for r in expected]
        FakeSerial.payload = []
        for line in lines:
            middle = len(line)//2
            FakeSerial.payload.extend([line[:middle], b'', line[middle:-1], b'', line[-1:]])
        with tempfile.TemporaryDirectory() as tmp, patch.object(probe, 'ROOT', Path(tmp)), \
             patch.dict(sys.modules, {'serial': types.SimpleNamespace(Serial=FakeSerial)}), \
             contextlib.redirect_stdout(io.StringIO()):
            probe.capture(argparse.Namespace(port='FAKE', label='attached', isolator='adum3160', note='', timeout=5))
            manifest = next(Path(tmp).rglob('manifest.json'))
            self.assertTrue(json.loads(manifest.read_text())['complete'])
            self.assertEqual((manifest.parent/'raw.jsonl').read_bytes(), b''.join(lines))
            self.assertEqual(json.loads((manifest.parent/'results.json').read_text()), expected)

    def test_unfinished_and_malformed_records_preserve_bytes(self):
        for payload, error, message in (([b'{"type":', b'', b'"meta"}'], ValueError, 'Unfinished trailing record'),
                                        ([b'{"type":broken}\n'], json.JSONDecodeError, None)):
            with self.subTest(payload=payload), tempfile.TemporaryDirectory() as tmp, \
                 patch.object(probe, 'ROOT', Path(tmp)), \
                 patch.dict(sys.modules, {'serial': types.SimpleNamespace(Serial=FakeSerial)}), \
                 contextlib.redirect_stdout(io.StringIO()):
                FakeSerial.payload = payload
                with self.assertRaises(error) as caught:
                    probe.capture(argparse.Namespace(port='FAKE', label='attached', isolator='adum3160', note='', timeout=.02))
                if message:
                    self.assertIn(message, str(caught.exception))
                manifest = next(Path(tmp).rglob('manifest.json'))
                self.assertFalse(json.loads(manifest.read_text())['complete'])
                self.assertEqual((manifest.parent/'raw.jsonl').read_bytes(), b''.join(payload))

    def test_timeout_not_success(self):
        result = probe.summarize(fixture())['pins']['pin7_target1']
        self.assertEqual(result['timeouts'], 1)
        self.assertEqual(result['transition_cycles']['n'], 63)
        self.assertEqual(result['transition_cycles']['max'], 120)
        self.assertEqual(result['raw_elapsed_cycles']['max'], 60000)

    def test_truncation_missing_trials_overflow(self):
        for mutate in (lambda r: r.pop(), lambda r: r.pop(1),
                       lambda r: r[-1].update(cadence_overflow=True)):
            rows = fixture(); mutate(rows)
            with self.assertRaises(ValueError): probe.validate(rows)

    def test_capture_compare_and_mismatch(self):
        FakeSerial.payload = [(json.dumps(r)+'\n').encode() for r in fixture()]
        with tempfile.TemporaryDirectory() as tmp, patch.object(probe, 'ROOT', Path(tmp)), \
             patch.dict(sys.modules, {'serial': types.SimpleNamespace(Serial=FakeSerial)}), \
             contextlib.redirect_stdout(io.StringIO()):
            for label in ('attached', 'attached', 'detached'):
                probe.capture(argparse.Namespace(port='FAKE', label=label, isolator='adum3160', note='synthetic', timeout=1))
            base = Path(tmp)/'assets/hardware_diagnostics'
            args = argparse.Namespace(attached=base/'pt8211_attached', detached=base/'pt8211_detached')
            output = io.StringIO()
            with contextlib.redirect_stdout(output): probe.compare(args)
            report = json.loads(output.getvalue())
            self.assertEqual(report['repeatability']['attached']['run_count'], 2)
            self.assertEqual(report['attached_minus_detached_median_cycles']['pin7_target1'], 0)
            manifest_path = next(args.detached.glob('*/manifest.json'))
            original = json.loads(manifest_path.read_text())
            changed = copy.deepcopy(original); changed['manual_usb_isolator'] = 'none'
            probe.write_json(manifest_path, changed)
            with self.assertRaisesRegex(ValueError, 'isolator'): probe.compare(args)
            probe.write_json(manifest_path, original)
            with patch.object(probe, 'load_runs', side_effect=[
                    [(Path('a'), {'manual_hardware_label': 'attached'}, {'build': 'a'}, {})],
                    [(Path('d'), {'manual_hardware_label': 'detached'}, {'build': 'b'}, {})]]):
                with self.assertRaisesRegex(ValueError, 'Firmware'): probe.compare(args)
            raw_path = manifest_path.parent/'raw.jsonl'
            raw_path.write_bytes(raw_path.read_bytes()+b'\n')
            with self.assertRaisesRegex(ValueError, 'hash'): probe.compare(args)

    def test_failed_capture_preserves_raw(self):
        FakeSerial.payload = [b'{"type":"meta","schema":1}\n']
        with tempfile.TemporaryDirectory() as tmp, patch.object(probe, 'ROOT', Path(tmp)), \
             patch.dict(sys.modules, {'serial': types.SimpleNamespace(Serial=FakeSerial)}), \
             contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(ValueError, 'Incomplete'):
                probe.capture(argparse.Namespace(port='FAKE', label='attached', isolator='none', note='', timeout=.01))
            manifest = next(Path(tmp).rglob('manifest.json'))
            self.assertFalse(json.loads(manifest.read_text())['complete'])
            self.assertEqual((manifest.parent/'raw.jsonl').read_bytes(), FakeSerial.payload[0])


if __name__ == '__main__': unittest.main()

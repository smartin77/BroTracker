import copy
import json
import os
import tempfile
import unittest
from pathlib import Path
from analyze_clicks import analyze, FIELDS

class ComparisonTests(unittest.TestCase):
    def setUp(self):
        base = Path(os.environ.get('BROTRACKER_TEST_DIR', Path(__file__).resolve().parents[2]/'build'/'bridge-clicks-tests'))
        base.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix='synthetic-', dir=base)
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.records = []
        self.cap = self.play = self.drop = 0
        self.received = bytes(range(20))
        self.accepted = self.received[:12]+self.received[16:]
        self.event(8)
        self.event(1)
        self.event(2, result=3, requested=4)  # short read
        self.cap += 3
        self.event(3, result=1, requested=3)  # partial write
        self.play += 1
        self.event(3, result=0, requested=2)
        self.event(3, result=-11, requested=2)
        self.event(3, result=2, requested=2)
        self.play += 2
        self.event(2, result=2, requested=2)
        self.cap += 2
        self.event(6, result=-32)  # transfer discontinuity, not diagnostic loss
        self.event(7, result=2)
        self.drop += 2
        # This synthetic recovery discards two frames; no accepted bytes from them.
        self.accepted = self.received[:12]
        self.event(1)
        self.event(9)
        self.meta = {'schema': 1, 'rate': 44100, 'format': 'S16_LE', 'channels': 2,
                     'frame_bytes': 4, 'queue_frames': 8192}
        self.completion = {'schema': 1, 'incomplete': False, 'writer_failed': False, 'lost_records': 0,
                           'queued_records': len(self.records), 'attempted_records': len(self.records)}
    def event(self, kind, **kwargs):
        r = dict.fromkeys(FIELDS, 0)
        r.update(seq=len(self.records)+1, ns=1000000000+len(self.records)*1000, kind=kind,
                 captured=self.cap, played=self.play, discarded=self.drop,
                 queued=self.cap-self.play-self.drop,
                 source=self.play+self.drop if kind == 3 else self.cap)
        r.update(kwargs)
        self.records.append(r)
    def save(self):
        (self.root/'run.json').write_text(json.dumps(self.meta))
        (self.root/'alsa-parameters.txt').write_text('synthetic fixture; no physical ALSA')
        (self.root/'completion.json').write_text(json.dumps(self.completion))
        (self.root/'received.s16le').write_bytes(self.received)
        (self.root/'accepted.s16le').write_bytes(self.accepted)
        (self.root/'transfers.tsv').write_text('\t'.join(FIELDS)+'\n'+''.join(
            '\t'.join(str(r[k]) for k in FIELDS)+'\n' for r in self.records))
        return analyze(self.root)
    def test_partial_zero_error_discard_accounting(self):
        r = self.save()
        self.assertTrue(r['complete'], r['errors'])
        self.assertTrue(r['accepted_equals_mapped_received'])
        self.assertEqual(r['discarded_application_frames'], 2)
        self.assertEqual(r['transfers']['capture']['short_positive'], 1)
        self.assertEqual(r['transfers']['playback']['short_positive'], 1)
        self.assertEqual(r['transfers']['playback']['zero'], 1)
        self.assertEqual(r['transfers']['playback']['errors'], {'-11': 1})
        self.assertEqual(len(r['sha256']), 6)
    def test_status_gap_and_recovery_reporting(self):
        self.records.pop()  # replace end with actual status fixtures
        self.event(4, avail=7, delay=9)
        self.event(5, avail=1200, delay=848)
        self.event(6, result=-32, state=1, avail=4, delay=4)
        self.event(6, result=-5, step=1)
        self.event(9, result=-1)
        self.completion['queued_records'] = self.completion['attempted_records'] = len(self.records)
        r = self.save()
        self.assertFalse(r['complete'])  # aborted recovery, evidence retained
        self.assertEqual(r['capture_delay_frames']['p50'], 9)
        self.assertEqual(r['xrun_recovery_counts']['capture_xrun'], 1)
        self.assertEqual(r['xrun_recovery_counts']['playback_xrun'], 2)
        self.assertEqual(r['xrun_recovery_counts']['failed_operations'], 1)
        self.assertEqual(r['errno_names']['-11'], 'EAGAIN')
        self.assertGreater(r['largest_service_gaps'][0]['gap_ns'], 0)

    def test_pcm_mismatch_is_not_hidden(self):
        self.accepted = b'\xff'+self.accepted[1:]
        r = self.save()
        self.assertTrue(r['complete'])  # transport complete; PCM differs
        self.assertFalse(r['accepted_equals_mapped_received'])
    def test_missing_duplicate_and_counter_records(self):
        for mode in ['missing', 'duplicate', 'counter']:
            original = copy.deepcopy(self.records)
            if mode == 'missing':
                self.records.pop(2)
            elif mode == 'duplicate':
                self.records.insert(2, dict(self.records[2]))
            else:
                self.records[4]['captured'] += 1
            self.assertFalse(self.save()['complete'], mode)
            self.records = original
    def test_overflow_and_interrupted_recording(self):
        self.completion['lost_records'] = 1
        self.completion['incomplete'] = True
        self.assertFalse(self.save()['complete'])
        (self.root/'completion.json').unlink()
        self.assertFalse(analyze(self.root)['complete'])
    def test_truncated_pcm_and_aborted_end(self):
        self.accepted = self.accepted[:-1]
        self.assertFalse(self.save()['complete'])
        self.accepted = self.received[:12]
        self.records[-1]['result'] = -1
        self.assertFalse(self.save()['complete'])
    def test_missing_end(self):
        self.records.pop()
        self.assertFalse(self.save()['complete'])
    def test_incompatible_schema(self):
        self.meta['frame_bytes'] = 8
        self.assertFalse(self.save()['complete'])
    def test_partial_event_line_is_rejected(self):
        self.save()
        with (self.root/'transfers.tsv').open('a') as f:
            f.write('15\t123')
        with self.assertRaises(ValueError):
            analyze(self.root)

if __name__ == '__main__':
    unittest.main()

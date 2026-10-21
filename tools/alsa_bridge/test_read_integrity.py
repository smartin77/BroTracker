import csv
import json
import os
from pathlib import Path
import tempfile
import unittest
from analyze_read_integrity import analyze,digest


class ReadIntegrityTests(unittest.TestCase):
    def setUp(self):
        base=Path(os.environ.get('BROTRACKER_TEST_DIR','build/read-integrity-tests'));base.mkdir(parents=True,exist_ok=True)
        self.tmp=tempfile.TemporaryDirectory(dir=base);self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
        self.pcm=bytes(range(20));(self.root/'received.s16le').write_bytes(self.pcm)
        (self.root/'transfers.tsv').write_text('kind\tsource\tresult\n2\t0\t2\n2\t2\t-11\n2\t2\t3\n')
        self.lines=['1\t0\t2\t%016x'%digest(self.pcm[:8]),'2\t2\t3\t%016x'%digest(self.pcm[8:])]
        self.done=dict(schema=1,algorithm='fnv1a64',attempted=2,written=2,lost=0,incomplete=False)
        self.save()

    def save(self):
        (self.root/'read-integrity.tsv').write_text('id\tsource\tframes\tdigest\n'+'\n'.join(self.lines)+'\n')
        (self.root/'read-integrity-completion.json').write_text(json.dumps(self.done))

    def run_report(self,usb=None):return analyze(self.root,dict(complete=True),usb)
    def usb(self,differences):return dict(usb_comparison_valid=True,alignment=[dict(status='ALIGNED',received_range=[0,5],differing_frames=differences)])

    def test_known_hash(self):self.assertEqual(digest(b'hello'),0xa430d84680aabd0b)
    def test_unchanged_partial_reads_and_usb_difference(self):
        r=self.run_report(self.usb(1))
        self.assertEqual(r['status'],'READ_RETURN_DIFFERS_FROM_USB');self.assertEqual(r['frames'],5)
    def test_unchanged_usb(self):
        self.assertEqual(self.run_report(self.usb(0))['status'],'READ_RETURN_MATCHES_USB')
    def test_altered_saved_bytes(self):
        (self.root/'received.s16le').write_bytes(bytes([99])+self.pcm[1:])
        r=self.run_report();self.assertEqual(r['status'],'RECORDER_PATH_MISMATCH');self.assertEqual(r['mismatching_read_ids'],[1])
    def test_missing_digest(self):
        self.lines=self.lines[:1];self.save();self.assertEqual(self.run_report()['status'],'UNKNOWN')
    def test_missing_file(self):
        (self.root/'read-integrity.tsv').unlink();self.assertEqual(self.run_report()['status'],'UNKNOWN')
    def test_wrong_boundary(self):
        self.lines[1]=self.lines[1].replace('2\t2\t3','2\t1\t3');self.save();self.assertEqual(self.run_report()['status'],'UNKNOWN')
    def test_overflow(self):
        self.done.update(lost=1,incomplete=True);self.save();self.assertEqual(self.run_report()['status'],'UNKNOWN')
    def test_no_usb_is_unknown(self):self.assertEqual(self.run_report()['status'],'UNKNOWN')
    def test_malformed_digest(self):
        for value in ['z'*16,'%016x\textra'%digest(self.pcm[:8])]:
            self.lines[0]='1\t0\t2\t'+value;self.save();self.assertEqual(self.run_report()['status'],'UNKNOWN')
    def test_partial_read_last_returned_byte_is_covered(self):
        (self.root/'received.s16le').write_bytes(self.pcm[:7]+bytes([99])+self.pcm[8:])
        self.assertEqual(self.run_report()['status'],'RECORDER_PATH_MISMATCH')
    def test_incomplete_usb_coverage(self):
        u=self.usb(1);u['alignment'][0]['received_range']=[0,4]
        self.assertEqual(self.run_report(u)['status'],'UNKNOWN')


if __name__=='__main__':unittest.main()

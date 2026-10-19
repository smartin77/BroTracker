import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import loading_classifier as classifier


class LoadingClassifierTest(unittest.TestCase):
    def rows(self, label='attached'):
        """Explicitly synthetic decision fixture; never physical evidence."""
        signature = 'PT8211_LIKE' if label == 'attached' else 'UNLOADED'
        rows = [copy.deepcopy(classifier.PROFILE['firmware'])]
        for pin in (7, 20, 21):
            for target in (0, 1):
                band = classifier.PROFILE['bands'][f'pin{pin}_target{target}'][signature]
                cycles = int(sum(band['median']) / 2)
                rows.extend(dict(type='pin', pin=pin, target=target, initial=1-target,
                                 final=target, bits=255 if target else 0,
                                 cycles=cycles, timeout=False) for _ in range(64))
        rows.extend(dict(type='control', elapsed_ms=(i+1)*100, callbacks=i+1,
                         cpu=1., cpu_peak=2., memory=1, memory_peak=3) for i in range(100))
        rows.append(dict(type='cadence', cycles=1741496))
        rows.append(dict(type='end', intervals=1, callbacks=2, cadence_overflow=False,
                         records=len(rows)+1))
        return rows

    @staticmethod
    def set_cycles(rows, key, value):
        pin=int(key.split('_')[0][3:]); target=int(key[-1])
        for r in rows:
            if r['type']=='pin' and r['pin']==pin and r['target']==target:
                r['cycles']=value

    def test_both_valid_signatures(self):
        for label,expected in (('attached','PT8211_LIKE'),('detached','UNLOADED')):
            r=classifier.classify_records(self.rows(label))
            self.assertEqual(r['prediction'],expected)
            self.assertGreaterEqual(r['minimum_margin_cycles'],0)

    def test_all_inclusive_median_boundaries_and_one_cycle_outside(self):
        for label,name in (('attached','PT8211_LIKE'),('detached','UNLOADED')):
            for key in classifier.FEATURES:
                low,high=classifier.PROFILE['bands'][key][name]['median']
                for edge in (low,high):
                    with self.subTest(key=key,label=label,edge=edge):
                        rows=self.rows(label);self.set_cycles(rows,key,int(edge))
                        result=classifier.classify_records(rows)
                        self.assertEqual(result['prediction'],name)
                        self.assertEqual(result['minimum_margin_cycles'],0)
                for outside in (low-1,high+1):
                    rows=self.rows(label);self.set_cycles(rows,key,int(outside))
                    self.assertEqual(classifier.classify_records(rows)['prediction'],'UNKNOWN')

    def test_unknown_gaps_and_unbounded_larger_delays(self):
        for key in classifier.FEATURES:
            u=classifier.PROFILE['bands'][key]['UNLOADED']['median'][1]
            p=classifier.PROFILE['bands'][key]['PT8211_LIKE']['median'][0]
            for value in (int((u+p)//2),10000):
                rows=self.rows();self.set_cycles(rows,key,value)
                self.assertEqual(classifier.classify_records(rows)['prediction'],'UNKNOWN')

    def test_mixed_primary_pins_and_pullup_veto(self):
        rows=self.rows();self.set_cycles(rows,'pin7_target0',485)
        r=classifier.classify_records(rows)
        self.assertEqual(r['prediction'],'UNKNOWN')
        self.assertIn('Pull-down',r['reasons'][0])
        rows=self.rows();self.set_cycles(rows,'pin20_target1',170)
        r=classifier.classify_records(rows)
        self.assertEqual(r['prediction'],'UNKNOWN')
        self.assertIn('Pull-up',r['reasons'][0])

    def test_timeout_missing_extra_and_bad_digital_trials(self):
        mutations=(lambda r:r[1].update(timeout=True),lambda r:r.pop(1),
                   lambda r:r.insert(1,copy.deepcopy(r[1])),lambda r:r[1].update(initial=0),
                   lambda r:r[1].update(final=1),lambda r:r[1].update(bits=1),
                   lambda r:r[1].pop('cycles'),lambda r:r[1].update(cycles=True),lambda r:r[1].update(cycles=float('nan')))
        for mutate in mutations:
            rows=self.rows();mutate(rows)
            self.assertEqual(classifier.classify_records(rows)['prediction'],'UNKNOWN')

    def test_robust_center_does_not_hide_out_of_profile_tail(self):
        rows=self.rows();rows[1]['cycles']=10000
        self.assertEqual(classifier.classify_records(rows)['prediction'],'UNKNOWN')
        # Same median and hard-trial bounds: central p95 must still pass.
        for count,expected in ((4,'PT8211_LIKE'),(5,'UNKNOWN')):
            rows=self.rows();self.set_cycles(rows,'pin7_target0',720)
            for r in rows[1:1+count]:r['cycles']=736
            self.assertEqual(classifier.classify_records(rows)['prediction'],expected)

    def test_firmware_parameters_and_profile_are_strict(self):
        for field,value in (('cpu_hz',599999999),('source_sha256','other'),('build','other'),
                            ('trials',63),('timeout_us',101),('settle_us',99),('schema',1)):
            rows=self.rows();rows[0][field]=value
            self.assertEqual(classifier.classify_records(rows)['prediction'],'UNKNOWN')
        profile=copy.deepcopy(classifier.PROFILE);profile['profile_version']=2
        with patch.object(classifier,'PROFILE',profile):
            self.assertEqual(classifier.classify_records(self.rows())['prediction'],'UNKNOWN')

    def test_profile_median_bands_are_disjoint(self):
        for key in classifier.FEATURES:
            band = classifier.PROFILE['bands'][key]
            self.assertLess(band['UNLOADED']['median'][1], band['PT8211_LIKE']['median'][0])

    def test_integrity_failure_and_manual_label_independence(self):
        import hashlib
        import zlib
        rows = self.rows()
        frames = []
        stream = 0
        for seq, row in enumerate(rows):
            if row['type'] == 'end':
                row['stream_crc32'] = f'{stream:08x}'
            payload = json.dumps(row, separators=(',', ':')).encode()
            frames.append(b'{"protocol":2,"seq":'+str(seq).encode()+b',"crc32":"'+
                          f'{zlib.crc32(payload):08x}'.encode()+b'","payload":'+payload+b'}\n')
            if row['type'] != 'end':
                stream = zlib.crc32(payload, stream)
        raw = b''.join(frames)
        with tempfile.TemporaryDirectory() as tmp:
            target=Path(tmp)
            (target/'raw.jsonl').write_bytes(raw)
            (target/'results.json').write_text(json.dumps(rows))
            (target/'manifest.json').write_text(json.dumps(dict(
                complete=True, wire_protocol=2, manual_hardware_label='attached',
                manual_usb_isolator='none', raw_sha256=hashlib.sha256(raw).hexdigest())))
            manifest=json.loads((target/'manifest.json').read_text());manifest['manual_hardware_label']='detached'
            (target/'manifest.json').write_text(json.dumps(manifest))
            self.assertEqual(classifier.classify_run(target)['prediction'],'PT8211_LIKE')
            manifest['manual_usb_isolator']='unvalidated-topology'
            (target/'manifest.json').write_text(json.dumps(manifest))
            self.assertEqual(classifier.classify_run(target)['prediction'],'UNKNOWN')
            manifest['manual_usb_isolator']='none'
            (target/'manifest.json').write_text(json.dumps(manifest))
            (target/'raw.jsonl').write_bytes(b'corrupted\n')
            result=classifier.classify_run(target)
            self.assertEqual(result['prediction'],'UNKNOWN')
            self.assertFalse(result['integrity_verified'])



if __name__=='__main__':unittest.main()

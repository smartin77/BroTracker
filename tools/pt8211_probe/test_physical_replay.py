"""Original physical evidence replay only; missing originals are explicit skips."""
import hashlib
import json
from pathlib import Path
import unittest

import loading_classifier as classifier


class PhysicalReplayTest(unittest.TestCase):
    def test_calibration_report_fingerprint_and_band_derivation(self):
        profile = classifier.PROFILE
        path = classifier.ROOT / profile['calibration_report']
        if not path.exists():
            self.skipTest('Missing original report: '+str(path))
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), profile['calibration_report_sha256'])
        report = json.loads(path.read_bytes())
        for key in classifier.FEATURES:
            for manual, signature in (('attached', 'PT8211_LIKE'), ('detached', 'UNLOADED')):
                distributions = [r['summary']['pins'][key]['transition_cycles']
                                 for r in report['runs'] if r['condition'] == manual]
                extrema = dict(median=[min(d['median'] for d in distributions), max(d['median'] for d in distributions)],
                               central=[min(d['p05'] for d in distributions), max(d['p95'] for d in distributions)],
                               trial=[min(d['min'] for d in distributions), max(d['max'] for d in distributions)])
                band = profile['bands'][key][signature]
                self.assertEqual(band['observed_extrema'], extrema)
                for name, (low, high) in extrema.items():
                    self.assertEqual(band[name], [low-profile['padding_cycles'], high+profile['padding_cycles']])

    def test_full_calibration_replay(self):
        missing = [str(classifier.ROOT / Path(p.replace('\\', '/')))
                   for p in classifier.PROFILE['selected_run_paths']
                   if not (classifier.ROOT / Path(p.replace('\\', '/')) / 'raw.jsonl').exists()]
        if missing:
            self.skipTest('Full 13-run replay blocked; missing originals: '+', '.join(missing))
        for p in classifier.PROFILE['selected_run_paths']:
            result = classifier.classify_run(classifier.ROOT / Path(p.replace('\\', '/')))
            self.assertTrue(result['integrity_verified'])
            self.assertNotEqual(result['prediction'], 'UNKNOWN')


def replay_case(relative):
    def test(self):
        path = classifier.ROOT / Path(relative.replace('\\', '/'))
        missing = [str(path/name) for name in ('manifest.json', 'raw.jsonl', 'results.json')
                   if not (path/name).exists()]
        if missing:
            self.skipTest('Missing original evidence: '+', '.join(missing))
        manifest = json.loads((path/'manifest.json').read_bytes())
        expected = {'attached': 'PT8211_LIKE', 'detached': 'UNLOADED'}[manifest['manual_hardware_label']]
        result = classifier.classify_run(path)
        self.assertTrue(result['integrity_verified'], result)
        self.assertEqual(result['prediction'], expected, result)
        self.assertEqual(result['minimum_margin_cycles'], 8)
    return test


paths = list(classifier.PROFILE['selected_run_paths'])
paths += ['assets/hardware_diagnostics/pt8211_attached/'+name for name in (
    '20261018T215700.434997Z-194bfe2f', '20261018T215735.599162Z-ee13663a',
    '20261018T215809.161405Z-93ee62f3')]
paths += ['assets/hardware_diagnostics/pt8211_detached/'+name for name in (
    '20261018T221153.351814Z-c3bdde66', '20261018T221246.746733Z-fb5f505e',
    '20261018T221328.971752Z-24c3b8fb')]
for relative in paths:
    # The missing third attached run is a historical UNKNOWN, never a presumed pass.
    test = replay_case(relative)
    if '93ee62f3' in relative:
        def test(self):
            relative = 'assets/hardware_diagnostics/pt8211_attached/20261018T215809.161405Z-93ee62f3'
            missing = [str(classifier.ROOT/relative/name) for name in ('raw.jsonl','manifest.json','results.json')
                       if not (classifier.ROOT/relative/name).exists()]
            if missing:
                self.skipTest('Historical UNKNOWN (reported 806-cycle trial), missing originals: '+', '.join(missing))
            self.assertEqual(classifier.classify_run(classifier.ROOT/relative)['prediction'], 'UNKNOWN')
    setattr(PhysicalReplayTest, 'test_original_'+relative.replace('\\','/').split('/')[-1].replace('.','_').replace('-','_'), test)

if __name__ == '__main__':
    unittest.main()

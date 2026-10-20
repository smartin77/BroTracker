import collections
import json
import os
from pathlib import Path
import pty
import tempfile
import time
import tty
import unittest
from unittest.mock import Mock, patch
import offline_runner as r

class RunnerTests(unittest.TestCase):
    def setUp(self):
        base=Path(os.environ.get('BROTRACKER_TEST_DIR','/tmp'));base.mkdir(parents=True,exist_ok=True)
        self.tmp=tempfile.TemporaryDirectory(prefix='synthetic-offline-',dir=base);self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)
    def experiment(self):
        e=r.Experiment(self.root,self.root/'workload');self.addCleanup(e.close_files);return e
    def fixture(self):
        h=2166136261
        for n in [1,1,12500&255,12500>>8,0,0,60,1,0]: h=((h^n)*16777619)&0xffffffff
        return ['BTPATTERN1 BEGIN 2 7 1 1 12500 1','BTPATTERN1 CELL 2 7 0 60 1 0','BTPATTERN1 END 2 7 1 '+str(h)]
    def test_incomplete_pcm_coverage_is_rejected(self):
        r.verify_pcm_coverage({2:[0,100],3:[0,100]},10,90)
        for bounds in [{2:[],3:[0,100]},{2:[20,100],3:[0,100]},{2:[0,100],3:[0,80]}]:
            with self.assertRaises(r.Failure):r.verify_pcm_coverage(bounds,10,90)
    def test_pattern_tempo_and_checksum_readback(self):
        lines=self.fixture();p=r.snapshot_content(lines,'BTPATTERN1',7)
        self.assertEqual(p['tempo_hundredths'],12500);self.assertEqual(p['cells'],[[60,1,0]])
        for bad in [lines[:-1],lines+[lines[1]],lines[:1]+[lines[1].replace('60','61')]+lines[2:]]:
            with self.assertRaises(r.Failure):r.snapshot_content(bad,'BTPATTERN1',7)
    def test_matching_btx_workload_rejects_content_tempo_and_gains(self):
        e=self.experiment();d=e.root/'btx';d.mkdir()
        expected=r.snapshot_content(self.fixture(),'BTPATTERN1',7);expected['gains']=[32768]*8
        (d/'workload.json').write_text(json.dumps(expected));e.check_workload(d,{'pattern':expected})
        for key,value in [('tempo_hundredths',13000),('cells',[[61,1,0]]),('gains',[0]*8)]:
            altered=dict(expected);altered[key]=value;(d/'workload.json').write_text(json.dumps(altered))
            with self.assertRaises(r.Failure):e.check_workload(d,{'pattern':expected})
    def test_serial_has_no_proxy_and_retains_fragments(self):
        # PTY is a synthetic device only, never a production relay.
        master,slave=pty.openpty();tty.setraw(slave);os.set_blocking(slave,False)
        self.addCleanup(os.close,master);self.addCleanup(os.close,slave)
        s=r.SerialSession.__new__(r.SerialSession);s.fd=slave;s.pending=bytearray();s.seq=0;s.history=collections.deque(maxlen=4096);s.event=Mock()
        s.rx=(self.root/'rx').open('wb');self.addCleanup(s.rx.close)
        os.write(master,b'BTTEST1 STATE PL');s.pump(.01);s.pump(.001);self.assertEqual(s.seq,0)
        os.write(master,b'AYING\n');s.pump(.01);s.rx.flush()
        self.assertEqual(s.history[-1][1],'BTTEST1 STATE PLAYING')
        self.assertEqual((self.root/'rx').read_bytes(),b'BTTEST1 STATE PLAYING\n')
        self.assertFalse(hasattr(s,'master'));self.assertFalse(hasattr(r,'Relay'))
    def test_no_competing_serial_reader(self):
        e=self.experiment();e.btx=Mock();e.btx.poll.return_value=None
        with patch.object(r.os,'open') as opened:
            with self.assertRaises(r.Failure):e.open_serial(e.root/'forbidden')
            opened.assert_not_called()
    def test_missing_stale_and_failed_status(self):
        e=self.experiment();self.assertIsNone(e.btx_status(e.root))
        for value in [dict(schema=1,ns=0,phase='running'),dict(schema=1,ns=time.monotonic_ns(),phase='failed',error='fixture')]:
            (e.root/'status.json').write_text(json.dumps(value))
            with self.assertRaises(r.Failure):e.btx_status(e.root)
    def test_premature_exit_and_setup_timeout(self):
        e=self.experiment();e.ensure_bridge=Mock();e.btx=Mock();e.btx.poll.return_value=0
        with self.assertRaises(r.Failure):e.wait_status(e.root,lambda v:True,time.monotonic()+1)
        e.btx.poll.return_value=None
        with self.assertRaises(r.Failure):e.wait_status(e.root,lambda v:True,time.monotonic()-.01)
    def test_active_phase_owned_by_runner_and_coverage(self):
        e=self.experiment();e.ensure_bridge=Mock();e.btx=Mock();e.btx.poll.return_value=None;e.status=Mock()
        clock=[10.];initial=dict(tick=100,pattern_hash=1,tempo_hundredths=12500,gains=[32768]*8)
        def status(directory):return dict(initial,tick=100+int((clock[0]-10)*100),phase='running',running=True,valid=True)
        e.btx_status=status
        with patch.object(r.time,'monotonic',side_effect=lambda:clock[0]),patch.object(r.time,'monotonic_ns',side_effect=lambda:int(clock[0]*1e9)),patch.object(r.time,'sleep',side_effect=lambda n:clock.__setitem__(0,clock[0]+n)):
            value=e.active_phase(e.root,initial)
        self.assertGreaterEqual(value['end_ns']-value['start_ns'],60_000_000_000)
        self.assertGreater(value['final']['tick'],value['initial']['tick'])
        self.assertGreaterEqual(r.RECORD_SECONDS,r.SETUP_SECONDS+2*r.PHASE_SECONDS+r.HANDOFF_SECONDS+30)
    def test_release_exit_and_first_observation_are_distinct(self):
        e=self.experiment();e.btx=Mock();e.btx.pid=123;e.btx.wait.return_value=0
        released=time.monotonic_ns()-1000000
        (e.root/'status.json').write_text(json.dumps(dict(schema=1,ns=released,phase='released',error='')))
        value,exited=e.release_btx(e.root)
        self.assertLess(value['ns'],exited);self.assertIsNone(e.btx)
        self.assertEqual((e.root/'command.txt').read_text(),'RELEASE\n')
    def test_cleanup_waits_for_owned_btx_then_stops_and_reports_failure(self):
        e=self.experiment();order=[];child=Mock();child.pid=9;child.poll.side_effect=[None,0]
        child.wait.side_effect=lambda **kw:order.append('reaped') or 0
        e.btx=child;e.playback_owned=True;e.session=Mock()
        def request(command,predicate):
            order.append(command);return 'BTTEST1 STOPPED' if command.endswith('STOP') else 'BTTEST1 STATE IDLE'
        e.session.request.side_effect=request
        self.assertEqual(e.cleanup(),[]);self.assertEqual(order[:3],['reaped','BTTEST1 STOP','BTTEST1 STATUS'])
        child.kill.assert_called_once()
        e.playback_owned=True;e.open_serial=Mock(side_effect=r.Failure('device unavailable'))
        self.assertIn('STOP/idle unconfirmed',e.cleanup()[0])
    def test_stop_does_not_affect_unowned_playback(self):
        e=self.experiment();e.open_serial=Mock();e.stop_playback();e.open_serial.assert_not_called()
    def test_sparse_off_checks_are_scheduled_consistently(self):
        e=self.experiment();e.ensure_bridge=Mock();e.session=Mock();e.open_serial=Mock(return_value=e.session)
        clock=[0.];calls=[]
        def observe():
            calls.append(clock[0]);return dict(ns=int(clock[0]*1e9),position=dict(tick=100+int(clock[0]*100)))
        e.session.observe.side_effect=observe
        with patch.object(r.time,'monotonic_ns',side_effect=lambda:int(clock[0]*1e9)),patch.object(r.time,'sleep',side_effect=lambda n:clock.__setitem__(0,clock[0]+n)):
            result=e.off_phase(e.root/'off',{'tick':99})
        self.assertEqual(calls,[0,30,60]);self.assertEqual(result['end_ns']-result['start_ns'],60_000_000_000)
    def test_failed_attempt_and_stale_artifact_are_preserved(self):
        e=self.experiment();package=self.root/'deploy/arkos';package.mkdir(parents=True)
        (package/'BroTrackerAlsaBridge').write_bytes(b'stale')
        (package/'bridge-clicks-artifacts.json').write_text(json.dumps({'sha256':{'BroTrackerAlsaBridge':'wrong'}}))
        with patch.object(r.subprocess,'Popen') as spawn:
            self.assertFalse(e.run());spawn.assert_not_called()
        value=json.loads((e.root/'summary.json').read_text());self.assertIn('artifact hash mismatch',value['failure'])
        self.assertTrue((e.root/'SUMMARY.txt').exists());self.assertTrue((e.root/'events.jsonl').exists())
    def test_matched_condition_orchestration_uses_btx_and_no_proxy(self):
        e=self.experiment();e.package=self.root;e.binary=self.root/'bridge';e.terminal=self.root/'btx'
        pattern=r.snapshot_content(self.fixture(),'BTPATTERN1',7);pattern['gains']=[65536]*8
        workload=dict(pattern=pattern,sample_metadata=[],available_device_pcm_hashes={},sample_identity='synthetic-unverified')
        e.readback=Mock(return_value=workload);e.close_serial=Mock();e.show=Mock();e.hide=Mock();e.status=Mock()
        e.wait_status=Mock(side_effect=[{'phase':'prepared'},{'phase':'running','tick':1}, {'phase':'prepared'},{'phase':'running','tick':1}])
        e.active_phase=Mock(return_value={'start_ns':10,'end_ns':70})
        e.off_phase=Mock(return_value={'start_ns':80,'end_ns':200})
        def stop():e.playback_owned=False
        e.stop_playback=Mock(side_effect=stop)
        def launch(command,log,**kwargs):
            child=Mock();child.pid=123;child.poll.return_value=None;child.wait.return_value=0
            if command[0]==str(e.binary):
                log.write_text('Queue capacity: synthetic\n')
                self.assertEqual(kwargs['env']['BROTRACKER_BRIDGE_CLICKS_LIMIT_SECONDS'],'240')
                if log.parent.name=='on':
                    trace=log.parent/'bridge-synthetic';trace.mkdir()
                    (trace/'run.json').write_text(json.dumps({'limit_seconds':240}))
                    (trace/'transfers.tsv').write_text('kind\tresult\tns\n2\t1\t0\n3\t1\t0\n2\t1\t300\n3\t1\t300\n')
            else:
                self.assertEqual(command[1],'--offline-click-diagnostic')
                self.assertNotIn('BROTRACKER_DIAGNOSTIC_SERIAL',kwargs['env'])
                d=Path(command[2]);(d/'workload.json').write_text(json.dumps(pattern))
                def wait(**unused):
                    self.assertEqual((d/'command.txt').read_text(),'RELEASE\n')
                    (d/'status.json').write_text(json.dumps(dict(schema=1,ns=time.monotonic_ns()-1000,phase='released',error='',tick=2)))
                    return 0
                child.wait.side_effect=wait
            return child
        e.launch=launch
        with patch.object(r,'analyze',return_value={'complete':True,'accepted_equals_mapped_received':True}):
            e.condition('on',4000000000);e.condition('off',4000000004)
        self.assertEqual(len(e.results),2);self.assertTrue(all(v['success'] for v in e.results))
        self.assertEqual(e.stop_playback.call_count,2);self.assertEqual(e.active_phase.call_count,2)
        self.assertTrue((e.root/'on/analysis.json').exists());self.assertTrue((e.root/'off/result.json').exists())
        self.assertFalse(e.playback_owned)

    def test_working_pattern_is_never_replaced(self):
        e=self.experiment();s=Mock();e.session=s
        baseline=self.fixture();working=[line.replace('BTPATTERN1','BTPREVIEW1').replace(' 7 ',' 8 ') for line in self.fixture()]
        p=r.snapshot_content(baseline,'BTPATTERN1',7)
        # The second valid snapshot has a different tempo/content signature.
        different=dict(p,tempo_hundredths=13000)
        s.seq=0;s.history=[];s.request.return_value='BTTEST1 STATE IDLE'
        with patch.object(r,'snapshot_content',side_effect=[p,different]):
            with self.assertRaises(r.Failure):e.readback(e.root/'read',7)
        self.assertFalse(any('SET' in c[0][0] or 'START' in c[0][0] for c in s.request.call_args_list))

if __name__=='__main__':unittest.main()

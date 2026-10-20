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
import offline_metadata as metadata
import prepare_offline as preparation

class RunnerTests(unittest.TestCase):
    def setUp(self):
        base=Path(os.environ.get('BROTRACKER_TEST_DIR','/tmp'));base.mkdir(parents=True,exist_ok=True)
        self.tmp=tempfile.TemporaryDirectory(prefix='synthetic-offline-',dir=base);self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)
    def experiment(self):
        e=r.Experiment(self.root,self.root/'workload');self.addCleanup(e.close_files);return e
    def test_optional_context_missing_fill_and_malformed_never_gate(self):
        missing=self.root/'missing'
        self.assertEqual(metadata.supplementary(missing,self.root)['status'],'unknown')
        for i,data in enumerate([b'{"topology":"FILL"}',b'not json',b'x'*16385]):
            d=self.root/str(i);d.mkdir();p=d/'manual';p.write_bytes(data)
            result=metadata.supplementary(p,d)
            self.assertFalse(result['verified']);self.assertEqual(p.read_bytes(),data)
            if len(data)<=16384:self.assertEqual((d/'supplementary-user-manifest.original').read_bytes(),data)
    def test_unknown_metadata_is_descriptive_and_bounded(self):
        self.assertEqual(metadata.read_fact(self.root/'absent')['status'],'unknown')
        p=self.root/'large';p.write_bytes(b'x'*65537)
        self.assertEqual(metadata.read_fact(p)['status'],'unknown')
        with patch.object(metadata,'command_fact',return_value={'status':'unknown','reason':'synthetic unavailable'}):
            value=metadata.collect(self.root)
        self.assertEqual(value['audible_click_status'],'NOT YET CONFIRMED')
        self.assertEqual(value['physical_wiring']['status'],'unknown')
    def test_preparation_idempotent_preserves_unrelated_and_old_entry(self):
        repo=self.root/'checkout';ports=self.root/'ports'
        (repo/'tools/arkos/port').mkdir(parents=True);(repo/'tools/alsa_bridge').mkdir(parents=True)
        (repo/'deploy/arkos').mkdir(parents=True)
        (repo/'deploy/arkos/bridge-clicks-artifacts.json').write_text('{}')
        (repo/'tools/alsa_bridge/offline_runner.py').write_text('# synthetic')
        source=repo/'tools/arkos/port/BroTracker Bridge Clicks.sh';source.write_bytes(b'#!/bin/sh\n')
        ports.mkdir();unrelated=ports/'User.sh';unrelated.write_bytes(b'keep')
        preparation.install(repo,ports);before={p.name:p.read_bytes() for p in ports.iterdir()}
        preparation.install(repo,ports);self.assertEqual(before,{p.name:p.read_bytes() for p in ports.iterdir()})
        source.write_bytes(b'#!/bin/sh\necho updated\n');preparation.install(repo,ports)
        self.assertEqual(unrelated.read_bytes(),b'keep')
        self.assertEqual(list(ports.glob('*.previous-*'))[0].read_bytes(),b'#!/bin/sh\n')
        link=self.root/'linked';link.symlink_to(ports,target_is_directory=True)
        with self.assertRaises(RuntimeError):preparation.install(repo,link)
    def test_installed_launcher_allows_unset_portmaster_variables(self):
        import subprocess
        repo=self.root/'checkout';ports=self.root/'ports';xdg=self.root/'xdg'
        (repo/'tools/arkos/port').mkdir(parents=True)
        (repo/'tools/alsa_bridge').mkdir(parents=True)
        (repo/'deploy/arkos').mkdir(parents=True)
        launcher=Path(__file__).resolve().parents[1]/'arkos/port/BroTracker Bridge Clicks.sh'
        original=launcher.read_bytes().replace(b'\r\n',b'\n')
        (repo/'tools/arkos/port'/launcher.name).write_bytes(original)
        (repo/'deploy/arkos/bridge-clicks-artifacts.json').write_text('{}')
        (repo/'tools/alsa_bridge/offline_runner.py').write_text('print("synthetic runner reached")\n')
        preparation.install(repo,ports)
        installed=ports/launcher.name
        self.assertEqual(installed.read_bytes(),original)
        control=xdg/'PortMaster';control.mkdir(parents=True)
        (control/'device_info.txt').write_text('echo "device_info:${DEVICE_INFO_VERSION}"\n')
        (control/'control.txt').write_text(
            'source "$controlfolder/device_info.txt"\n'
            'get_controls() { echo "controls:${UNSET_CONTROLS}"; }\n'
            'pm_platform_helper() { echo "helper:${UNSET_HELPER}"; }\n'
            'pm_finish() { echo "finish:${UNSET_FINISH}"; }\n')
        env=dict(os.environ,XDG_DATA_HOME=str(xdg))
        for key in ['DEVICE_INFO_VERSION','UNSET_CONTROLS','UNSET_HELPER','UNSET_FINISH']:
            env.pop(key,None)
        # Reproduce the confirmed failure independently, then exercise the installed entry.
        failing=subprocess.run(['bash','-uc','source "$1"','fixture',str(control/'device_info.txt')],
                               env=env,capture_output=True,text=True,timeout=5)
        self.assertNotEqual(failing.returncode,0)
        self.assertIn('DEVICE_INFO_VERSION: unbound variable',failing.stderr)
        result=subprocess.run(['bash',str(installed)],env=env,capture_output=True,text=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        for marker in ['device_info:','controls:','helper:','synthetic runner reached']:
            self.assertIn(marker,result.stdout)
        self.assertNotIn('unbound variable',result.stderr)
        self.assertNotIn('finish:',result.stdout)  # External/network-capable return hook is never called.
    def test_non_utf8_process_names_preserve_ownership_refusal(self):
        proc=self.root/'proc';proc.mkdir()
        for pid,data in [('118',b'irq/45-\xc8\n'),('120',b'irq/152-\xc8\n')]:
            d=proc/pid;d.mkdir();(d/'comm').write_bytes(data)
        (proc/'121').mkdir()  # Process disappeared before comm could be read.
        r.refuse_existing_sessions(proc)
        owner=proc/'122';owner.mkdir()
        for name in [b'BroTrackerTerm',b'BroTrackerAlsa',b'offline_runner']:
            (owner/'comm').write_bytes(name+b'\n')
            with self.assertRaisesRegex(r.Failure,'existing user audio/UI session'):
                r.refuse_existing_sessions(proc)
    def test_unexpected_exception_retains_traceback_and_cleans_up(self):
        e=self.experiment();e.preflight=Mock(side_effect=RuntimeError('synthetic unexpected crash'))
        e.cleanup=Mock(return_value=[])
        self.assertFalse(e.run());e.cleanup.assert_called_once();self.assertTrue(e.cleanup_done)
        trace=(e.root/'runner-traceback.txt').read_text()
        self.assertIn('Traceback (most recent call last)',trace)
        self.assertIn('RuntimeError: synthetic unexpected crash',trace)
        summary=json.loads((e.root/'summary.json').read_text())
        self.assertEqual(summary['failure'],'RuntimeError: synthetic unexpected crash')
        self.assertTrue((e.root/'SUMMARY.txt').exists())
    def test_cancellation_at_each_stage_preserves_evidence_and_owned_cleanup(self):
        for stage in ['preflight','BTX-active','handoff','BTX-off']:
            with self.subTest(stage=stage):
                e=self.experiment();e.status=Mock()
                child=Mock();child.pid=123;child.poll.return_value=None;child.wait.return_value=0
                child.kill.side_effect=lambda:setattr(child.poll,'return_value',0)
                serial=Mock()
                serial.request.side_effect=lambda command,predicate: 'BTTEST1 STOPPED' if command.endswith('STOP') else 'BTTEST1 STATE IDLE'
                def setup():
                    e.session=serial
                    if stage=='preflight': e.cancel()
                    e.playback_owned=True;e.btx=child;e.bridge=Mock();e.bridge.wait.return_value=0
                e.preflight=setup
                def condition(mode,identity):
                    if stage=='BTX-active':
                        e.ensure_bridge=Mock();e.btx_status=Mock(return_value=dict(phase='running',running=True,valid=True,tick=2,pattern_hash=1,tempo_hundredths=12500,gains=[]))
                        with patch.object(r.time,'sleep',side_effect=lambda n:e.cancel()):
                            e.active_phase(e.root,dict(tick=1,pattern_hash=1,tempo_hundredths=12500,gains=[]))
                    elif stage=='handoff':
                        child.wait.side_effect=[r.Cancelled('cancel during serial handoff'),0]
                        e.release_btx(e.root)
                    else:
                        e.btx=None;e.ensure_bridge=Mock();e.open_serial=Mock(return_value=serial)
                        serial.observe.return_value=dict(ns=time.monotonic_ns(),position=dict(tick=2))
                        with patch.object(r.time,'sleep',side_effect=lambda n:e.cancel()):
                            e.off_phase(e.root/'off',dict(tick=1))
                e.condition=condition
                self.assertFalse(e.run())
                summary=json.loads((e.root/'summary.json').read_text())
                self.assertEqual(summary['outcome'],'CANCELLED');self.assertFalse(summary['success'])
                self.assertTrue(e.cleanup_done);serial.close.assert_called_once()
                if stage!='preflight':
                    self.assertEqual([call[0][0] for call in serial.request.call_args_list],['BTTEST1 STOP','BTTEST1 STATUS'])
                else: self.assertFalse(serial.request.called)
                self.assertTrue((e.root/'events.jsonl').exists())
    def test_result_deadlines_and_immediate_exit(self):
        for success,seconds in [(False,r.RESULT_SECONDS),(True,r.RESULT_SECONDS)]:
            e=self.experiment();e.results_finalized=True;e.show=Mock();e.viewer=Mock();e.viewer.poll.return_value=None
            clock=[0.]
            with patch.object(r.time,'monotonic',side_effect=lambda:clock[0]),patch.object(r.time,'sleep',side_effect=lambda n:clock.__setitem__(0,clock[0]+n)):
                e.wait_results(success)
            self.assertGreaterEqual(clock[0],seconds);self.assertLess(clock[0],seconds+.2)
            e.cancelled=False;e.finishing=True;clock[0]=0
            with patch.object(r.time,'monotonic',side_effect=lambda:clock[0]),patch.object(r.time,'sleep',side_effect=lambda n:e.cancel()):
                e.wait_results(success)
            self.assertFalse(e.cancelled);self.assertTrue(e.results_dismissed);self.assertEqual(clock[0],0)
    def test_precise_cancellation_source_is_retained_in_timeline_and_reason(self):
        e=self.experiment();e.cancel_producers[123]='btx'
        source=dict(producer_pid=123,view='btx',phase='RUNNING BTX-active',reason='controller_back',
                    sdl_event_type=1617,input_kind='button',input_id=4,controller_instance=0)
        (e.root/'cancel-123.json').write_text(json.dumps(source))
        with self.assertRaisesRegex(r.Cancelled,'btx: controller_back .*button=4'):
            e.cancel_from_view()
        records=[json.loads(line) for line in (e.root/'events.jsonl').read_text().splitlines()]
        record=next(v for v in records if v['event']=='cancellation_requested')
        self.assertEqual(record['source'],source)
    def test_intentional_view_shutdown_records_transition_without_cancel(self):
        e=self.experiment();e.viewer=Mock();e.viewer.pid=123;e.viewer.poll.return_value=None
        e.viewer.wait.return_value=0
        e.hide()
        self.assertIsNone(e.viewer);self.assertFalse(e.cancelled)
        records=[json.loads(line) for line in (e.root/'events.jsonl').read_text().splitlines()]
        self.assertEqual(records[-1]['event'],'owned_view_shutdown_requested')
    def test_cancelled_partial_run_defers_analysis_and_returns_after_cleanup(self):
        e=self.experiment();directory=e.root/'on';directory.mkdir();(directory/'bridge-partial').mkdir()
        e.preflight=Mock(side_effect=r.Cancelled('emergency B'))
        e.run_analysis=Mock(side_effect=AssertionError('must not analyze partial evidence on emergency exit'))
        self.assertFalse(e.run());e.run_analysis.assert_not_called()
        self.assertTrue((directory/'analysis-deferred.json').exists())
        self.assertEqual(json.loads((e.root/'summary.json').read_text())['outcome'],'CANCELLED')
    def test_saved_completed_result_is_immutable_when_b_dismisses(self):
        e=self.experiment();e.preflight=Mock()
        e.condition=lambda mode,identity:e.results.append(dict(mode=mode,success=True))
        self.assertTrue(e.run());before=(e.root/'summary.json').read_bytes()
        e.cancel('B dismisses completed results')
        self.assertFalse(e.cancelled);self.assertTrue(e.results_dismissed)
        self.assertEqual((e.root/'summary.json').read_bytes(),before)
        self.assertEqual(json.loads(before)['outcome'],'COMPLETED')
    def test_analysis_worker_is_cancellable_and_cleanup_reaps_only_owned_worker(self):
        e=self.experiment();d=e.root/'on';d.mkdir();trace=d/'bridge-synthetic';trace.mkdir()
        child=Mock();child.pid=555;child.poll.return_value=None;child.wait.return_value=0
        e.launch=Mock(return_value=child);e.check_view=Mock();e.status=Mock()
        with patch.object(r.time,'sleep',side_effect=lambda n:e.cancel('B during analysis')):
            with self.assertRaises(r.Cancelled):e.run_analysis(d,trace)
        self.assertEqual(e.cleanup(),[]);child.kill.assert_called_once();child.wait.assert_called_once()
    def test_analysis_worker_timeout_is_bounded_and_preserves_trace(self):
        e=self.experiment();d=e.root/'on';d.mkdir();trace=d/'bridge-synthetic';trace.mkdir()
        child=Mock();child.pid=555;child.poll.return_value=None;child.wait.return_value=0
        e.launch=Mock(return_value=child);e.check_view=Mock();e.status=Mock()
        with patch.object(r.time,'monotonic',side_effect=[0,r.ANALYSIS_SECONDS+1]):
            with self.assertRaisesRegex(r.Failure,'analysis timeout'):e.run_analysis(d,trace)
        self.assertTrue(trace.exists());self.assertEqual(e.cleanup(),[]);child.kill.assert_called_once()
    def test_real_analysis_worker_completes_with_strict_transfer_bounds(self):
        from test_analyze_clicks import ComparisonTests
        fixture=ComparisonTests('test_partial_zero_error_discard_accounting');fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        fixture.save()
        e=self.experiment();e.repo=Path(__file__).resolve().parents[2]
        d=e.root/'on';d.mkdir()
        report=e.run_analysis(d,fixture.root)
        self.assertTrue(report['complete']);self.assertTrue(report['accepted_equals_mapped_received'])
        self.assertEqual(set(report['pcm_transfer_bounds_ns']),{'capture','playback'})
        self.assertIsNone(e.analysis);self.assertTrue((d/'analysis.json').exists())
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
        e=self.experiment();e.ensure_bridge=Mock();e.status=Mock();e.session=Mock();e.open_serial=Mock(return_value=e.session)
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
        self.assertFalse(value['technical_success']);self.assertEqual(value['audible_click_status'],'NOT YET CONFIRMED')
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
        def analysis(directory,trace):
            report=dict(complete=True,accepted_equals_mapped_received=True,pcm_transfer_bounds_ns={'capture':[0,300],'playback':[0,300]})
            r.save(directory/'analysis.json',report);return report
        e.run_analysis=analysis
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

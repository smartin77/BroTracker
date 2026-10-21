#!/usr/bin/env python3
"""Bounded offline experiment; BTX exclusively owns serial during the active phase."""
import argparse
import collections
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import select
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time
import tty
import traceback
from offline_metadata import collect, supplementary
from usbmon_capture import Capture

PHASE_SECONDS=60
SETUP_SECONDS=60
RECORD_SECONDS=240
HANDOFF_SECONDS=8
CHECK_SECONDS=3
ANALYSIS_SECONDS=180
RESULT_SECONDS=3
class Failure(RuntimeError): pass
class Cancelled(Failure): pass

def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def save(path,value):
    with Path(path).open('x',encoding='utf-8') as f: json.dump(value,f,indent=2); f.write('\n')
def atomic(path,text):
    pending=path.with_name(path.name+'.pending'); pending.write_text(text,encoding='utf-8'); os.replace(pending,path)

def verify_pcm_coverage(bounds,start,end):
    for side,times in bounds.items():
        if not times or min(times)>start or max(times)<end:
            raise Failure('PCM does not cover both measured phases: '+str(side))

def position(line):
    m=re.fullmatch(r'BTPATTERN1 POS 2 ([012]) ([01]) ([0-9]+) ([0-9]+) ([0-9]+) ([0-9]+) ([0-9]+)',line)
    if not m: return None
    state,valid,tick,row,loop,rows,channels=map(int,m.groups())
    if not 1<=rows<=64 or not 1<=channels<=8: raise Failure('invalid telemetry dimensions')
    if valid and (state==0 or row!=tick//96%rows or loop!=tick//96//rows): raise Failure('incoherent telemetry')
    return dict(state=state,valid=valid,tick=tick,row=row,loop=loop,rows=rows,channels=channels)

def snapshot_content(lines,namespace,identity):
    chosen=[]
    for line in lines:
        fields=line.split()
        if len(fields)>3 and fields[0]==namespace and fields[2]=='2' and fields[3]==str(identity): chosen.append(fields)
    if not chosen or chosen[0][1]!='BEGIN' or chosen[-1][1]!='END': raise Failure('missing pattern readback')
    first,last=chosen[0],chosen[-1]
    if len(first)!=8 or len(last)!=6: raise Failure('malformed readback envelope')
    rows,channels,tempo,count=map(int,first[4:])
    if not 1<=rows<=64 or not 1<=channels<=8 or count!=rows*channels or len(chosen)!=count+2: raise Failure('pattern readback count mismatch')
    h=2166136261
    def byte(v):
        nonlocal h
        h=((h^v)*16777619)&0xffffffff
    for n in [rows,channels]+[(tempo>>(8*i))&255 for i in range(4)]: byte(n)
    cells=[]
    for index,f in enumerate(chosen[1:-1]):
        if len(f)!=8 or f[1]!='CELL' or int(f[4])!=index: raise Failure('pattern cell order/shape mismatch')
        values=list(map(int,f[5:]))
        if any(v<0 or v>255 for v in values) or values[2]>1 or not (24<=values[0]<=127 or values[0] in [254,255]): raise Failure('pattern cell byte out of range')
        cells.append(values)
        for n in values: byte(n)
    if int(last[4])!=count or int(last[5])!=h: raise Failure('pattern readback checksum mismatch')
    return dict(version=2,rows=rows,channels=channels,tempo_hundredths=tempo,cells=cells,pattern_hash=h)

def refuse_existing_sessions(proc_root=Path('/proc'),owned_pids=()):
    for proc in proc_root.glob('[0-9]*'):
        if int(proc.name) in owned_pids: continue
        try: name=(proc/'comm').read_bytes().strip()
        except (PermissionError,FileNotFoundError,ProcessLookupError): continue
        if name.startswith((b'BroTrackerTerm',b'BroTrackerAlsa',b'offline_runner')):
            raise Failure('existing user audio/UI session: '+str(proc)+' '+repr(name))


class SerialSession:
    """Bounded nonblocking requests, used only when BTX has no serial ownership."""
    def __init__(self,port,prefix,event):
        self.fd=None; self.rx=None; self.txfile=None
        self.event=event; self.history=collections.deque(maxlen=4096); self.seq=0; self.pending=bytearray()
        try:
            self.fd=os.open(port,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
            fcntl.ioctl(self.fd,termios.TIOCEXCL)
            tty.setraw(self.fd)
            attrs=termios.tcgetattr(self.fd); attrs[2]|=termios.CLOCAL|termios.CREAD; attrs[2]&=~termios.CRTSCTS
            attrs[4]=attrs[5]=termios.B115200; termios.tcsetattr(self.fd,termios.TCSANOW,attrs)
            fcntl.ioctl(self.fd,termios.TIOCMBIS,struct.pack('I',termios.TIOCM_DTR|termios.TIOCM_RTS))
            self.rx=Path(str(prefix)+'.rx').open('xb'); self.txfile=Path(str(prefix)+'.tx').open('xb')
        except Exception: self.close(); raise
    def pump(self,timeout=.05):
        ready,_,_=select.select([self.fd],[],[],timeout)
        if not ready: return
        data=os.read(self.fd,4096)
        if not data: raise Failure('serial disconnected')
        self.rx.write(data)
        for b in data:
            if b==10:
                text=bytes(self.pending).rstrip(b'\r').decode('ascii'); self.pending.clear(); self.seq+=1
                self.history.append((self.seq,text)); self.event('serial_line',seq=self.seq,text=text)
            else:
                self.pending.append(b)
                if len(self.pending)>4096: raise Failure('oversized serial line; raw bytes retained')
    def request(self,command,predicate,timeout=CHECK_SECONDS):
        after=self.seq; data=(command+'\n').encode(); offset=0; end=time.monotonic()+timeout
        self.event('serial_request',command=command)
        while time.monotonic()<end:
            if offset<len(data):
                _,write,_=select.select([],[self.fd],[],.02)
                if write:
                    try: n=os.write(self.fd,data[offset:])
                    except BlockingIOError: n=0
                    self.txfile.write(data[offset:offset+n]); offset+=n
            self.pump(.02)
            for seq,line in self.history:
                if seq>after and offset==len(data) and predicate(line): return line
                if seq>after and (line.startswith('BTTEST1 ERROR') or line.startswith('BTAUDIO1 ERROR')): raise Failure(line)
        raise Failure('serial request timeout: '+command)
    def observe(self):
        # A STATUS response is the freshness barrier; do not treat backed-up POS as current.
        state=self.request('BTTEST1 STATUS',lambda s:s.startswith('BTTEST1 STATE '))
        if state!='BTTEST1 STATE PLAYING': raise Failure('playback not confirmed: '+state)
        after=self.seq; end=time.monotonic()+CHECK_SECONDS; first=None
        while time.monotonic()<end:
            self.pump()
            for seq,line in self.history:
                if seq<=after: continue
                p=position(line)
                if p and p['state']==1 and p['valid']:
                    if first and p['tick']>first['tick']: return dict(ns=time.monotonic_ns(),position=p)
                    first=p; after=seq
        raise Failure('no advancing fresh telemetry after STATUS')
    def close(self):
        # Preserve any trailing fragment explicitly; do not parse/repair it.
        if self.pending: self.event('serial_trailing_fragment',hex=bytes(self.pending).hex())
        for f in [self.rx,self.txfile]:
            if f: f.close()
        self.rx=self.txfile=None
        if self.fd is not None: os.close(self.fd); self.fd=None

def terminate_owned(child,force=False):
    if child is None: return None
    if child.poll() is None: child.kill() if force else child.terminate()
    try: return child.wait(timeout=8)
    except subprocess.TimeoutExpired:
        child.kill(); child.wait(timeout=3)
        raise Failure('owned process needed forced shutdown; attempt failed')

class Experiment:
    def __init__(self,repo,workload,usbmon=False,read_integrity=False):
        self.repo=Path(repo).resolve(); self.package=self.repo/'deploy/arkos'
        base=self.repo/'assets/hardware_diagnostics/bridge_clicks'; base.mkdir(parents=True,exist_ok=True)
        self.root=Path(tempfile.mkdtemp(prefix=time.strftime('%Y%m%dT%H%M%SZ-',time.gmtime())+'offline-',dir=base))
        self.events=(self.root/'events.jsonl').open('x',encoding='utf-8'); self.status_path=self.root/'STATUS.txt'
        self.bridge=self.btx=self.viewer=self.session=self.analysis=None
        self.playback_owned=False; self.workload=Path(workload) if workload else None; self.results=[]; self.config_path=None
        self.config_before=None; self.owned_logs=[]; self.recording_error=False; self.display_verified=False
        self.release_ns=None; self.active_dir=None; self.cleanup_done=False; self.cancelled=False; self.finishing=False; self.cancel_producers={}; self.cancel_records=set(); self.current_phase="initializing"; self.results_finalized=False; self.results_dismissed=False
        self.read_integrity_requested=read_integrity
        self.usb_requested=usbmon or read_integrity;self.usb_capture=None;self.usb_state=None
    def retain_traceback(self):
        try:
            with (self.root/'runner-traceback.txt').open('a',encoding='utf-8') as f:
                traceback.print_exc(file=f)
        except OSError:
            self.recording_error=True  # Still proceed to owned-process/serial cleanup.
    def event(self,kind,**details):
        try:
            record=dict(ns=time.monotonic_ns(),event=kind); record.update(details)
            self.events.write(json.dumps(record)+'\n'); self.events.flush()
        except OSError: self.recording_error=True
    def status(self,text):
        self.current_phase=text.splitlines()[0] if text else "unknown"
        atomic(self.status_path,text+'\n')
        print(text,flush=True); self.event('status',text=text)
    def launch(self,command,log,**kwargs):
        f=log.open('xb'); self.owned_logs.append(f)
        child=subprocess.Popen(command,stdout=f,stderr=subprocess.STDOUT,**kwargs)
        if '--diagnostic-status' in command or '--offline-click-diagnostic' in command:
            self.cancel_producers[child.pid]='status' if '--diagnostic-status' in command else 'btx'
        self.event('owned_process_started',pid=child.pid,command=list(map(str,command))); return child
    def show(self):
        if self.viewer is None:
            self.viewer=self.launch([str(self.terminal),'--diagnostic-status',str(self.status_path)],self.root/('display-'+str(time.monotonic_ns())+'.log'),cwd=self.package,env=dict(os.environ,BROTRACKER_DIAGNOSTIC_RUNNER_PID=str(os.getpid()),BROTRACKER_DIAGNOSTIC_CANCEL_DIR=str(self.root)))
    def hide(self):
        if self.viewer:
            self.event('owned_view_shutdown_requested',pid=self.viewer.pid,view='status',phase=self.current_phase)
            terminate_owned(self.viewer)
        self.viewer=None
    def open_serial(self,prefix):
        if self.btx is not None and self.btx.poll() is None: raise Failure('refusing competing BTX serial reader')
        if self.session is not None: raise Failure('serial session already open')
        owners=subprocess.run(['fuser',self.port],capture_output=True,timeout=3)
        if owners.returncode!=1: raise Failure('serial busy or ownership uncertain; no process killed')
        self.session=SerialSession(self.port,prefix,self.event); self.event('runner_serial_open'); return self.session
    def close_serial(self):
        if self.session: self.session.close(); self.session=None; self.event('runner_serial_closed')
    def preflight(self):
        self.status('PREPARING: validating offline artifacts and existing sessions')
        self.lock=(self.repo/'assets/hardware_diagnostics/bridge_clicks/.runner.lock').open('a')
        fcntl.flock(self.lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        manifest=json.loads((self.package/'bridge-clicks-artifacts.json').read_text())
        self.terminal=self.package/'BroTrackerTerminal'; self.binary=self.package/'BroTrackerAlsaBridge'
        # Show cancellation UI before dependency, config, device and serial checks.
        if self.terminal.exists() and sha(self.terminal)==manifest['sha256'].get('BroTrackerTerminal'):
            self.display_verified=True; self.show()
        for name,flag,expected in [('BroTrackerAlsaBridge','--diagnostic-info','bridge-clicks-offline-v2'),
                                   ('BroTrackerTerminal','--offline-diagnostic-info','btx-offline-clicks-v2')]:
            path=self.package/name
            if sha(path)!=manifest['sha256'][name]: raise Failure('artifact hash mismatch: '+name)
            if not os.access(path,os.X_OK): raise Failure('binary not executable: '+name)
            data=path.read_bytes()[:20]
            if data[:4]!=b'\x7fELF' or struct.unpack_from('<H',data,18)[0]!=183:
                raise Failure('not an ARM64 ELF: '+name)
            result=subprocess.run([str(path),flag],capture_output=True,timeout=10,check=True)
            if json.loads(result.stdout)['id']!=expected: raise Failure('stale diagnostic binary: '+name)
            if self.read_integrity_requested and name=='BroTrackerAlsaBridge' and json.loads(result.stdout).get('read_integrity')!=1:
                raise Failure('bridge lacks read-return integrity capability')
            deps=subprocess.run(['ldd','-r',str(path)],capture_output=True,timeout=15)
            (self.root/(name+'-dependencies.txt')).write_bytes(deps.stdout+deps.stderr)
            if deps.returncode or b'not found' in deps.stdout+deps.stderr or b'undefined symbol' in deps.stdout+deps.stderr:
                raise Failure('missing binary dependency: '+name)
        for name in ['assets/fonts/brotracker.btf','assets/fonts/brotracker.bfm','assets/dummy_my_tune.json']:
            data=(self.package/name).read_bytes()
            if Path(name).suffix in ['.json','.btf']: data=data.replace(b'\r\n',b'\n')
            if hashlib.sha256(data).hexdigest()!=manifest['assets_sha256'][name]: raise Failure('package asset hash mismatch: '+name)
        self.display_verified=True
        for path,digest in manifest.get('source_sha256',{}).items():
            resolved=(self.repo/path).resolve()
            if self.repo not in resolved.parents or hashlib.sha256(resolved.read_bytes().replace(b'\r\n',b'\n')).hexdigest()!=digest:
                raise Failure('artifact/source fingerprint mismatch: '+path)
        save(self.root/'artifacts.json',manifest)
        # Do not replace an existing bridge/BTX session, even if it is idle.
        refuse_existing_sessions(owned_pids=(self.viewer.pid,) if self.viewer else ())
        if shutil.disk_usage(self.root).free<512*1024*1024:
            raise Failure('need at least 512 MiB free storage')
        if self.usb_requested and shutil.disk_usage(self.root).free<1024*1024*1024:
            raise Failure('optional USB capture needs at least 1 GiB free storage')
        save(self.root/'supplementary-context.json',supplementary(self.workload,self.root))
        save(self.root/'automatic-metadata.json',collect(self.repo))
        cfg=subprocess.run([str(self.terminal),'--offline-diagnostic-config'],cwd=self.package,capture_output=True,timeout=10)
        (self.root/'config-preflight.txt').write_bytes(cfg.stdout+cfg.stderr)
        if cfg.returncode: raise Failure('persistent configuration incompatible; choose bridge or leave output absent; no config changed')
        values=dict(line.split('=',1) for line in cfg.stdout.decode().splitlines() if '=' in line)
        self.config_path=Path(values['path']); self.config_before=sha(self.config_path) if self.config_path.exists() else None
        if self.config_before: (self.root/'config-original.ini').write_bytes(self.config_path.read_bytes())
        self.show()
        ports=[]
        for item in Path('/sys/class/tty').glob('ttyACM*'):
            for parent in (item/'device').resolve().parents:
                if (parent/'idVendor').exists():
                    if (parent/'idVendor').read_text().strip()=='16c0' and (parent/'idProduct').read_text().strip()=='048a':
                        ports.append('/dev/'+item.name)
                    break
        if len(ports)!=1: raise Failure('need exactly one Teensy CDC 16c0:048a: '+repr(ports))
        self.port=ports[0]
        save(self.root/'connected-device.json',collect(self.repo,self.port))
        # Check existing owners before exclusive open. No arbitrary process kills.
        owners=subprocess.run(['fuser',self.port],capture_output=True,timeout=5)
        if owners.returncode!=1: raise Failure('serial device busy or ownership uncertain: '+owners.stdout.decode(errors='replace'))
        session=self.open_serial(self.root/'preflight')
        try:
            state=session.request('BTTEST1 HELLO',lambda s:s.startswith('BTTEST1 STATE '))
            if state not in ['BTTEST1 STATE IDLE','BTTEST1 STATE DONE']:
                raise Failure('preexisting playback is not idle; left untouched: '+state)
        finally: self.close_serial()
        check=subprocess.run([str(self.binary),'--preflight'],capture_output=True,timeout=15)
        (self.root/'alsa-preflight.log').write_bytes(check.stdout+check.stderr)
        if check.returncode: raise Failure('ALSA device access/negotiation failed (no competing owners were killed)')
        self.check_view()

    def readback(self,prefix,identity):
        session=self.session or self.open_serial(prefix)
        state=session.request('BTTEST1 HELLO',lambda s:s.startswith('BTTEST1 STATE '))
        if state not in ['BTTEST1 STATE IDLE','BTTEST1 STATE DONE'] and not self.playback_owned:
            raise Failure('existing playback must be idle; not replaced: '+state)
        baseline_after=session.seq
        session.request('BTPATTERN1 GET 2 '+str(identity),lambda s:s.startswith('BTPATTERN1 END 2 '+str(identity)+' '))
        baseline=snapshot_content([line for seq,line in session.history if seq>baseline_after],'BTPATTERN1',identity)
        working_after=session.seq
        session.request('BTPREVIEW1 GET 1 '+str(identity+1)+' 0',lambda s:s.startswith('BTPREVIEW1 END 2 '+str(identity+1)+' '))
        working=snapshot_content([line for seq,line in session.history if seq>working_after],'BTPREVIEW1',identity+1)
        if baseline!=working: raise Failure('volatile working pattern differs from published readback; not overwritten')
        gain_line=session.request('BTGAIN1 GET 1',lambda s:s.startswith('BTGAIN1 STATE 1 '))
        values=list(map(int,gain_line.split()[3:]))
        if len(values)!=10 or any(n<0 or n>65536 for n in values[2:]): raise Failure('malformed channel gains')
        baseline['gains']=values[2:]
        # Drain a bounded tail of already offered device metadata, never a recording callback.
        until=time.monotonic()+.3
        while time.monotonic()<until: session.pump(.02)
        metadata=sorted(set(line for _,line in session.history if line.startswith(('BTPATTERN1 WAVHASH ','BTPATTERN1 WAVMETA '))))
        hashes={}
        for line in metadata:
            fields=line.split()
            if fields[:3]==['BTPATTERN1','WAVHASH','1'] and len(fields)==7 and fields[4]=='1':
                hashes[fields[3]]=dict(frames=int(fields[5]),pcm_fnv1a=int(fields[6]))
        result=dict(pattern=baseline,sample_metadata=metadata,available_device_pcm_hashes=hashes,
                    sample_identity='Available device WAVHASH/WAVMETA retained; built-in/other sample bytes and complete slot-to-file mapping are not verified by this protocol.')
        save(Path(str(prefix)+'.readback.json'),result)
        if not (self.root/'automatic-workload.json').exists():
            save(self.root/'automatic-workload.json',dict(source='Read-only device pattern, tempo and gain queries; no user description',
                verified=result,firmware_protocol_evidence=[line for _,line in session.history
                    if line.startswith(('BTTEST1 ', 'BTPATTERN1 CAP', 'BTAUDIO1 '))],
                exact_firmware_build=dict(status='unknown',reason='Existing protocol does not advertise a unique firmware build identity')))

        return result
    def check_view(self):
        if self.viewer is not None and self.viewer.poll() is not None:
            raise Failure('Diagnostic status view exited unexpectedly: '+str(self.viewer.returncode))
    def ensure_bridge(self):
        self.check_view()
        if self.recording_error: raise Failure('runner timeline write failed')
        if self.bridge is None or self.bridge.poll() is not None: raise Failure('owned bridge exited prematurely')
        if shutil.disk_usage(self.root).free<64*1024*1024: raise Failure('storage reserve exhausted')
    def btx_status(self,directory):
        path=directory/'status.json'
        if not path.exists(): return None
        if path.stat().st_size>2048: raise Failure('oversized BTX status')
        value=json.loads(path.read_text())
        now=time.monotonic_ns()
        if value.get('schema')!=1 or not 0<=now-value['ns']<=2000000000: raise Failure('stale/incompatible BTX status')
        if value.get('error') or value['phase']=='failed': raise Failure('BTX diagnostic failure: '+str(value))
        self.event('btx_status',status=value)
        return value
    def wait_status(self,directory,predicate,deadline):
        while time.monotonic()<deadline:
            self.ensure_bridge()
            if self.btx.poll() is not None: raise Failure('BTX exited before required status')
            value=self.btx_status(directory)
            if value and predicate(value): return value
            time.sleep(.1)
        raise Failure('BTX setup/status timeout')
    def check_workload(self,directory,expected):
        value=json.loads((directory/'workload.json').read_text())
        if value!=expected['pattern']: raise Failure('BTX readback differs from verified pattern/tempo/gains')
    def active_phase(self,directory,initial):
        start=time.monotonic_ns(); deadline=time.monotonic()+PHASE_SECONDS
        previous=initial['tick']; last=initial; progress_ns=start
        self.event('phase_begin',phase='BTX-active',start_ns=start,position=initial)
        while time.monotonic()<deadline:
            self.ensure_bridge()
            if self.btx.poll() is not None: raise Failure('BTX exited during active phase')
            value=self.btx_status(directory)
            if not value or value['phase']!='running' or not value['running'] or not value['valid'] or value['tick']<previous:
                raise Failure('active playback coverage missing/reset/not confirmed')
            if (value['pattern_hash'],value['tempo_hundredths'],value['gains'])!=(initial['pattern_hash'],initial['tempo_hundredths'],initial['gains']):
                raise Failure('active workload mismatch')
            now=time.monotonic_ns()
            if value['tick']>previous: progress_ns=now
            elif now-progress_ns>2000000000: raise Failure('active playback ticks stopped advancing')
            previous=value['tick']; last=value
            self.status('RUNNING '+(self.active_dir.name if self.active_dir else 'test')+' / BTX-active: '+str(max(0,int(deadline-time.monotonic())))+'s\nNo input required.\nAuto-return after both pairs.')
            time.sleep(.5)
        self.ensure_bridge()
        if self.btx.poll() is not None: raise Failure('BTX exited before full active coverage')
        if last['tick']<=initial['tick']: raise Failure('no active playback progress')
        end=time.monotonic_ns(); self.event('phase_end',phase='BTX-active',end_ns=end,position=last)
        return dict(start_ns=start,end_ns=end,initial=initial,final=last)
    def release_btx(self,directory):
        self.event('btx_release_requested',pid=self.btx.pid)
        atomic(directory/'command.txt','RELEASE\n')
        code=self.btx.wait(timeout=HANDOFF_SECONDS)
        exited=time.monotonic_ns(); self.event('btx_exit_observed',pid=self.btx.pid,code=code,exit_observed_ns=exited)
        value=self.btx_status(directory)
        if code!=0 or not value or value['phase']!='released': raise Failure('BTX did not report clean diagnostic serial release')
        self.release_ns=value['ns']
        if self.release_ns>exited: raise Failure('invalid serial release ordering')
        self.event('serial_release_report',serial_release_ns=self.release_ns,btx_exit_observed_ns=exited)
        self.btx=None
        return value,exited
    def off_phase(self,prefix,previous):
        session=self.open_serial(prefix)
        first=session.observe()
        self.event('first_btx_off_observation',**first)
        if first['position']['tick']<previous['tick']: raise Failure('timeline reset across BTX serial handoff')
        # No polling worker between three scheduled bursts; keep FD exclusively
        # owned. Each STATUS + two fresh POS burst is bounded to <=6 seconds.
        start=time.monotonic_ns(); deadline=start+PHASE_SECONDS*1000000000
        observations=[first]
        self.event('phase_begin',phase='BTX-off',start_ns=start,position=first)
        for offset in [PHASE_SECONDS/2,PHASE_SECONDS]:
            target=start+int(offset*1e9)
            while time.monotonic_ns()<target:
                self.ensure_bridge()
                self.status('RUNNING '+(self.active_dir.name if self.active_dir else 'test')+' / BTX-off: '+str(max(0,int((deadline-time.monotonic_ns())/1e9)))+'s\nNo input required.\nAuto-return after both pairs.')
                time.sleep(min(.5,max(0,(target-time.monotonic_ns())/1e9)))
            self.ensure_bridge(); value=session.observe(); self.ensure_bridge()
            if value['position']['tick']<=observations[-1]['position']['tick']: raise Failure('off-phase timeline stalled/reset')
            observations.append(value); self.event('off_observation',**value)
        end=time.monotonic_ns()
        if end<deadline: raise Failure('incomplete off-phase coverage')
        self.event('phase_end',phase='BTX-off',end_ns=end)
        return dict(start_ns=start,end_ns=end,observations=observations,
                    limitation='Sparse serial checks at begin/mid/end add load identically in on/off. Continuity between observations and physical sound are unverified.')
    def stop_playback(self):
        if not self.playback_owned: return
        if self.btx is not None and self.btx.poll() is None: raise Failure('cannot STOP while BTX still owns serial')
        session=self.session or self.open_serial(self.root/('cleanup-'+str(time.monotonic_ns())))
        session.request('BTTEST1 STOP',lambda s:s=='BTTEST1 STOPPED')
        state=session.request('BTTEST1 STATUS',lambda s:s.startswith('BTTEST1 STATE '))
        if state!='BTTEST1 STATE IDLE': raise Failure('STOP/idle not confirmed: '+state)
        self.playback_owned=False; self.event('playback_stop_confirmed')
    def condition(self,mode,identity):
        directory=self.root/mode; directory.mkdir(); self.active_dir=directory
        result=dict(mode=mode,success=False); self.results.append(result)
        self.status('PREPARING '+mode+': verifying existing working/published pattern and gains')
        before=self.readback(directory/'before',identity); self.close_serial()
        result['workload_before']=before
        if len(self.results)>1 and before!=self.results[0]['workload_before']: raise Failure('matched workload/sample metadata differs between runs')
        if mode=='on' and self.usb_requested:
            self.status('PREPARING optional USB/HCD capture\nPCM measurement is unchanged.')
            self.event('usb_capture_start_requested')
            try:
                self.usb_capture=Capture(directory/'usb',self.port)
                if not self.usb_capture.start(self.check_view):
                    self.stop_usb()
                    self.event('usb_capture_unavailable',result=self.usb_state)
                else:self.event('usb_capture_ready')
            except Cancelled:raise
            except Exception as e:
                self.stop_usb()
                self.usb_state=dict(clean_stop=False,reason=str(e))
                self.event('usb_capture_unavailable',result=self.usb_state)
        env=os.environ.copy()
        for key in ['BROTRACKER_DIAGNOSTICS_DIR','BROTRACKER_BRIDGE_CLICKS_DIR','BROTRACKER_DIAGNOSTIC_STATUS_FILE','BROTRACKER_READ_INTEGRITY']:
            env.pop(key,None)
        env['BROTRACKER_BRIDGE_CLICKS_LIMIT_SECONDS']=str(RECORD_SECONDS)
        if mode=='on': env['BROTRACKER_BRIDGE_CLICKS_DIR']=str(directory)
        if mode=='on' and self.read_integrity_requested:env['BROTRACKER_READ_INTEGRITY']='1'
        setup_deadline=time.monotonic()+SETUP_SECONDS
        self.bridge=self.launch([str(self.binary),'--auto'],directory/'bridge.log',env=env,cwd=self.package)
        self.event('recording_setup_deadline',deadline_ns=time.monotonic_ns()+SETUP_SECONDS*1000000000)
        while time.monotonic()<setup_deadline:
            self.ensure_bridge()
            if b'Queue capacity:' in (directory/'bridge.log').read_bytes(): break
            time.sleep(.1)
        else: raise Failure('bridge setup timeout')
        diagnostic=directory/'btx'; diagnostic.mkdir()
        env['BROTRACKER_DIAGNOSTIC_STATUS_FILE']=str(self.status_path)
        env['BROTRACKER_DIAGNOSTIC_RUNNER_PID']=str(os.getpid())
        env['BROTRACKER_DIAGNOSTIC_CANCEL_DIR']=str(self.root)
        if Path('/tmp/brotracker-arkos.log').exists():
            shutil.copyfile('/tmp/brotracker-arkos.log',directory/'btx-log-before-launch.log')
        self.hide()
        self.btx=self.launch([str(self.terminal),'--offline-click-diagnostic',str(diagnostic)],directory/'btx.log',env=env,cwd=self.package)
        prepared=self.wait_status(diagnostic,lambda v:v['phase']=='prepared',setup_deadline)
        self.check_workload(diagnostic,before)
        if mode=='on':
            traces=list(directory.glob('bridge-*'))
            if len(traces)!=1: raise Failure('paired trace missing/ambiguous')
            meta=json.loads((traces[0]/'run.json').read_text())
            if meta['limit_seconds']!=RECORD_SECONDS: raise Failure('wrong recording cap/stale binary')
        # Mark ownership before START publication: failure can leave its outcome unknown.
        self.playback_owned=True; atomic(diagnostic/'command.txt','START\n')
        running=self.wait_status(diagnostic,lambda v:v['phase']=='running' and v['valid'] and v['running'],setup_deadline)
        result['active']=self.active_phase(diagnostic,running)
        released,exited=self.release_btx(diagnostic)
        result['serial_release_ns']=released['ns']; result['btx_exit_observed_ns']=exited
        if Path('/tmp/brotracker-arkos.log').exists(): (directory/'btx-internal.log').write_bytes(Path('/tmp/brotracker-arkos.log').read_bytes())
        self.show(); self.status('RUNNING '+mode+' / BTX-off\nNo input required.\nAuto-return after both pairs.')
        result['off']=self.off_phase(directory/'off',released)
        self.stop_playback()
        after=self.readback(directory/'after',identity+2); self.close_serial()
        result['workload_after']=after
        if after!=before: raise Failure('workload changed across condition')
        self.event('bridge_shutdown_begin',pid=self.bridge.pid)
        result['bridge_exit']=terminate_owned(self.bridge); self.bridge=None
        self.event('bridge_shutdown_end',code=result['bridge_exit'])
        if result['bridge_exit']!=0: raise Failure('bridge finalization failed')
        self.stop_usb()
        if mode=='on':
            report=self.run_analysis(directory,traces[0])
            if self.usb_requested:result['usb_comparison']=report.get('usb_comparison',dict(usb_comparison_valid=False,errors=['USB analysis missing']))
            if self.read_integrity_requested:result['read_integrity']=report.get('read_integrity',dict(status='UNKNOWN',errors=['read integrity analysis missing']))
            if not report['complete'] or not report['accepted_equals_mapped_received']: raise Failure('paired PCM incomplete/mismatched')
            verify_pcm_coverage(report['pcm_transfer_bounds_ns'],result['active']['start_ns'],result['off']['end_ns'])
            result['pcm_phase_coverage']=True
        else: result['pcm_phase_coverage']='not recorded by design; telemetry observations only'
        result['success']=True; save(directory/'result.json',result)
    def run_analysis(self,directory,trace):
        # Run existing strict analysis off the orchestration thread. No audio is active.
        self.status('FINALIZING '+directory.name+' recording\nAudio stopped. Please wait.\nB: emergency exit.')
        timeout=ANALYSIS_SECONDS*(2 if self.usb_requested else 1)
        self.event('analysis_begin',trace=str(trace),timeout_seconds=timeout)
        command=[sys.executable,'-B',str(self.repo/'tools/alsa_bridge/analyze_clicks.py'),str(trace),'--output',str(directory/'analysis.json')]
        if self.usb_requested:command+=['--usbmon',str(directory/'usb')]
        if self.read_integrity_requested:command+=['--read-integrity']
        self.analysis=self.launch(command,directory/'analysis.log')
        began=time.monotonic();next_update=began
        while self.analysis.poll() is None:
            self.check_view()
            now=time.monotonic()
            if now-began>=timeout: raise Failure('analysis timeout; raw evidence retained')
            if now>=next_update:
                self.status('FINALIZING '+directory.name+' recording: '+str(int(now-began))+'s\nAudio stopped. Please wait.\nB: emergency exit.')
                next_update=now+1
            time.sleep(.1)
        code=self.analysis.wait(timeout=1);self.analysis=None
        self.event('analysis_end',trace=str(trace),code=code,elapsed_seconds=time.monotonic()-began)
        report=json.loads((directory/'analysis.json').read_text())
        if code or not report.get('complete') or not report.get('accepted_equals_mapped_received'):
            raise Failure('paired PCM incomplete/mismatched; see analysis.json')
        return report
    def stop_usb(self):
        if self.usb_capture:
            try:self.usb_state=self.usb_capture.stop()
            except Cancelled:raise
            except Exception as e:self.usb_state=dict(clean_stop=False,reason='USB stop/reap unconfirmed: '+str(e))
            self.usb_capture=None
            self.event('usb_capture_stopped',result=self.usb_state)
    def cleanup(self):
        self.event('cleanup_begin')
        errors=[]
        if self.analysis:
            try:
                terminate_owned(self.analysis,force=True)
                self.event('analysis_worker_reaped',pid=self.analysis.pid)
            except Exception as e: errors.append('analysis cleanup: '+str(e))
            self.analysis=None
        if self.btx:
            try:
                # On failure only, terminate our own UI; process wait proves release.
                terminate_owned(self.btx,force=True); self.event('cleanup_btx_reaped',pid=self.btx.pid)
            except Exception as e: errors.append(str(e))
            if self.btx.poll() is not None: self.btx=None
        try: self.stop_playback()
        except Exception as e: errors.append('STOP/idle unconfirmed: '+str(e))
        try: self.close_serial()
        except Exception as e: errors.append('serial finalization: '+str(e))
        if self.bridge:
            try:
                self.event('bridge_shutdown_begin',pid=self.bridge.pid)
                code=terminate_owned(self.bridge)
                self.event('bridge_shutdown_end',code=code)
            except Exception as e: errors.append(str(e))
            self.bridge=None
        try:self.stop_usb()
        except Exception as e:errors.append('USB capture cleanup: '+str(e))
        self.event('cleanup_end',errors=errors)
        return errors
    def run(self):
        failure=None
        try:
            self.preflight()
            for i,mode in enumerate(['on','off']): self.condition(mode,4000000000+i*4)
        except Cancelled as e:
            self.cancelled=True; failure=str(e); self.event('cancelled',reason=failure)
        except Exception as e:
            self.retain_traceback()
            failure=type(e).__name__+': '+str(e); self.event('failure',reason=failure)
        self.finishing=True
        try: self.status('CANCELLED: closing owned resources' if self.cancelled else 'FINALIZING: closing owned resources')
        except OSError: self.recording_error=True
        errors=self.cleanup(); self.cleanup_done=True
        if self.active_dir and not (self.active_dir/'btx-internal.log').exists() and Path('/tmp/brotracker-arkos.log').exists():
            shutil.copyfile('/tmp/brotracker-arkos.log',self.active_dir/'btx-internal.log')
        if self.recording_error: errors.append('runner timeline evidence incomplete')
        if self.config_path is not None and (sha(self.config_path) if self.config_path.exists() else None)!=self.config_before:
            errors.append('persistent user config changed; original retained, not overwritten')
        # Emergency/failed runs must not synchronously analyze large partial dumps.
        # Preserve raw/partial reports and explicitly defer optional offline replay.
        for trace in self.root.glob('*/bridge-*'):
            if not trace.is_dir(): continue
            if (trace.parent/'analysis.json').exists() and not failure and not self.cancelled: continue
            save(trace.parent/'analysis-deferred.json',dict(status='NOT ANALYZED',
                 reason='Experiment cancelled or failed; bounded exit takes precedence; any existing report retained',trace=str(trace),
                 command=[sys.executable,'-B',str(self.repo/'tools/alsa_bridge/analyze_clicks.py'),str(trace),
                          '--output',str(trace.parent/'analysis-offline.json')]))
            self.event('analysis_deferred',trace=str(trace))
        if not failure and not self.cancelled and (len(self.results)!=2 or not all(v.get('success') for v in self.results)):
            errors.append('both complete matched conditions required')
        usb_result=next((v['usb_comparison'] for v in self.results if 'usb_comparison' in v),None)
        if self.usb_requested and not (usb_result or {}).get('usb_comparison_valid'):
            errors.append('USB comparison invalid/unavailable; PCM condition results retained')
        read_result=next((v['read_integrity'] for v in self.results if 'read_integrity' in v),None)
        if self.read_integrity_requested and (read_result or {}).get('status') not in ('READ_RETURN_DIFFERS_FROM_USB','READ_RETURN_MATCHES_USB'):
            errors.append('read-return comparison UNKNOWN or recorder mismatch; original evidence retained')
        summary=dict(success=not self.cancelled and not failure and not errors,technical_success=not self.cancelled and not failure and not errors,
            outcome='CANCELLED' if self.cancelled else ('FAILED' if failure or errors else 'COMPLETED'),
            usb_capture_requested=self.usb_requested,usb_capture=self.usb_state,usb_comparison=usb_result,
            read_integrity_requested=self.read_integrity_requested,read_integrity=read_result,
            audible_click_status='NOT YET CONFIRMED',listening_report='User may report clicks and phase afterward; no event markers or external recording required',failure=failure,cleanup_errors=errors,conditions=self.results,
            limitations='No physical listening verification. Sparse off-phase serial checks are observer load and do not prove gap-free playback. '
            'Available sample hashes/metadata do not establish every sample binding. The separate 10 Hz SDL status view remains during BTX-off and adds observer load. No hardware limit or click cause inferred.'
            +(' Optional usbmon/tcpdump runs only in diagnostics-on and adds observer load; it is HCD evidence, not guaranteed on-wire truth.' if self.usb_requested else ''))
        if self.read_integrity_requested:summary['limitations']+=' Independent FNV-1a read digests and a separate writer add observer load only during diagnostics-on; hash collisions/common-mode faults remain possible.'
        # Freeze the result decision before publication; later B only dismisses UI.
        self.results_finalized=True
        self.event('results_publication_begin',outcome=summary['outcome'])
        save(self.root/'summary.json',summary)
        text=summary['outcome']+'\nMatched on/off: '+str(sum(v.get('success',False) for v in self.results))+'/2\n'+(failure or '')+'\n'+'\n'.join(errors)+'\nAudible clicks: NOT YET CONFIRMED. Phase times retained for later listening report. See summary.json'
        (self.root/'SUMMARY.txt').write_text(text+'\n'+str(self.root)+'\n')
        # Full errors and paths stay in evidence, never shrink the on-screen font.
        short_error=(failure or '; '.join(errors)).splitlines()[0][:100] if failure or errors else ''
        self.status(summary['outcome']+'\nMatched on/off: '+str(sum(v.get('success',False) for v in self.results))+
                    '/2\n'+short_error+'\nAudible clicks:\nNOT YET CONFIRMED\nReturning to Ports automatically.\nDetails saved in run logs.')
        self.event('results_finalized',outcome=summary['outcome'])
        return summary['success']
    def cancel_from_view(self):
        source=None
        for pid,view in self.cancel_producers.items():
            path=self.root/('cancel-'+str(pid)+'.json')
            if path.name in self.cancel_records or not path.exists(): continue
            try:
                if path.stat().st_size>2048: raise ValueError('oversized source record')
                value=json.loads(path.read_text())
                if value['producer_pid']!=pid or value['view']!=view: raise ValueError('source ownership mismatch')
                source=value;self.cancel_records.add(path.name)
                self.event('results_dismiss_input' if self.results_finalized else 'cancellation_requested',source=source,observed_phase=self.current_phase)
            except (OSError,ValueError,KeyError) as error:
                self.event('cancellation_source_error',path=path.name,reason=str(error))
        if source:
            reason=(source['view']+': '+source['reason']+' (SDL event '+str(source['sdl_event_type'])+
                    ', '+source['input_kind']+'='+str(source['input_id'])+')')
        else:
            reason='Unattributed cancellation signal: source record unavailable'
            self.event('cancellation_requested',source=None,observed_phase=self.current_phase,reason=reason)
        self.cancel(reason)
    def cancel(self,reason='Cancellation requested by runner'):
        if self.results_finalized:
            self.results_dismissed=True;self.event('results_dismissed',reason=reason);return
        if self.cancelled: return
        self.cancelled=True
        if not self.finishing: raise Cancelled(reason)
    def wait_results(self,success):
        # Cancelled attempts return immediately after cleanup; other outcomes are timed.
        if self.cancelled: return
        self.show()
        deadline=time.monotonic()+RESULT_SECONDS
        while not self.results_dismissed and time.monotonic()<deadline:
            if self.viewer is None or self.viewer.poll() is not None: break
            time.sleep(.1)
    def close_files(self):
        if hasattr(self,'lock'): self.lock.close()
        for log in self.owned_logs: log.close()
        self.events.close()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]); p.add_argument('--workload',type=Path,help='optional unverified supplementary description; never required')
    p.add_argument('--no-display',action='store_true',help='shell/device-free tests only; Ports uses SDL status')
    p.add_argument('--usbmon',action='store_true',help='opt-in bounded USB/HCD capture; prepare permissions first')
    p.add_argument('--read-integrity',action='store_true',help='opt-in independent read-return digests plus USB capture')
    args=p.parse_args(); e=Experiment(args.repo,args.workload or Path.home()/'BroTracker/bridge-click-workload.json',args.usbmon,args.read_integrity)
    if args.no_display: e.show=lambda:None
    def interrupted(signum,frame): raise Failure('runner interrupted by signal '+str(signum))
    for sig in [signal.SIGTERM,signal.SIGINT,signal.SIGHUP]: signal.signal(sig,interrupted)
    signal.signal(signal.SIGUSR1,lambda signum,frame:e.cancel_from_view())
    try:
        success=e.run()
        if not args.no_display and e.display_verified:
            try: e.wait_results(success)
            except Exception: pass
        return 0 if success else 1
    except Exception:
        e.retain_traceback()
        raise
    finally:
        # Even a report/storage exception must not bypass bounded STOP cleanup.
        if not e.cleanup_done: e.cleanup()
        e.hide(); e.close_files()
if __name__=='__main__': raise SystemExit(main())

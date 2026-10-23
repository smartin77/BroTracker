#!/usr/bin/env python3
"""Actual ARM64 BTX config/status checks. Never opens serial or audio hardware."""
import json
import os
from pathlib import Path
import subprocess
import signal
import shutil
import sys
import tempfile
import time

binary=Path(sys.argv[1]).resolve()
repo=Path(__file__).resolve().parents[2]
base=Path(sys.argv[2]); base.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='synthetic-btx-',dir=base) as tmp:
    work=Path(tmp); env=os.environ.copy()
    env.update(XDG_DATA_HOME=str(work/'prefs'),SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
    installed=work/'installed'
    installed.mkdir()
    shutil.copy2(binary,installed/binary.name)
    binary=installed/binary.name
    elsewhere=work/'different-cwd'; elsewhere.mkdir()
    legacy=work/'prefs/BroTracker/BroTracker Terminal/config.ini'
    legacy.parent.mkdir(parents=True)
    legacy.write_text('invalid legacy config must not be read')
    config=installed/'config/config.ini'; config.parent.mkdir()
    (config.parent/'config.ini.example').write_text('invalid example must not be read')
    # An unrelated CWD config also must never influence the installed application.
    (elsewhere/'config').mkdir()
    (elsewhere/'config/config.ini').write_text('invalid CWD config must not be read')
    cases=[('bridge',0),('pt8211',2),('',0),('invalid',2)]
    for value,code in cases:
        data=('[btx]\nschema_version=1\nnew_pattern_length=32\n[audio]\noutput='+value+'\n').encode()
        config.write_bytes(data)
        check=subprocess.run([str(binary),'--offline-diagnostic-config'],env=env,cwd=elsewhere,capture_output=True,timeout=8)
        assert check.returncode==code,(value,check.stdout,check.stderr)
        assert ('path='+str(config)).encode() in check.stdout
        assert config.read_bytes()==data
    config.unlink()
    assert subprocess.run([str(binary),'--offline-diagnostic-config'],env=env,cwd=elsewhere,capture_output=True,timeout=8).returncode==0
    assert not config.exists()
    missing=subprocess.run([str(binary),'--offline-diagnostic-config'],env=env,cwd=elsewhere,capture_output=True,timeout=8)
    assert b'config.ini absent; using built-in defaults' in missing.stdout
    assert b'row_numbering=d1' in missing.stdout
    # The user's actual console combination is honored directly.
    config.write_text('[btx]\nschema_version=1\nnew_pattern_length=32\n[audio]\noutput=pt8211\n[display]\nrow_numbering=h0\n[diagnostics]\nbridge_audible_test=yes\n')
    loaded=subprocess.run([str(binary),'--offline-diagnostic-config'],env=env,cwd=elsewhere,capture_output=True,timeout=8)
    assert b'route=pt8211' in loaded.stdout and b'row_numbering=h0' in loaded.stdout
    evidence=legacy.parent/'diagnostics/bridge-audible'
    evidence.mkdir(parents=True)
    retained=evidence/'retained-evidence'; retained.write_bytes(b'original recording evidence')
    listening=subprocess.run([str(binary),'--bridge-audible-config'],env=env,cwd=elsewhere,capture_output=True,timeout=8)
    assert listening.returncode==0 and listening.stdout.strip()==str(evidence).encode()
    assert retained.read_bytes()==b'original recording evidence'
    assert not (installed/'config/diagnostics').exists()
    config.write_text('[btx]\nschema_version=1\nnew_pattern_length=32\n[audio]\noutput=bridge\n')
    original=config.read_bytes()
    status=work/'STATUS.txt'; status.write_text('PREPARING synthetic offline diagnostic\n')
    cancel_signals=[]
    previous=signal.signal(signal.SIGUSR1,lambda *args:cancel_signals.append(True))
    env.update(BROTRACKER_DIAGNOSTIC_RUNNER_PID=str(os.getpid()),BROTRACKER_DIAGNOSTIC_CANCEL_DIR=str(work))
    viewer=subprocess.Popen([str(binary),'--diagnostic-status',str(status)],env=env,cwd=repo/'deploy/arkos',stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    try:
        time.sleep(.3); assert viewer.poll() is None,'headless status display failed'
    finally:
        viewer.terminate(); viewer.wait(timeout=5)
        signal.signal(signal.SIGUSR1,previous)
    assert not cancel_signals,'intentional view transition incorrectly cancelled runner'
    assert not list(work.glob('cancel-*.json')),'intentional transition fabricated cancellation source'
print('Actual BTX executable-relative config/different-CWD/defaults/example+legacy exclusion/evidence-path and headless SDL checks passed; no hardware.')

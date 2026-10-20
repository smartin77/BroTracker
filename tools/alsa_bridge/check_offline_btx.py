#!/usr/bin/env python3
"""Actual ARM64 BTX config/status checks. Never opens serial or audio hardware."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

binary=Path(sys.argv[1]).resolve()
repo=Path(__file__).resolve().parents[2]
base=Path(sys.argv[2]); base.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='synthetic-btx-',dir=base) as tmp:
    work=Path(tmp); env=os.environ.copy()
    env.update(XDG_DATA_HOME=str(work/'prefs'),SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
    config=work/'prefs/BroTracker/BroTracker Terminal/config.ini'; config.parent.mkdir(parents=True)
    cases=[('bridge',0),('pt8211',2),('',0),('invalid',2)]
    for value,code in cases:
        data=('[btx]\nschema_version=1\nnew_pattern_length=32\n[audio]\noutput='+value+'\n').encode()
        config.write_bytes(data)
        check=subprocess.run([str(binary),'--offline-diagnostic-config'],env=env,capture_output=True,timeout=8)
        assert check.returncode==code,(value,check.stdout,check.stderr)
        assert ('path='+str(config)).encode() in check.stdout
        assert config.read_bytes()==data
    config.unlink()
    assert subprocess.run([str(binary),'--offline-diagnostic-config'],env=env,capture_output=True,timeout=8).returncode==0
    assert not config.exists()
    config.write_text('[btx]\nschema_version=1\nnew_pattern_length=32\n[audio]\noutput=bridge\n')
    original=config.read_bytes()
    status=work/'STATUS.txt'; status.write_text('PREPARING synthetic offline diagnostic\n')
    viewer=subprocess.Popen([str(binary),'--diagnostic-status',str(status)],env=env,cwd=repo/'deploy/arkos',stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    try:
        time.sleep(.3); assert viewer.poll() is None,'headless status display failed'
    finally:
        viewer.terminate(); viewer.wait(timeout=5)
print('Actual BTX shared configuration and headless SDL status checks passed; no hardware.')

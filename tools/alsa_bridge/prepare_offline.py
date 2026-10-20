#!/usr/bin/env python3
"""Install only the diagnostic Ports entry and checkout pointer. No device/network access."""
import argparse
import os
from pathlib import Path
import tempfile


def install(repo,ports):
    repo=Path(repo).resolve(); ports=Path(ports)
    source=repo/'tools/arkos/port/BroTracker Bridge Clicks.sh'
    if not source.is_file() or not (repo/'tools/alsa_bridge/offline_runner.py').is_file():
        raise RuntimeError('Not a complete BroTracker checkout')
    if not (repo/'deploy/arkos/bridge-clicks-artifacts.json').is_file():
        raise RuntimeError('Update checkout including matching deploy/arkos artifacts first')
    if ports.is_symlink(): raise RuntimeError('Ports directory must not be a symlink')
    ports.mkdir(parents=True,exist_ok=True)
    for name,data,mode in [('BroTracker Bridge Clicks.sh',source.read_bytes().replace(b'\r\n',b'\n'),0o755),
                          ('BroTracker Bridge Clicks.repo.path',(str(repo)+'\n').encode(),0o644)]:
        target=ports/name
        if target.is_symlink() or (target.exists() and not target.is_file()):
            raise RuntimeError('Refusing nonregular diagnostic entry: '+str(target))
        if target.exists() and target.read_bytes()==data:
            target.chmod(mode); continue
        if target.exists():
            fd,backup=tempfile.mkstemp(prefix=name+'.previous-',dir=str(ports))
            with os.fdopen(fd,'wb') as f: f.write(target.read_bytes())
        fd,pending=tempfile.mkstemp(prefix='.brotracker-diagnostic-',dir=str(ports))
        try:
            with os.fdopen(fd,'wb') as f: f.write(data)
            os.chmod(pending,mode); os.replace(pending,target)
        finally:
            if os.path.exists(pending): os.unlink(pending)
    print('Prepared Ports > BroTracker Bridge Clicks. Checkout: '+str(repo))
    print('Disconnect Wi-Fi, connect Teensy, launch the Ports entry. No manifest or input required.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--ports-dir',type=Path,default=Path('/roms/ports'))
    a=p.parse_args(); install(Path(__file__).resolve().parents[2],a.ports_dir)

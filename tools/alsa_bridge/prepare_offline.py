#!/usr/bin/env python3
"""Install diagnostic Ports entry; optional USB checks never record live traffic."""
import argparse
import os
from pathlib import Path
import tempfile


def install(repo,ports,usbmon=False):
    repo=Path(repo).resolve(); ports=Path(ports)
    source=repo/'tools/arkos/port/BroTracker Bridge Clicks.sh'
    if not source.is_file() or not (repo/'tools/alsa_bridge/offline_runner.py').is_file():
        raise RuntimeError('Not a complete BroTracker checkout')
    if not (repo/'deploy/arkos/bridge-clicks-artifacts.json').is_file():
        raise RuntimeError('Update checkout including matching deploy/arkos artifacts first')
    receipt=None
    if usbmon:
        from usbmon_capture import permission_check
        receipt=permission_check()  # Ignore cached sudo tickets, fail before offline launch.
        import re,json
        receipt['dependencies']=re.sub(r'\(0x[0-9a-fA-F]+\)','(address)',receipt['dependencies'])
    if ports.is_symlink(): raise RuntimeError('Ports directory must not be a symlink')
    ports.mkdir(parents=True,exist_ok=True)
    entries=[('BroTracker Bridge Clicks.sh',source.read_bytes().replace(b'\r\n',b'\n'),0o755),
             ('BroTracker Bridge Clicks.repo.path',(str(repo)+'\n').encode(),0o644),
             ('BroTracker Bridge Clicks.usbmon',b'enabled\n' if usbmon else b'disabled\n',0o644)]
    if receipt is not None:
        entries.append(('BroTracker Bridge Clicks.usbmon-preflight.json',(json.dumps(receipt,indent=2)+'\n').encode(),0o644))
    for name,data,mode in entries:
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
    print('Optional USB/HCD capture: '+('enabled; per-packet completeness still requires offline validation' if usbmon else 'disabled'))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--ports-dir',type=Path,default=Path('/roms/ports'))
    p.add_argument('--usbmon',action='store_true',help='opt in; noninteractive sudo and installed tcpdump semantics must pass')
    a=p.parse_args()
    try: install(Path(__file__).resolve().parents[2],a.ports_dir,a.usbmon)
    except (OSError,ValueError,RuntimeError) as e:p.error(str(e))

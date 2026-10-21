#!/usr/bin/env python3
"""Copy checked Linux artifacts for later user review/Git delivery; never stages Git."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build-dir',required=True,type=Path)
p.add_argument('--package-dir',required=True,type=Path)
a=p.parse_args()
checks=a.build_dir/'offline-abi-checks.txt'
if not checks.exists() or 'OFFLINE ABI CHECKS PASSED' not in checks.read_text():
    p.error('run the documented Eoan build/ABI checks first')
manifest={'schema':1,'sha256':{},'source_sha256':{},'build_checks':checks.read_text(),'source_hash_policy':'CRLF normalized to LF; ELF hashes are raw bytes'}
for name in ['BroTrackerAlsaBridge','BroTrackerTerminal']:
    source=a.build_dir/name
    data=source.read_bytes()
    if data[:4]!=b'\x7fELF' or struct.unpack_from('<H',data,18)[0]!=183: p.error('not ARM64: '+name)
    digest=hashlib.sha256(data).hexdigest()
    # The checks file names the exact binaries it verified, not an earlier build.
    if digest+'  '+name not in checks.read_text(): p.error('ABI check hash mismatch: '+name)
    manifest['sha256'][name]=digest
for path in ['tools/alsa_bridge/main.cpp','tools/alsa_bridge/click_diagnostics.h',
             'tools/alsa_bridge/offline_runner.py','tools/alsa_bridge/analyze_clicks.py',
             'tools/alsa_bridge/offline_metadata.py','tools/alsa_bridge/prepare_offline.py',
             'tools/alsa_bridge/analyze_usbmon.py','tools/alsa_bridge/usbmon_capture.py',
             'tools/alsa_bridge/read_integrity.h','tools/alsa_bridge/analyze_read_integrity.py',
             'ui/arkos/main.cpp','ui/arkos/diagnostic_status.h','ui/arkos/offline_click_mode.h','src/ui/bringup_serial.h']:
    manifest['source_sha256'][path]=hashlib.sha256(Path(path).read_bytes().replace(b'\r\n',b'\n')).hexdigest()
manifest['assets_sha256']={}
for name in ['assets/fonts/brotracker.btf','assets/fonts/brotracker.bfm','assets/dummy_my_tune.json']:
    data=(a.package_dir/name).read_bytes()
    if Path(name).suffix in ['.json','.btf']: data=data.replace(b'\r\n',b'\n')
    manifest['assets_sha256'][name]=hashlib.sha256(data).hexdigest()
a.package_dir.mkdir(parents=True,exist_ok=True)
for name in manifest['sha256']:
    shutil.copyfile(a.build_dir/name,a.package_dir/name)
    (a.package_dir/name).chmod(0o755)
(a.package_dir/'bridge-clicks-artifacts.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Prepared Linux deploy artifacts and hash manifest; review together with sources. No Git writes.')

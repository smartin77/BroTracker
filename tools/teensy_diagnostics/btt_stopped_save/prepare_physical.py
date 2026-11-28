"""Generate fresh native fixture and an isolated, fixed-path stage/read helper config."""
import hashlib
import json
from pathlib import Path
import subprocess
import uuid

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/btt-stopped-save'
folder=OUT/'hardware-input'
folder.mkdir(exist_ok=False)
nonce=uuid.uuid4().hex[:10]
directory='BTT-save-'+nonce
command=['c++','-std=c++17','-O2','-I.',str(Path(__file__).with_name('fixture.cpp')),
         'tools/btt/foundation.cpp','-o',str(folder/'fixture.exe')]
for name,args in [('fixture-build.log',command),('fixture-generate.log',[str(folder/'fixture.exe'),str(folder)])]:
    result=subprocess.run(args,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (OUT/name).write_bytes(result.stdout)
    if result.returncode:raise RuntimeError(name)
data=(folder/'input.btt').read_bytes()
header='#pragma once\n#include <cstdint>\n'
header+=f'constexpr char kDirectory[]="/{directory}";\nconstexpr char kPath[]="/{directory}/ordinary.btt";\n'
header+=f'constexpr char kStageCommand[]="STAGE {nonce}";\n'
header+='constexpr uint8_t kFixture[]={\n'+','.join(str(b) for b in data)+'\n};\n'
(folder/'fixture_bytes.h').write_text(header)
config=f'''[platformio]
src_dir = {Path(__file__).with_name('physical').as_posix()}
lib_dir = {ROOT.as_posix()}/libraries
build_dir = {(OUT/'helper-firmware').as_posix()}
[env:teensy41]
platform = teensy
framework = arduino
board = teensy41
build_flags = -D USB_MIDI_AUDIO_SERIAL -I{folder.as_posix()}
'''
(OUT/'helper.ini').write_text(config)
manifest=dict(directory=directory,path=directory+'/ordinary.btt',nonce=nonce,
    expected_usb_serial='17681760',expected_cid='6f0303534441424310aa001504018b00',
    expected_sectors=61076480,expected_fat=32,
    files={name:dict(bytes=len((folder/name).read_bytes()),sha256=hashlib.sha256((folder/name).read_bytes()).hexdigest())
           for name in ('input.btt','expected.btt')})
(OUT/'physical-manifest.json').write_text(json.dumps(manifest,indent=2))
print(json.dumps(manifest,indent=2))

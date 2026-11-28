"""Prepare isolated actual-HEAD baseline/current map builds; measure without upload."""
import configparser
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import zipfile

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/btt-stopped-save'

def run(command,name):
    result=subprocess.run([str(a) for a in command],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (OUT/name).write_bytes(result.stdout)
    if result.returncode:raise RuntimeError(f'{name}: exit {result.returncode}')
    return result.stdout.decode(errors='replace')

def prepare():
    OUT.mkdir(parents=True,exist_ok=True)
    paths=['platformio.ini','firmware','libraries','src','tools/btt','tools/teensy_usb_audio',
           'tools/teensy_diagnostics/btt_admission/map_flags.py']
    archive=subprocess.check_output(['git','archive','--format=zip','HEAD',*paths],cwd=ROOT)
    baseline=OUT/'baseline-source'
    baseline.mkdir(exist_ok=False)
    with zipfile.ZipFile(io.BytesIO(archive)) as z:z.extractall(baseline)
    for profile in ('baseline','current'):
        config=configparser.ConfigParser(interpolation=None)
        config.read(ROOT/'platformio.ini')
        config['platformio']['build_dir']=(OUT/(profile+'-firmware')).as_posix()
        config['env:teensy41_dev']['extra_scripts']='${env:teensy41.extra_scripts}\npost:tools/teensy_diagnostics/btt_admission/map_flags.py'
        config['env:teensy41_dev']['build_flags']+='\n-fstack-usage'
        stream=io.StringIO();config.write(stream)
        (OUT/(profile+'.ini')).write_text('\n'.join(line.rstrip() for line in stream.getvalue().splitlines()).rstrip()+'\n')
    (OUT/'baseline-head.txt').write_text(subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT).decode())

def measure():
    info=json.loads((ROOT/'.pio/build/teensy41_dev/idedata.json').read_text())
    compiler=Path(info['cxx_path']);nm=compiler.parent/'arm-none-eabi-nm.exe';size=compiler.parent/'arm-none-eabi-size.exe'
    flags=info['cxx_flags']+['-D'+d for d in info['defines']]
    for folder in info['includes']['build']+[str(ROOT),str(ROOT/'tools/btt')]:flags+=['-I',folder]
    probe=OUT/'layout.cpp'
    probe.write_text('#include "tools/btt/stopped_load.h"\nextern "C" {\n'
        'char size_StoppedLoad[sizeof(btt::production::StoppedLoad)];\n'
        'char size_FsSave[sizeof(btt::prototype::FsSave)];\n'
        'char size_SaveReport[sizeof(btt::production::SaveReport)];\n'
        'char size_RecoveryReport[sizeof(btt::production::RecoveryReport)];\n}\n')
    command=[compiler,*flags,'-c',probe,'-o',OUT/'layout.o']
    run(command,'layout-compile.log');(OUT/'layout-command.json').write_text(json.dumps([str(a) for a in command],indent=2))
    symbols=run([nm,'-S',OUT/'layout.o'],'layout-symbols.txt')
    layouts={p[3]:int(p[1],16) for line in symbols.splitlines() if len(p:=line.split())==4 and p[3].startswith('size_')}
    (OUT/'layouts.json').write_text(json.dumps(layouts,indent=2))
    memory={};identities={}
    for profile in ('baseline','current'):
        folder=OUT/(profile+'-firmware')/'teensy41_dev';elf=folder/'firmware.elf'
        sections=run([size,'-A',elf],profile+'-sections.txt')
        symbols=run([nm,'-n','-S','-C',elf],profile+'-symbols.txt')
        sizes={p[0]:int(p[1]) for line in sections.splitlines() if len(p:=line.split())==3 and p[0].startswith('.')}
        addresses={p[-1]:int(p[0],16) for line in symbols.splitlines() if len(p:=line.split())>=3 and p[-1] in ('_stext','_etext','_itcm_block_count')}
        banks=addresses['_itcm_block_count'];ram1=sizes['.data']+sizes['.bss'];ram2=sizes['.bss.dma']
        memory[profile]=dict(itcm=addresses['_etext']-addresses['_stext'],banks=banks,ram1=ram1,
            stack_gap=524288-banks*32768-ram1,ram2=ram2,heap_interval=524288-ram2)
        import re
        stamp=re.findall(rb'BTINS1 TUNINGBUILD 1 [^\x00\r\n]+',elf.read_bytes())
        identities[profile]=dict(build_line=stamp[0].decode(),images={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (elf,folder/'firmware.hex')})
        source_root=ROOT if profile=='current' else OUT/'baseline-source'
        identities[profile]['source_sha256']={name:hashlib.sha256((source_root/name).read_bytes()).hexdigest()
            for name in ('platformio.ini','firmware/teensy/BroTracker/pattern_bringup.cpp',
                'tools/btt/stopped_load.h','tools/btt/stopped_load.cpp','tools/btt/fs_save.h','tools/btt/fs_save.cpp',
                'src/bringup/btt_serial_policy.h')}
    assert 'FsSave::Run(' in (OUT/'current-symbols.txt').read_text()
    memory['delta']={k:memory['current'][k]-v for k,v in memory['baseline'].items()}
    (OUT/'memory.json').write_text(json.dumps(memory,indent=2))
    (OUT/'firmware-identities.json').write_text(json.dumps(identities,indent=2))
    frames=[]
    for file in (OUT/'current-firmware').rglob('*.su'):
        for line in file.read_text().splitlines():
            if any(s in line for s in ('StoppedLoad','FsSave','foundation.cpp','BttCommand','BttSaveReply','BttMedia','BttIdle',
                                      'ServiceSerial()','ClassifyBttSerialCommand')):
                frames.append(str(file.relative_to(ROOT))+'\t'+line)
    (OUT/'stack-frames.tsv').write_text('\n'.join(frames)+'\n')
    undefined=run([nm,'-u','-C',OUT/'current-firmware/teensy41_dev/src/btt_load.cpp.o'],'coordinator-undefined.txt')
    assert 'operator new(' not in undefined and 'malloc' not in undefined
    (OUT/'target-checks.txt').write_text('PASS: actual production ELF roots StoppedLoad::Save and FsSave::Run.\n'
        'PASS: coordinator object has no allocating operator new/malloc reference.\n')
    print(json.dumps(dict(layouts=layouts,memory=memory),indent=2))

if __name__=='__main__':{'prepare':prepare,'measure':measure}[sys.argv[1]]()

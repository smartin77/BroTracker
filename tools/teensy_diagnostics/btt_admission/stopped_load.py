"""Collect stopped-load target layouts, real baseline/final maps and static budgets.

Run after building an isolated git-archive HEAD in OUT/baseline-source and the
map/stack configuration in OUT/mapped. Never uploads or accesses the device.
"""
from pathlib import Path
import argparse
import csv
import hashlib
import json
import shutil
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out',type=Path,default=Path('build/btt-stopped-load'))
args=parser.parse_args()
out=args.out.resolve()
root=Path(__file__).resolve().parents[3]
mapped=out/'mapped/teensy41_dev'
metadata=mapped/'idedata.json'
if not metadata.exists():metadata=root/'.pio/build/teensy41_dev/idedata.json'
info=json.loads(metadata.read_text())
assert 'BROTRACKER_BTT_STOPPED_LOAD' in info['defines']
compiler=Path(info['cxx_path'])
def run(argv):
    return subprocess.check_output([str(x) for x in argv],cwd=root)
def arm(tool,*argv):
    return run([compiler.parent/('arm-none-eabi-'+tool+compiler.suffix),*argv])
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

probe=out/'load-layout.cpp'
types={'StoppedLoad':'btt::production::StoppedLoad','Candidates':'btt::prototype::Candidate[2]',
       'Document':'btt::Document','Pattern':'btt::Pattern','Workspace':'btt::Workspace',
       'RuntimeDefinitions':'BroTracker::InstrumentDefinitions','RuntimeBindings':'BroTracker::VariableRateSampleBindings',
       'RealtimeProjection':'BroTracker::RealtimePattern','Backings':'btt::prototype::FsBackings',
       'SourceService':'btt::prototype::SdSourceService','ProducerStorage':'BroTracker::SdSampleStreaming::Storage'}
probe.write_text('#include "tools/btt/stopped_load.h"\n'+''.join(
    'extern "C" {unsigned char size_'+name+'[sizeof('+kind+')];}\n' for name,kind in types.items()))
flags=info['cxx_flags']+['-D'+d for d in info['defines']]
for folder in info['includes']['build']+[str(root),str(root/'src')]:flags+=['-I',folder]
command=[compiler,*flags,'-c',probe,'-o',out/'load-layout.o']
(out/'layout-compile-command.json').write_text(json.dumps([str(x) for x in command],indent=2))
subprocess.run([str(x) for x in command],cwd=root,check=True)
symbols=arm('nm','-S','--size-sort',out/'load-layout.o').decode()
(out/'layout-symbols.txt').write_text(symbols)
sizes={line.split()[3][5:]:int(line.split()[1],16) for line in symbols.splitlines()
       if len(line.split())==4 and line.split()[3].startswith('size_')}
(out/'layouts.json').write_text(json.dumps(sizes,indent=2)+'\n')

def measure(label,elf):
    nm=arm('nm','-n','-S','-C',elf).decode()
    sections=arm('objdump','-h',elf).decode()
    (out/(label+'-symbols.txt')).write_text(nm)
    (out/(label+'-sections.txt')).write_bytes(arm('size','-A',elf))
    (out/(label+'-symbol-sections.txt')).write_bytes(arm('objdump','-t','-C',elf))
    addresses={f[-1]:int(f[0],16) for line in nm.splitlines() if len(f:=line.split())>=3}
    sec={f[1]:dict(size=int(f[2],16),vma=int(f[3],16),lma=int(f[4],16))
         for line in sections.splitlines() if len(f:=line.split())>=7 and f[0].isdigit()}
    result=dict(itcm_banks=addresses['_itcm_block_count'],ram1_static=sec['.data']['size']+sec['.bss']['size'],
        ram1_local_stack_gap=addresses['_estack']-addresses['_ebss'],ram2_static=sec['.bss.dma']['size'],
        ram2_heap_interval=addresses['_heap_end']-addresses['_heap_start'],external_static=sec['.bss.extram']['size'],
        flash_image=addresses['_flashimagelen'],sections=sec,elf_sha256=sha(elf))
    if label=='final':
        load=[line for line in nm.splitlines() if line.endswith('::g_btt_load')]
        assert len(load)==1 and int(load[0].split()[1],16)==sizes['StoppedLoad']
        assert nm.count('ResourcePaths()::pool')==1
    return result

baseline=out/'baseline-source/.pio/build/teensy41_dev'
memory={name:measure(name,folder/'firmware.elf') for name,folder in [('baseline',baseline),('final',mapped)]}
ordinary=root/'.pio/build/teensy41_dev'
ordinary_memory=measure('ordinary',ordinary/'firmware.elf')
assert ordinary_memory['sections']==memory['final']['sections'],'Ordinary/mapped section layout differs'
(out/'ordinary-map-equivalence.txt').write_text('PASS: ordinary and mapped ELF section sizes, VMAs and LMAs match.\n')
memory['delta']={k:memory['final'][k]-memory['baseline'][k] for k in (
    'itcm_banks','ram1_static','ram1_local_stack_gap','ram2_static','ram2_heap_interval','flash_image')}
assert memory['delta']['ram2_static'] in range(sizes['StoppedLoad'],sizes['StoppedLoad']+32)
assert memory['final']['itcm_banks']==memory['baseline']['itcm_banks']
memory['provisional_reserves']=dict(stack_irq=32768,heap=65536,
    ram1_after_stack_irq=memory['final']['ram1_local_stack_gap']-32768,
    ram2_after_heap=memory['final']['ram2_heap_interval']-65536,
    coordinator_guard=2048,coordinator_with_guard=sizes['StoppedLoad']+2048,
    coordinator_limit=65536)
assert sizes['StoppedLoad']+2048<=65536
(out/'memory-comparison.json').write_text(json.dumps(memory,indent=2)+'\n')
with (out/'lifecycle.csv').open('w',newline='') as stream:
    writer=csv.writer(stream);writer.writerow(['phase','component','bytes','evidence'])
    for phase in ['snapshot_validation','candidate_prepare','before_audio_ack','after_commit','cancel_quarantine']:
        writer.writerow([phase,'existing_ram1',memory['baseline']['ram1_static'],'source-verified HEAD link'])
        writer.writerow([phase,'existing_ram2',memory['baseline']['ram2_static'],'source-verified HEAD link; includes the only PCM cache'])
        writer.writerow([phase,'stopped_load_all_members',sizes['StoppedLoad'],'target sizeof; allocated simultaneously for every phase'])
        for name,value in sizes.items():
            if name not in ('StoppedLoad','ProducerStorage'):writer.writerow([phase,'included_'+name,value,'included in coordinator, not additive'])
        writer.writerow([phase,'stack_irq_allowance',32768,'unproven allowance, not runtime watermark'])
        writer.writerow([phase,'heap_allowance',65536,'unproven allowance, not runtime watermark'])
frames=[]
for path in mapped.rglob('*.su'):
    for line in path.read_text(errors='replace').splitlines():
        fields=line.split('\t')
        if len(fields)==3:frames.append([int(fields[1]),fields[2],fields[0]])
with (out/'stack-frames.csv').open('w',newline='') as stream:
    writer=csv.writer(stream);writer.writerow(['bytes','kind','function']);writer.writerows(sorted(frames,reverse=True))
for label,folder in [('baseline',baseline),('final',mapped)]:
    for name in ('firmware.elf','firmware.hex'):shutil.copyfile(folder/name,out/(label+'-'+name))
shutil.copyfile(mapped/'firmware.map',out/'final-firmware.map')
identity=dict(git_head=run(['git','rev-parse','HEAD']).decode().strip(),uploaded=False,
    final_hex_sha256=sha(out/'final-firmware.hex'),baseline_hex_sha256=sha(out/'baseline-firmware.hex'),
    ordinary_hex_sha256=sha(ordinary/'firmware.hex'),
    source_sha256={p:sha(root/p) for p in run(['git','ls-files','-m','-o','--exclude-standard']).decode().splitlines() if (root/p).is_file()})
(out/'firmware-identities.json').write_text(json.dumps(identity,indent=2)+'\n')
print(json.dumps({name:{k:v for k,v in values.items() if k not in ('sections','elf_sha256')} for name,values in memory.items()},indent=2))

"""Read-only ELF measurements and isolated HEAD baseline config; never uploads."""
import configparser,hashlib,io,json,subprocess,sys,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/btt-export'
def prepare():
    source=OUT/'baseline-source';source.mkdir(exist_ok=False)
    names=['platformio.ini','firmware','libraries','src','tools/btt','tools/teensy_usb_audio','tools/teensy_diagnostics/btt_admission/map_flags.py']
    data=subprocess.check_output(['git','archive','--format=zip','HEAD',*names],cwd=ROOT)
    with zipfile.ZipFile(io.BytesIO(data)) as archive:archive.extractall(source)
    config=configparser.ConfigParser(interpolation=None);config.read(source/'platformio.ini')
    config['platformio']['build_dir']=(OUT/'baseline-head-firmware').as_posix()
    with (source/'baseline.ini').open('w') as file:config.write(file)
    (OUT/'baseline-head.txt').write_bytes(subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT))
def measure():
    package=Path.home()/'.platformio/packages/toolchain-gccarmnoneeabi-teensy/bin'
    result={}
    for label,folder in [('baseline',OUT/'baseline-head-firmware/teensy41_dev'),('current',ROOT/'.pio/build/teensy41_dev')]:
        elf=folder/'firmware.elf'
        sections=subprocess.check_output([str(package/'arm-none-eabi-size.exe'),'-A',str(elf)]).decode()
        symbols=subprocess.check_output([str(package/'arm-none-eabi-nm.exe'),'-n','-S','-C',str(elf)]).decode()
        (OUT/(label+'-sections.txt')).write_text(sections);(OUT/(label+'-symbols.txt')).write_text(symbols)
        sizes={p[0]:int(p[1]) for line in sections.splitlines() if len(p:=line.split())==3 and p[0].startswith('.')}
        addresses={p[-1]:int(p[0],16) for line in symbols.splitlines() if len(p:=line.split())>=3 and p[-1] in ('_stext','_etext','_itcm_block_count')}
        banks=addresses['_itcm_block_count'];ram1=sizes['.data']+sizes['.bss'];ram2=sizes['.bss.dma']
        result[label]=dict(itcm=addresses['_etext']-addresses['_stext'],banks=banks,itcm_padding=banks*32768-(addresses['_etext']-addresses['_stext']),
            ram1=ram1,stack_gap=524288-banks*32768-ram1,ram2=ram2,heap_interval=524288-ram2,
            flash_code=sum(sizes.get(k,0) for k in ('.text.code','.text.itcm','.fini','.ARM.exidx')))
        result[label]['images']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (elf,folder/'firmware.hex')}
    result['delta']={k:result['current'][k]-v for k,v in result['baseline'].items() if isinstance(v,int)}
    (OUT/'memory.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
if __name__=='__main__':{'prepare':prepare,'measure':measure}[sys.argv[1]]()

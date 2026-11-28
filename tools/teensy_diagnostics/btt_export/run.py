"""Explicit identity-checked dedicated-card export checks; no uncertain mutation retries.
Run with PlatformIO Python (pyserial). Evidence directories are exclusive.
"""
import hashlib,json,subprocess,sys,time,uuid,struct,zlib,re
from pathlib import Path
import serial
from serial.tools import list_ports
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/btt-export'
MANIFEST=json.loads((OUT/'physical-manifest.json').read_text()) if (OUT/'physical-manifest.json').exists() else {}
def need(ok,message):
    if not ok:raise RuntimeError(message)

def ports():
    end=time.monotonic()+15
    while True:
        found=list(list_ports.comports())
        matches=[p for p in found if p.vid==0x16c0 and p.serial_number==MANIFEST['expected_usb_serial']]
        teensy=[p for p in found if p.vid==0x16c0]
        need(len(teensy)<=1 and (not teensy or len(matches)==1),'USB board identity ambiguous/wrong')
        if matches:break
        need(time.monotonic()<end,'USB board identity missing after reenumeration wait')
        time.sleep(.25) # Read-only discovery, never retries an upload or protocol request.
    return matches[0], [dict(device=p.device,vid=p.vid,pid=p.pid,serial=p.serial_number) for p in found]

class Wire:
    def __init__(self,folder):
        self.folder=folder;self.lines=[];self.pending=bytearray();self.start=time.monotonic()
        port,listing=ports();(folder/'ports.json').write_text(json.dumps(listing,indent=2))
        self.raw=(folder/'serial.raw').open('xb');self.log=(folder/'serial.log').open('x')
        self.events=(folder/'serial-events.jsonl').open('x')
        self.wire=serial.Serial(port.device,115200,timeout=.05,write_timeout=3)
        self.pump(.5)
    def record(self,direction,data):
        self.events.write(json.dumps(dict(seconds=time.monotonic()-self.start,direction=direction,hex=data.hex()))+'\n');self.events.flush()
    def pump(self,seconds,predicate=None):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            data=self.wire.read(min(self.wire.in_waiting or 1,4096))
            if data:
                self.record('RX',data);self.raw.write(data);self.raw.flush();self.pending.extend(data)
                while b'\n' in self.pending:
                    line,_,rest=self.pending.partition(b'\n');self.pending[:]=rest
                    text=line.decode('ascii',errors='replace').rstrip('\r');self.lines.append(text)
                    self.log.write('RX '+text+'\n');self.log.flush()
            if predicate and predicate():return True
        return False
    def ask(self,line,predicate,seconds=12):
        start=len(self.lines);data=(line+'\n').encode();self.record('TX',data)
        self.log.write('TX '+line+'\n');self.log.flush()
        need(self.wire.write(data)==len(data),'Short write, no automatic retry');self.wire.flush()
        def finished():
            rows=self.lines[start:]
            return predicate(rows) or any(s.startswith(('ERROR ','BTTEST1 ERROR ','BTBTT1 ERROR ')) for s in rows)
        need(self.pump(seconds,finished),'Timeout, do not retry automatically: '+line)
        need(predicate(self.lines[start:]),'Device rejected request; no retry: '+line+'; '+repr(self.lines[start:]))
        self.pump(.2);return self.lines[start:]
    def expect(self,line,reply,seconds=12):return self.ask(line,lambda rows:reply in rows,seconds)
    def close(self):
        self.wire.close()
        if not self.events.closed:self.record('CLOSED',bytes(self.pending))
        self.raw.close();self.log.close();self.events.close()

def prepare():
    folder=OUT/'hardware-input';folder.mkdir(exist_ok=False)
    nonce=uuid.uuid4().hex[:10];directory='BTT-export-'+nonce
    for name,args in [('fixture-build.log',['g++','-std=c++17','-O2','-I.',str(Path(__file__).with_name('fixture.cpp')),'tools/btt/foundation.cpp','-o',str(folder/'fixture.exe')]),
                      ('fixture-generate.log',[str(folder/'fixture.exe'),str(folder)])]:
        result=subprocess.run(args,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(OUT/name).write_bytes(result.stdout);need(result.returncode==0,name)
    data=(folder/'input.btt').read_bytes()
    header='#pragma once\n#include <cstdint>\n'+f'constexpr char kDirectory[]="/{directory}";\nconstexpr char kPath[]="/{directory}/ordinary.btt";\nconstexpr char kStageCommand[]="STAGE {nonce}";\n'
    header+='constexpr uint8_t kFixture[]={'+','.join(map(str,data))+'};\n'
    (folder/'fixture_bytes.h').write_text(header)
    helper=OUT/'helper-source';helper.mkdir(exist_ok=False)
    source=(ROOT/'tools/teensy_diagnostics/btt_stopped_save/physical/main.cpp').read_text().replace('generation<=16','generation<=256')
    (helper/'main.cpp').write_text(source)
    (OUT/'helper.ini').write_text(f'''[platformio]
src_dir = {helper.as_posix()}
lib_dir = {ROOT.as_posix()}/libraries
build_dir = {(OUT/'helper-firmware').as_posix()}
[env:teensy41]
platform = teensy
framework = arduino
board = teensy41
build_flags = -D USB_MIDI_AUDIO_SERIAL -I{folder.as_posix()}
''')
    manifest=dict(directory=directory,path=directory+'/ordinary.btt',nonce=nonce,expected_usb_serial='17681760',
        expected_cid='6f0303534441424310aa001504018b00',expected_sectors=61076480,expected_fat=32,
        input_sha256=hashlib.sha256(data).hexdigest(),bytes=len(data))
    (OUT/'physical-manifest.json').write_text(json.dumps(manifest,indent=2));print(json.dumps(manifest,indent=2))

def main():
    action=sys.argv[1]
    if action=='prepare':prepare();return
    folder=OUT/('physical-'+action);folder.mkdir(exist_ok=False);wire=None;result=dict(action=action,passed=False)
    try:
        if action.startswith('upload-'):
            prerequisite=OUT/('physical-preflight/result.json' if action=='upload-helper' else 'physical-stage/result.json')
            need(prerequisite.exists() and json.loads(prerequisite.read_text()).get('passed'),'Verified identity/staging prerequisite missing')
            port,listing=ports();(folder/'ports.json').write_text(json.dumps(listing,indent=2))
            args=['-c',str(OUT/'helper.ini'),'-e','teensy41'] if action=='upload-helper' else ['-e','teensy41_dev']
            command=[sys.executable,'-m','platformio','run',*args,'-t','nobuild','-t','upload','--upload-port',port.device]
            (folder/'command.json').write_text(json.dumps(command,indent=2))
            with (folder/'upload.log').open('xb') as log:completed=subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
            need(completed.returncode==0,'Upload failed; no automatic retry');result['passed']=True;return
        wire=Wire(folder)
        if action=='stage':
            info=wire.expect('INFO','IDENTIFIED')
            need(f"MEDIA {MANIFEST['expected_cid']} {MANIFEST['expected_sectors']} 32" in info,'Card identity mismatch')
            result['legacy']=[s for s in info if s.startswith('LEGACY ')]
            result['backings']=[list(map(int,s.split()[1:])) for s in info if s.startswith('BACKING ')]
            wire.expect('STAGE '+MANIFEST['nonce'],'STAGED',30)
            dump=wire.expect('DUMP','DUMPED',30);data=bytearray()
            for line in dump:
                if line.startswith('DATA '):
                    _,offset,encoded=line.split();need(int(offset)==len(data),'Dump offset');data.extend(bytes.fromhex(encoded))
            need(data==(OUT/'hardware-input/input.btt').read_bytes(),'Stage readback differs');(folder/'readback.btt').write_bytes(data)
        else:
            hello=wire.ask('BTTEST1 HELLO',lambda rows:any(s.startswith('BTPUBLISH1 NEXT 1 ') for s in rows))
            need('BTTEST1 STATE IDLE' in hello,'Device must be IDLE, no automatic STOP')
            facts=next(s.split() for s in hello if s.startswith('BTDEVICE1 FACTS 1 '));need(facts[3]==MANIFEST['expected_usb_serial'],'Wrong board')
            result.update(board=facts[3],boot=facts[4],verified_cid=MANIFEST['expected_cid'])
            route=facts[6] if facts[5]=='initialized' else 'bridge'
            wire.expect('BTAUDIO1 SET 1 '+route,'BTAUDIO1 SELECTED 1 '+route)
            wire.expect('BTBTT1 MEDIA 1',f"BTBTT1 MEDIA 1 {MANIFEST['expected_cid']} {MANIFEST['expected_sectors']} 32 64")
            if (action.startswith('exercise') or action=='final'):
                stamp=re.search(rb'BTINS1 TUNINGBUILD 1 [^\x00\r\n]+',(ROOT/'.pio/build/teensy41_dev/firmware.elf').read_bytes()).group().decode()
                need(stamp in hello,'Running firmware build identity differs from final normal image')
                result['firmware_build_line']=stamp
            if action.startswith('exercise'):
                need('BTBTT1 EXPORTCAP 1 64' in hello,'Wrong firmware/capability')
                known={tuple(pair) for pair in json.loads((OUT/'physical-stage/result.json').read_text())['backings']}
                # A prior task-owned attempt may have accepted a backing before
                # a later host failure. Its recorded successful LOAD is evidence
                # of this collision; never delete that retained artifact.
                for prior in OUT.glob('physical-exercise*/result.json'):
                    record=json.loads(prior.read_text());trace=prior.with_name('serial.log')
                    if 'known_collisions' in record and trace.exists() and ('BTBTT1 LOAD 503 '+MANIFEST['path']) in trace.read_text() and 'BTBTT1 STATE 1 11 ok ' in trace.read_text():
                        known.add((0,record['known_collisions']+1))
                for generation in range(1,257):
                    rows=wire.ask('BTBTT1 LOAD 503 '+MANIFEST['path'],lambda rows:any(s.startswith(('BTBTT1 STATE 1 11 ','BTBTT1 STATE 1 12 ')) for s in rows),45)
                    if any(s.startswith('BTBTT1 STATE 1 11 ok ') for s in rows):break
                    need(any('snapshot-create-collision-or-io' in s for s in rows) and (0,generation) in known,'Uncertain LOAD failure, no retry')
                else:raise RuntimeError('No free backing generation')
                result['known_collisions']=generation-1
                wire.close();wire=None
                target=folder/'windows-btx';executable=ROOT/'deploy/windows/BroTrackerTerminal.exe'
                args=[str(executable),'--export-btt',str(target),MANIFEST['expected_usb_serial'],facts[4]]
                (folder/'command.json').write_text(json.dumps(args,indent=2))
                completed=subprocess.run(args,cwd=ROOT,timeout=200)
                need(completed.returncode==0,'Actual Windows BTX export failed: '+str(completed.returncode))
                data=(target/'exported.btt').read_bytes();expected=(OUT/'hardware-input/expected.btt').read_bytes();need(data==expected,'Export byte mismatch')
                u32=lambda at:struct.unpack_from('<I',data,at)[0]
                need(data[:4]==b'BTT1' and u32(16)==len(data),'Envelope length');need(zlib.crc32(data[:20]+data[24:])==u32(20),'Whole CRC')
                for i in range(struct.unpack_from('<H',data,12)[0]):
                    at=32+24*i;offset,length,crc=struct.unpack_from('<III',data,at+8);need(zlib.crc32(data[offset:offset+length])==crc,'Section CRC')
                result.update(bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),independent_crcs=True,executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest())
                check=folder/'after-btx';check.mkdir();wire=Wire(check);wire.ask('BTTEST1 HELLO',lambda rows:any(s.startswith('BTPUBLISH1 NEXT 1 ') for s in rows))
            status=wire.ask('BTTEST1 STATUS',lambda rows:any(s.startswith('BTPUBLISH1 NEXT 1 ') for s in rows));need('BTTEST1 STATE IDLE' in status,'Final state not IDLE');result['final_state']='IDLE'
        result['passed']=True
    except Exception as error:result['error']=str(error);raise
    finally:
        if wire:
            wire.close();result['serial_closed']=not wire.wire.is_open
        (folder/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
if __name__=='__main__':main()

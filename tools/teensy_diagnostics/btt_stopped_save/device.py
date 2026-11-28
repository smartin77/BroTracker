"""Identity-checked explicit physical steps. Never retries an uncertain write/upload.
Run with PlatformIO's Python (pyserial installed). All logs/exports are exclusive.
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time
import serial
from serial.tools import list_ports

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/btt-stopped-save'
MANIFEST=json.loads((OUT/'physical-manifest.json').read_text())

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

def snapshot(wire,id):
    lines=wire.ask(f'BTPATTERN1 GET 2 {id}',lambda rows:any(s.startswith(f'BTPATTERN1 END 2 {id} ') for s in rows))
    header=next(s.split() for s in lines if s.startswith(f'BTPATTERN1 BEGIN 2 {id} '))
    _,rows,channels,bpm,count=map(int,header[3:]);need((rows,channels,bpm,count)==(8,8,13750,64),'Snapshot dimensions/BPM')
    cells=[list(map(int,s.split()[3:])) for s in lines if s.startswith(f'BTPATTERN1 CELL 2 {id} ')]
    need([c[:2] for c in cells]==[[id,i] for i in range(64)],'Snapshot cell identity/order')
    expected=[]
    for i in range(64):
        row=i//8;expected.append([60+row,0 if row%2 else 1,0] if i%8==row%2 else [255,255,0])
    need([c[2:] for c in cells]==expected,'Snapshot notes/stable-ID runtime mapping')
    payload=bytes([rows,channels])+bpm.to_bytes(4,'little')+bytes(v for c in cells for v in c[2:])
    fnv=2166136261
    for b in payload:fnv=((fnv^b)*16777619)&0xffffffff
    need(f'BTPATTERN1 END 2 {id} {count} {fnv}' in lines,'Snapshot checksum')
    return hashlib.sha256(payload).hexdigest()

def save(wire,id,status,reason=None):
    lines=wire.ask(f'BTBTT1 SAVE 1 {id}',lambda rows:any(s.startswith(f'BTBTT1 SAVED 1 {id} ') for s in rows),45)
    report=next(s for s in lines if s.startswith(f'BTBTT1 SAVED 1 {id} '))
    need(report.split()[4]==status,'Unexpected Save outcome: '+report)
    if reason:need(report.endswith(' '+reason),'Unexpected rejection: '+report)
    return report

next_generation=1
staging_slot=0
def load(wire):
    global next_generation,staging_slot
    stages=[json.loads(p.read_text()) for p in OUT.glob('physical-*/result.json')]
    stages=[s for s in stages if s.get('action')=='stage' and s.get('passed')]
    need(len(stages)==1,'Require successful staging/collision inventory')
    known={tuple(pair) for pair in stages[0]['backings']}
    # Only retry a positively identified registry collision, never an uncertain
    # LOAD. Old snapshots are historical evidence and are never deleted to pass.
    for attempt in range(16):
        lines=wire.ask('BTBTT1 LOAD 503 '+MANIFEST['path'],lambda rows:any(
            s.startswith(('BTBTT1 STATE 1 11 ','BTBTT1 STATE 1 12 ','BTBTT1 ERROR ')) for s in rows),45)
        if 'BTBTT1 STATE 1 11 ok 503 13750 2 1' in lines:
            next_generation+=1;staging_slot=1-staging_slot;return attempt
        need(any('snapshot-create-collision-or-io' in s for s in lines) and (staging_slot,next_generation) in known,
             'LOAD failed without positively inventoried collision; no automatic retry')
        next_generation+=1
    raise RuntimeError('Bounded known collision attempts exhausted')

def main():
    action=sys.argv[1];folder=OUT/('physical-'+(sys.argv[2] if len(sys.argv)>2 else action));folder.mkdir(exist_ok=False)
    result=dict(action=action,passed=False);wire=None
    try:
        if action.startswith('upload-'):
            profile='helper' if action=='upload-helper' or action=='upload-inspect' else 'current'
            port,listing=ports();(folder/'ports.json').write_text(json.dumps(listing,indent=2))
            command=[sys.executable,'-m','platformio','run','-c',str(OUT/(profile+'.ini')),
                '-e','teensy41' if profile=='helper' else 'teensy41_dev','-t','nobuild','-t','upload','--upload-port',port.device]
            (folder/'command.json').write_text(json.dumps(command,indent=2))
            with (folder/'upload.log').open('xb') as log:completed=subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
            need(completed.returncode==0,'Upload failed; no automatic retry')
            build=OUT/('helper-firmware/teensy41' if profile=='helper' else 'current-firmware/teensy41_dev')
            result['image_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (build/'firmware.hex',build/'firmware.elf')}
            result['passed']=True;return
        wire=Wire(folder)
        if action in ('stage','inspect'):
            identity=f"MEDIA {MANIFEST['expected_cid']} {MANIFEST['expected_sectors']} {MANIFEST['expected_fat']}"
            info=wire.expect('INFO','IDENTIFIED')
            need(identity in info,'Card identity mismatch before writes')
            result['legacy']=[s for s in info if s.startswith('LEGACY ')]
            result['backings']=[list(map(int,s.split()[1:])) for s in info if s.startswith('BACKING ')]
            if action=='inspect':
                stages=[json.loads(p.read_text()) for p in OUT.glob('physical-*/result.json')]
                stages=[s for s in stages if s.get('action')=='stage' and s.get('passed')]
                need(len(stages)==1,'Require one successful staged fixture record')
                before=stages[0]
                need(result['legacy']==before['legacy'],'Legacy files changed during protocol exercise')
            if action=='stage':wire.expect('STAGE '+MANIFEST['nonce'],'STAGED',30)
            dump=wire.expect('DUMP','DUMPED',30)
            if action=='inspect':need('RECOVERY 0 0' in dump,'Successful Save left a recovery sibling')
            data=bytearray();length=int(next(s.split()[1] for s in dump if s.startswith('BYTES ')))
            for s in dump:
                if s.startswith('DATA '):
                    _,offset,hexbytes=s.split();need(int(offset)==len(data),'Dump offset mismatch');data.extend(bytes.fromhex(hexbytes))
            need(len(data)==length,'Dump incomplete')
            (folder/'canonical.btt').write_bytes(data)
            expected=(OUT/'hardware-input'/('input.btt' if action=='stage' else 'expected.btt')).read_bytes()
            need(bytes(data)==expected,'Independent file export differs from native expected')
            # Independent envelope/section CRC verification on exported bytes.
            import struct,zlib
            u32=lambda at:struct.unpack_from('<I',data,at)[0]
            need(data[:4]==b'BTT1' and u32(16)==len(data),'Envelope/size')
            need(zlib.crc32(data[:20]+data[24:])==u32(20),'Whole CRC')
            count=struct.unpack_from('<H',data,12)[0]
            for i in range(count):
                at=32+i*24;offset,length,crc=struct.unpack_from('<III',data,at+8)
                need(zlib.crc32(data[offset:offset+length])==crc,'Section CRC')
            result.update(bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),cid=MANIFEST['expected_cid'],independent_crcs=True)
        else:
            hello=wire.ask('BTTEST1 HELLO',lambda rows:any(s.startswith('BTPUBLISH1 NEXT 1 ') for s in rows))
            need('BTTEST1 STATE IDLE' in hello,'Board not IDLE')
            need(any(s.startswith('BTDEVICE1 FACTS 1 '+MANIFEST['expected_usb_serial']+' ') for s in hello),'Wrong device')
            if action!='preflight':
                identities=json.loads((OUT/'firmware-identities.json').read_text())
                need(identities['current']['build_line'] in hello,'Firmware build stamp mismatch')
                need('BTBTT1 SAVECAP 1' in hello,'Missing Save capability')
            if action=='exercise':
                wire.expect('BTAUDIO1 SET 1 bridge','BTAUDIO1 SELECTED 1 bridge')
                # Query the new read-only media facts before any LOAD snapshot/write.
                wire.expect('BTBTT1 MEDIA 1',f"BTBTT1 MEDIA 1 {MANIFEST['expected_cid']} {MANIFEST['expected_sectors']} 32 64")
                save(wire,1,'rejected','no-retained-tune')
                result['known_collisions']=load(wire)
                result['before_snapshot']=snapshot(wire,810001)
                first=save(wire,2,'saved');need(save(wire,2,'saved')==first,'Duplicate response changed')
                wire.expect('BTBTT1 SAVEGET 1 2',first);save(wire,3,'saved')
                wire.ask('BTBTT1 LOAD 503 '+MANIFEST['directory']+'/missing.btt',lambda rows:any(s.startswith('BTBTT1 STATE 1 12 ') for s in rows))
                save(wire,4,'saved')
                wire.expect('BTTEST1 START','BTTEST1 STARTED');save(wire,5,'rejected','not-settled')
                wire.expect('BTTEST1 PAUSE','BTTEST1 PAUSED');rejected=save(wire,6,'rejected','not-settled')
                wire.expect('BTTEST1 STOP','BTTEST1 STOPPED');need(save(wire,6,'rejected')==rejected,'Rejected duplicate executed later')
                save(wire,7,'saved');load(wire);save(wire,8,'saved')
                result['after_snapshot']=snapshot(wire,810002)
                need(result['after_snapshot']==result['before_snapshot'],'Saved/reloaded runtime changed')
                wire.close();wire=Wire(make_reconnect(folder))
                reconnect=wire.ask('BTTEST1 HELLO',lambda rows:any(s.startswith('BTPUBLISH1 NEXT 1 ') for s in rows))
                need('BTSESSION1 LINEAGE 1 0 -' in reconnect,'Lineage became persistent')
                wire.expect('BTSESSION1 CLAIM 1 1234567890abcdef1234567890abcdef','BTSESSION1 LINEAGE 1 0 -')
                need(save(wire,8,'saved')=='BTBTT1 SAVED 1 8 saved 1 1 0 1 0 ok','Reconnect duplicate did not replay')
                wire.expect('BTSTORE1 SAVE 1','BTBTT1 ERROR retained-tune-or-load-busy')
                result['final_snapshot']=snapshot(wire,810003)
            elif action=='final':
                wire.expect('BTAUDIO1 SET 1 bridge','BTAUDIO1 SELECTED 1 bridge')
                wire.expect('BTBTT1 STATUS','BTBTT1 STATE 1 0 none 0 0 0 0')
                wire.expect('BTBTT1 MEDIA 1',f"BTBTT1 MEDIA 1 {MANIFEST['expected_cid']} {MANIFEST['expected_sectors']} 32 64")
            elif action!='preflight':raise RuntimeError('Unknown action')
            status=wire.ask('BTTEST1 STATUS',lambda rows:any(s.startswith('BTPUBLISH1 NEXT 1 ') for s in rows))
            need('BTTEST1 STATE IDLE' in status,'Final state not IDLE')
        result['passed']=True
    except Exception as error:
        result['error']=str(error);raise
    finally:
        if wire:wire.close()
        (folder/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))

def make_reconnect(folder):
    target=folder/'reconnect';target.mkdir(exist_ok=False);return target

if __name__=='__main__':main()

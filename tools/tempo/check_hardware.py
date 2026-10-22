"""Opt-in bounded serial checks for session-only live tempo; no audio recording.
Uses only supported protocol controls. Restores the initial published tune on
success. On failure, attempts STOP while serial remains usable; always closes
its owned handle.
"""
import argparse
import json
from pathlib import Path
import time
import serial
from serial.tools import list_ports

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--hardware',action='store_true',required=True)
parser.add_argument('--output',type=Path,default=Path('build/live-tempo'))
args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
ports=[p for p in list_ports.comports() if p.vid==0x16c0 and p.pid==0x048a]
assert len(ports)==1,ports
port=ports[0];connection=None;transcript=[];checks=[];snapshot_id=0

def open_port():
    return serial.Serial(port.device,115200,timeout=.02,write_timeout=2)
def send(line):
    transcript.append('TX '+line);connection.write((line+'\n').encode());connection.flush()
def collect(seconds=.25):
    deadline=time.monotonic()+seconds;lines=[]
    while time.monotonic()<deadline:
        line=connection.readline().decode('ascii',errors='replace').strip()
        if line:transcript.append('RX '+line);lines.append(line)
    return lines
def state():
    send('BTTEMPO1 GET 1');lines=collect()
    rows=[list(map(int,s.split()[3:])) for s in lines if s.startswith('BTTEMPO1 STATE 1 ')]
    assert rows,lines
    settled,applied,nxt,pending,tempo,published=rows[-1]
    return dict(settled=settled,applied=applied,next=nxt,pending=pending,tempo=tempo,published=published)
def tempo(value):
    before=state();identity=before['next'];send(f'BTTEMPO1 SET 1 {identity} {value}')
    lines=collect(.3);assert f'BTTEMPO1 SUBMITTED 1 {identity}' in lines,lines
    for _ in range(6):
        after=state()
        if after['settled']==identity:break
    assert after['applied']==identity and after['tempo']==value and after['pending']==0,after
    checks.append(dict(operation='tempo',before=before,after=after));return after

def transport(command,ack):
    send('BTTEST1 '+command);lines=collect(.3);assert 'BTTEST1 '+ack in lines,lines;return lines

def hash_bytes(values):
    result=2166136261
    for value in values:result=((result^value)*16777619)&0xffffffff
    return result

def snapshot(working=False):
    global snapshot_id
    snapshot_id+=1;namespace='BTPREVIEW1' if working else 'BTPATTERN1'
    send(f'BTPREVIEW1 GET 1 {snapshot_id} 0' if working else f'BTPATTERN1 GET 2 {snapshot_id}')
    lines=collect(.8)
    header=[s for s in lines if s.startswith(namespace+' BEGIN 2 ')]
    assert len(header)==1,lines
    identity,rows,channels,bpm,count=map(int,header[0].split()[3:]);assert identity==snapshot_id and count==rows*channels
    cells=[tuple(map(int,s.split()[3:])) for s in lines if s.startswith(namespace+' CELL 2 ')]
    assert len(cells)==count,(len(cells),count,lines)
    assert [c[1] for c in cells]==list(range(count))
    payload=[rows,channels,*bpm.to_bytes(4,'little')]
    for cell in cells:assert cell[0]==identity;payload.extend(cell[2:])
    end=[tuple(map(int,s.split()[3:])) for s in lines if s.startswith(namespace+' END 2 ')]
    assert end==[(identity,count,hash_bytes(payload))],end
    return dict(rows=rows,channels=channels,tempo=bpm,cells=[c[2:] for c in cells])

def publish(tune):
    send('BTTEST1 STATUS');lines=collect()
    identities=[int(s.split()[-1]) for s in lines if s.startswith('BTPUBLISH1 NEXT 1 ')]
    assert identities,lines
    identity=identities[-1];rows=tune['rows'];channels=tune['channels'];bpm=tune['tempo'];cells=tune['cells']
    send(f'BTPUBLISH1 BEGIN 2 {identity} {rows} {channels} {len(cells)} {bpm}')
    for index,(note,instrument,timing) in enumerate(cells):send(f'BTPUBLISH1 CELL 1 {identity} {index} {note} {instrument} {timing}')
    payload=[rows,channels,*bpm.to_bytes(4,'little')]
    for cell in cells:payload.extend(cell)
    send(f'BTPUBLISH1 COMMIT 1 {identity} {len(cells)} {hash_bytes(payload)}')
    lines=collect(1)
    assert f'BTPUBLISH1 SUBMITTED 1 {identity}' in lines,lines
    assert f'BTPUBLISH1 APPLIED 1 {identity}' in lines,lines
    read=snapshot();assert read==tune,(read,tune)
    checks.append(dict(operation='publication',identity=identity,tempo=bpm,verified_cells=len(cells)))

initial=None
try:
    connection=open_port();collect(.2);send('BTTEST1 HELLO');hello=collect(.8)
    assert 'BTTEST1 STATE IDLE' in hello,hello
    assert 'BTTEMPO1 CAP 1' in hello and 'BTPUBLISH1 TEMPOCAP 2' in hello
    send('BTAUDIO1 SET 1 default');assert 'BTAUDIO1 SELECTED 1 bridge' in collect(.5)
    send('BTMIDI1 SET 1 12');assert 'BTMIDI1 SELECTED 1 12' in collect(.3)
    initial=snapshot();assert initial['tempo']==15500,initial
    assert state()['tempo']==15500 and state()['published']==15500
    checks.append(dict(operation='initial-fixture',applied=15500,published=15500,playback='stopped'))
    before=state()
    for value in (3199,30001,4294967295):
        send(f'BTTEMPO1 SET 1 {before["next"]} {value}');lines=collect()
        assert f'BTTEMPO1 REJECTED 1 {before["next"]}' in lines,lines
        assert state()==before
    checks.append(dict(operation='invalid-range',result='transactionally rejected'))
    tempo(12505);assert snapshot()['tempo']==15500
    transport('START','STARTED');tempo(15000);tempo(10000)
    playing=collect(.3)
    ticks=[int(s.split()[6]) for s in transcript if s.startswith('RX BTPATTERN1 POS 2 1 1 ')]
    assert len(ticks)>10 and ticks[-1]>ticks[0] and all(a<=b for a,b in zip(ticks,ticks[1:])),ticks
    checks.append(dict(operation='playing-continuity',telemetry=len(ticks),first_tick=ticks[0],last_tick=ticks[-1]))
    paused=transport('PAUSE','PAUSED');positions=[s for s in paused if s.startswith('BTPATTERN1 POS 2 2 1 ')]
    assert positions,paused
    paused_tick=positions[-1].split()[5];tempo(13000)
    after_pause=collect(.2);assert all(s.split()[5]==paused_tick for s in after_pause if s.startswith('BTPATTERN1 POS 2 2 1 '))
    working=snapshot(True);assert working['tempo']==13000
    publish(working);assert state()['published']==13000
    tempo(12550);tempo(state()['published']) # Restore the published tempo baseline.
    transport('CONTINUE','CONTINUED');assert state()['tempo']==13000
    transport('STOP','STOPPED');assert state()['tempo']==13000
    transport('START','STARTED');assert state()['tempo']==13000;transport('STOP','STOPPED')
    connection.close();connection=open_port();send('BTTEST1 HELLO');assert 'BTTEST1 STATE IDLE' in collect(.6)
    assert state()['tempo']==13000 and snapshot()['tempo']==13000
    checks.append(dict(operation='reconnect-and-stop-start-retention',tempo=13000,automatic_start=False))
    publish(initial);assert state()['tempo']==15500 and state()['published']==15500
    send('BTTEST1 STOP');collect(.3);send('BTTEST1 STATUS');assert 'BTTEST1 STATE IDLE' in collect(.3)
    checks.append(dict(operation='final-state',playback='stopped',tempo=15500,published=15500,pattern='initial fixture restored'))
finally:
    if connection and connection.is_open:
        try:send('BTTEST1 STOP');collect(.2)
        finally:connection.close()
    (args.output/'hardware.log').write_text('\n'.join(transcript)+'\n')
    (args.output/'hardware.json').write_text(json.dumps(dict(port=port.device,serial=port.serial_number,checks=checks,serial_closed=not connection or not connection.is_open),indent=2)+'\n')
print(json.dumps(dict(port=port.device,serial=port.serial_number,checks=checks),indent=2))

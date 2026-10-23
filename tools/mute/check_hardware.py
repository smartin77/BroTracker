"""Authorized bounded session-mute/transport/readback check. No audio recording.
Restores initial mask/pattern, stops playback and closes its serial handle.
"""
import argparse,json,time
from pathlib import Path
import serial
from serial.tools import list_ports
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--hardware',action='store_true',required=True)
parser.add_argument('--output',type=Path,default=Path('build/channel-mute/hardware'))
args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
ports=[p for p in list_ports.comports() if p.vid==0x16c0 and p.pid==0x048a]
assert len(ports)==1,ports
port=ports[0];connection=None;log=[];checks=[];initial=None;initial_mask=None;sid=0

def collect(seconds=.2):
    result=[];end=time.monotonic()+seconds
    while time.monotonic()<end:
        line=connection.readline().decode('ascii',errors='replace').strip()
        if line:result.append(line);log.append('RX '+line)
    return result

def send(line):
    log.append('TX '+line);connection.write((line+'\n').encode());connection.flush()

def request(line,expected,seconds=.25):
    send(line);lines=collect(seconds);assert expected in lines,(expected,lines);return lines

def state():
    send('BTMUTE1 GET 1');lines=collect()
    rows=[list(map(int,v.split()[3:])) for v in lines if v.startswith('BTMUTE1 STATE 1 ')]
    assert rows,lines
    return rows[-1]

def set_mask(mask):
    before=state();identity=before[1];assert identity
    request(f'BTMUTE1 SET 1 {identity} {mask}',f'BTMUTE1 SUBMITTED 1 {identity}')
    after=state();assert after[0]==identity and after[2]==mask,after
    checks.append(dict(operation='mask',applied=identity,mask=mask));return after

def checksum(values):
    h=2166136261
    for v in values:h=((h^v)*16777619)&0xffffffff
    return h

def snapshot():
    global sid
    sid+=1;send(f'BTPATTERN1 GET 2 {sid}');lines=collect(.8)
    heads=[s for s in lines if s.startswith('BTPATTERN1 BEGIN 2 ')]
    assert len(heads)==1,lines
    ident,rows,channels,tempo,count=map(int,heads[0].split()[3:]);assert ident==sid
    cells=[list(map(int,s.split()[5:])) for s in lines if s.startswith('BTPATTERN1 CELL 2 ')]
    assert len(cells)==count==rows*channels
    payload=[rows,channels,*tempo.to_bytes(4,'little')]
    for c in cells:payload.extend(c)
    assert f'BTPATTERN1 END 2 {sid} {count} {checksum(payload)}' in lines
    return dict(rows=rows,channels=channels,tempo=tempo,cells=cells)

def publish(tune):
    send('BTTEST1 STATUS');lines=collect();ids=[int(s.split()[-1]) for s in lines if s.startswith('BTPUBLISH1 NEXT 1 ')];assert ids,lines
    identity=ids[-1];rows=tune['rows'];channels=tune['channels'];tempo=tune['tempo'];cells=tune['cells']
    send(f'BTPUBLISH1 BEGIN 2 {identity} {rows} {channels} {len(cells)} {tempo}')
    for i,c in enumerate(cells):send(f'BTPUBLISH1 CELL 1 {identity} {i} '+' '.join(map(str,c)))
    payload=[rows,channels,*tempo.to_bytes(4,'little')]
    for c in cells:payload.extend(c)
    request(f'BTPUBLISH1 COMMIT 1 {identity} {len(cells)} {checksum(payload)}',f'BTPUBLISH1 APPLIED 1 {identity}',.5)

try:
    connection=serial.Serial(port.device,115200,timeout=.02,write_timeout=2)
    hello=request('BTTEST1 HELLO','BTMUTE1 CAP 1',.6)
    for line in ('BTTEMPO1 CAP 1','BTPREVIEW1 CAP 1','BTPUBLISH1 CAP 1','BTGAIN1 CAP 1'):
        assert line in hello,(line,hello)
    assert any(s.startswith('BTPUBLISH1 NEXT 1 ') for s in hello),hello
    request('BTTEST1 STOP','BTTEST1 STOPPED');initial_mask=state()[2];assert initial_mask==0,initial_mask
    initial=snapshot();assert initial['tempo']==15500
    request('BTMIDI1 SET 1 12','BTMIDI1 SELECTED 1 12')
    request('BTAUDIO1 SET 1 default','BTAUDIO1 SELECTED 1 bridge')
    before=state()
    for identity,mask in ((0,1),(before[1],256)):
        request(f'BTMUTE1 SET 1 {identity} {mask}',f'BTMUTE1 REJECTED 1 {identity}')
        assert state()==before
    set_mask(1)
    request('BTTEST1 START','BTTEST1 STARTED',.3)
    set_mask(3);set_mask(2)
    ticks=[]
    for _ in range(2):
        lines=request('BTTEST1 STATUS','BTTEST1 STATE PLAYING',.3)
        ticks.extend(int(s.split()[5]) for s in lines if s.startswith('BTPATTERN1 POS 2 1 1 '))
        assert not any(s.startswith('BTTEST1 ERROR') for s in lines),lines
    assert len(ticks)>1 and ticks[-1]>ticks[0],ticks
    checks.append(dict(operation='playing-continuity',first_tick=ticks[0],last_tick=ticks[-1]))
    request('BTTEST1 PAUSE','BTTEST1 PAUSED');assert state()[2]==2
    request('BTTEST1 CONTINUE','BTTEST1 CONTINUED');assert state()[2]==2
    request('BTTEST1 STOP','BTTEST1 STOPPED');assert state()[2]==2
    publish(initial);assert state()[2]==2;assert snapshot()==initial
    checks.append(dict(operation='transport-publication-retention',mask=2))
    nxt=state()[1]
    for i,mask in enumerate((1,3,7)):send(f'BTMUTE1 SET 1 {nxt+i} {mask}')
    burst=collect(.4)
    for i in range(3):assert f'BTMUTE1 SUBMITTED 1 {nxt+i}' in burst,burst
    final=state();assert final[0]==nxt+2 and final[2]==7,final
    connection.close();connection=serial.Serial(port.device,115200,timeout=.02,write_timeout=2)
    request('BTTEST1 HELLO','BTTEST1 STATE IDLE',.5);assert state()[2]==7
    checks.append(dict(operation='coalesced-burst-reconnect',mask=7,automatic_start=False))
    set_mask(255);set_mask(initial_mask)
    request('BTTEST1 STOP','BTTEST1 STOPPED');assert snapshot()==initial
    checks.append(dict(operation='final',mask=state()[2],stopped=True,original_pattern=True))
finally:
    if connection and connection.is_open:
        try:
            send('BTTEST1 STOP');collect(.2)
            if initial_mask is not None:set_mask(initial_mask)
        finally:connection.close()
    (args.output/'hardware.log').write_text('\n'.join(log)+'\n')
    report=dict(port=port.device,serial=port.serial_number,checks=checks,serial_closed=not connection or not connection.is_open,
        scope='CDC masks/transport/readback only; no listening, external delivery or audible alignment proof')
    (args.output/'hardware.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))

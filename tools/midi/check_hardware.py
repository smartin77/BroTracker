"""Authorized, bounded Windows Teensy USB MIDI/serial check. No routing or audio proof.
Temporarily publishes an eight-row MIDI fixture, then restores the captured tune,
stops playback and closes only its own serial/MIDI handles. Requires pyserial.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import time
import serial
from serial.tools import list_ports

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--hardware',action='store_true',required=True)
parser.add_argument('--output',type=Path,default=Path('build/midi-foundation'))
args=parser.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
ports=[p for p in list_ports.comports() if p.vid==0x16c0 and p.pid==0x048a]
assert len(ports)==1,ports
port=ports[0]; connection=None; handle=w.HANDLE(); initial=None
transcript=[]; events=[]; checks=[]; snapshot_id=0; restored=False
mm=c.WinDLL('winmm')
class Caps(c.Structure):
    _fields_=[('mid',w.WORD),('pid',w.WORD),('version',w.DWORD),('name',w.WCHAR*32),('support',w.DWORD)]
callback_type=c.WINFUNCTYPE(None,w.HANDLE,w.UINT,c.c_size_t,c.c_size_t,c.c_size_t)
@callback_type
def callback(_handle,message,_instance,data,stamp):
    if message==0x3c3: # MIM_DATA, callback observation on this Windows host.
        events.append(dict(message=int(data)&0xffffff,winmm_ms=int(stamp),host_ns=time.perf_counter_ns()))
mm.midiInOpen.argtypes=[c.POINTER(w.HANDLE),w.UINT,callback_type,c.c_size_t,w.DWORD]
for name in ('midiInStart','midiInStop','midiInReset','midiInClose'):
    getattr(mm,name).argtypes=[w.HANDLE]
candidates=[]
for index in range(mm.midiInGetNumDevs()):
    caps=Caps(); assert mm.midiInGetDevCapsW(index,c.byref(caps),c.sizeof(caps))==0
    if 'Teensy' in caps.name or 'BroTracker' in caps.name: candidates.append((index,caps.name))
assert len(candidates)==1,candidates
midi_index,midi_name=candidates[0]

def send(line):
    transcript.append('TX '+line); connection.write((line+'\n').encode()); connection.flush()
def collect(seconds=.25):
    deadline=time.monotonic()+seconds; result=[]
    while time.monotonic()<deadline:
        line=connection.readline().decode('ascii',errors='replace').strip()
        if line: transcript.append('RX '+line); result.append(line)
    return result
def request(line,expected,seconds=.25):
    send(line); lines=collect(seconds); assert expected in lines,(expected,lines); return lines
def hash_bytes(values):
    result=2166136261
    for value in values: result=((result^value)*16777619)&0xffffffff
    return result
def snapshot():
    global snapshot_id
    snapshot_id+=1; send(f'BTPATTERN1 GET 2 {snapshot_id}'); lines=collect(.8)
    header=[s for s in lines if s.startswith('BTPATTERN1 BEGIN 2 ')]; assert len(header)==1,lines
    identity,rows,channels,bpm,count=map(int,header[0].split()[3:]); assert identity==snapshot_id
    cells=[tuple(map(int,s.split()[3:])) for s in lines if s.startswith('BTPATTERN1 CELL 2 ')]
    assert len(cells)==count==rows*channels
    assert [v[1] for v in cells]==list(range(count))
    payload=[rows,channels,*bpm.to_bytes(4,'little')]
    for cell in cells: assert cell[0]==identity; payload.extend(cell[2:])
    assert f'BTPATTERN1 END 2 {identity} {count} {hash_bytes(payload)}' in lines
    return dict(rows=rows,channels=channels,tempo=bpm,cells=[list(v[2:]) for v in cells])
def publish(tune):
    send('BTTEST1 STATUS'); lines=collect()
    identity=[int(v.split()[-1]) for v in lines if v.startswith('BTPUBLISH1 NEXT 1 ')][-1]
    rows=tune['rows']; channels=tune['channels']; bpm=tune['tempo']; cells=tune['cells']
    send(f'BTPUBLISH1 BEGIN 2 {identity} {rows} {channels} {len(cells)} {bpm}')
    for index,(note,instrument,timing) in enumerate(cells): send(f'BTPUBLISH1 CELL 1 {identity} {index} {note} {instrument} {timing}')
    payload=[rows,channels,*bpm.to_bytes(4,'little')]
    for cell in cells: payload.extend(cell)
    request(f'BTPUBLISH1 COMMIT 1 {identity} {len(cells)} {hash_bytes(payload)}',f'BTPUBLISH1 APPLIED 1 {identity}',1)
    assert snapshot()==tune

def open_serial(): return serial.Serial(port.device,115200,timeout=.02,write_timeout=2)
try:
    result=mm.midiInOpen(c.byref(handle),midi_index,callback,0,0x30000)
    assert result==0,('MIDI input may be owned elsewhere; no other application was closed',result)
    assert mm.midiInStart(handle)==0
    connection=open_serial(); collect(.2)
    request('BTTEST1 HELLO','BTMIDI1 CAP 1',.6)
    request('BTTEST1 STOP','BTTEST1 STOPPED')
    request('BTTEST1 START','BTTEST1 ERROR midi-not-ready-or-restart-required')
    for value in (0,17): request(f'BTMIDI1 SET 1 {value}','BTMIDI1 ERROR 1 invalid-channel')
    request('BTMIDI1 SET 1 12','BTMIDI1 SELECTED 1 12')
    request('BTMIDI1 SET 1 12','BTMIDI1 SELECTED 1 12')
    request('BTMIDI1 SET 1 13','BTMIDI1 ERROR 1 restart-required')
    request('BTAUDIO1 SET 1 default','BTAUDIO1 SELECTED 1 bridge')
    initial=snapshot()
    tune=dict(rows=8,channels=2,tempo=12500,cells=[[255,255,0] for _ in range(16)])
    # NOTE_EMPTY=255, NOTE_OFF=254; verify these against core/note.h before changing.
    tune['cells'][0]=[69,1,0]; tune['cells'][3]=[69,1,0]
    tune['cells'][4]=[254,255,0]; tune['cells'][7]=[72,1,0]
    tune['cells'][9]=[254,255,1]; tune['cells'][10]=[69,1,0]
    tune['cells'][12]=[69,1,0]; tune['cells'][14]=[69,2,0]
    publish(tune)
    events.clear(); request('BTTEST1 START','BTTEST1 STARTED',3.05)
    request('BTTEST1 STOP','BTTEST1 STOPPED')
    expected=[0x64459b,0x00458b,0x64489b,0x00488b,0x64459b,0x00458b,0x64459b,0x00458b]
    assert [v['message'] for v in events[:24]]==expected*3,events
    assert all((v['message']&15)==11 for v in events),events
    checks.append(dict(operation='notes-order-duplicate-deferred-retrigger-replacement-loop',messages=len(events),first_eight=expected,observed_loop_intervals_ms=[events[i]['winmm_ms']-events[i-8]['winmm_ms'] for i in (8,16,24) if i<len(events)]))
    before=len(events); request('BTTEST1 START','BTTEST1 STARTED',.035)
    request('BTTEST1 PAUSE','BTTEST1 PAUSED',.04)
    assert any(v['message']==0x00458b for v in events[before:]),events[before:]
    request('BTTEST1 CONTINUE','BTTEST1 CONTINUED',.035)
    request('BTTEST1 STOP','BTTEST1 STOPPED',.08)
    checks.append(dict(operation='pause-continue-stop',result='acknowledged; note cleanup observed'))
    send('BTMIDI1 GET 1'); diagnostics=collect(); state=[v for v in diagnostics if v.startswith('BTMIDI1 STATE 1 ')][-1]
    timing=[v for v in diagnostics if v.startswith('BTMIDI1 TIMING 1 ')][-1]
    assert timing.split()[6]=='0',timing
    assert state.split()[3:6]==['0','0','0'],state
    checks.append(dict(operation='adapter-state',line=state,timing=timing))
    connection.close(); connection=open_serial()
    request('BTTEST1 HELLO','BTTEST1 STATE IDLE',.6)
    request('BTMIDI1 SET 1 12','BTMIDI1 SELECTED 1 12')
    checks.append(dict(operation='reconnect',result='same channel idempotent; playback idle'))
finally:
    midi_close_result=0
    try:
        if connection and connection.is_open:
            try:
                request('BTTEST1 STOP','BTTEST1 STOPPED')
                if initial: publish(initial); restored=True
                request('BTTEST1 STATUS','BTTEST1 STATE IDLE')
            finally: connection.close()
    finally:
        # Always close MIDI even if serial cleanup/restoration itself raises.
        if handle.value:
            mm.midiInStop(handle); mm.midiInReset(handle)
            midi_close_result=mm.midiInClose(handle)
        (args.output/'hardware.log').write_text('\n'.join(transcript)+'\n')
        summary=dict(port=port.device,serial=port.serial_number,midi_input=midi_name,checks=checks,events=events,
            restored=restored,serial_closed=not connection or not connection.is_open,midi_close_result=midi_close_result,
            timing_scope='Windows MIDI callback observations only; no external audio or end-to-end synchronization measurement')
        (args.output/'hardware.json').write_text(json.dumps(summary,indent=2)+'\n')
assert midi_close_result==0,midi_close_result
print(json.dumps({k:v for k,v in summary.items() if k!='events'},indent=2))

"""Offline readable wire evidence and independent streaming byte/CRC comparison."""
import hashlib,json,sys,zlib
from collections import Counter
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
folder=Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'build/btt-export/physical-exercise-timing/windows-btx'
expected_path=ROOT/'build/btt-export/hardware-input/expected.btt'
pending={'TX':bytearray(),'RX':bytearray()};rows={'TX':[],'RX':[]};received=hashlib.sha256();received_bytes=0
with (folder/'serial-readable.log').open('x') as readable:
    for line in (folder/'serial-events.log').open():
        timestamp,direction,encoded=line.rstrip('\n').split(' ',2);data=bytes.fromhex(encoded)
        if direction=='RX':received.update(data);received_bytes+=len(data)
        pending[direction].extend(data)
        while b'\n' in pending[direction]:
            raw,_,tail=pending[direction].partition(b'\n');pending[direction][:]=tail
            text=raw.decode('ascii').rstrip('\r');rows[direction].append(text)
            readable.write(f'{timestamp} {direction} {text}\n')
raw_hash=hashlib.sha256()
with (folder/'serial.raw').open('rb') as stream:
    for chunk in iter(lambda:stream.read(4096),b''):raw_hash.update(chunk)
assert received.digest()==raw_hash.digest()
begins=[s.split() for s in rows['RX'] if s.startswith('BTBTT1 EXPORTBEGIN ')]
dones=[s.split() for s in rows['RX'] if s.startswith('BTBTT1 EXPORTDONE ')]
assert len(begins)==len(dones)==1
begin,done=begins[0],dones[0]
assert done[3:7]==begin[3:7] and done[7]==begin[8]
assert not any(s.startswith('BTBTT1 EXPORTERROR ') for s in rows['RX'])
id,generation,total,whole=map(int,begin[3:7]);token=begin[8]
offset=chunks=0;digest=hashlib.sha256();rolling=0
with expected_path.open('rb') as expected:
    for line in rows['RX']:
        if not line.startswith('BTBTT1 EXPORTDATA '):continue
        w=line.split();assert len(w)==10 and w[2]=='1' and int(w[3])==id and int(w[4])==generation and w[9]==token
        at,n,crc=map(int,w[5:8]);data=bytes.fromhex(w[8])
        assert at==offset and n==len(data)==min(64,total-offset) and zlib.crc32(data)==crc
        assert expected.read(n)==data
        digest.update(data);rolling=zlib.crc32(data[:20]+data[24:] if offset==0 else data,rolling)
        offset+=n;chunks+=1
    assert expected.read(1)==b''
assert offset==total and rolling==whole
forbidden=('BTBTT1 LOAD ','BTBTT1 SAVE ','BTSTORE1 SAVE ','BTTEST1 START','BTTEST1 STOP','BTTEST1 PAUSE','BTTEST1 CONTINUE',
    'BTPUBLISH1 BEGIN','BTPUBLISH1 CELL','BTPUBLISH1 END','BTPREVIEW1 BEGIN','BTPREVIEW1 CELL','BTPREVIEW1 END')
assert not any(line.startswith(forbidden) for line in rows['TX'])
summary=dict(passed=True,bytes=offset,chunks=chunks,sha256=digest.hexdigest(),request=id,generation=generation,boot=begin[7],challenge=token,
    max_export_line_bytes=max(len(s)+1 for s in rows['RX'] if s.startswith('BTBTT1 EXPORT')),
    rx_bytes=received_bytes,tx_verbs=dict(Counter(' '.join(s.split()[:2]) for s in rows['TX'])),
    unfinished_tails={k:bytes(v).hex() for k,v in pending.items() if v},no_music_or_transport_mutation=True)
with (folder/'wire-summary.json').open('x') as file:json.dump(summary,file,indent=2)
print(json.dumps(summary,indent=2))

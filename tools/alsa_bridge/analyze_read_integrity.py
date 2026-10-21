"""Independent FNV-1a read-return checks; a digest is not a cryptographic proof."""
import csv
import hashlib
import json
import re
from pathlib import Path


def digest(data):
    value=14695981039346656037
    for byte in data:
        value=((value ^ byte)*1099511628211)&0xffffffffffffffff
    return value


def analyze(directory, pcm_report, usb_report=None):
    directory=Path(directory)
    result=dict(status='UNKNOWN',errors=[],algorithm='fnv1a64',observer_load='one bounded byte-wise digest and independent SPSC publication per positive read',
                collision_limit='64-bit noncryptographic hash: equality is evidence, not proof; collisions and common-mode faults remain possible')
    try:
        names=['read-integrity.tsv','read-integrity-completion.json']
        result['sha256']={n:hashlib.sha256((directory/n).read_bytes()).hexdigest() for n in names}
        completion=json.loads((directory/names[1]).read_text())
        if completion.get('schema')!=1 or completion.get('algorithm')!='fnv1a64' or completion.get('incomplete') is not False or completion.get('lost')!=0:
            raise ValueError('read integrity incomplete/overflow or incompatible format')
        if not pcm_report.get('complete'):
            raise ValueError('PCM evidence incomplete')
        received=(directory/'received.s16le').read_bytes()
        with (directory/'transfers.tsv').open() as f:
            reads=[r for r in csv.DictReader(f,delimiter='\t') if int(r['kind'])==2 and int(r['result'])>0]
        count=source=mismatches=0
        failed_ids=[]
        with (directory/names[0]).open() as f:
            reader=csv.DictReader(f,delimiter='\t')
            if reader.fieldnames!=['id','source','frames','digest']:raise ValueError('invalid read digest columns')
            for row in reader:
                if set(row)!=set(reader.fieldnames) or any(v is None for v in row.values()):raise ValueError('malformed read digest record')
                if count>=len(reads):raise ValueError('extra read digest record')
                original=reads[count];count+=1
                frames=int(row['frames'])
                if int(row['id'])!=count or int(row['source'])!=source or int(original['source'])!=source or frames!=int(original['result']) or not 1<=frames<=256:
                    raise ValueError('missing/duplicated/inconsistent read digest boundary')
                data=received[source*4:(source+frames)*4]
                if len(data)!=frames*4:raise ValueError('read digest range exceeds saved PCM')
                if not re.fullmatch('[0-9a-f]{16}',row['digest']):raise ValueError('invalid digest encoding')
                if digest(data)!=int(row['digest'],16):
                    mismatches+=1
                    if len(failed_ids)<4096:failed_ids.append(count)
                source+=frames
        if count!=len(reads) or count!=completion.get('attempted') or count!=completion.get('written') or source*4!=len(received):
            raise ValueError('incomplete read digest coverage/counts')
        result.update(reads=count,frames=source,digest_mismatches=mismatches,mismatching_read_ids=failed_ids,
                      mismatching_ids_truncated=mismatches>len(failed_ids),coverage_complete=True)
        if mismatches:
            result.update(status='RECORDER_PATH_MISMATCH',interpretation='Immediate read digest differs from saved PCM; investigate subsequent application/recording path, not an ALSA attribution.')
        elif usb_report and usb_report.get('usb_comparison_valid'):
            end=0
            for a in usb_report['alignment']:
                if a.get('status')!='ALIGNED' or a.get('received_range',[None])[0]!=end:
                    raise ValueError('USB alignment incomplete/inconsistent')
                end=a['received_range'][1]
            if end!=source:raise ValueError('USB alignment does not cover all received frames')
            differences=sum(a['differing_frames'] for a in usb_report['alignment'])
            result.update(status='READ_RETURN_DIFFERS_FROM_USB' if differences else 'READ_RETURN_MATCHES_USB',
                          usb_differing_frames=differences,
                          interpretation='All saved read digests match. USB differences were present at the immediate post-read observation, subject to digest collisions/common-mode faults; this does not identify a kernel cause.')
        else:
            result.update(digests_match=True,interpretation='Read digests match saved PCM, but aligned complete USB evidence is unavailable; boundary comparison UNKNOWN.')
    except (OSError,ValueError,KeyError,TypeError) as e:
        result['errors'].append(str(e))
    return result

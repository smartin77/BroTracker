#!/usr/bin/env python3
"""Strict USB_LINUX_MMAPPED pcap replay. HCD observations are not on-wire proof."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import struct
import tempfile

SNAPLEN = 262144
MAX_BYTES = 128 * 1024 * 1024


def records(path):
    with Path(path).open('rb') as f:
        magic = f.read(4)
        endian = {b'\xd4\xc3\xb2\xa1': '<', b'\xa1\xb2\xc3\xd4': '>'}.get(magic)
        if not endian:
            raise ValueError('require classic microsecond pcap, not text/pcapng')
        header = f.read(20)
        if len(header) != 20:
            raise ValueError('unfinished pcap header')
        major, minor, _, _, snap, link = struct.unpack(endian+'HHIIII', header)
        if (major, minor, link) != (2, 4, 220):
            raise ValueError('require DLT_USB_LINUX_MMAPPED (220), including ISO descriptors')
        while True:
            h = f.read(16)
            if not h:
                return
            if len(h) != 16:
                raise ValueError('unfinished pcap record header')
            sec, usec, captured, original = struct.unpack(endian+'IIII', h)
            if captured > min(snap, SNAPLEN) or captured < 64:
                raise ValueError('invalid captured length/snapshot')
            data = f.read(captured)
            if len(data) != captured:
                raise ValueError('unfinished pcap record payload')
            ident, event, transfer, endpoint, device, bus, setup, flag, ts, us, status, length, retained = struct.unpack_from(endian+'QBBBBHccqiiII', data)
            errors, iso_count = struct.unpack_from(endian+'iI', data, 40)
            interval, start, flags, ndesc = struct.unpack_from(endian+'iiII', data, 48)
            if ndesc > 128 or 64 + 16*ndesc > captured:
                raise ValueError('ISO descriptor array truncated/unsupported')
            descriptors = [dict(zip(('status', 'offset', 'length', 'padding'),
                                   struct.unpack_from(endian+'iIII', data, 64+16*i))) for i in range(ndesc)]
            yield dict(id=ident, event=chr(event), transfer=transfer, endpoint=endpoint,
                       device=device, bus=bus, setup_flag=setup[0], data_flag=flag[0], status=status, urb_length=length,
                       data_length=retained, iso_count=iso_count, iso_error_count=errors,
                       interval=interval, start_frame=start, flags=flags,
                       pcap_snapshot_length=snap, pcap_linktype=link,
                       pcap_seconds=sec, pcap_microseconds=usec, usb_seconds=ts, usb_microseconds=us,
                       captured_length=captured, original_length=original, descriptors=descriptors,
                       payload=data[64+16*ndesc:], retained_complete=(captured == 64+retained))


def stats(text):
    result = {}
    for label in ['packets captured', 'packets received by filter', 'packets dropped by kernel', 'packets dropped by interface']:
        m = re.search(r'^\s*(\d+) '+label.replace('packets','packets?')+r'\s*$', text, re.M)
        if m:
            result[label] = int(m.group(1))
    return result


def align(usb, pcm, boundaries):
    """Content anchors per ALSA recovery epoch; never wall-clock resampling."""
    reports = []
    for begin, end in zip(boundaries, boundaries[1:]):
        segment = pcm[begin*4:end*4]
        frames = len(segment)//4
        votes = {}
        anchors = [(f, 64) for f in sorted(set([0, frames//8, frames//3, frames//2, frames*2//3, max(0, frames-128)]))]
        # A long initial silent prefix + onset can distinguish otherwise identical
        # musical loops. It is content evidence, never a timestamp assumption.
        first = next((i for i in range(min(frames, 131000)) if segment[i*4:i*4+4] != bytes(4)), None)
        if first is not None:
            anchors.append((0, first+64))
        used = 0
        ambiguous = False
        prefix_matches = None
        for frame, size in anchors:
            needle = segment[frame*4:(frame+size)*4]
            if len(needle) != size*4 or not any(needle):
                continue
            used += 1
            offset = 0
            matches = set()
            while True:
                offset = usb.find(needle, offset)
                if offset < 0:
                    break
                if offset % 4 == 0:
                    matches.add(offset//4-frame)
                offset += 1
                if len(matches) > 4096:
                    ambiguous = True
                    break
            for candidate in matches:
                votes[candidate] = votes.get(candidate, 0)+1
            if frame == 0 and size > 64:
                prefix_matches = matches
        eligible = [c for c, n in votes.items() if n >= 2 and c >= 0 and (c+frames)*4 <= len(usb)]
        exact = []
        offset = 0
        while len(exact) < 2:
            offset = usb.find(segment, offset)
            if offset < 0:
                break
            if offset % 4 == 0:
                exact.append(offset//4)
            offset += 1
        candidates, basis = eligible, 'unique multi-anchor content offset'
        if len(exact) == 1 and exact[0] in eligible:
            candidates, basis = exact, 'unique whole-epoch byte match'
        elif len(exact) > 1:
            candidates = exact
        elif prefix_matches is not None and len(prefix_matches) == 1:
            selected = list(prefix_matches)[0]
            if selected in eligible:
                candidates, basis = [selected], 'unique initial-silence/onset anchor plus independent content anchors'
        result = dict(received_range=[begin, end], anchors=used, candidate_offsets=eligible,
                      whole_epoch_exact_matches=exact,
                      anchor_limit_hit=ambiguous, status='UNKNOWN')
        if not ambiguous and len(candidates) == 1:
            candidate = candidates[0]
            difference = []
            total = 0
            prior_match = 0
            for i in range(frames):
                if segment[i*4:i*4+4] != usb[(candidate+i)*4:(candidate+i+1)*4]:
                    total += 1
                    absolute = begin+i
                    if absolute >= 2048 and pcm[absolute*4:(absolute+1)*4] == pcm[(absolute-2048)*4:(absolute-2047)*4]:
                        prior_match += 1
                    if len(difference) < 4096:
                        difference.append(begin+i)
            result.update(status='ALIGNED', usb_start_frame=candidate, differing_frames=total,
                          selection_basis=basis,
                          differing_frame_indices=difference, difference_list_truncated=total>len(difference),
                          differing_pcm_frames_equal_2048_prior=prior_match)
        reports.append(result)
    return reports


def analyze(directory, pcm_directory):
    directory, pcm_directory = Path(directory), Path(pcm_directory)
    target = json.loads((directory/'target.json').read_text())
    completion = json.loads((directory/'capture-result.json').read_text())
    counters = stats((directory/'capture.log').read_text(errors='replace'))
    derived = Path(tempfile.mkdtemp(prefix='replay-',dir=str(directory)))
    errors = []
    if not completion.get('clean_stop'):
        errors.append('capture helper did not confirm a clean requested stop')
    if counters.get('packets dropped by kernel') != 0 or counters.get('packets dropped by interface', 0) != 0:
        errors.append('USB capture loss or missing kernel loss statistics')
    if 'packets captured' not in counters or 'packets received by filter' not in counters:
        errors.append('tcpdump footer missing')
    pending, terminal, count, completions = {}, [], 0, 0
    audio = bytearray()
    last_positive = 0
    written = 0
    with (derived/'usb-records.jsonl').open('x', encoding='utf-8') as notes:
        try:
            for record in records(directory/'capture.pcap'):
                count += 1
                payload = record.pop('payload')
                record['record'] = count
                matches = (record['bus'], record['device'], record['endpoint'], record['transfer']) == (target['bus'], target['device'], target['endpoint'], 0)
                if not matches:
                    errors.append('capture filter admitted an unrelated USB transfer')
                elif record['event'] == 'S':
                    if record['id'] in pending:
                        errors.append('duplicate USB submission')
                    pending[record['id']] = count
                elif record['event'] in ['C', 'E']:
                    if record['id'] not in pending:
                        errors.append('completion without retained submission')
                    else:
                        del pending[record['id']]
                    if record['event'] == 'E':
                        errors.append('USB submission error')
                    else:
                        completions += 1
                        desc = record['descriptors']
                        lengths = sum(d['length'] for d in desc)
                        cancelled_tail = record['status'] in [-2, -104, -108] and lengths == 0
                        if cancelled_tail:
                            terminal.append(count)
                        else:
                            if record['status'] or record['iso_error_count']:
                                errors.append('USB URB completion error')
                            if not record['retained_complete'] or record['iso_count'] != len(desc) or not desc:
                                errors.append('incomplete ISO metadata or snaplen truncation')
                            if record['captured_length'] < record['original_length']:
                                errors.append('pcap original length exceeds captured length')
                            if record['data_flag'] != 0 and lengths:
                                errors.append('ISO payload not retained')
                            if lengths != record['urb_length']:
                                errors.append('ISO descriptor lengths differ from URB actual length')
                            previous_end = 0
                            for d in desc:
                                d['reconstructed_byte_offset'] = len(audio)
                                end = d['offset']+d['length']
                                if d['status'] or d['length'] % 4 or d['offset'] < previous_end or end > len(payload):
                                    errors.append('ISO packet error, overlap, partial frame or missing sparse payload')
                                    d['reconstructed'] = False
                                else:
                                    audio.extend(payload[d['offset']:end])
                                    d['reconstructed'] = True
                                    if d['length']:
                                        last_positive = count
                                previous_end = end
                else:
                    errors.append('unknown USB event')
                line = json.dumps(record)+'\n'
                written += len(line)
                if written > MAX_BYTES:
                    raise ValueError('derived USB metadata storage limit reached; original pcap retained')
                notes.write(line)
        except (ValueError, OSError) as e:
            errors.append(str(e))
    if pending:
        errors.append('missing USB completions at capture end')
    if any(n < last_positive for n in terminal):
        errors.append('cancelled URBs inside audio stream')
    if not completions or not audio:
        errors.append('no reconstructed ISO audio')
    if counters.get('packets captured') != count:
        errors.append('tcpdump packet count does not match saved pcap')
    with (derived/'usb-audio.s16le').open('xb') as f:
        f.write(audio)  # Partial on failure; never fabricate holes/silence.
    reconstruction_complete = not errors
    alignment = []
    if not errors:
        pcm = (pcm_directory/'received.s16le').read_bytes()
        boundaries = [0]
        with (pcm_directory/'transfers.tsv').open() as f:
            for row in csv.DictReader(f, delimiter='\t'):
                if int(row['kind']) == 6 and int(row['step']) == 0:
                    boundaries.append(int(row['captured']))
        boundaries = sorted(set(boundaries+[len(pcm)//4]))
        alignment = align(bytes(audio), pcm, boundaries)
        if not alignment or any(r['status'] != 'ALIGNED' for r in alignment):
            errors.append('content alignment ambiguous/unavailable; no comparison verdict')
    return dict(schema=1, usb_comparison_valid=not errors, errors=sorted(set(errors)), target=target,
                derived_directory=str(derived),
                records=count, completions=completions, stats=counters,
                reconstruction_complete=reconstruction_complete, reconstructed_frames=len(audio)//4, alignment=alignment,
                pcap_sha256=hashlib.sha256((directory/'capture.pcap').read_bytes()).hexdigest(),
                audio_sha256=hashlib.sha256(audio).hexdigest(),
                interpretation='usbmon observes the driver/HCD boundary, not guaranteed bus truth. '
                'Differing bytes do not by themselves identify a Teensy defect. Partial reconstruction is explicitly invalid.')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    p.add_argument('pcm_directory', type=Path)
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    try:
        result = analyze(a.directory, a.pcm_directory)
        from analyze_clicks import analyze as analyze_pcm
        if not analyze_pcm(a.pcm_directory)['complete']:
            result['usb_comparison_valid']=False
            result['errors'].append('PCM evidence incomplete; comparison invalid')
    except (OSError, ValueError, KeyError) as e:
        result = dict(usb_comparison_valid=False, errors=[str(e)])
    with a.output.open('x', encoding='utf-8') as f:
        json.dump(result, f, indent=2); f.write('\n')
    raise SystemExit(0 if result['usb_comparison_valid'] else 1)

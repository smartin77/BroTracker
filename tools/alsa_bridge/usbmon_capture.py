#!/usr/bin/env python3
"""Narrow privileged usbmon helper; UI stays unprivileged, output uses inherited FDs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import select
import shutil
import signal
import stat
import struct
import subprocess
import sys
import tempfile
import time

from analyze_usbmon import MAX_BYTES, SNAPLEN, records

DURATION = 240
serving_capture = False


def discover(port, sysfs=Path('/sys/class/tty')):
    if not re.fullmatch(r'/dev/ttyACM\d+', str(port)):
        raise ValueError('require discovered Teensy CDC port')
    node = (sysfs/Path(port).name/'device').resolve()
    usb = next((p for p in [node]+list(node.parents) if (p/'idVendor').exists()), None)
    if usb is None or (usb/'idVendor').read_text().strip() != '16c0' or (usb/'idProduct').read_text().strip() != '048a':
        raise ValueError('CDC ancestry is not Teensy 16c0:048a')
    raw = (usb/'descriptors').read_bytes()
    pos, interface, fmt, candidates = 0, None, None, []
    while pos < len(raw):
        length = raw[pos]
        if length < 2 or pos+length > len(raw):
            raise ValueError('malformed USB descriptors')
        d = raw[pos:pos+length]; pos += length
        if d[1] == 4 and length >= 9:
            interface = (d[2], d[3], d[5], d[6]); fmt = None
        elif d[1] == 0x24 and length >= 11 and d[2:4] == b'\x02\x01':
            rates = [int.from_bytes(d[8+i*3:11+i*3], 'little') for i in range(d[7])]
            fmt = dict(channels=d[4], subframe_bytes=d[5], bits=d[6], rates=rates)
        elif d[1] == 5 and length >= 7 and interface and interface[2:] == (1, 2) and interface[1] > 0:
            if d[2] & 0x80 and d[3] & 3 == 1 and d[3] >> 4 & 3 == 0:
                if fmt != dict(channels=2, subframe_bytes=2, bits=16, rates=[44100]):
                    raise ValueError('audio IN format is not the supported stereo PCM16/44100 profile')
                candidates.append(dict(endpoint=d[2], interface=interface[0], alternate=interface[1],
                                       max_packet_bytes=int.from_bytes(d[4:6], 'little') & 0x7ff, format=fmt))
    if len(candidates) != 1:
        raise ValueError('need exactly one supported audio-data IN endpoint')
    result = candidates[0]
    result.update(bus=int((usb/'busnum').read_text()), device=int((usb/'devnum').read_text()),
                  sysfs_path=str(usb), port=str(port), descriptor_sha256=hashlib.sha256(raw).hexdigest())
    if not 1 <= result['bus'] <= 65535 or not 1 <= result['device'] <= 127:
        raise ValueError('invalid USB bus/device; all-bus usbmon0 is forbidden')
    return result


def packet_filter(target):
    return 'link[9] = 0 and link[10] = %d and link[11] = %d' % (target['endpoint'], target['device'])


def fixture():
    """Sparse ISO offsets, plus an unrelated device, for installed offline filter tests."""
    chunks = [struct.pack('<IHHIIII', 0xa1b2c3d4, 2, 4, 0, 0, SNAPLEN, 220)]
    for device in [7, 8]:
        for event in b'SC':
            payload = b'abcd'+bytes(36)+b'WXYZ' if event == ord('C') else b''
            desc = struct.pack('<iIIIiIII', 0, 0, 4, 0, 0, 40, 4, 0)
            header = bytearray(64)
            struct.pack_into('<QBBBBHccqiiII', header, 0, device, event, 0, 0x83, device, 1,
                             b'-', b'\0' if payload else b'<', 1, 0, 0, 8, len(desc)+len(payload))
            struct.pack_into('<iIiiII', header, 40, 0, 2, 1, 100, 0, 2)
            body = bytes(header)+desc+payload
            original = 64+len(desc)+(8 if payload else 0)
            chunks.append(struct.pack('<IIII', 1, 0, len(body), original)+body)
    return b''.join(chunks)


def checked(command):
    if serving_capture and select.select([sys.stdin.buffer], [], [], 0)[0]:
        raise ValueError('owner stopped during USB setup')
    r = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=8, env=dict(os.environ, LC_ALL='C'))
    if r.returncode:
        raise ValueError('command failed: '+repr(command)+': '+r.stderr.decode(errors='replace')[:500])
    return (r.stdout+r.stderr).decode(errors='replace')


def semantics():
    tcpdump = shutil.which('tcpdump')
    if not tcpdump:
        raise ValueError('tcpdump not installed')
    version = checked([tcpdump, '--version'])
    if not list(Path('/dev').glob('usbmon[1-9]*')):
        checked(['modprobe', 'usbmon'])
    interfaces = checked([tcpdump, '-D'])
    available = re.findall(r'\b(usbmon[1-9][0-9]*)\b', interfaces)
    if not available:
        raise ValueError('no single-bus usbmon interface; load usbmon before preparation')
    types = checked([tcpdump, '-i', available[0], '-L'])  # List, never collect live packets.
    if 'USB_LINUX_MMAPPED' not in types:
        raise ValueError('installed libpcap lacks 64-byte mmap USB capture')
    with tempfile.TemporaryDirectory(prefix='brotracker-usbmon-semantics-') as tmp:
        original = Path(tmp)/'fixture.pcap'; selected = Path(tmp)/'selected.pcap'
        original.write_bytes(fixture())
        checked([tcpdump, '-Z', 'root', '-r', str(original), '-w', str(selected), packet_filter(dict(endpoint=0x83, device=7))])
        saved = list(records(selected))
        if len(saved) != 2 or [r['event'] for r in saved] != ['S', 'C']:
            raise ValueError('installed libpcap filter lost submission/completion metadata')
        c = saved[1]
        if not c['retained_complete'] or c['iso_count'] != 2 or c['payload'] != b'abcd'+bytes(36)+b'WXYZ' or c['descriptors'][1]['offset'] != 40:
            raise ValueError('installed offline pcap path does not preserve sparse ISO payload')
    dependencies = checked(['ldd', tcpdump])
    libraries = re.findall(r'libpcap[^\s]*\s+=>\s+(\S+)', dependencies)
    return dict(tcpdump=tcpdump, version=version, dependencies=dependencies,
                tcpdump_sha256=hashlib.sha256(Path(tcpdump).read_bytes()).hexdigest(),
                libpcap_sha256={p: hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in libraries},
                interface_types=types, sparse_fixture_and_filter_verified=True,
                live_payload_completeness='UNVERIFIED until every captured ISO descriptor is checked offline')


def emit(kind, value):
    print(kind+' '+json.dumps(value), file=sys.stderr, flush=True)


def sudo_command():
    return ['sudo', '-n', '-k', '--', sys.executable, str(Path(__file__).resolve())]


def permission_check():
    r = subprocess.run(sudo_command(), input=b'{"action":"check"}\n', stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, timeout=60)
    if r.returncode:
        raise ValueError('USB option needs password-free noninteractive sudo for the helper; '
                         'no sudoers changes are made: '+r.stderr.decode(errors='replace')[:600])
    return json.loads(r.stdout)


class Capture:
    """Unprivileged owner. Helper watches pipe EOF and enforces hard limits itself."""
    def __init__(self, directory, port):
        self.directory = Path(directory); self.directory.mkdir()
        self.port = port; self.child = None; self.files = []; self.error = None; self.result = None

    def start(self, check_cancel=lambda: None):
        try:
            target = discover(self.port)
            (self.directory/'target.json').write_text(json.dumps(target, indent=2)+'\n')
            out = (self.directory/'capture.pcap').open('xb')
            self.files.append(out)
            log = (self.directory/'capture.log').open('xb')
            self.files.append(log)
            self.child = subprocess.Popen(sudo_command(), stdin=subprocess.PIPE, stdout=out, stderr=log)
            self.child.stdin.write((json.dumps(dict(action='capture', port=self.port))+'\n').encode())
            self.child.stdin.flush()
            deadline = time.monotonic()+20
            while time.monotonic() < deadline:
                check_cancel()
                text = (self.directory/'capture.log').read_text(errors='replace')
                if 'USBMON_READY ' in text:
                    return True
                if self.child.poll() is not None:
                    raise ValueError('USB helper exited before readiness; see capture.log')
                time.sleep(.1)
            raise ValueError('USB capture setup timeout')
        except (OSError, ValueError, subprocess.SubprocessError) as e:
            self.error = str(e)
            return False

    def stop(self):
        if self.result is not None:
            return self.result
        result = dict(clean_stop=False, reason=self.error or 'helper result unavailable')
        if self.child:
            try:
                self.child.stdin.close()  # EOF stops only this owned privileged helper.
            except OSError:
                pass
            try:
                self.child.wait(timeout=18)
            except (OSError, subprocess.TimeoutExpired) as e:
                self.error = 'USB helper reap unconfirmed: '+str(e)
            text = (self.directory/'capture.log').read_text(errors='replace')
            for line in text.splitlines():
                if line.startswith('USBMON_RESULT '):
                    result = json.loads(line[len('USBMON_RESULT '):])
            result['helper_returncode'] = self.child.returncode
            if self.error or self.child.returncode != 0:
                result.update(clean_stop=False, owner_error=self.error)
        for file in self.files:
            file.close()
        self.files = []
        with (self.directory/'capture-result.json').open('x') as f:
            json.dump(result, f, indent=2); f.write('\n')
        self.result = result
        return result


def capture(port):
    output = os.fstat(sys.stdout.fileno())
    if not stat.S_ISREG(output.st_mode) or output.st_size:
        raise ValueError('capture stdout must be a new empty regular file owned by the caller')
    identity = semantics()
    target = discover(port)
    command = [identity['tcpdump'], '-i', 'usbmon'+str(target['bus']), '-y', 'USB_LINUX_MMAPPED',
               '-n', '-U', '-s', str(SNAPLEN), '-B', '1024', '-w', '-', packet_filter(target)]
    emit('USBMON_IDENTITY', dict(target=target, installed=identity, command=command,
                                 duration_limit_seconds=DURATION, file_limit_bytes=MAX_BYTES))
    def limit():
        resource.setrlimit(resource.RLIMIT_FSIZE, (MAX_BYTES, MAX_BYTES))
    child = None
    reason = 'helper failure'
    stopping = False
    began = time.monotonic()
    try:
        child = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=sys.stdout.buffer, stderr=subprocess.PIPE,
                                 env=dict(os.environ, LC_ALL='C'), preexec_fn=limit)
        pending = bytearray()
        while child.poll() is None:
            ready, _, _ = select.select([sys.stdin.buffer, child.stderr], [], [], .1)
            if sys.stdin.buffer in ready:
                # The only owner controls this inherited pipe. EOF is also STOP.
                os.read(sys.stdin.fileno(), 64)
                reason = 'owner requested stop'; stopping = True; break
            if time.monotonic()-began >= DURATION:
                reason = 'duration limit'; break
            if child.stderr in ready:
                data = os.read(child.stderr.fileno(), 4096)
                sys.stderr.buffer.write(data); sys.stderr.buffer.flush()
                pending.extend(data)
                if len(pending) > 32768:
                    raise ValueError('oversized tcpdump startup log')
                if b'listening on ' in pending and b'USB_LINUX_MMAPPED' in pending:
                    emit('USBMON_READY', target); pending.clear()
        if child.poll() is None:
            child.send_signal(signal.SIGINT)
        try:
            _, tail = child.communicate(timeout=4)
        except subprocess.TimeoutExpired:
            child.kill(); _, tail = child.communicate(timeout=2); reason = 'forced tcpdump kill'
        sys.stderr.buffer.write(tail); sys.stderr.buffer.flush()
        result = dict(clean_stop=stopping and child.returncode == 0, reason=reason,
                      tcpdump_returncode=child.returncode, elapsed_seconds=time.monotonic()-began)
        emit('USBMON_RESULT', result)
    finally:
        if child is not None and child.poll() is None:
            child.kill(); child.wait(timeout=2)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.parse_args()
    if os.geteuid() != 0:
        p.error('only this helper needs noninteractive sudo; do not run the UI as root')
    # Identical sudo command at preparation/runtime: no reliance on a cached
    # password ticket or different command-line sudoers matches. No UI as root.
    request = bytearray()
    deadline = time.monotonic()+5
    while not request.endswith(b'\n') and len(request) <= 256:
        if time.monotonic() >= deadline:
            raise ValueError('owner command timeout')
        if select.select([sys.stdin.buffer], [], [], .1)[0]:
            b = os.read(sys.stdin.fileno(), 1)
            if not b:
                raise ValueError('owner closed command pipe')
            request.extend(b)
    action = json.loads(request)
    if action['action'] == 'check':
        print(json.dumps(semantics()))
    elif action['action'] == 'capture':
        global serving_capture
        serving_capture = True
        capture(action['port'])
    else:
        raise ValueError('unsupported helper command')


if __name__ == '__main__':
    try:
        main()
    except Exception as e:
        emit('USBMON_FAILURE', dict(reason=str(e)))
        raise SystemExit(1)

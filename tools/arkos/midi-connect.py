#!/usr/bin/env python3
"""Development ALSA route keeper. No CDC, MIDI messages or device resets."""
import argparse
import fcntl
import json
import logging
from logging.handlers import RotatingFileHandler
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import stat
import select
import struct
import tempfile
import shutil
import ctypes

SOURCE = 'BroTracker USB audio MIDI 1'
# The only supported external destination; names select it, USB identity verifies it.
H4MIDI_PROFILE = dict(port_name='H4MIDI-WC MIDI 1', client_name='H4MIDI-WC',
                     usb_identity=('7104', '2100'))
DESTINATION = H4MIDI_PROFILE['port_name']
KEEPER_NAME = 'btx-midi-keep'


def set_keeper_name(name=KEEPER_NAME):
    """Name the actual Linux keeper thread, not merely its argv or parent."""
    try:
        encoded = name.encode('ascii')
    except UnicodeEncodeError as error:
        raise RuntimeError('Keeper process name must be ASCII') from error
    if not encoded or len(encoded) > 15 or b'\0' in encoded:
        raise RuntimeError('Keeper process name must be 1-15 ASCII bytes without NUL')
    try:
        libc = ctypes.CDLL(None, use_errno=True)
        prctl = libc.prctl
        prctl.restype = ctypes.c_int
        prctl.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong]
        desired = ctypes.create_string_buffer(encoded)
        actual = ctypes.create_string_buffer(16)
        for operation, buffer in ((15, desired), (16, actual)):  # PR_SET_NAME / PR_GET_NAME
            if prctl(operation, ctypes.byref(buffer), 0, 0, 0) != 0:
                raise OSError(ctypes.get_errno(), 'prctl process-name operation failed')
        if actual.value != encoded:
            raise RuntimeError('Kernel process-name verification mismatch')
    except (OSError, AttributeError) as error:
        raise RuntimeError('Keeper process-name setup failed: ' + str(error)) from error


def keeper_identity_ready(value):
    if isinstance(value, str) or value.get('process_name') != KEEPER_NAME:
        return False
    try:
        return Path('/proc/{}/comm'.format(value['pid'])).read_text().strip() == KEEPER_NAME
    except (OSError, KeyError):
        return False


def runtime_directory():
    base = Path(os.environ.get('TMPDIR', '/tmp'))
    if not base.is_absolute() or not base.is_dir():
        base = Path('/tmp')
    return base / ('brotracker-' + str(os.getuid()))


def prepare_directory(directory):
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    info = directory.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
        raise RuntimeError('Unsafe MIDI runtime directory: ' + str(directory))


def migrate_legacy(directory):
    """Preserve recognized old runtime files; never touch tunes/config/project files."""
    legacy = Path.home() / 'BroTracker'
    locks = []
    try:
        for name in ('midi-connect.lock', 'btx-midi-health.lock'):
            path = legacy / name
            if path.exists():
                if path.is_symlink():
                    raise RuntimeError('Refusing legacy runtime symlink: ' + str(path))
                lock = path.open('r')
                locks.append(lock)
                try:
                    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                except BlockingIOError:
                    raise RuntimeError('Legacy keeper/BTX still active; close it before runtime-path upgrade')
        names = ['midi-connect.log', 'midi-connect-launch.log', 'midi-connect.lock',
                 'midi-connect-status.json', 'midi-connect-status.btx', 'midi-connect-status.next',
                 'midi-connect-status.btx.next', 'btx-midi-health.state', 'btx-midi-health.lock']
        names += ['midi-connect.log.' + str(n) for n in range(1, 4)]
        if legacy.is_dir():
            names += [path.name for path in legacy.glob('btx-midi-health.next.*')
                      if path.name.rsplit('.', 1)[-1].isdigit()]
        # Check all candidates before moving any of them.
        paths = [legacy / name for name in names if (legacy / name).exists() or (legacy / name).is_symlink()]
        for path in paths:
            info = path.lstat()
            if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid():
                raise RuntimeError('Unsafe legacy runtime file: ' + str(path))
        if paths:
            archive = Path(tempfile.mkdtemp(prefix='legacy-', dir=str(directory)))
            for path in paths:
                shutil.move(str(path), str(archive / path.name))
            print('Preserved legacy runtime files: ' + str(archive))
    finally:
        for lock in locks:
            lock.close()


def ports(text):
    result = {}
    client = None
    for line in text.splitlines():
        match = re.match(r"^client (\d+): '.*' \[", line)
        if match:
            client = int(match[1])
        else:
            match = re.match(r"^\s+(\d+) '([^']*)'\s*$", line)
            if match and client is not None:
                address = '{}:{}'.format(client, int(match[1]))
                if address in result:
                    raise RuntimeError('Duplicate ALSA port address: ' + address)
                result[address] = match[2].rstrip()
    return result


def discover(text, name):
    matches = [address for address, value in ports(text).items() if value == name]
    if not matches:
        raise RuntimeError('Missing port: {}. Connect and power the device, then retry.'.format(name))
    if len(matches) != 1:
        raise RuntimeError('Ambiguous port: {} ({}) â€” no route changed.'.format(name, ', '.join(matches)))
    return matches[0]


def connected(text, source, destination):
    client = None
    current = None
    for line in text.splitlines():
        match = re.match(r"^client (\d+): '.*' \[", line)
        if match:
            client = int(match[1])
            current = None
            continue
        match = re.match(r"^\s+(\d+) '([^']*)'\s*$", line)
        if match and client is not None:
            current = '{}:{}'.format(client, int(match[1]))
        elif current == source and line.strip().startswith('Connecting To:'):
            if destination in re.findall(r'\b\d+:\d+\b', line.split(':', 1)[1]):
                return True
    return False


def aconnect(*arguments):
    try:
        result = subprocess.run(['aconnect', *arguments], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, universal_newlines=True,
                                env=dict(os.environ, LC_ALL='C'), timeout=5)
    except FileNotFoundError:
        raise RuntimeError('Missing aconnect. Install alsa-utils during development setup.')
    except subprocess.TimeoutExpired:
        raise RuntimeError('aconnect timed out; inspect the devices and retry.')
    if result.returncode:
        detail = (result.stderr or result.stdout).strip()
        raise RuntimeError('aconnect {} failed ({}): {}'.format(' '.join(arguments), result.returncode, detail))
    return result.stdout


def connect(run=aconnect):
    source = discover(run('-i'), SOURCE)
    destination = discover(run('-o'), DESTINATION)
    listing = run('-l')
    # Recheck identity after discovery; never trust old client numbers.
    if discover(listing, SOURCE) != source or discover(listing, DESTINATION) != destination:
        raise RuntimeError('ALSA ports changed during discovery; no connection attempted. Retry.')
    label = '{} ({}) -> {} ({})'.format(SOURCE, source, DESTINATION, destination)
    if connected(listing, source, destination):
        print('OK: already connected\n' + label)
        return
    run(source, destination)
    listing = run('-l')
    if (discover(listing, SOURCE) != source or discover(listing, DESTINATION) != destination
            or not connected(listing, source, destination)):
        raise RuntimeError('Connection could not be verified; inspect ALSA ports/routes before retrying.')
    print('OK: connected\n' + label)


def raw_listing():
    result = subprocess.run(['amidi', '-l'], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            universal_newlines=True, env=dict(os.environ, LC_ALL='C'), timeout=3)
    if result.returncode:
        raise RuntimeError('raw-MIDI identity listing unavailable')
    return result.stdout


def raw_card(name, direction, text, sys=Path('/sys'), dev=Path('/dev/snd')):
    """Exact ALSA raw-port identity, canonicalized through control-device sysfs.

    Numeric client/card equality and USB VID/PID are never selection criteria.
    Control aliases are collapsed only when their actual character-device ancestry
    proves that they refer to the same raw-MIDI subdevice.
    """
    candidates = set()
    for line in text.splitlines():
        match = re.fullmatch(r'\s*([IO]+)\s+hw:(\d+),(\d+),(\d+)\s+(.+?)\s*', line)
        if not match or match[5] != name or direction not in match[1]:
            continue
        info = os.stat(str(dev / ('controlC' + match[2])))
        if not stat.S_ISCHR(info.st_mode):
            return None
        control = (sys / 'dev/char' / '{}:{}'.format(os.major(info.st_rdev), os.minor(info.st_rdev))).resolve(strict=True)
        canonical = re.fullmatch(r'controlC(\d+)', control.name)
        if not canonical:
            return None
        card = (sys / 'class/sound' / ('card' + canonical[1])).resolve(strict=True)
        midi = (sys / 'class/sound' / ('midiC{}D{}'.format(canonical[1], match[3]))).resolve(strict=True)
        if control.parent != card or midi.parent != card:
            return None
        candidates.add((canonical[1], match[3], match[4]))
    return next(iter(candidates))[0] if len(candidates) == 1 else None


def identity(listing, address, read_raw=raw_listing, sys=Path('/sys'), dev=Path('/dev/snd')):
    """Card/USB incarnation supplements the supported name; never selects a port."""
    client = address.split(':')[0]
    match = re.search(r"^client " + client + r": (.*)$", listing, re.M)
    header = match.group(1) if match else ''
    result = {'address': address, 'client': header[:200]}
    card = re.search(r'\bcard=(\d+)\b', header)
    try:
        number = card.group(1) if card else None
        if card is None:
            name = ports(listing).get(address)
            if name not in (SOURCE, DESTINATION):
                return result
            # Same supported sequencer name must itself be unambiguous.
            if discover(listing, name) != address:
                return result
            number = raw_card(name, 'I' if name == SOURCE else 'O', read_raw(), sys, dev)
        if number is None:
            return result
        node = (sys / 'class/sound' / ('card' + number) / 'device').resolve(strict=True)
        for parent in (node, *node.parents):
            try:
                usb = {k: (parent / k).read_text().strip()[:100]
                       for k in ('idVendor', 'idProduct', 'busnum', 'devnum')}
                usb.update(path=parent.name, inode=parent.stat().st_ino)
                if not re.fullmatch(r'\d+-\d+(?:\.\d+)*', parent.name):
                    return result
                if (sys / 'class/sound' / ('card' + number) / 'device').resolve(strict=True) != node:
                    return result
                if card is None and raw_card(name, 'I' if name == SOURCE else 'O', read_raw(), sys, dev) != number:
                    return result
                if parent.stat().st_ino != usb['inode'] or (parent/'devnum').read_text().strip() != usb['devnum']:
                    return result
                result['usb'] = usb
                break
            except OSError:
                pass
    except (OSError, RuntimeError, subprocess.TimeoutExpired):
        pass  # Missing/ambiguous ancestry is reduced visibility, never a guess.
    return result


def observe_device(directional, listing, port_name, client_name, identify):
    """Read-only independent supported-port observation; USB mapping supplements ALSA."""
    try:
        address = discover(directional, port_name)
        try:
            combined_address = discover(listing, port_name)
        except RuntimeError as error:
            raise RuntimeError('ALSA combined observation could not verify directional port: ' + str(error))
        if combined_address != address:
            raise RuntimeError('ALSA identity changed during device observation')
        if not re.search(r"^client " + address.split(':')[0] + r": '" + re.escape(client_name) +
                         r"' \[type=kernel(?:,|\])", listing, re.M):
            raise RuntimeError('Supported port is not on its expected hardware ALSA client: ' + address)
        observed = identify(listing, address)
        expected = ('16c0', '048a') if port_name == SOURCE else H4MIDI_PROFILE['usb_identity']
        usb = observed.get('usb')
        if usb and (usb.get('idVendor'), usb.get('idProduct')) != expected:
            raise RuntimeError('USB identity does not match supported device profile: ' + address)
        return dict(state='found', identity=observed, detail='Supported hardware ALSA port verified: ' + address +
                    ('; USB incarnation mapped' if usb else '; USB mapping unavailable; reduced visibility'))
    except (OSError, RuntimeError) as error:
        detail = str(error)
        state = 'ambiguous' if detail.startswith(('Ambiguous', 'Duplicate')) else 'missing' if detail.startswith('Missing port:') else 'unknown'
        return dict(state=state, detail=detail)


class KernelErrors:
    """Read-only ring observations. Baseline old errors; latch new errors per incarnation."""
    def __init__(self, read=None):
        self.read = read or self.read_ring
        self.watermark = None
        self.key = None
        self.error = ''
        self.latched = {}
        self.lifecycle = {}
        self.pending = []
        self.history_lost = False

    @staticmethod
    def read_ring():
        r = subprocess.run(['dmesg', '--color=never'], stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, universal_newlines=True,
                           env=dict(os.environ, LC_ALL='C'), timeout=3)
        if r.returncode:
            raise RuntimeError('kernel log unreadable')
        return r.stdout

    def poll(self, identities):
        key = json.dumps([item.get('usb') for item in identities], sort_keys=True)
        changed = key != self.key
        if changed:
            self.key, self.error = key, ''
        paths = set()
        for item in identities:
            path = item.get('usb', {}).get('path')
            if path:
                paths.add(path)
                while '.' in path:
                    path = path.rsplit('.', 1)[0]
                    paths.add(path)
        try:
            text = self.read()
        except (OSError, RuntimeError, subprocess.TimeoutExpired):
            return 'reduced: kernel USB errors cannot be observed', self.error
        lines = []
        for line in text.splitlines():
            match = re.match(r'^\[\s*(\d+\.\d+)\]\s+(.*)', line)
            if match:
                lines.append((float(match.group(1)), match.group(2)))
        previous = self.watermark
        if not lines:
            return 'reduced: kernel timestamps unavailable; USB errors cannot be attributed', self.error
        if lines:
            self.watermark = lines[-1][0]
        if previous is not None and self.watermark < previous:
            self.lifecycle.clear()
            self.pending.clear()
            self.latched.clear()
            self.history_lost = True
        # Preserve newly observed errors independently of ALSA discovery. Their
        # lifecycle token can later be joined to a verified USB incarnation.
        for stamp, line in lines:
            event = re.match(r'^usb (\d+-\d+(?:\.\d+)*): (?:new .* USB device number (\d+) using |USB disconnect, device number (\d+))', line)
            if event:
                path = event[1]
                token = (stamp, 'new' if event[2] else 'disconnect', event[2] or event[3])
                old = self.lifecycle.get(path)
                if old is None or stamp > old[0]:
                    self.lifecycle[path] = token
                elif stamp == old[0] and token != old:
                    self.lifecycle[path] = (stamp, 'ambiguous', None)
            error = re.match(r'^usb (\d+-\d+(?:\.\d+)*): .*?(?:urb status -\d+|error -\d+|over-current|disabled by hub)', line, re.I)
            if previous is not None and stamp > previous and error:
                token = self.lifecycle.get(error[1])
                proof = token if token and token[1] == 'new' else None
                self.pending.append((stamp, error[1], line[:500], proof))
        if len(self.lifecycle) > 128:
            self.history_lost = True
            self.lifecycle = dict(sorted(self.lifecycle.items(), key=lambda item: item[1][0])[-128:])
        if len(self.pending) > 128:
            self.history_lost = True
            self.pending = self.pending[-128:]
        # A disconnect/replacement can race the ALSA/sysfs snapshot. Do not label
        # its errors with the stale incarnation observed just before that event.
        stale_paths = set()
        for item in identities:
            usb = item.get('usb', {})
            path = usb.get('path')
            if not path:
                continue
            lifecycle = self.lifecycle.get(path)
            if lifecycle and (lifecycle[1] != 'new' or lifecycle[2] != usb.get('devnum')):
                stale_paths.add(path)
        if stale_paths:
            paths = {p for p in paths if p not in stale_paths}
            self.error = ''
        births = []
        for item in identities:
            usb = item.get('usb', {})
            token = self.lifecycle.get(usb.get('path'))
            if token and token[1] == 'new' and token[2] == usb.get('devnum'):
                births.append(token[0])
        incarnation = json.dumps([key, [self.lifecycle.get(item.get('usb', {}).get('path')) for item in identities]])
        self.error = '' if stale_paths else self.latched.get(incarnation, '')
        unresolved = False
        for stamp, path, line, proof in self.pending:
            related = [item['usb'] for item in identities if 'usb' in item and
                       (item['usb']['path'] == path or item['usb']['path'].startswith(path + '.'))]
            if not related:
                continue
            current = self.lifecycle.get(path)
            if current and current[1] == 'new' and current[0] > stamp:
                continue  # The error precedes this replacement's enumeration.
            exact = [usb for usb in related if usb['path'] == path and usb['devnum'] == (current[2] if current else None)]
            proven = current and current[1] == 'new' and (current == proof or
                     (current[0] < stamp and (proof is None or proof[0] < current[0])))
            if proven and exact and path not in stale_paths:
                self.error = line[:500]
                self.latched[incarnation] = self.error
                if len(self.latched) > 16:
                    del self.latched[next(iter(self.latched))]
            else:
                unresolved = True
        visibility = 'kernel log observed; delivery unverified'
        if not paths or any('usb' not in item for item in identities):
            visibility = 'reduced: ALSA identity lacks USB mapping; error attribution incomplete'
        elif stale_paths:
            visibility = 'reduced: USB incarnation changed during observation; awaiting rediscovery'
        elif unresolved:
            visibility = 'reduced: observed USB errors awaiting proven incarnation attribution'
        elif len(births) != len(identities):
            visibility = 'reduced: enumeration history missing; observing new USB errors only'
        if previous is not None and lines and self.watermark < previous:
            visibility = 'reduced: kernel log discontinuity; USB history incomplete'
        if self.history_lost:
            visibility = 'reduced: USB observation history incomplete; attribution may be unavailable'
        return visibility, self.error


class Keeper:
    MAX_ATTEMPTS = 5
    def __init__(self, run=aconnect, identify=identity, errors=None, emit=None):
        self.run, self.identify = run, identify
        self.errors = errors or KernelErrors()
        self.emit = emit or (lambda value: None)
        self.key, self.attempts, self.next_try = None, 0, 0
        self.was_connected = False
        self.last = None
        self.devices = {}

    def report(self, state, detail, identities=(), route=False, visibility='unknown', usb_error=''):
        value = dict(state=state, detail=str(detail)[:1000], identities=list(identities),
                     subscription=route, delivery='unverified', visibility=visibility,
                     attempts=self.attempts, usb_error=usb_error, devices=dict(self.devices))
        if value != self.last:
            self.emit(value)
            self.last = value
        return value

    def step(self, now):
        self.devices = {label: dict(state='unknown', detail='ALSA observation unavailable for this sample')
                        for label in ('Teensy', 'H4MIDI')}
        ids = []
        visibility = 'reduced: USB/kernel observation unavailable for this sample'
        try:
            incoming, outgoing, listing = self.run('-i'), self.run('-o'), self.run('-l')
            for label, directional, port_name, client_name in (
                    ('Teensy', incoming, SOURCE, 'BroTracker USB audio'),
                    ('H4MIDI', outgoing, DESTINATION, H4MIDI_PROFILE['client_name'])):
                self.devices[label] = observe_device(directional, listing, port_name, client_name, self.identify)
            ids = [device['identity'] for device in self.devices.values() if device['state'] == 'found']
            unavailable = [device for device in self.devices.values() if device['state'] != 'found']
            if unavailable:
                self.key, self.attempts, self.was_connected = None, 0, False
                visibility, usb_error = self.errors.poll(ids)
                state = 'ambiguous' if any(d['state'] == 'ambiguous' for d in unavailable) else 'unavailable' if any(
                    d['state'] == 'unknown' for d in unavailable) else 'missing'
                return self.report(state, '; '.join(d['detail'] for d in unavailable), ids,
                                   visibility=visibility, usb_error=usb_error)
            source, destination = [device['identity']['address'] for device in self.devices.values()]
            key = json.dumps(ids, sort_keys=True)
            if key != self.key:
                self.key, self.attempts, self.next_try, self.was_connected = key, 0, now, False
            visibility, usb_error = self.errors.poll(ids)
            present = connected(listing, source, destination)
            if present:
                # Even an existing route must still belong to the discovered names.
                if discover(listing, SOURCE) != source or discover(listing, DESTINATION) != destination:
                    raise RuntimeError('ALSA identities changed during observation')
                self.was_connected = True
                return self.report('unavailable' if usb_error else 'connected',
                    ('USB error: ' + usb_error + '; subscription remains present; not repaired') if usb_error else
                    'ALSA subscription verified; MIDI delivery is not verified', ids, True, visibility, usb_error)
            if self.was_connected:
                self.attempts, self.next_try, self.was_connected = 0, now, False
            if self.attempts >= self.MAX_ATTEMPTS or now < self.next_try:
                return self.report('unavailable', 'Route absent; retry budget exhausted; waiting for device/port change' if
                    self.attempts >= self.MAX_ATTEMPTS else 'Route absent; waiting for capped retry', ids, False, visibility)
            self.attempts += 1
            self.next_try = now + min(2 ** self.attempts, 16)
            self.report('connecting', 'Attempt {} of {}'.format(self.attempts, self.MAX_ATTEMPTS), ids, False, visibility)
            # Refresh both directions and identities immediately before mutation.
            fresh = self.run('-l')
            if (discover(self.run('-i'), SOURCE) != source or discover(self.run('-o'), DESTINATION) != destination or
                    discover(fresh, SOURCE) != source or discover(fresh, DESTINATION) != destination or
                    [self.identify(fresh, a) for a in (source, destination)] != ids):
                raise RuntimeError('ALSA identity changed before connection; no route attempted')
            if not connected(fresh, source, destination):
                self.run(source, destination)
            verified = self.run('-l')
            if (discover(verified, SOURCE) != source or discover(verified, DESTINATION) != destination or
                    [self.identify(verified, a) for a in (source, destination)] != ids or
                    not connected(verified, source, destination)):
                raise RuntimeError('ALSA subscription could not be verified')
            self.was_connected = True
            return self.report('unavailable' if usb_error else 'connected',
                ('USB error retained: ' + usb_error + '; route verification is not USB recovery') if usb_error else
                'ALSA subscription verified; MIDI delivery is not verified', ids, True, visibility, usb_error)
        except (OSError, RuntimeError) as error:
            return self.report('unavailable', str(error), ids, False, visibility)


def lock_keeper(directory):
    lock = (directory / 'midi-connect.lock').open('a')
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        lock.close()
        return None
    return lock


def status_snapshot(directory):
    try:
        value = json.loads((directory / 'midi-connect-status.json').read_text())
        boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
        process = Path('/proc/{}/stat'.format(value['pid'])).read_text().split(') ', 1)[1].split()
        start = process[19]
        if process[0] in ('Z', 'X'):
            return 'unavailable: keeper exited; route not revalidated'
        if value['boot_id'] != boot or value['process_start'] != start:
            return 'unavailable: keeper identity stale; MIDI delivery unverified'
        if not 0 <= time.monotonic() - value['observed_monotonic'] <= 20:
            return 'unavailable: keeper status stale; MIDI delivery unverified'
        return value
    except (OSError, ValueError, KeyError, IndexError):
        return 'unavailable: keeper status not yet available; MIDI delivery unverified'


def status(directory):
    value = status_snapshot(directory)
    if isinstance(value, str):
        return value
    return '{}: {}\nRoute USB: {}\nTeensy: {}\nVisibility: {}\nKeeper PID: {}'.format(
            value['state'], value['detail'], value.get('usb_error') or 'no attributed error observed; delivery unverified',
            value.get('teensy', {}).get('detail', 'BTX health unavailable'), value['visibility'], value['pid'])


def screen(directory, color=False):
    """Presentation only; reuse keeper identity/freshness and live BTX checks."""
    value = status_snapshot(directory)
    lines = ['BroTracker MIDI Connect', '  Live for 10s; reopen after USB swap', '']
    def row(label, text, good=False, uncertain=False):
        # Never render untrusted control characters from diagnostics on the VT.
        text = ''.join(c if 32 <= ord(c) < 127 else ' ' for c in str(text))[:38 - len(label) - 4]
        if color:
            text = '\033[1;{}m{}\033[0m'.format(32 if good else 33 if uncertain else 31, text)
        lines.append('  {}: {}'.format(label, text))
    if isinstance(value, str):
        row('Route', 'STALE' if 'stale' in value else 'UNAVAILABLE')
        row('Teensy', 'UNKNOWN', uncertain=True)
        row('H4MIDI', 'UNKNOWN', uncertain=True)
        row('USB', 'UNKNOWN', uncertain=True)
    else:
        state = value['state']
        row('Route', 'PRESENT' if value['subscription'] else state.upper(), value['subscription'])
        # New keepers publish independent observations. Older snapshots retain
        # conservative display behavior until that keeper is explicitly upgraded.
        for label, name in (('Teensy', SOURCE), ('H4MIDI', DESTINATION)):
            device = value.get('devices', {}).get(label)
            if device is not None:
                device_state = device['state']
                row(label, device_state.upper(), device_state == 'found', uncertain=device_state == 'unknown')
            else:
                found = len(value.get('identities', [])) == 2 and (value['subscription'] or
                    state == 'connecting' or value['detail'].startswith('Route absent;'))
                row(label, 'FOUND' if found else state.upper()
                    if state in ('missing', 'ambiguous') and name in value['detail'] else 'UNKNOWN', found, uncertain=not found and not (state in ('missing', 'ambiguous') and name in value['detail']))
        error = value.get('usb_error')
        match = re.search(r'(\d+-\d+(?:\.\d+)*).*?status\s+(-?\d+)', error or '')
        brief = 'ERROR {} {}'.format(match[2], match[1]) if match else 'ERROR (SEE LOG)'
        row('USB', brief if error else 'REDUCED VISIBILITY' if value['visibility'].startswith('reduced')
            else 'NO NEW ERROR OBSERVED', not error and not value['visibility'].startswith('reduced'),
            uncertain=not error and value['visibility'].startswith('reduced'))
    health = btx_health(directory)
    if health['state'] != 'unknown':
        row('MIDI', 'FAULT {}'.format(health['reason']) if health['state'] == 'fault' else health['state'].upper(),
            health['state'] == 'healthy')
    if 'cleanup' in health:
        complete = not health['cleanup'] and health['state'] in ('healthy', 'fault')
        row('Cleanup', 'PENDING / UNCERTAIN' if health['cleanup'] else
            'COMPLETE' if complete else 'UNKNOWN', complete)
    lines += ['', '  Delivery: UNVERIFIED', '  Logs: ' + str(directory),
              '  Details: brotracker-midi-connect.log', '']
    return '\n'.join(lines)


def watch_screen(directory):
    """Redraw changed status and its bottom button message as one display."""
    terminal = sys.stdout.isatty()
    color = terminal and 'NO_COLOR' not in os.environ
    previous = [None]
    def refresh(button_message=None):
        shown = screen(directory, color)
        if button_message is not None:
            shown += '\n' + button_message
        if shown != previous[0]:
            if terminal:
                # Erase the entire old display, including its footer, before repainting.
                sys.stdout.write('\033[s' if previous[0] is None else '\033[u\033[J')
            print(shown, flush=True)
            previous[0] = shown
    return wait_for_handheld(refresh=refresh)


def wait_for_handheld(seconds=10, inputs=Path('/sys/class/input'), devices=Path('/dev/input'), refresh=None):
    """Read native R36H gamepad events; no grabs, mapping daemon or SDL UI.

    Support the captured mapping and the actual R36H evdev controller name.
    Ignore already queued events so the button launching Ports cannot dismiss it.
    """
    handles = []
    event = struct.Struct('@llHHi')  # native Linux struct input_event (32/64 bit)
    try:
        for node in inputs.glob('event*'):
            try:
                if (node / 'device/name').read_text().strip() not in ('retrogame_joypad', 'GO-Super Gamepad'):
                    continue
                fd = os.open(str(devices / node.name), os.O_RDONLY | os.O_NONBLOCK)
                handles.append(fd)
                try:
                    while os.read(fd, event.size * 64):
                        pass
                except BlockingIOError:
                    pass
            except OSError:
                continue
        if not handles:
            unavailable = 'UNAVAILABLE'
            if sys.stdout.isatty() and 'NO_COLOR' not in os.environ:
                unavailable = '\033[1;31mUNAVAILABLE\033[0m'
            button_message = '  Buttons: {} (10s timeout)'.format(unavailable)
        else:
            button_message = '  Any gamepad button to return (10s)'
        if refresh is None:
            print(button_message, flush=True)
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if refresh is not None:
                refresh(button_message)
            remaining = max(0, deadline - time.monotonic())
            ready, _, _ = select.select(handles, [], [], min(.5, remaining) if refresh else remaining)
            for fd in ready:
                try:
                    data = os.read(fd, event.size * 64)
                except BlockingIOError:
                    continue
                except OSError:
                    handles.remove(fd)
                    os.close(fd)
                    continue
                if not data:
                    handles.remove(fd)
                    os.close(fd)
                    continue
                for offset in range(0, len(data) - event.size + 1, event.size):
                    _, _, kind, code, pressed = event.unpack_from(data, offset)
                    if kind == 1 and pressed == 1:  # EV_KEY, new press only
                        return True
        return False
    finally:
        for fd in handles:
            os.close(fd)


def btx_health(directory, now=None):
    """Observation only. Never open CDC or interpret keeper routing as firmware health."""
    unknown = {'state': 'unknown', 'detail': 'BTX not reporting fresh firmware health; delivery unverified'}
    try:
        fd = os.open(str(directory / 'btx-midi-health.state'), os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
        try:
            info = os.fstat(fd)
            if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_size > 1024:
                return unknown
            fields = os.read(fd, 1025).decode().split()
        finally:
            os.close(fd)
        if len(fields) != 15 or fields[:2] != ['BTMIDIHEALTH', '1']:
            return unknown
        pid, start, boot = int(fields[2]), fields[3], fields[4]
        observed = int(fields[5]); flags = [int(x) for x in fields[6:12]]
        reason, age, transport = int(fields[12]), int(fields[13]), fields[14]
        current = int((time.monotonic() if now is None else now) * 1000)
        if (pid <= 0 or not start.isdecimal() or any(x not in (0, 1) for x in flags) or not 0 <= reason <= 8 or
                not 0 <= age <= 4294967295 or transport not in ('playing', 'paused', 'stopped', 'idle', 'unknown') or
                current < observed or current - observed > 3000 or
                boot != Path('/proc/sys/kernel/random/boot_id').read_text().strip()):
            return unknown
        process = Path('/proc/{}/stat'.format(pid)).read_text().split(') ', 1)[1].split()
        if process[0] in ('Z', 'X') or process[19] != start:
            return unknown
        connected_, supported, known, configured, fault, cleanup = flags
        base = dict(owner_pid=pid, owner_start=start, boot_id=boot, observed_ms=observed,
                    firmware_age_ms=age + current - observed, transport=transport,
                    fault=bool(fault), cleanup=bool(cleanup), reason=reason)
        if not connected_:
            return dict(base, state='unavailable', detail='BTX reports CDC unavailable; Teensy MIDI health unknown')
        if not supported:
            return dict(base, state='unknown', detail='Firmware MIDI health unsupported/unavailable')
        if not known or age + current - observed > 3000:
            return dict(base, state='unknown', detail='Firmware MIDI health stale/unknown; delivery unverified')
        if fault:
            action = ('stopped + cleanup=0: user START requests recovery' if transport in ('stopped', 'idle') and not cleanup
                      else 'user STOP; wait STOPPED + cleanup=0; then user START')
            if not configured:
                action = 'USB unavailable: restore USB; user STOP; wait STOPPED + cleanup=0; then user START'
            return dict(base, state='fault', detail='MIDI fault {}{}; {}; {}'.format(reason,
                ' (completion stall)' if reason == 8 else '', 'cleanup uncertain' if cleanup else 'cleanup complete', action))
        return dict(base, state='healthy' if configured and not cleanup else 'unavailable',
                    detail='Teensy USB MIDI {}; cleanup={}; external delivery unverified'.format(
                        'configured' if configured else 'unavailable', cleanup))
    except (OSError, ValueError, IndexError):
        return unknown


def route_wire(value):
    def text(s): return str(s).replace('\n', ' ').replace('\r', ' ').replace('\t', ' ')[:500]
    return ('BTMIDIROUTE 1 {pid} {process_start} {boot_id} {at} {state} {subscription}\n'.format(
                at=int(value['observed_monotonic'] * 1000), **dict(value, subscription=int(value['subscription']))) +
            'detail ' + text(value['detail']) + '\nusb ' + text(value.get('usb_error', '')) +
            '\nvisibility ' + text(value['visibility']) + '\n')


def serve(directory, lock):
    logger = logging.getLogger('midi-connect')
    logger.setLevel(logging.INFO)
    handler = RotatingFileHandler(str(directory / 'brotracker-midi-connect.log'), maxBytes=131072, backupCount=3)
    handler.namer = lambda name: re.sub(r'\.log\.(\d+)$', r'(\1).log', name)
    class BoundedFormatter(logging.Formatter):
        def format(self, record):
            return ('{} mono={:.6f} {}'.format(time.strftime('%Y-%m-%dT%H:%M:%S%z'),
                    time.monotonic(), super().format(record)))[:4096]
    handler.setFormatter(BoundedFormatter())
    logger.addHandler(handler)
    try:
        set_keeper_name()
    except Exception:
        logger.exception('Keeper startup failed: process name not verified; no readiness or routing')
        handler.flush()
        lock.close()
        logger.removeHandler(handler)
        handler.close()
        raise
    boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
    start = Path('/proc/self/stat').read_text().split(') ', 1)[1].split()[19]
    last_health = [None]
    def publish(value, changed=False):
        health = btx_health(directory)
        health_key = {k: v for k, v in health.items() if k not in ('observed_ms', 'firmware_age_ms')}
        health_changed = health_key != last_health[0]
        last_health[0] = health_key
        value = dict(value, schema=1, pid=os.getpid(), boot_id=boot, process_start=start, process_name=KEEPER_NAME,
                     wall_time=time.strftime('%Y-%m-%dT%H:%M:%S%z'), observed_monotonic=time.monotonic(), teensy=health)
        temp = directory / 'midi-connect-status.next'
        temp.write_text(json.dumps(value, sort_keys=True))
        temp.replace(directory / 'midi-connect-status.json')
        temp = directory / 'midi-connect-status.btx.next'
        temp.write_text(route_wire(value))
        temp.replace(directory / 'midi-connect-status.btx')
        if changed or health_changed:
            logger.info(json.dumps(value, sort_keys=True))
            handler.flush()
            os.fsync(handler.stream.fileno())
    keeper = Keeper(emit=lambda value: publish(value, True))
    logger.info('keeper started pid=%s boot=%s name=%s verified; ALSA only; no CDC or playback control', os.getpid(), boot, KEEPER_NAME)
    try:
        while True:
            publish(keeper.step(time.monotonic()))
            time.sleep(2)
    except Exception:
        logger.exception('keeper stopped unexpectedly; no route removed')
        handler.flush()
    finally:
        lock.close()


def ensure(directory):
    lock = lock_keeper(directory)
    if lock is None:
        # Another launch may still be initializing its named child. Reuse the
        # same bounded readiness wait; never spawn a competing keeper.
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            if keeper_identity_ready(status_snapshot(directory)):
                print(status(directory))
                return
            time.sleep(.05)
        raise RuntimeError('Keeper lock held but process name/status unverified; inspect logs; an old keeper requires explicit stop/relaunch')
    try:
        migrate_legacy(directory)
        child = subprocess.Popen([sys.executable, '-B', str(Path(__file__).resolve()),
                                  '--keeper-fd', str(lock.fileno())], pass_fds=(lock.fileno(),),
                                 stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL, start_new_session=True)
        deadline = time.monotonic() + 2
        ready = False
        while time.monotonic() < deadline:
            try:
                value = status_snapshot(directory)
                if not isinstance(value, str) and value['pid'] == child.pid and keeper_identity_ready(value):
                    ready = True
                    break
            except (OSError, ValueError, KeyError):
                pass
            if child.poll() is not None:
                raise RuntimeError('Keeper startup failed (exit {}); inspect {} for process-name/initialization errors'.format(
                    child.returncode, directory / 'brotracker-midi-connect.log'))
            time.sleep(.05)
        if not ready:
            raise RuntimeError('Keeper startup/name not confirmed within 2 seconds; child may still finish; inspect ' +
                               str(directory / 'brotracker-midi-connect.log'))
        print(status(directory))
    finally:
        lock.close()  # Child retains the same locked open file description.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--status', action='store_true')
    parser.add_argument('--screen', action='store_true')
    parser.add_argument('--wait-buttons', action='store_true')
    parser.add_argument('--migrate-legacy', action='store_true', help='Preserve inactive legacy runtime files without starting routing')
    parser.add_argument('--keeper-fd', type=int, help=argparse.SUPPRESS)
    args = parser.parse_args()
    os.umask(0o077)
    directory = runtime_directory()
    if directory.exists() or directory.is_symlink():
        prepare_directory(directory)
    if args.screen:
        if args.wait_buttons:
            watch_screen(directory)
            return
        print(screen(directory, sys.stdout.isatty() and 'NO_COLOR' not in os.environ))
        return
    if args.wait_buttons:
        wait_for_handheld()
        return
    if args.status:
        print(status(directory))
        return
    prepare_directory(directory)
    if args.migrate_legacy:
        lock = lock_keeper(directory)
        if lock is None:
            raise RuntimeError('Keeper active; migrate only while stopped')
        try:
            migrate_legacy(directory)
        finally:
            lock.close()
        return
    if args.keeper_fd is not None:
        serve(directory, os.fdopen(args.keeper_fd, 'a'))
    else:
        ensure(directory)


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError) as error:
        print('FAILED: ' + str(error), file=sys.stderr)
        sys.exit(1)

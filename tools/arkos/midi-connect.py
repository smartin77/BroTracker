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

SOURCE = 'BroTracker USB audio MIDI 1'
DESTINATION = 'H4MIDI-WC MIDI 1'


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
        raise RuntimeError('Ambiguous port: {} ({}) — no route changed.'.format(name, ', '.join(matches)))
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


def identity(listing, address):
    """Card/USB incarnation supplements the supported name; never selects a port."""
    client = address.split(':')[0]
    match = re.search(r"^client " + client + r": (.*)$", listing, re.M)
    header = match.group(1) if match else ''
    result = {'address': address, 'client': header[:200]}
    card = re.search(r'\bcard=(\d+)\b', header)
    if card:
        node = (Path('/sys/class/sound') / ('card' + card.group(1)) / 'device').resolve()
        for parent in (node, *node.parents):
            try:
                result['usb'] = {k: (parent / k).read_text().strip()[:100]
                                 for k in ('idVendor', 'idProduct', 'busnum', 'devnum')}
                result['usb']['path'] = parent.name
                result['usb']['inode'] = parent.stat().st_ino
                break
            except OSError:
                pass
    return result


class KernelErrors:
    """Read-only ring observations. Baseline old errors; latch new errors per incarnation."""
    def __init__(self, read=None):
        self.read = read or self.read_ring
        self.watermark = None
        self.key = None
        self.error = ''
        self.latched = {}

    @staticmethod
    def read_ring():
        r = subprocess.run(['dmesg', '--color=never'], stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, universal_newlines=True,
                           env=dict(os.environ, LC_ALL='C'), timeout=3)
        if r.returncode:
            raise RuntimeError('kernel log unreadable')
        return r.stdout

    def poll(self, identities):
        key = json.dumps(identities, sort_keys=True)
        changed = key != self.key
        if changed:
            self.key, self.error = key, self.latched.get(key, '')
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
        previous = None if changed else self.watermark
        if not lines:
            return 'reduced: kernel timestamps unavailable; USB errors cannot be attributed', self.error
        if lines:
            self.watermark = lines[-1][0]
        births = []
        birth_paths = {}
        if changed:
            for item in identities:
                usb = item.get('usb', {})
                pattern = r'^usb ' + re.escape(usb.get('path', '?')) + r': new .* USB device number ' + re.escape(usb.get('devnum', '?')) + r' using '
                stamps = [stamp for stamp, line in lines if re.search(pattern, line)]
                if stamps:
                    births.append(stamps[-1])
                    path = usb['path']
                    while True:
                        birth_paths[path] = min(stamps[-1], birth_paths.get(path, stamps[-1]))
                        if '.' not in path:
                            break
                        path = path.rsplit('.', 1)[0]
        for stamp, line in lines:
            path = next((path for path in paths if ('usb ' + path + ':') in line), None)
            since = birth_paths.get(path) if changed else previous
            if since is not None and stamp > since and path and re.search(
                    r'urb status -\d+|error -\d+|over-current|disabled by hub', line, re.I):
                self.error = line[:500]
                self.latched[key] = self.error
                if len(self.latched) > 16:
                    del self.latched[next(iter(self.latched))]
        visibility = 'kernel log observed; delivery unverified'
        if not paths or any('usb' not in item for item in identities):
            visibility = 'reduced: ALSA identity lacks USB mapping; error attribution incomplete'
        elif changed and len(births) != len(identities):
            visibility = 'reduced: enumeration history missing; observing new USB errors only'
        if previous is not None and lines and self.watermark < previous:
            visibility = 'reduced: kernel log discontinuity; USB history incomplete'
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

    def report(self, state, detail, identities=(), route=False, visibility='unknown', usb_error=''):
        value = dict(state=state, detail=str(detail)[:1000], identities=list(identities),
                     subscription=route, delivery='unverified', visibility=visibility,
                     attempts=self.attempts, usb_error=usb_error)
        if value != self.last:
            self.emit(value)
            self.last = value
        return value

    def step(self, now):
        ids = []
        visibility = 'reduced: USB/kernel observation unavailable for this sample'
        try:
            incoming, outgoing, listing = self.run('-i'), self.run('-o'), self.run('-l')
            try:
                source = discover(incoming, SOURCE)
                destination = discover(outgoing, DESTINATION)
            except RuntimeError as error:
                self.key, self.attempts, self.was_connected = None, 0, False
                visibility, _ = self.errors.poll(())
                return self.report('ambiguous' if 'Ambiguous' in str(error) else 'missing', str(error), visibility=visibility)
            ids = [self.identify(listing, a) for a in (source, destination)]
            for address, name in ((source, 'BroTracker USB audio'), (destination, 'H4MIDI-WC')):
                if not re.search(r"^client " + address.split(':')[0] + r": '" + re.escape(name) +
                                 r"' \[type=kernel(?:,|\])", listing, re.M):
                    raise RuntimeError('Supported port is not on its expected hardware ALSA client: ' + address)
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


def status(directory):
    try:
        value = json.loads((directory / 'midi-connect-status.json').read_text())
        boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
        process = Path('/proc/{}/stat'.format(value['pid'])).read_text().split(') ', 1)[1].split()
        start = process[19]
        if process[0] in ('Z', 'X'):
            return 'unavailable: keeper exited; route not revalidated'
        if value['boot_id'] != boot or value['process_start'] != start:
            return 'unavailable: keeper identity stale; MIDI delivery unverified'
        if time.monotonic() - value['observed_monotonic'] > 20:
            return 'unavailable: keeper status stale; MIDI delivery unverified'
        return '{}: {}\nRoute USB: {}\nTeensy: {}\nVisibility: {}\nKeeper PID: {}'.format(
            value['state'], value['detail'], value.get('usb_error') or 'no attributed error observed; delivery unverified',
            value.get('teensy', {}).get('detail', 'BTX health unavailable'), value['visibility'], value['pid'])
    except (OSError, ValueError, KeyError, IndexError):
        return 'unavailable: keeper status not yet available; MIDI delivery unverified'


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
    handler = RotatingFileHandler(str(directory / 'midi-connect.log'), maxBytes=131072, backupCount=3)
    class BoundedFormatter(logging.Formatter):
        def format(self, record):
            return ('{} mono={:.6f} {}'.format(time.strftime('%Y-%m-%dT%H:%M:%S%z'),
                    time.monotonic(), super().format(record)))[:4096]
    handler.setFormatter(BoundedFormatter())
    logger.addHandler(handler)
    boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
    start = Path('/proc/self/stat').read_text().split(') ', 1)[1].split()[19]
    last_health = [None]
    def publish(value, changed=False):
        health = btx_health(directory)
        health_key = {k: v for k, v in health.items() if k not in ('observed_ms', 'firmware_age_ms')}
        health_changed = health_key != last_health[0]
        last_health[0] = health_key
        value = dict(value, schema=1, pid=os.getpid(), boot_id=boot, process_start=start,
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
    logger.info('keeper started pid=%s boot=%s; ALSA only; no CDC or playback control', os.getpid(), boot)
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
        print(status(directory))
        return
    try:
        child = subprocess.Popen([sys.executable, '-B', str(Path(__file__).resolve()),
                                  '--keeper-fd', str(lock.fileno())], pass_fds=(lock.fileno(),),
                                 stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL, start_new_session=True)
        deadline = time.monotonic() + 2
        ready = False
        while time.monotonic() < deadline:
            try:
                if json.loads((directory / 'midi-connect-status.json').read_text())['pid'] == child.pid:
                    ready = True
                    break
            except (OSError, ValueError, KeyError):
                pass
            if child.poll() is not None:
                raise RuntimeError('Keeper failed to start; inspect Python/ALSA availability')
            time.sleep(.05)
        print(status(directory) if ready else 'connecting: keeper starting; route and MIDI delivery unverified')
    finally:
        lock.close()  # Child retains the same locked open file description.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--status', action='store_true')
    parser.add_argument('--keeper-fd', type=int, help=argparse.SUPPRESS)
    args = parser.parse_args()
    os.umask(0o077)
    directory = Path.home() / 'BroTracker'
    if args.status:
        print(status(directory))
        return
    directory.mkdir(parents=True, exist_ok=True)
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

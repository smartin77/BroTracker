#!/usr/bin/env python3
"""Manual development route only; never disconnect or change another subscription."""
import fcntl
import os
from pathlib import Path
import re
import subprocess
import sys

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


def main():
    directory = Path.home() / 'BroTracker'
    directory.mkdir(parents=True, exist_ok=True)
    # Serializes repeated launches without creating or deleting subscriptions.
    with (directory / 'midi-connect.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        connect()


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError) as error:
        print('FAILED: ' + str(error), file=sys.stderr)
        sys.exit(1)

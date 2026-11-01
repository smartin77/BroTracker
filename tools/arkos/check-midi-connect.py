#!/usr/bin/env python3
"""Isolated fixtures only: no ALSA devices, MIDI traffic or real console access."""
import contextlib
import importlib.util
import io
import os
import json
import signal
import sys
import time
from pathlib import Path
import pty
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('midi_connect', HERE / 'midi-connect.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


def listing(source=28, destination=41, route=False, extra=''):
    return ("client {}: 'BroTracker USB audio' [type=kernel,card=2]\n"
            "    0 'BroTracker USB audio MIDI 1'\n{}"
            "client {}: 'H4MIDI-WC' [type=kernel,card=1]\n"
            "    0 'H4MIDI-WC MIDI 1'\n"
            "client 88: 'Unrelated' [type=user,pid=123]\n"
            "    9 'Other port      '\n\tConnecting To: 99:0\n{}"
            ).format(source, '\tConnecting To: 77:2, {}:0[real:1]\n'.format(destination) if route
                     else '\tConnecting To: 77:2\n', destination, extra)


class Peer:
    def __init__(self, source=28, destination=41):
        self.source, self.destination = source, destination
        self.route = False
        self.calls = []
        self.fail = False
        self.verify = True

    def __call__(self, *args):
        self.calls.append(args)
        if args in (('-i',), ('-o',), ('-l',)):
            return listing(self.source, self.destination, self.route)
        if args != ('{}:0'.format(self.source), '{}:0'.format(self.destination)):
            raise AssertionError('Unintended routing command: ' + repr(args))
        if self.fail:
            raise RuntimeError('Connection failed: permission denied')
        self.route = self.verify
        return ''


class Checks(unittest.TestCase):
    def test_btx_health_freshness_fault_cleanup_disconnect_and_restart(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); path = root / 'btx-midi-health.state'
            boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
            start = Path('/proc/self/stat').read_text().split(') ', 1)[1].split()[19]
            self.assertEqual(tool.btx_health(root, 100)['state'], 'unknown')
            def write(flags='1 1 1 1 1 1', reason=8, age=0, transport='stopped', owner_start=start, owner_boot=boot):
                path.write_text('BTMIDIHEALTH 1 {} {} {} 100000 {} {} {} {}\n'.format(
                    os.getpid(), owner_start, owner_boot, flags, reason, age, transport))
            write(); health = tool.btx_health(root, 100)
            self.assertEqual(health['state'], 'fault'); self.assertIn('wait STOPPED + cleanup=0', health['detail'])
            write('1 1 1 1 1 0');self.assertIn('START requests recovery', tool.btx_health(root, 100)['detail'])
            write('1 1 1 1 1 0', transport='playing');self.assertIn('user STOP', tool.btx_health(root, 100)['detail'])
            write('1 1 1 1 0 0', reason=0);self.assertEqual(tool.btx_health(root, 100)['state'], 'healthy')
            self.assertEqual(tool.btx_health(root, 104)['state'], 'unknown')
            write('1 1 1 1 0 0', age=2999);self.assertEqual(tool.btx_health(root, 100.002)['state'], 'unknown')
            write('0 0 0 0 0 0');self.assertEqual(tool.btx_health(root, 100)['state'], 'unavailable')
            write(owner_start=str(int(start)+1));self.assertEqual(tool.btx_health(root, 100)['state'], 'unknown')
            write(owner_boot='00000000-0000-0000-0000-000000000000');self.assertEqual(tool.btx_health(root, 100)['state'], 'unknown')
            write('1 1 0 0 0 0');self.assertEqual(tool.btx_health(root, 100)['state'], 'unknown')
            path.unlink();path.symlink_to('/dev/null');self.assertEqual(tool.btx_health(root, 100)['state'], 'unknown')

    def test_route_wire_separates_usb_error_and_sanitizes_line_data(self):
        value = dict(pid=123, process_start='456', boot_id='00000000-0000-0000-0000-000000000000',
                     observed_monotonic=10, state='unavailable', subscription=True, detail='route present',
                     usb_error='urb status -32\ninjected', visibility='reduced: fixture')
        wire = tool.route_wire(value)
        self.assertEqual(wire.splitlines()[0], 'BTMIDIROUTE 1 123 456 00000000-0000-0000-0000-000000000000 10000 unavailable 1')
        self.assertEqual(len(wire.splitlines()), 4)
        self.assertEqual(wire.splitlines()[2], 'usb urb status -32 injected')

    def test_btx_process_restart_cannot_reuse_old_health(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            script = '''import os,time,pathlib,sys
p=pathlib.Path(sys.argv[1]);boot=pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip()
start=pathlib.Path('/proc/self/stat').read_text().split(') ',1)[1].split()[19]
p.write_text('BTMIDIHEALTH 1 {} {} {} {} 1 1 1 1 0 0 0 0 stopped\\n'.format(os.getpid(),start,boot,int(time.monotonic()*1000)))
sys.stdin.read()
'''
            previous = None
            for unused in range(2):
                path = root / 'btx-midi-health.state'
                child = subprocess.Popen([sys.executable, '-c', script, str(path)], stdin=subprocess.PIPE)
                try:
                    for attempt in range(100):
                        health = tool.btx_health(root)
                        if health.get('owner_pid') == child.pid: break
                        time.sleep(.01)
                    self.assertEqual(health['state'], 'healthy')
                    self.assertNotEqual(health['owner_pid'], previous)
                    previous = child.pid
                finally:
                    child.communicate(timeout=3)
                self.assertEqual(tool.btx_health(root)['state'], 'unknown')

    def keeper(self, peer, errors=None, events=None):
        return tool.Keeper(run=peer, identify=lambda listing, address: {'address': address},
                           errors=errors or tool.KernelErrors(lambda: '[ 1.0] baseline'),
                           emit=(events.append if events is not None else None))

    def test_arrival_in_either_order_only_routes_supported_pair(self):
        for first in (tool.SOURCE, tool.DESTINATION):
            peer = Peer()
            absent = {tool.SOURCE, tool.DESTINATION}
            def run(*args):
                text = peer(*args)
                if args[0].startswith('-'):
                    for name in absent:
                        text = text.replace(name, 'absent')
                return text
            keeper = self.keeper(run)
            self.assertEqual(keeper.step(0)['state'], 'missing')
            absent.remove(first)
            self.assertEqual(keeper.step(2)['state'], 'missing')
            self.assertFalse(peer.route)
            absent.clear()
            self.assertEqual(keeper.step(4)['state'], 'connected')
            self.assertEqual(peer.calls.count(('28:0', '41:0')), 1)
            self.assertTrue(tool.connected(listing(route=True), '88:9', '99:0'))

    def test_route_loss_and_reconnect_with_new_numbers(self):
        peer = Peer(); keeper = self.keeper(peer)
        self.assertEqual(keeper.step(0)['state'], 'connected')
        peer.route = False
        self.assertEqual(keeper.step(2)['state'], 'connected')
        self.assertEqual(peer.calls.count(('28:0', '41:0')), 2)
        peer.source, peer.destination, peer.route = 35, 71, False
        self.assertEqual(keeper.step(4)['state'], 'connected')
        self.assertEqual(peer.calls.count(('35:0', '71:0')), 1)

    def test_ambiguous_ports_never_mutate_routes(self):
        peer = Peer()
        def run(*args):
            self.assertTrue(args[0].startswith('-'))
            return peer(*args) + "client 90: 'Duplicate' [type=kernel,card=9]\n    0 'H4MIDI-WC MIDI 1'\n"
        self.assertEqual(self.keeper(run).step(0)['state'], 'ambiguous')
        self.assertFalse(peer.route)

    def test_bounded_retry_and_stable_failure_does_not_flood(self):
        peer = Peer(); peer.fail = True; events = []; keeper = self.keeper(peer, events=events)
        for now in (0, 2, 6, 14, 30):
            self.assertEqual(keeper.step(now)['state'], 'unavailable')
        for now in range(32, 200, 2): keeper.step(now)
        self.assertEqual(peer.calls.count(('28:0', '41:0')), 5)
        count = len(events)
        for now in range(200, 300, 2): keeper.step(now)
        self.assertEqual(len(events), count)
        peer.destination, peer.fail = 55, False
        self.assertEqual(keeper.step(302)['state'], 'connected')

    def test_changed_identity_before_mutation_is_rejected(self):
        peer = Peer(); calls = [0]
        def identify(listing, address):
            calls[0] += 1
            return {'address': address, 'generation': 1 if calls[0] <= 2 else 2}
        keeper = self.keeper(peer); keeper.identify = identify
        self.assertEqual(keeper.step(0)['state'], 'unavailable')
        self.assertFalse(peer.route)

    def test_kernel_error_latches_without_route_repair_and_visibility_failures(self):
        ring = ['[ 1.0] usb 1-1.1: urb status -32']
        errors = tool.KernelErrors(lambda: ring[0])
        peer = Peer(); keeper = self.keeper(peer, errors)
        keeper.identify = lambda listing, address: {'address': address, 'usb': {'path': '1-1.1', 'devnum': '4'}}
        self.assertEqual(keeper.step(0)['state'], 'connected')  # Historical error is baselined.
        ring[0] += '\n[ 2.0] usb 1-1.1: urb status -32'
        state = keeper.step(2)
        self.assertEqual(state['state'], 'unavailable'); self.assertTrue(state['subscription'])
        self.assertIn('not repaired', state['detail'])
        keeper.step(4)
        self.assertEqual(peer.calls.count(('28:0', '41:0')), 1)
        def denied(): raise RuntimeError('permission denied')
        errors.read = denied
        self.assertIn('reduced', keeper.step(6)['visibility'])
        self.assertEqual(keeper.last['delivery'], 'unverified')

    def test_kernel_errors_are_attributed_to_current_enumeration_not_reused_path(self):
        ring = ('[ 1.0] usb 1-1.1: urb status -32\n'
                '[ 2.0] usb 1-1.1: new full-speed USB device number 4 using dwc2\n'
                '[ 3.0] usb 1-1.1: urb status -32')
        errors = tool.KernelErrors(lambda: ring)
        current = [{'usb': {'path': '1-1.1', 'devnum': '4'}}]
        self.assertIn('urb status', errors.poll(current)[1])
        errors.poll(())  # Transient ALSA disappearance must not erase this incarnation's fault.
        self.assertIn('urb status', errors.poll(current)[1])
        ring += '\n[ 4.0] usb 1-1.1: new full-speed USB device number 5 using dwc2'
        self.assertEqual(errors.poll([{'usb': {'path': '1-1.1', 'devnum': '5'}}])[1], '')

    def test_named_software_port_cannot_impersonate_supported_hardware_client(self):
        peer = Peer()
        def run(*args):
            self.assertTrue(args[0].startswith('-'))
            return peer(*args).replace("'H4MIDI-WC' [type=kernel,card=1]", "'H4MIDI-WC' [type=user,pid=17]")
        self.assertEqual(self.keeper(run).step(0)['state'], 'unavailable')
        self.assertFalse(peer.route)

    def test_detached_single_keeper_repeated_launches_and_stale_status(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); (root / 'bin').mkdir()
            for name, script in [('aconnect', '#!/bin/sh\nexit 0\n'),
                                 ('dmesg', '#!/bin/sh\necho "[ 1.0] fixture"\n')]:
                path = root / 'bin' / name; path.write_text(script); path.chmod(0o755)
            env = dict(os.environ, HOME=temp, PATH=str(root / 'bin') + ':' + os.environ['PATH'])
            pid = None
            try:
                commands = [[sys.executable, '-B', str(HERE / 'midi-connect.py')]] * 2
                children = [subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE) for cmd in commands]
                for child in children:
                    out, err = child.communicate(timeout=8); self.assertEqual(child.returncode, 0, err)
                directory = root / 'BroTracker'
                value = json.loads((directory / 'midi-connect-status.json').read_text()); pid = value['pid']
                self.assertEqual(value['teensy']['state'], 'unknown')
                self.assertTrue((directory / 'midi-connect-status.btx').read_text().startswith('BTMIDIROUTE 1 '))
                again = subprocess.run(commands[0], env=env, stdout=subprocess.PIPE, timeout=5)
                self.assertIn(str(pid), again.stdout.decode())
                self.assertIsNone(tool.lock_keeper(directory))
                self.assertEqual(value['state'], 'missing')
                self.assertTrue((directory / 'midi-connect.log').exists())
                os.kill(pid, signal.SIGTERM)
                for unused in range(100):
                    lock = tool.lock_keeper(directory)
                    if lock is not None: lock.close(); break
                    time.sleep(.02)
                else: self.fail('keeper lock leaked after process exit')
                value['observed_monotonic'] = 0
                (directory / 'midi-connect-status.json').write_text(json.dumps(value))
                self.assertIn('unavailable', tool.status(directory))
                subprocess.run(commands[0], env=env, stdout=subprocess.PIPE, check=True, timeout=5)
                restarted = json.loads((directory / 'midi-connect-status.json').read_text())
                self.assertNotEqual(restarted['pid'], pid)
                pid = restarted['pid']
                self.assertNotIn('stale', tool.status(directory))
            finally:
                if pid is not None:
                    try: os.kill(pid, signal.SIGTERM)
                    except ProcessLookupError: pass

    def test_discovery_and_repeat_with_changing_client_numbers(self):
        for source, destination in ((28, 41), (135, 22)):
            peer = Peer(source, destination)
            with contextlib.redirect_stdout(io.StringIO()) as output:
                tool.connect(peer)
                tool.connect(peer)
            self.assertIn('OK: connected', output.getvalue())
            self.assertIn('OK: already connected', output.getvalue())
            self.assertEqual(peer.calls.count(('{}:0'.format(source), '{}:0'.format(destination))), 1)
            self.assertTrue(peer.route)
            self.assertTrue(tool.connected(listing(route=True), '88:9', '99:0'))

    def test_missing_or_ambiguous_source_and_destination_never_connect(self):
        for name in (tool.SOURCE, tool.DESTINATION):
            for ambiguous in (False, True):
                text = listing()
                if ambiguous:
                    text += "client 70: 'Second device' [type=kernel,card=3]\n    0 '{}'\n".format(name)
                else:
                    text = text.replace(name, 'Different name')
                calls = []
                def run(*args):
                    calls.append(args)
                    self.assertTrue(args[0].startswith('-'))
                    return text
                with self.assertRaisesRegex(RuntimeError, 'Ambiguous' if ambiguous else 'Missing'):
                    tool.connect(run)

    def test_direction_is_checked_before_using_combined_listing(self):
        calls = []
        def run(*args):
            calls.append(args)
            return listing().replace(tool.SOURCE, 'Not readable') if args == ('-i',) else listing()
        with self.assertRaisesRegex(RuntimeError, 'Missing port'):
            tool.connect(run)
        self.assertEqual(calls, [('-i',)])

    def test_port_change_before_connect_is_safe_failure(self):
        peer = Peer()
        def run(*args):
            return listing(source=31) if args == ('-l',) else peer(*args)
        with self.assertRaisesRegex(RuntimeError, 'changed during discovery'):
            tool.connect(run)
        self.assertEqual(peer.calls, [('-i',), ('-o',)])

    def test_connection_failure_or_unconfirmed_result_is_not_success(self):
        for fail in (False, True):
            peer = Peer()
            peer.fail, peer.verify = fail, False
            with self.assertRaisesRegex(RuntimeError, 'Connection failed' if fail else 'could not be verified'):
                tool.connect(peer)
            self.assertEqual(peer.calls.count(('28:0', '41:0')), 1)

    def test_exact_names_and_duplicate_addresses(self):
        with self.assertRaisesRegex(RuntimeError, 'Missing'):
            tool.discover(listing().replace(tool.SOURCE, tool.SOURCE + ' extra'), tool.SOURCE)
        with self.assertRaisesRegex(RuntimeError, 'Duplicate'):
            tool.ports(listing() + "client 28: 'Duplicate' [type=kernel]\n    0 'Other'\n")
        self.assertFalse(tool.connected(listing(route=True), '28:0', '41:1'))

    def test_command_failure_timeout_missing_tool_and_locale(self):
        with mock.patch.object(tool.subprocess, 'run', side_effect=FileNotFoundError):
            with self.assertRaisesRegex(RuntimeError, 'Missing aconnect'):
                tool.aconnect('-i')
        with mock.patch.object(tool.subprocess, 'run', side_effect=subprocess.TimeoutExpired('aconnect', 5)):
            with self.assertRaisesRegex(RuntimeError, 'timed out'):
                tool.aconnect('-l')
        with mock.patch.object(tool.subprocess, 'run', return_value=subprocess.CompletedProcess([], 1, '', 'ALSA unavailable')):
            with self.assertRaisesRegex(RuntimeError, 'ALSA unavailable'):
                tool.aconnect('-i')
        with mock.patch.object(tool.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, 'ports', '')) as run:
            self.assertEqual(tool.aconnect('-i'), 'ports')
            self.assertEqual(run.call_args[1]['env']['LC_ALL'], 'C')
            self.assertEqual(run.call_args[0][0], ['aconnect', '-i'])

    def test_ports_entry_shows_feedback_and_keeps_log_without_btx(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'ports').mkdir()
            (root / 'bin').mkdir()
            launcher = root / 'ports' / 'BroTracker MIDI Connect.sh'
            shutil.copyfile(HERE / 'port' / launcher.name, launcher)
            for name, script in [('python3', '#!/bin/sh\necho "OK: fixture feedback"\nexit 0\n'),
                                 ('sleep', '#!/bin/sh\nexit 0\n')]:
                path = root / 'bin' / name
                path.write_text(script)
                path.chmod(0o755)
            master, slave = pty.openpty()
            try:
                result = subprocess.run(['bash', str(launcher)], stdout=slave, stderr=slave,
                                        env=dict(os.environ, HOME=temp, PATH=str(root / 'bin') + ':' + os.environ['PATH']),
                                        timeout=5)
                os.close(slave)
                slave = None
                shown = os.read(master, 4096).decode()
            finally:
                os.close(master)
                if slave is not None:
                    os.close(slave)
            self.assertEqual(result.returncode, 0)
            self.assertIn('OK: fixture feedback', shown)
            self.assertIn('OK: fixture feedback', (root / 'BroTracker' / 'midi-connect-launch.log').read_text())


if __name__ == '__main__':
    unittest.main(verbosity=2)

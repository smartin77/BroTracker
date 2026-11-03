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
import stat
from types import SimpleNamespace
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


class KeeperNameTests(unittest.TestCase):
    def test_set_and_get_kernel_name_are_verified(self):
        libc = mock.Mock()
        def call(operation, buffer, *unused):
            if operation == 16:
                tool.ctypes.memmove(buffer, tool.KEEPER_NAME.encode() + b'\0', len(tool.KEEPER_NAME) + 1)
            return 0
        libc.prctl.side_effect = call
        with mock.patch.object(tool.ctypes, 'CDLL', return_value=libc):
            tool.set_keeper_name()
        self.assertEqual([c.args[0] for c in libc.prctl.call_args_list], [15, 16])
        self.assertLessEqual(len(tool.KEEPER_NAME.encode('ascii')), 15)

    def test_bad_names_syscall_failure_and_verification_mismatch(self):
        for name in ('', 'x' * 16, 'bad\0name', '\u00e9'):
            with self.subTest(name=name), self.assertRaises(RuntimeError):
                tool.set_keeper_name(name)
        for results, expected in [([-1], 'setup failed'), ([0, -1], 'setup failed'), ([0, 0], 'verification mismatch')]:
            libc = mock.Mock(); libc.prctl.side_effect = results
            with mock.patch.object(tool.ctypes, 'CDLL', return_value=libc), self.assertRaisesRegex(RuntimeError, expected):
                tool.set_keeper_name()

    def test_naming_failure_logs_without_readiness_or_routing(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp); lock = tool.lock_keeper(directory)
            with mock.patch.object(tool, 'set_keeper_name', side_effect=RuntimeError('denied')), mock.patch.object(tool, 'Keeper') as route:
                with self.assertRaisesRegex(RuntimeError, 'denied'):
                    tool.serve(directory, lock)
                route.assert_not_called()
            self.assertFalse((directory / 'midi-connect-status.json').exists())
            self.assertFalse((directory / 'midi-connect-status.btx').exists())
            self.assertIn('process name not verified', (directory / 'brotracker-midi-connect.log').read_text())
            self.assertTrue(lock.closed)

    def test_parent_reports_child_failure_and_releases_lock(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            child = mock.Mock(pid=123, returncode=1); child.poll.return_value = 1
            with mock.patch.object(tool, 'migrate_legacy'), mock.patch.object(tool.subprocess, 'Popen', return_value=child):
                with self.assertRaisesRegex(RuntimeError, 'process-name/initialization errors'):
                    tool.ensure(directory)
            lock = tool.lock_keeper(directory); self.assertIsNotNone(lock); lock.close()
            child.terminate.assert_not_called(); child.kill.assert_not_called()

    def test_parent_timeout_never_claims_readiness_or_kills_child(self):
        with tempfile.TemporaryDirectory() as temp:
            child = mock.Mock(pid=123); child.poll.return_value = None
            with mock.patch.object(tool, 'migrate_legacy'), mock.patch.object(tool, 'status_snapshot', return_value='unavailable'), mock.patch.object(
                    tool.subprocess, 'Popen', return_value=child), mock.patch.object(tool.time, 'monotonic', side_effect=[0, 0, 3]), mock.patch.object(tool.time, 'sleep'):
                with self.assertRaisesRegex(RuntimeError, 'startup/name not confirmed'):
                    tool.ensure(Path(temp))
            child.terminate.assert_not_called(); child.kill.assert_not_called()

    def test_actual_named_keeper_accepts_pid_targeted_sigterm(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); (root / 'bin').mkdir()
            for name, text in [('aconnect', '#!/bin/sh\nexit 0\n'), ('dmesg', '#!/bin/sh\necho "[ 1.0] fixture"\n')]:
                path = root / 'bin' / name; path.write_text(text); path.chmod(0o755)
            directory = root / ('brotracker-' + str(os.getuid())); tool.prepare_directory(directory)
            lock = tool.lock_keeper(directory); child = None
            try:
                env = dict(os.environ, HOME=temp, TMPDIR=temp, PATH=str(root / 'bin') + ':' + os.environ['PATH'])
                child = subprocess.Popen([sys.executable, '-B', str(HERE / 'midi-connect.py'), '--keeper-fd', str(lock.fileno())],
                                         env=env, pass_fds=(lock.fileno(),), stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
                lock.close()
                deadline = time.monotonic() + 5
                value = 'unavailable'
                while time.monotonic() < deadline:
                    value = tool.status_snapshot(directory)
                    if tool.keeper_identity_ready(value): break
                    if child.poll() is not None: self.fail(child.stderr.read().decode())
                    time.sleep(.02)
                self.assertTrue(tool.keeper_identity_ready(value))
                self.assertEqual(value['pid'], child.pid)
                child.terminate()
                self.assertEqual(child.wait(timeout=5), -signal.SIGTERM)
                released = tool.lock_keeper(directory); self.assertIsNotNone(released); released.close()
                self.assertIn('unavailable', tool.status(directory))
            finally:
                lock.close()
                if child is not None:
                    if child.poll() is None: child.kill(); child.wait(timeout=5)
                    child.stderr.close()

    def test_locked_legacy_identity_is_not_reported_as_ready(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp); lock = tool.lock_keeper(directory)
            try:
                with mock.patch.object(tool, 'status_snapshot', return_value=dict(pid=os.getpid())), mock.patch.object(tool.subprocess, 'Popen') as spawn:
                    with self.assertRaisesRegex(RuntimeError, 'process name/status unverified'):
                        tool.ensure(directory)
                    spawn.assert_not_called()
            finally:
                lock.close()


class RuntimeTests(unittest.TestCase):
    def test_private_per_user_path_ignores_checkout_and_bad_temp(self):
        with tempfile.TemporaryDirectory() as temp, mock.patch.dict(os.environ, HOME=temp, TMPDIR=temp):
            expected = Path(temp) / ('brotracker-' + str(os.getuid()))
            self.assertEqual(tool.runtime_directory(), expected)
            tool.prepare_directory(expected)
            self.assertEqual(stat.S_IMODE(expected.stat().st_mode), 0o700)
            expected.chmod(0o755)
            with self.assertRaisesRegex(RuntimeError, 'Unsafe'):
                tool.prepare_directory(expected)
            with mock.patch.dict(os.environ, TMPDIR='relative'):
                self.assertEqual(tool.runtime_directory().parent, Path('/tmp'))

    def test_legacy_migration_preserves_logs_state_and_project(self):
        with tempfile.TemporaryDirectory() as temp, mock.patch.dict(os.environ, HOME=temp, TMPDIR=temp):
            legacy = Path(temp) / 'BroTracker'
            legacy.mkdir(); (legacy / '.git').mkdir()
            (legacy / 'config.ini').write_text('unchanged')
            files = {'midi-connect.log': 'original log', 'midi-connect.log.1': 'older log',
                     'midi-connect-status.json': 'old state', 'midi-connect.lock': '',
                     'btx-midi-health.state': 'health', 'btx-midi-health.lock': '', 'btx-midi-health.next.123': 'partial'}
            for name, value in files.items(): (legacy / name).write_text(value)
            directory = tool.runtime_directory(); tool.prepare_directory(directory)
            with contextlib.redirect_stdout(io.StringIO()): tool.migrate_legacy(directory)
            archives = list(directory.glob('legacy-*')); self.assertEqual(len(archives), 1)
            for name, value in files.items():
                self.assertFalse((legacy / name).exists())
                self.assertEqual((archives[0] / name).read_text(), value)
            tool.migrate_legacy(directory)
            self.assertEqual(len(list(directory.glob('legacy-*'))), 1)
            self.assertEqual((legacy / 'config.ini').read_text(), 'unchanged')
            self.assertTrue((legacy / '.git').is_dir())

    def test_migration_only_cli_never_starts_keeper_or_routes(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); legacy = root / 'BroTracker'; legacy.mkdir()
            (legacy / 'midi-connect.log').write_text('preserve me')
            env = dict(os.environ, HOME=temp, TMPDIR=temp)
            result = subprocess.run([sys.executable, '-B', str(HERE / 'midi-connect.py'), '--migrate-legacy'],
                                    env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)
            directory = root / ('brotracker-' + str(os.getuid()))
            self.assertEqual(list(legacy.iterdir()), [])
            archive = list(directory.glob('legacy-*'))[0]
            self.assertEqual((archive / 'midi-connect.log').read_text(), 'preserve me')
            self.assertFalse((directory / 'midi-connect-status.json').exists())
            self.assertFalse((directory / 'brotracker-midi-connect.log').exists())
            lock = tool.lock_keeper(directory); self.assertIsNotNone(lock); lock.close()

    def test_active_legacy_owner_or_symlink_blocks_migration(self):
        with tempfile.TemporaryDirectory() as temp, mock.patch.dict(os.environ, HOME=temp, TMPDIR=temp):
            legacy = Path(temp) / 'BroTracker'; legacy.mkdir()
            directory = tool.runtime_directory(); tool.prepare_directory(directory)
            for name in ('midi-connect.lock', 'btx-midi-health.lock'):
                with (legacy / name).open('w') as lock:
                    tool.fcntl.flock(lock, tool.fcntl.LOCK_EX | tool.fcntl.LOCK_NB)
                    with self.assertRaisesRegex(RuntimeError, 'still active'):
                        tool.migrate_legacy(directory)
                    self.assertTrue((legacy / name).exists())
                    with mock.patch.object(tool.subprocess, 'Popen') as spawn:
                        with self.assertRaisesRegex(RuntimeError, 'still active'):
                            tool.ensure(directory)
                        spawn.assert_not_called()
                    released = tool.lock_keeper(directory)
                    self.assertIsNotNone(released); released.close()
            (legacy / 'midi-connect.log').symlink_to(legacy / 'midi-connect.lock')
            with self.assertRaisesRegex(RuntimeError, 'Unsafe'):
                tool.migrate_legacy(directory)
            self.assertFalse(list(directory.glob('legacy-*')))


class ScreenTests(unittest.TestCase):
    def display(self, value, color=False, health=None):
        with mock.patch.object(tool, 'status_snapshot', return_value=value), mock.patch.object(
                tool, 'btx_health', return_value=health or {'state': 'unknown'}):
            return tool.screen(Path('/unused'), color)

    def test_route_usb_error_and_health_are_independent(self):
        value = dict(state='connected', subscription=True, identities=[{}, {}],
                     usb_error='usb 1-1.2: urb status -32', visibility='kernel log observed', detail='')
        shown = self.display(value, True, dict(state='healthy', cleanup=False))
        self.assertIn('\033[1;32mPRESENT\033[0m', shown)
        self.assertIn('\033[1;31mERROR -32 1-1.2\033[0m', shown)
        self.assertIn('Delivery: UNVERIFIED', shown)
        self.assertIn('Live for 10s; reopen after USB swap', shown)
        plain = self.display(value)
        self.assertNotIn('\033', plain)
        self.assertNotIn('  MIDI:', plain)
        self.assertTrue(all(len(line) <= 50 for line in plain.splitlines()))

    def test_unknown_health_is_hidden_without_hiding_route_or_usb(self):
        value = dict(state='unavailable', subscription=True, identities=[{}, {}],
                     usb_error='usb 1-1.2: urb status -32', visibility='kernel log observed', detail='')
        shown = self.display(value, health=dict(state='unknown', cleanup=True))
        self.assertNotIn('  MIDI:', shown)
        self.assertIn('Route: PRESENT', shown)
        self.assertIn('Teensy: FOUND', shown)
        self.assertIn('H4MIDI: FOUND', shown)
        self.assertIn('USB: ERROR -32 1-1.2', shown)
        self.assertIn('Cleanup: PENDING / UNCERTAIN', shown)
        self.assertIn('Delivery: UNVERIFIED', shown)

    def test_available_health_and_faults_remain_visible(self):
        for state, expected, color in [('healthy', 'HEALTHY', 32),
                                       ('unavailable', 'UNAVAILABLE', 31),
                                       ('fault', 'FAULT 8', 31)]:
            with self.subTest(state=state):
                shown = self.display('unavailable', color=True,
                                     health=dict(state=state, reason=8, cleanup=state != 'healthy'))
                self.assertIn('MIDI: \033[1;{}m{}\033[0m'.format(color, expected), shown)
                if state != 'healthy':
                    self.assertIn('Cleanup: \033[1;31mPENDING / UNCERTAIN\033[0m', shown)

    def test_missing_stale_and_restarted_btx_hide_old_health(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            path = root / 'btx-midi-health.state'
            start = Path('/proc/self/stat').read_text().split(') ', 1)[1].split()[19]
            boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
            self.assertNotIn('  MIDI:', tool.screen(root))
            for observed, owner_start in [(96000, start), (100000, str(int(start) + 1))]:
                with self.subTest(observed=observed, owner_start=owner_start):
                    path.write_text('BTMIDIHEALTH 1 {} {} {} {} 1 1 1 1 1 1 8 0 stopped\n'.format(
                        os.getpid(), owner_start, boot, observed))
                    with mock.patch.object(tool.time, 'monotonic', return_value=100):
                        shown = tool.screen(root)
                    self.assertNotIn('  MIDI:', shown)
                    self.assertNotIn('FAULT 8', shown)
                    self.assertNotIn('Cleanup:', shown)

    def test_partial_discovery_never_invents_other_device(self):
        for state in ('missing', 'ambiguous'):
            value = dict(state=state, subscription=False, identities=[], usb_error='',
                         visibility='reduced: missing mapping', detail=tool.DESTINATION)
            shown = self.display(value)
            self.assertIn('H4MIDI: ' + state.upper(), shown)
            self.assertIn('Teensy: UNKNOWN', shown)
            self.assertIn('USB: REDUCED VISIBILITY', shown)
        for reason, expected in [('unavailable: keeper status stale', 'STALE'),
                                 ('unavailable: keeper exited', 'UNAVAILABLE')]:
            self.assertIn('Route: ' + expected, self.display(reason))

    def test_screen_reuses_real_keeper_age_and_process_identity(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            start = Path('/proc/self/stat').read_text().split(') ', 1)[1].split()[19]
            value = dict(pid=os.getpid(), process_start=start,
                         boot_id=Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
                         observed_monotonic=time.monotonic() - 21)
            path = root / 'midi-connect-status.json'
            path.write_text(json.dumps(value))
            self.assertIn('Route: STALE', tool.screen(root))
            value['observed_monotonic'] = time.monotonic()
            value['process_start'] = '0'
            path.write_text(json.dumps(value))
            self.assertIn('Route: STALE', tool.screen(root))
            path.unlink()
            self.assertIn('Route: UNAVAILABLE', tool.screen(root))

    def test_fault_cleanup_and_control_characters(self):
        shown = self.display('unavailable', health=dict(state='fault', reason=8, cleanup=True))
        self.assertIn('MIDI: FAULT 8', shown)
        self.assertIn('Cleanup: PENDING / UNCERTAIN', shown)

    def test_cleanup_completion_requires_valid_health(self):
        for state in ('unknown', 'unavailable', 'healthy', 'fault'):
            for pending in (False, True):
                with self.subTest(state=state, pending=pending):
                    health = dict(state=state, cleanup=pending, reason=8)
                    shown = self.display('unavailable', color=True, health=health)
                    expected = ('PENDING / UNCERTAIN' if pending else
                                'COMPLETE' if state in ('healthy', 'fault') else 'UNKNOWN')
                    color = 32 if expected == 'COMPLETE' else 31
                    self.assertIn('Cleanup: \033[1;{}m{}\033[0m'.format(color, expected), shown)
                    if expected != 'COMPLETE':
                        self.assertNotIn('COMPLETE', shown)
        for state in ('unknown', 'unavailable'):
            shown = self.display('unavailable', health=dict(state=state))
            self.assertNotIn('Cleanup:', shown)

    def test_cleanup_display_from_real_health_telemetry(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
            start = Path('/proc/self/stat').read_text().split(') ', 1)[1].split()[19]
            cases = [
                ('1 1 1 1 0 0', 0, 0, 'COMPLETE'),
                ('1 1 1 1 1 0', 8, 0, 'COMPLETE'),
                ('1 1 1 1 1 1', 8, 0, 'PENDING / UNCERTAIN'),
                ('0 0 0 0 0 0', 0, 0, 'UNKNOWN'),
                ('1 1 1 0 0 0', 0, 0, 'UNKNOWN'),
                ('0 0 0 0 0 1', 0, 0, 'PENDING / UNCERTAIN'),
                ('1 0 0 0 0 0', 0, 0, 'UNKNOWN'),
                ('1 1 0 0 0 0', 0, 0, 'UNKNOWN'),
                ('1 1 1 1 0 0', 0, 3001, 'UNKNOWN'),
            ]
            for flags, reason, age, expected in cases:
                with self.subTest(flags=flags, age=age):
                    (root / 'btx-midi-health.state').write_text(
                        'BTMIDIHEALTH 1 {} {} {} 100000 {} {} {} stopped\n'.format(
                            os.getpid(), start, boot, flags, reason, age))
                    with mock.patch.object(tool.time, 'monotonic', return_value=100):
                        shown = tool.screen(root)
                        unknown = tool.btx_health(root)['state'] == 'unknown'
                    self.assertEqual('  MIDI:' not in shown, unknown)
                    if expected is None:
                        self.assertNotIn('Cleanup:', shown)
                    else:
                        self.assertIn('Cleanup: ' + expected, shown)
                    if expected != 'COMPLETE':
                        self.assertNotIn('COMPLETE', shown)

    def test_partial_discovery_and_reduced_visibility_are_not_errors(self):
        for missing, other in ((tool.SOURCE, 'H4MIDI'), (tool.DESTINATION, 'Teensy')):
            value = dict(state='missing', subscription=False, identities=[], usb_error='',
                         visibility='reduced: ALSA identity lacks USB mapping', detail='Missing port: ' + missing)
            shown = self.display(value, True)
            self.assertIn('\033[1;31mMISSING\033[0m', shown)
            self.assertIn(other + ': \033[1;33mUNKNOWN\033[0m', shown)
            self.assertIn('USB: \033[1;33mREDUCED VISIBILITY\033[0m', shown)
            self.assertNotIn('ERROR', shown)
            plain = self.display(value)
            self.assertIn(other + ': UNKNOWN', plain)
            self.assertIn('USB: REDUCED VISIBILITY', plain)
            self.assertIn('MISSING', plain)
            self.assertNotIn('\033', plain)

    def test_visible_screen_refreshes_changes_without_repainting_unchanged_text(self):
        with mock.patch.object(tool, 'screen', side_effect=['missing', 'missing', 'connected', 'fault']), mock.patch.object(
                tool.sys.stdout, 'isatty', return_value=True), contextlib.redirect_stdout(io.StringIO()) as out:
            # redirect_stdout supplies the terminal stream used by watch_screen.
            with mock.patch.object(out, 'isatty', return_value=True), mock.patch.object(tool, 'wait_for_handheld') as wait:
                def watching(**kwargs):
                    for unused in range(4): kwargs['refresh']()
                    return True
                wait.side_effect = watching
                self.assertTrue(tool.watch_screen(Path('/unused')))
            self.assertEqual(out.getvalue(), '\033[smissing\n\033[u\033[Jconnected\n\033[u\033[Jfault\n')

    def test_button_footer_stays_below_status_when_rows_appear_and_disappear(self):
        for available in (False, True):
            with self.subTest(buttons_available=available), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                if available:
                    (root / 'event2/device').mkdir(parents=True)
                    (root / 'event2/device/name').write_text('GO-Super Gamepad')
                clock = [0.0]
                frames = ['Route: MISSING', 'Route: PRESENT\nMIDI: FAULT 8\nCleanup: PENDING',
                          'Route: PRESENT', 'Route: PRESENT']
                def select_wait(handles, write, error, timeout):
                    clock[0] += timeout
                    return [], [], []
                original_wait = tool.wait_for_handheld
                def wait(**kwargs):
                    return original_wait(seconds=2, inputs=root, **kwargs)
                with contextlib.redirect_stdout(io.StringIO()) as out, mock.patch.object(out, 'isatty', return_value=True), mock.patch.object(
                        tool, 'screen', side_effect=frames), mock.patch.object(tool, 'wait_for_handheld', side_effect=wait), mock.patch.object(
                        tool.time, 'monotonic', side_effect=lambda: clock[0]), mock.patch.object(tool.select, 'select', side_effect=select_wait), mock.patch.object(
                        tool.os, 'open', return_value=77), mock.patch.object(tool.os, 'read', side_effect=BlockingIOError), mock.patch.object(tool.os, 'close'):
                    self.assertFalse(tool.watch_screen(root))
                shown = out.getvalue()
                self.assertTrue(shown.startswith('\033[sRoute: MISSING'))
                displays = shown[len('\033[s'):].split('\033[u\033[J')
                self.assertEqual(len(displays), 3)  # Unchanged final observation is not repainted.
                footer = 'Any gamepad button to return' if available else 'Buttons:'
                for display in displays:
                    self.assertEqual(display.count(footer), 1)
                    self.assertIn(footer, display.splitlines()[-1])
                self.assertIn('MIDI: FAULT 8', displays[1])
                self.assertNotIn('MIDI:', displays[2])
                self.assertNotIn('Cleanup:', displays[2])

    def test_open_screen_uses_each_new_keeper_observation(self):
        missing = dict(state='missing', subscription=False, identities=[], usb_error='',
                       visibility='reduced: no mapping', detail='Missing port: ' + tool.SOURCE)
        connected = dict(state='connected', subscription=True, identities=[{}, {}],
                         usb_error='', visibility='kernel log observed', detail='verified subscription')
        error = dict(connected, state='unavailable', usb_error='usb 1-1.4: urb status -32')
        def watching(**kwargs):
            for unused in range(4): kwargs['refresh']()
            return False
        with mock.patch.object(tool, 'status_snapshot', side_effect=[missing, connected, error, 'unavailable: keeper status stale']), mock.patch.object(
                tool, 'btx_health', return_value={'state': 'unknown'}), mock.patch.object(tool, 'wait_for_handheld', side_effect=watching), contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertFalse(tool.watch_screen(Path('/unused')))
        shown = out.getvalue()
        self.assertLess(shown.index('Route: MISSING'), shown.index('Route: PRESENT'))
        self.assertLess(shown.index('H4MIDI: UNKNOWN'), shown.index('H4MIDI: FOUND'))
        self.assertLess(shown.index('Route: PRESENT'), shown.index('USB: ERROR -32 1-1.4'))
        self.assertIn('Route: STALE', shown)
        self.assertNotIn('  MIDI:', shown)

    def test_refresh_wait_keeps_original_deadline(self):
        clock = [100.0]
        def select_wait(handles, write, error, timeout):
            self.assertLessEqual(timeout, .5)
            clock[0] += timeout
            return [], [], []
        with tempfile.TemporaryDirectory() as temp, mock.patch.object(tool.time, 'monotonic', side_effect=lambda: clock[0]), mock.patch.object(
                tool.select, 'select', side_effect=select_wait), contextlib.redirect_stdout(io.StringIO()):
            refresh = mock.Mock()
            self.assertFalse(tool.wait_for_handheld(inputs=Path(temp), refresh=refresh))
            self.assertEqual(clock[0], 110)
            self.assertEqual(refresh.call_count, 20)

    def test_button_wait_timeout_and_missing_input(self):
        with tempfile.TemporaryDirectory() as temp, mock.patch.object(tool.time, 'monotonic',
                side_effect=[100, 100, 100, 110]), mock.patch.object(tool.select, 'select',
                return_value=([], [], [])) as wait, contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertFalse(tool.wait_for_handheld(inputs=Path(temp)))
            self.assertIn('Buttons: UNAVAILABLE', out.getvalue())
            wait.assert_called_once_with([], [], [], 10)

    def test_inaccessible_gamepad_reports_timeout_fallback(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'event2/device').mkdir(parents=True)
            (root / 'event2/device/name').write_text('GO-Super Gamepad\n')
            with mock.patch.object(tool.os, 'open', side_effect=PermissionError()), mock.patch.object(
                    tool.time, 'monotonic', side_effect=[100, 110]), contextlib.redirect_stdout(io.StringIO()) as out:
                self.assertFalse(tool.wait_for_handheld(inputs=root))
                self.assertIn('Buttons: UNAVAILABLE', out.getvalue())

    def test_native_gamepad_press_drains_launch_and_ignores_release(self):
        for name in ('retrogame_joypad', 'GO-Super Gamepad'):
            with self.subTest(name=name):
                self.check_native_gamepad(name)

    def check_native_gamepad(self, name):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'event2/device').mkdir(parents=True)
            (root / 'event2/device/name').write_text(name + '\n')
            event = tool.struct.Struct('@llHHi')
            press = event.pack(0, 0, 1, 304, 1)
            release = event.pack(0, 0, 1, 304, 0)
            with mock.patch.object(tool.os, 'open', return_value=77), mock.patch.object(
                    tool.os, 'read', side_effect=[press, BlockingIOError(), release, press]), mock.patch.object(
                    tool.os, 'close') as close, mock.patch.object(tool.select, 'select',
                    return_value=([77], [], [])), contextlib.redirect_stdout(io.StringIO()):
                refresh = mock.Mock()
                self.assertTrue(tool.wait_for_handheld(inputs=root, refresh=refresh))
                self.assertEqual(refresh.call_count, 2)
                close.assert_called_once_with(77)


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
    @contextlib.contextmanager
    def usb_fixture(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); sysfs = root/'sys'; dev = root/'dev/snd'
            (sysfs/'class/sound').mkdir(parents=True); (sysfs/'dev/char').mkdir(parents=True);dev.mkdir(parents=True)
            def add(card, path, number):
                usb = sysfs/'devices/usb1'/path; interface = usb/(path+':1.2')
                node = interface/'sound'/('card'+str(card));node.mkdir(parents=True)
                for key,value in dict(idVendor='7104',idProduct='2100',busnum='1',devnum=str(number)).items():
                    (usb/key).write_text(value)
                (node/'device').symlink_to(interface)
                (sysfs/'class/sound'/node.name).symlink_to(node)
                for name in ('controlC'+str(card),'midiC{}D0'.format(card)):
                    (node/name).touch();(sysfs/'class/sound'/name).symlink_to(node/name)
                (sysfs/'dev/char'/('116:'+str(card+5))).symlink_to(node/('controlC'+str(card)))
                (dev/('controlC'+str(card))).touch()
                return usb
            original = os.stat
            def character(path, *args, **kwargs):
                p=Path(path)
                if p.parent==dev:
                    canonical=p.resolve(strict=True);card=int(canonical.name[len('controlC'):])
                    return SimpleNamespace(st_mode=stat.S_IFCHR,st_rdev=os.makedev(116,card+5))
                return original(path,*args,**kwargs)
            with mock.patch.object(tool.os,'stat',side_effect=character):
                yield sysfs,dev,add

    def test_handheld_raw_identity_uses_real_control_ancestry_and_collapses_alias(self):
        captured="client 20: 'BroTracker USB audio' [type=kernel]\n    0 'BroTracker USB audio MIDI 1'\nclient 24: 'H4MIDI-WC' [type=kernel]\n    0 'H4MIDI-WC MIDI 1'\n"
        with self.usb_fixture() as (sysfs,dev,add):
            add(1,'1-1.3',5);add(2,'1-1.2',6)
            (dev/'controlC7').symlink_to(dev/'controlC1')
            raw='Dir Device    Name\nIO  hw:1,0,0  BroTracker USB audio MIDI 1\nIO  hw:7,0,0  BroTracker USB audio MIDI 1\nIO  hw:2,0,0  H4MIDI-WC MIDI 1\n'
            source=tool.identity(captured,'20:0',lambda:raw,sysfs,dev)
            dest=tool.identity(captured,'24:0',lambda:raw,sysfs,dev)
            self.assertEqual(source['usb']['path'],'1-1.3');self.assertEqual(dest['usb']['path'],'1-1.2')
            self.assertEqual(dest['usb']['devnum'],'6')
            # Captured after H4 arrival: the handheld retargets controlC7 to H4.
            (dev/'controlC7').unlink();(dev/'controlC7').symlink_to(dev/'controlC2')
            raw=raw.replace('hw:7,0,0  BroTracker USB audio MIDI 1','hw:7,0,0  H4MIDI-WC MIDI 1')
            raw+='IO hw:2,0,1 H4MIDI-WC MIDI 2\nIO hw:7,0,1 H4MIDI-WC MIDI 2\n'
            self.assertEqual(tool.identity(captured,'24:0',lambda:raw,sysfs,dev)['usb'],dest['usb'])
            modern=captured.replace('[type=kernel]','[type=kernel,card=2]')
            self.assertEqual(tool.identity(modern,'24:0',lambda:self.fail('unnecessary fallback'),sysfs,dev)['usb'],dest['usb'])

    def test_raw_identity_rejects_ambiguity_missing_ancestry_and_wrong_direction(self):
        captured="client 24: 'H4MIDI-WC' [type=kernel]\n    0 'H4MIDI-WC MIDI 1'\n"
        with self.usb_fixture() as (sysfs,dev,add):
            add(2,'1-1.2',6);add(3,'1-1.4',7)
            raw='IO hw:2,0,0 H4MIDI-WC MIDI 1\n'
            def resolve(text=raw): return tool.identity(captured,'24:0',lambda:text,sysfs,dev)
            self.assertNotIn('usb',resolve(raw+'IO hw:3,0,0 H4MIDI-WC MIDI 1\n'))
            self.assertNotIn('usb',resolve(raw.replace('IO','I')))
            self.assertNotIn('usb',resolve(raw+'IO hw:9,0,0 H4MIDI-WC MIDI 1\n'))
            (sysfs/'class/sound/midiC2D0').unlink()
            self.assertNotIn('usb',resolve())
            self.assertNotIn('usb',tool.identity(captured.replace('kernel','kernel,card=9'),'24:0',lambda:raw,sysfs,dev))

    def test_raw_identity_follows_reenumeration_and_changing_card_client_numbers(self):
        captured="client 24: 'H4MIDI-WC' [type=kernel]\n    0 'H4MIDI-WC MIDI 1'\n"
        with self.usb_fixture() as (sysfs,dev,add):
            add(2,'1-1.2',6)
            old=tool.identity(captured,'24:0',lambda:'IO hw:2,0,0 H4MIDI-WC MIDI 1\n',sysfs,dev)
            (sysfs/'class/sound/card2').unlink()
            self.assertNotIn('usb',tool.identity(captured,'24:0',lambda:'IO hw:2,0,0 H4MIDI-WC MIDI 1\n',sysfs,dev))
            shutil.rmtree(sysfs/'devices/usb1/1-1.2')
            add(4,'1-1.2',8)
            new=tool.identity(captured.replace('24:','36:'),'36:0',lambda:'IO hw:4,0,0 H4MIDI-WC MIDI 1\n',sysfs,dev)
            self.assertNotEqual(old['usb'],new['usb']);self.assertEqual(new['usb']['devnum'],'8')

    def test_captured_offline_h4_errors_only_after_baseline_and_for_current_incarnation(self):
        ring=['[ 655.654389] usb 1-1.3: new high-speed USB device number 5 using dwc2\n'
              '[ 831.575724] usb 1-1.4: USB disconnect, device number 4']
        errors=tool.KernelErrors(lambda:ring[0]);errors.poll(())
        ids=[{'address':'20:0','usb':{'path':'1-1.3','devnum':'5','inode':1}},
             {'address':'24:0','usb':{'path':'1-1.2','devnum':'6','inode':2}}]
        ring[0]+='\n[ 834.083375] usb 1-1.2: new full-speed USB device number 6 using dwc2'
        self.assertEqual(errors.poll(ids)[1],'')
        ring[0]+='\n[ 1109.965043] usb 1-1.2: urb status -32'
        self.assertIn('usb 1-1.2: urb status -32',errors.poll(ids)[1])
        # Client renumbering does not make historical errors new, or erase this fault.
        ids[1]['address']='36:0';self.assertIn('urb status',errors.poll(ids)[1])
        ring[0]+='\n[ 1217.878955] usb 1-1.2: USB disconnect, device number 6'
        visibility,error=errors.poll(ids);self.assertIn('rediscovery',visibility);self.assertEqual(error,'')
        ring[0]+='\n[ 1230.0] usb 1-1.2: new full-speed USB device number 8 using dwc2\n[ 1231.0] usb 1-1.2: urb status -32'
        self.assertEqual(errors.poll(ids)[1],'')  # Stale ALSA snapshot cannot label replacement errors.
        ids[1]['usb']=dict(path='1-1.2',devnum='8',inode=3)
        self.assertIn('urb status',errors.poll(ids)[1])  # Deferred error is proven to belong to device 8.
        ring[0]+='\n[ 1232.0] usb 1-1.4: urb status -32'
        self.assertEqual(errors.poll(ids)[1],'usb 1-1.2: urb status -32')  # Sibling cannot replace the H4 fault.
        ring[0]+='\n[ 1233.0] usb 1-1.2: urb status -32'
        self.assertIn('urb status',errors.poll(ids)[1])

    def test_identity_failures_and_changed_raw_port_never_guess_usb(self):
        captured="client 24: 'H4MIDI-WC' [type=kernel]\n    0 'H4MIDI-WC MIDI 1'\n"
        with self.usb_fixture() as (sysfs,dev,add):
            add(2,'1-1.2',6)
            for error in (FileNotFoundError('amidi'),subprocess.TimeoutExpired('amidi',3)):
                with mock.patch.object(tool,'raw_listing',side_effect=error):
                    self.assertNotIn('usb',tool.identity(captured,'24:0',tool.raw_listing,sysfs,dev))
            samples=iter(['IO hw:2,0,0 H4MIDI-WC MIDI 1\n','IO hw:2,0,0 Other MIDI 1\n'])
            self.assertNotIn('usb',tool.identity(captured,'24:0',lambda:next(samples),sysfs,dev))

    def test_kernel_read_failure_during_replacement_does_not_replay_old_device_error(self):
        ring=['[ 1.0] usb 1-1.2: new full-speed USB device number 6 using dwc2']
        errors=tool.KernelErrors(lambda:ring[0]);errors.poll([{'usb':dict(path='1-1.2',devnum='6')}])
        ids=[{'usb':dict(path='1-1.2',devnum='8')}]
        def denied(): raise RuntimeError('unreadable')
        errors.read=denied;self.assertIn('reduced',errors.poll(ids)[0])
        ring[0]+='\n[ 2.0] usb 1-1.2: urb status -32\n[ 3.0] usb 1-1.2: new full-speed USB device number 8 using dwc2'
        errors.read=lambda:ring[0];self.assertEqual(errors.poll(ids)[1],'')
        ring[0]+='\n[ 4.0] usb 1-1.2: urb status -32'
        self.assertIn('urb status',errors.poll(ids)[1])

    def test_deferred_error_survives_missing_alsa_identity_and_ring_rollover(self):
        ring=['[ 1.0] usb 1-1.2: new full-speed USB device number 6 using dwc2']
        errors=tool.KernelErrors(lambda:ring[0]);old=[{'usb':dict(path='1-1.2',devnum='6',inode=1)}]
        errors.poll(old)
        ring[0]+='\n[ 2.0] usb 1-1.2: USB disconnect, device number 6\n[ 3.0] usb 1-1.2: new full-speed USB device number 8 using dwc2\n[ 4.0] usb 1-1.2: urb status -32'
        self.assertEqual(errors.poll(old)[1],'')
        errors.poll(())  # ALSA disappears while sysfs/port discovery catches up.
        ring[0]='[ 5.0] unrelated ring data after the lifecycle records scrolled out'
        visibility,error=errors.poll([{'usb':dict(path='1-1.2',devnum='8',inode=2)}])
        self.assertIn('urb status -32',error);self.assertNotIn('reduced',visibility)

    def test_missing_or_ambiguous_lifecycle_keeps_deferred_error_explicitly_uncertain(self):
        for lifecycle in ('', '[ 3.0] usb 1-1.2: new full-speed USB device number 8 using dwc2\n[ 3.0] usb 1-1.2: new full-speed USB device number 9 using dwc2\n'):
            with self.subTest(lifecycle=lifecycle):
                ring=['[ 1.0] initial baseline']
                errors=tool.KernelErrors(lambda:ring[0]);errors.poll(())
                ring[0]+='\n'+lifecycle+'[ 4.0] usb 1-1.2: urb status -32'
                errors.poll(())
                ids=[{'usb':dict(path='1-1.2',devnum='8',inode=2)}]
                visibility,error=errors.poll(ids)
                self.assertEqual(error,'');self.assertIn('reduced',visibility)
                visibility,error=errors.poll(ids)
                self.assertEqual(error,'');self.assertIn('reduced',visibility)

    def test_old_error_cannot_transfer_to_another_replacement_and_buffers_are_bounded(self):
        ring=['[ 1.0] usb 1-1.2: new full-speed USB device number 6 using dwc2']
        errors=tool.KernelErrors(lambda:ring[0]);errors.poll(())
        ring[0]+='\n[ 2.0] usb 1-1.2: urb status -32\n[ 3.0] usb 1-1.2: USB disconnect, device number 6\n[ 4.0] usb 1-1.2: new full-speed USB device number 8 using dwc2'
        errors.poll(())
        self.assertEqual(errors.poll([{'usb':dict(path='1-1.2',devnum='8')}])[1],'')
        ring[0]+=''.join('\n[ {}.0] usb 1-1.2: urb status -32'.format(i) for i in range(5,150))
        errors.poll(())
        self.assertLessEqual(len(errors.pending),128)
        self.assertIn('reduced',errors.poll([{'usb':dict(path='1-1.2',devnum='8')}])[0])

    def test_late_lifecycle_evidence_can_resolve_previously_uncertain_error(self):
        ring=['[ 1.0] usb 1-1.2: new full-speed USB device number 6 using dwc2']
        errors=tool.KernelErrors(lambda:ring[0]);errors.poll(())
        ring[0]+='\n[ 4.0] usb 1-1.2: urb status -32'
        ids=[{'usb':dict(path='1-1.2',devnum='8',inode=2)}]
        visibility,error=errors.poll(ids);self.assertIn('reduced',visibility);self.assertEqual(error,'')
        ring[0]=ring[0].replace('[ 4.0]', '[ 2.0] usb 1-1.2: USB disconnect, device number 6\n[ 3.0] usb 1-1.2: new full-speed USB device number 8 using dwc2\n[ 4.0]')
        visibility,error=errors.poll(ids);self.assertIn('urb status -32',error);self.assertNotIn('reduced',visibility)

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

    def test_independent_devices_when_either_or_both_are_absent(self):
        for absent in ({tool.SOURCE}, {tool.DESTINATION}, {tool.SOURCE, tool.DESTINATION}):
            with self.subTest(absent=absent):
                calls = []
                def run(*args):
                    calls.append(args)
                    self.assertTrue(args[0].startswith('-'))
                    text = listing()
                    for name in absent: text = text.replace(name, 'absent')
                    return text
                events = []
                value = self.keeper(run, events=events).step(0)
                for label, name in (('Teensy', tool.SOURCE), ('H4MIDI', tool.DESTINATION)):
                    expected = 'missing' if name in absent else 'found'
                    self.assertEqual(value['devices'][label]['state'], expected)
                    with mock.patch.object(tool, 'status_snapshot', return_value=value), mock.patch.object(tool, 'btx_health', return_value={'state': 'unknown'}):
                        self.assertIn(label + ': ' + expected.upper(), tool.screen(Path('/unused')))
                self.assertFalse(value['subscription'])
                self.assertEqual(value['delivery'], 'unverified')
                self.assertEqual(events[-1]['devices'], value['devices'])
                self.assertEqual(calls, [('-i',), ('-o',), ('-l',)])

    def test_independent_h4_ambiguity_wrong_client_and_incomplete_usb_visibility(self):
        for problem, expected in (('duplicate', 'ambiguous'), ('software', 'unknown'), ('no-usb', 'found'), ('wrong-usb', 'unknown')):
            with self.subTest(problem=problem):
                text = listing().replace(tool.SOURCE, 'absent')
                if problem == 'duplicate': text += "client 70: 'H4MIDI-WC' [type=kernel]\n    0 'H4MIDI-WC MIDI 1'\n"
                if problem == 'software': text = text.replace("'H4MIDI-WC' [type=kernel,card=1]", "'H4MIDI-WC' [type=user,pid=1]")
                def run(*args):
                    self.assertTrue(args[0].startswith('-'))
                    return text
                keeper = self.keeper(run)
                if problem == 'wrong-usb': keeper.identify = lambda listing, address: dict(address=address, usb=dict(idVendor='0000', idProduct='0000'))
                value = keeper.step(0)
                self.assertEqual(value['devices']['H4MIDI']['state'], expected)
                self.assertFalse(value['subscription'])
                if problem == 'no-usb':
                    self.assertIn('USB mapping unavailable', value['devices']['H4MIDI']['detail'])
                    self.assertIn('reduced', value['visibility'])
                if problem == 'wrong-usb': self.assertIn('does not match', value['devices']['H4MIDI']['detail'])

    def test_directional_port_disappearing_before_combined_listing_is_unknown(self):
        def run(*args):
            self.assertTrue(args[0].startswith('-'))
            return listing().replace(tool.DESTINATION, 'absent') if args == ('-l',) else listing()
        value = self.keeper(run).step(0)
        self.assertEqual(value['devices']['H4MIDI']['state'], 'unknown')
        self.assertIn('could not verify directional port', value['devices']['H4MIDI']['detail'])
        self.assertEqual(value['devices']['Teensy']['state'], 'found')
        self.assertFalse(value['subscription'])

    def test_verified_h4_usb_without_teensy_and_unknown_observation(self):
        text = listing().replace(tool.SOURCE, 'absent')
        keeper = self.keeper(lambda *args: text)
        keeper.identify = lambda listing, address: dict(address=address, usb=dict(
            path='1-1.3', devnum='4', idVendor='7104', idProduct='2100'))
        value = keeper.step(0)
        self.assertEqual(value['devices']['H4MIDI']['state'], 'found')
        self.assertEqual(value['identities'][0]['usb']['devnum'], '4')
        self.assertFalse(value['subscription'])
        def failed(*args): raise RuntimeError('ALSA observation failed')
        keeper.run = failed
        value = keeper.step(2)
        self.assertEqual(value['devices']['H4MIDI']['state'], 'unknown')
        self.assertEqual(value['identities'], [])

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
        ring = ['[ 0.5] usb 1-1.1: new full-speed USB device number 4 using dwc2\n[ 1.0] usb 1-1.1: urb status -32']
        errors = tool.KernelErrors(lambda: ring[0])
        peer = Peer(); keeper = self.keeper(peer, errors)
        keeper.identify = lambda listing, address: {'address': address, 'usb': dict(path='1-1.1', devnum='4',
            idVendor='16c0' if address == '28:0' else '7104', idProduct='048a' if address == '28:0' else '2100')}
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
        self.assertEqual(errors.poll(current)[1], '')  # Existing ring is history.
        ring += '\n[ 3.5] usb 1-1.1: urb status -32'
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
            env = dict(os.environ, HOME=temp, TMPDIR=temp, PATH=str(root / 'bin') + ':' + os.environ['PATH'])
            directory = root / ('brotracker-' + str(os.getuid()))
            directory.mkdir(mode=0o700)
            (directory / 'brotracker-midi-connect.log').write_text('x' * 131100)
            pid = None
            try:
                commands = [[sys.executable, '-B', str(HERE / 'midi-connect.py')]] * 2
                children = [subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE) for cmd in commands]
                for child in children:
                    out, err = child.communicate(timeout=8); self.assertEqual(child.returncode, 0, err)
                directory = root / ('brotracker-' + str(os.getuid()))
                value = json.loads((directory / 'midi-connect-status.json').read_text()); pid = value['pid']
                self.assertEqual(value['teensy']['state'], 'unknown')
                self.assertEqual(value['process_name'], tool.KEEPER_NAME)
                self.assertEqual(Path('/proc/{}/comm'.format(pid)).read_text().strip(), tool.KEEPER_NAME)
                self.assertTrue((directory / 'midi-connect-status.btx').read_text().startswith('BTMIDIROUTE 1 '))
                again = subprocess.run(commands[0], env=env, stdout=subprocess.PIPE, timeout=5)
                self.assertIn(str(pid), again.stdout.decode())
                self.assertIsNone(tool.lock_keeper(directory))
                self.assertEqual(value['state'], 'missing')
                self.assertTrue((directory / 'brotracker-midi-connect.log').exists())
                self.assertEqual((directory / 'brotracker-midi-connect(1).log').read_text(), 'x' * 131100)
                self.assertFalse((root / 'BroTracker').exists())
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
        self.check_ports_feedback(0)

    def test_failed_ensure_remains_prominent_with_existing_snapshot(self):
        self.check_ports_feedback(42)
        self.check_ports_feedback(42, no_color=True)

    def check_ports_feedback(self, failure, no_color=False):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'ports').mkdir()
            (root / 'bin').mkdir()
            launcher = root / 'ports' / 'BroTracker MIDI Connect.sh'
            shutil.copyfile(HERE / 'port' / launcher.name, launcher)
            (root / 'ports/brotracker').mkdir()
            shutil.copyfile(HERE / 'midi-connect.py', root / 'ports/brotracker/midi-connect.py')
            directory = root / ('brotracker-' + str(os.getuid()))
            directory.mkdir(mode=0o700)
            (directory / 'brotracker-midi-connect-launch.log').write_text('previous launch evidence')
            (directory / 'midi-connect-status.json').write_text(json.dumps(dict(
                pid=os.getpid(), process_start=Path('/proc/self/stat').read_text().split(') ', 1)[1].split()[19],
                boot_id=Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
                observed_monotonic=time.monotonic(), state='connected', subscription=True,
                identities=[{}, {}], usb_error='', visibility='kernel log observed', detail='verified subscription')))
            for name, script in [('python3', '#!/bin/sh\ncase "$*" in\n*--screen*) "$TEST_PYTHON" -B "$2" --screen; echo "Buttons: fixture";;\n*--wait-buttons*) echo "Buttons: fixture";;\n*) echo "VERBOSE diagnostic fixture"; exit "$TEST_FAILURE";;\nesac\nexit 0\n'),
                                 ('sleep', '#!/bin/sh\nexit 0\n')]:
                path = root / 'bin' / name
                path.write_text(script)
                path.chmod(0o755)
            master, slave = pty.openpty()
            env = dict(os.environ, HOME=temp, TMPDIR=temp, PATH=str(root / 'bin') + ':' + os.environ['PATH'],
                       TEST_PYTHON=sys.executable, TEST_FAILURE=str(failure))
            if no_color:
                env['NO_COLOR'] = '1'
            else:
                env.pop('NO_COLOR', None)
            try:
                result = subprocess.run(['bash', str(launcher)], stdout=slave, stderr=slave,
                                        env=env,
                                        timeout=5)
                os.close(slave)
                slave = None
                shown = os.read(master, 4096).decode()
            finally:
                os.close(master)
                if slave is not None:
                    os.close(slave)
            self.assertEqual(result.returncode, failure)
            self.assertIn('PRESENT', shown)
            if failure:
                self.assertIn('Launch: FAILED (exit 42)', shown)
                self.assertIn('brotracker-midi-connect-launch.log', shown)
                self.assertLess(shown.index('Launch: FAILED'), shown.index('BroTracker MIDI Connect'))
                if not no_color:
                    self.assertIn('\033[1;31m  Launch: FAILED', shown)
            else:
                self.assertNotIn('Launch: FAILED', shown)
            self.assertIn('Buttons: fixture', shown)
            self.assertIn(str(directory), shown)
            self.assertEqual((directory / 'brotracker-midi-connect-launch(1).log').read_text(), 'previous launch evidence')
            self.assertNotIn('VERBOSE', shown)
            self.assertTrue(shown.endswith('\033[0m'))
            logged = (directory / 'brotracker-midi-connect-launch.log').read_text()
            self.assertIn('VERBOSE diagnostic fixture', logged)
            self.assertNotIn('\033', logged)
            self.assertIn('Ports cleanup may stop the keeper', logged)


if __name__ == '__main__':
    unittest.main(verbosity=2)

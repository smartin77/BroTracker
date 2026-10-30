#!/usr/bin/env python3
"""Isolated fixtures only: no ALSA devices, MIDI traffic or real console access."""
import contextlib
import importlib.util
import io
import os
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
            self.assertIn('OK: fixture feedback', (root / 'BroTracker' / 'midi-connect.log').read_text())


if __name__ == '__main__':
    unittest.main(verbosity=2)

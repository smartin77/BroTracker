import importlib.util
import io
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("device_storage", Path(__file__).with_name("device_storage.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class Connection:
    instances = []
    result = "ok"
    state = "IDLE"
    audio_ack = "BTAUDIO1 SELECTED 1 bridge"
    def __init__(self, *args, **kwargs):
        self.pending = bytearray()
        self.commands = []
        self.closed = False
        self.instances.append(self)
    def __enter__(self):
        return self
    def __exit__(self, *args):
        self.closed = True
    def write(self, raw):
        line = raw.decode().strip()
        self.commands.append(line)
        if line == "BTTEST1 HELLO":
            response = "BTTEST1 STATE IDLE\nBTSTORE1 CAP 1\nBTPUBLISH1 NEXT 1 1\n"
        elif line.startswith("BTAUDIO1 SET"):
            response = self.audio_ack + "\n"
        elif line == "BTTEST1 STATUS":
            response = f"BTTEST1 STATE {self.state}\nBTPUBLISH1 NEXT 1 1\n"
        else:
            operation = "-".join(line.split()[1:2] + line.split()[3:]).lower()
            response = f"BTSTORE1 SUBMITTED 1 {operation} 1\nBTSTORE1 RESULT 1 {operation} {self.result} 1\n"
        self.pending.extend(response.encode())
        return len(raw)
    def read(self, count):
        result = bytes(self.pending[:count])
        del self.pending[:count]
        return result

class ToolTests(unittest.TestCase):
    def setUp(self):
        Connection.instances = []
        Connection.state = "IDLE"
        Connection.result = "ok"
        Connection.audio_ack = "BTAUDIO1 SELECTED 1 bridge"
    def invoke(self, *args):
        serial = types.ModuleType("serial")
        serial.Serial = Connection
        tools = types.ModuleType("serial.tools")
        tools.list_ports = types.SimpleNamespace(comports=lambda: [])
        with patch.dict(sys.modules, {"serial": serial, "serial.tools": tools}), patch.object(sys, "argv", ["device_storage.py", *args]), patch("sys.stdout", new=io.StringIO()):
            return module.main()
    def test_each_explicit_operation_closes_without_transport_start(self):
        for operation, wire in module.COMMANDS.items():
            self.assertEqual(self.invoke(operation, "--port", "fixture", "--audio-output", "bridge"), 0)
            connection = Connection.instances[-1]
            self.assertTrue(connection.closed)
            self.assertEqual(connection.commands, ["BTTEST1 HELLO", "BTAUDIO1 SET 1 bridge", "BTTEST1 STATUS", wire])
    def test_device_rejections_are_not_retried(self):
        for result in ("missing", "exists", "busy-or-not-ready", "invalid", "recovery-required", "io"):
            Connection.result = result
            self.assertEqual(self.invoke("restore", "--port", "fixture"), 1)
            self.assertEqual(Connection.instances[-1].commands.count(module.COMMANDS["restore"]), 1)
            self.assertTrue(Connection.instances[-1].closed)
    def test_matching_routes_and_default_resolution(self):
        for requested, resolved in (("bridge", "bridge"), ("pt8211", "pt8211"),
                                    ("default", "bridge"), ("default", "pt8211")):
            with self.subTest(requested=requested, resolved=resolved):
                Connection.audio_ack = f"BTAUDIO1 SELECTED 1 {resolved}"
                self.assertEqual(self.invoke("save", "--port", "fixture", "--audio-output", requested), 0)
                self.assertEqual(Connection.instances[-1].commands,
                                 ["BTTEST1 HELLO", f"BTAUDIO1 SET 1 {requested}",
                                  "BTTEST1 STATUS", "BTSTORE1 SAVE 1"])
                self.assertTrue(Connection.instances[-1].closed)
    def test_bad_audio_ack_closes_connection_and_log_without_storage_or_retry(self):
        cases = [("bridge", "BTAUDIO1 SELECTED 1 pt8211"),
                 ("pt8211", "BTAUDIO1 SELECTED 1 bridge")]
        malformed = ("BTAUDIO1 SELECTED", "BTAUDIO1 SELECTED 1", "BTAUDIO1 SELECTED 2 bridge",
                     "BTAUDIO1 SELECTED 1 bridge extra", "BTAUDIO1 SELECTED 1 bridge ",
                     "BTAUDIO1 SELECTED  1 bridge", "BTAUDIO1 SELECTED 1\tbridge",
                     "BTAUDIO1 SELECTED 1 default", "BTAUDIO1 SELECTED 1 unknown",
                     "BTAUDIO1 SELECTED 1 BRIDGE", "BTAUDIO1 SELECTEDgarbage",
                     "BTAUDIO1 ERROR 1 restart-required", "BTAUDIO1 UNKNOWN 1 bridge")
        cases.extend((request, ack) for request in ("bridge", "pt8211", "default") for ack in malformed)
        for requested, ack in cases:
            with self.subTest(requested=requested, ack=ack):
                Connection.audio_ack = ack
                log = io.StringIO()
                with patch.object(Path, "open", return_value=log):
                    with self.assertRaisesRegex(RuntimeError, "invalid audio acknowledgment"):
                        self.invoke("save", "--port", "fixture", "--audio-output", requested,
                                    "--log", "fixture.log")
                self.assertEqual(Connection.instances[-1].commands,
                                 ["BTTEST1 HELLO", f"BTAUDIO1 SET 1 {requested}"])
                self.assertTrue(Connection.instances[-1].closed)
                self.assertTrue(log.closed)
    def test_paused_and_playing_do_not_issue_storage_or_stop(self):
        for state in ("PAUSED", "PLAYING"):
            Connection.state = state
            with self.assertRaises(RuntimeError):
                self.invoke("save", "--port", "fixture")
            self.assertEqual(Connection.instances[-1].commands, ["BTTEST1 HELLO", "BTTEST1 STATUS"])
            self.assertTrue(Connection.instances[-1].closed)
    def test_list_does_not_open(self):
        self.assertEqual(self.invoke("--list"), 0)
        self.assertFalse(Connection.instances)
    def test_result_shape(self):
        self.assertTrue(module.successful_result("BTSTORE1 RESULT 1 save backup-retained 0"))
        for line in ("BTSTORE1 RESULT 2 save ok 0", "BTSTORE1 RESULT 1 restore ok", "BTSTORE1 RESULT 1 save ok nope"):
            self.assertFalse(module.successful_result(line))

if __name__ == "__main__":
    unittest.main()

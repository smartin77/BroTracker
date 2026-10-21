"""Bounded opt-in serial transport check; no recording, bridge diagnostics or listening.
Run with PlatformIO's Python (pyserial), after upload and with BTX closed.
"""
import argparse
import json
from pathlib import Path
import time
import serial
from serial.tools import list_ports

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--hardware", action="store_true", required=True)
parser.add_argument("--output", type=Path, default=Path("build/synth-foundation"))
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
ports = [p for p in list_ports.comports() if p.vid == 0x16C0 and p.pid == 0x048A]
assert len(ports) == 1, "Expected one Teensy USB MIDI/audio/serial device: " + repr(ports)
port = ports[0]
transcript = []
report = {"port": port.device, "serial": port.serial_number, "hardware_id": port.hwid}
connection = serial.Serial(port.device, 115200, timeout=0.05, write_timeout=1)

def send(command):
    transcript.append("TX " + command)
    connection.write((command + "\n").encode("ascii"))
    connection.flush()

def collect(seconds):
    lines = []
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        raw = connection.readline()
        if raw:
            line = raw.decode("ascii", errors="replace").strip()
            lines.append(line)
            transcript.append("RX " + line)
    return lines

started = False
try:
    collect(0.3)
    send("BTTEST1 HELLO")
    startup = collect(1.5)
    assert "BTTEST1 STATE IDLE" in startup, startup
    assert "BTAUDIO1 CAP 1" in startup
    report["startup"] = "IDLE; audio, transport, snapshot capabilities advertised"
    send("BTAUDIO1 SET 1 default")
    route = collect(0.7)
    assert "BTAUDIO1 SELECTED 1 bridge" in route, route
    report["route"] = "default negotiated as bridge (USB_BRIDGE)"
    send("BTTEST1 START")
    started = True
    playing = collect(3.0)
    assert "BTTEST1 STARTED" in playing, playing
    ticks = [int(line.split()[5]) for line in playing if line.startswith("BTPATTERN1 POS 2 1 1 ")]
    assert len(ticks) >= 10 and ticks[-1] > ticks[0], ticks
    assert all(a <= b for a, b in zip(ticks, ticks[1:])), ticks
    assert not any("ERROR" in line for line in playing), playing
    report["playback"] = {"ack": "STARTED", "telemetry_count": len(ticks),
                          "first_tick": ticks[0], "last_tick": ticks[-1], "errors": 0}
    send("BTTEST1 STOP")
    stopped = collect(0.6)
    assert "BTTEST1 STOPPED" in stopped, stopped
    started = False
    send("BTTEST1 STATUS")
    status = collect(0.5)
    assert "BTTEST1 STATE IDLE" in status, status
    assert any(line.startswith("BTPATTERN1 POS 2 0 0 0 0 0 32 8") for line in status), status
    report["final_state"] = "STOPPED/IDLE; invalid zeroed position; 32 rows, 8 channels"
    report["checks"] = "PASS (automated transport only; no listening/UX approval)"
finally:
    if started:
        send("BTTEST1 STOP")
        collect(0.6)
    connection.close()
    report["serial_handle_closed"] = not connection.is_open
    (args.output / "hardware.log").write_text("\n".join(transcript) + "\n")
    (args.output / "hardware.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))

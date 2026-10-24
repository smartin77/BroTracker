"""Explicit Teensy SD fixture/session operations. Never STARTs or edits host config.
Close BTX/monitors first. --list does not open a port. Requires pyserial.
"""
import argparse
import sys
import time
from pathlib import Path

COMMANDS = {
    **{f"seed-{n}": f"BTSTORE1 SEED 1 {n}" for n in range(1, 5)},
    **{f"migrate-{n}": f"BTSTORE1 MIGRATE 1 {n}" for n in range(1, 5)},
    "load": None, "replace-3": "BTSTORE1 REPLACE 1 3",
    "save": "BTSTORE1 SAVE 1", "restore": "BTSTORE1 RESTORE 1",
}


def valid_stem(stem):
    if stem == "none":
        return True
    base = stem[:-6] if stem.endswith(".btdev") else stem
    return (1 <= len(base) <= 24 and all(c in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-" for c in base))


def successful_result(line, expected_operation=None):
    fields = line.split()
    return (len(fields) == 6 and fields[:3] == ["BTSTORE1", "RESULT", "1"]
            and (expected_operation is None or fields[3] == expected_operation)
            and fields[4] in ("ok", "backup-retained") and fields[5].isdigit())


def valid_audio_selection(requested, line):
    fields = line.split(" ")
    return (len(fields) == 4 and fields[:3] == ["BTAUDIO1", "SELECTED", "1"]
            and fields[3] in ("bridge", "pt8211")
            and (requested == "default" or fields[3] == requested))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", nargs="?", choices=COMMANDS)
    parser.add_argument("--list", action="store_true", help="List serial candidates without opening them")
    parser.add_argument("--stem", help="LOAD filename stem: 1..24 ASCII letters/digits/underscore/hyphen, optional .btdev; exact none=clean125")
    parser.add_argument("--port", help="Explicit discovered Teensy CDC port; never assume a COM number")
    parser.add_argument("--audio-output", choices=("default", "bridge", "pt8211"),
                        help="Explicitly activate the chosen production route if not already initialized; no playback")
    parser.add_argument("--log", type=Path, help="Optional local evidence transcript (exclusive creation)")
    args = parser.parse_args()
    if args.operation == "load":
        if not args.stem or not valid_stem(args.stem):
            parser.error("load requires --stem: 1..24 ASCII letters/digits/underscore/hyphen with optional .btdev; no paths/quotes")
        stem = args.stem
        command, expected_operation = f"BTSTORE1 LOAD 1 {stem}", f"load-{stem}"
    else:
        if args.stem is not None:
            parser.error("--stem is only valid with load")
        command, expected_operation = COMMANDS.get(args.operation), args.operation
    import serial
    from serial.tools import list_ports
    if args.list:
        for port in list_ports.comports():
            print(port.device, port.description, port.hwid)
        return 0
    if not args.operation or not args.port:
        parser.error("operation and --port required; use --list to discover, and close BTX first")
    # Refuse to overwrite evidence; reserve output before touching a device.
    log = args.log.open("x", encoding="utf-8") if args.log else None
    started = time.monotonic()
    def record(direction, line):
        text = f"{time.monotonic()-started:.3f} {direction} {line}"
        print(text)
        if log:
            log.write(text + "\n")
            log.flush()
    try:
        with serial.Serial(args.port, 115200, timeout=.05, write_timeout=1, exclusive=True) as connection:
            def send(line):
                record("TX", line)
                raw = (line + "\n").encode("ascii")
                if connection.write(raw) != len(raw):
                    raise RuntimeError("short serial write; operation outcome may be unknown; inspect before retry")
            def receive_until(predicate, seconds=8):
                deadline = time.monotonic() + seconds
                pending = bytearray()
                count = 0
                while time.monotonic() < deadline and count < 4096:
                    value = connection.read(1)
                    if not value:
                        continue
                    if value == b"\n":
                        line = pending.decode("ascii", errors="replace").rstrip("\r")
                        pending.clear()
                        count += 1
                        record("RX", line)
                        if predicate(line):
                            return line
                    else:
                        pending.extend(value)
                        if len(pending) > 512:
                            raise RuntimeError("oversized device line")
                raise RuntimeError("bounded response timeout; outcome unknown, no automatic retry")
            send("BTTEST1 HELLO")
            receive_until(lambda s: s == "BTSTORE1 CAP 1")
            if args.audio_output:
                send(f"BTAUDIO1 SET 1 {args.audio_output}")
                line = receive_until(lambda s: s.startswith("BTAUDIO1"))
                if not valid_audio_selection(args.audio_output, line):
                    raise RuntimeError(f"invalid audio acknowledgment for {args.audio_output}: {line}")
            send("BTTEST1 STATUS")
            state = receive_until(lambda s: s.startswith("BTTEST1 STATE "))
            if state != "BTTEST1 STATE IDLE":
                raise RuntimeError("fully stopped transport required; STOP explicitly in BTX, then close it")
            # Finish outstanding discovery lines before the one explicit operation.
            receive_until(lambda s: s.startswith("BTPUBLISH1 NEXT 1 "))
            send(command)
            result = receive_until(lambda s: s.startswith("BTSTORE1 RESULT 1 "), seconds=15)
            return 0 if successful_result(result, expected_operation) else 1
    finally:
        if log:
            log.close()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as error:
        print(f"storage operation failed: {error}", file=sys.stderr)
        sys.exit(1)

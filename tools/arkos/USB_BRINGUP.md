# BroTracker Terminal (BTX): ArkOS / Teensy WAV control test

This is a disposable `BTTEST1` USB CDC bring-up exchange, not BroTracker's
final communication protocol. Keep `USB_MIDI_AUDIO_SERIAL` selected.
No audio is transported by these commands.

BroTracker Terminal uses **BTX** as communication shorthand; the temporary
protocol on the wire remains `BTTEST1`. Its ArkOS executable is
`BroTrackerTerminal`.

The Linux terminal discovers USB VID:PID `16c0:048a` through sysfs and opens the
associated `/dev/ttyACM*` at 115200 with DTR asserted. It retries once a second;
permissions/open errors, transmitted commands, received lines, completion and
timeouts are flushed to `/tmp/brotracker-arkos.log`. The console user must
have read/write permission for the tty. The Windows terminal uses the same
BTTEST1 protocol/state logic with identity-based COM discovery and a Windows
transport. See [Windows terminal instructions](../windows/README.md) for its
keyboard controls and log location.

- Firmware boots in IDLE with both players stopped, including queued streams.
  Playback begins only after BTTEST1 START; there is no UI-detection delay or
  automatic fallback. Early commands survive clock synchronization via the
  bounded startup serial handoff.
- HELLO/STATUS report IDLE after boot. Connecting alone does not start playback.
- ArkOS uses mapped SDL GameController buttons from the opened controller:
  **L1 / B** requests START when ready/stopped/finished and RESTART while playing.
  START safely resets the sequence to Test1. START while waiting for the handshake
  is ignored; duplicate pending START/RESTART requests are suppressed.
- **R1 / X** requests STOP while playback is active or START is queued, waits for
  acknowledgement, and stays in the application. A subsequent R1 / X when stopped
  exits. R1 / X also exits from waiting, ready or naturally finished states without
  an unnecessary STOP. Other controller buttons, keyboard events (including volume
  keys), raw joystick events and trigger axes are ignored for these actions.
- During a pending STOP, further button actions are ignored. SDL_QUIT can upgrade
  that pending STOP to STOP-then-exit; otherwise it safely stops active/queued
  playback before exiting. The UI remains responsive while awaiting replies.
- Rapid START then STOP presses preserve START followed by STOP, even while STATUS
  is outstanding. STOP does not replace an unsent START. The existing shutdown
  deadline still applies; a slow/missing reply is reported as unconfirmed.
- A disconnect returns to discovery. Commands are never replayed automatically.
  An unsent START is reported as cancelled; a possibly transmitted START is reported
  as unconfirmed. After the new handshake, mapped controls use the reported state.
  If exiting while disconnected or after a timeout, stopping is unconfirmed;
  previously requested playback may continue.
- Natural sequence completion still blinks the Teensy LED three times. STOP
  cancels both players' current and queued streams and does not signal completion.

Lines are ASCII with LF (CRLF accepted). Commands: `BTTEST1 HELLO`, `STATUS`,
`START`, `STOP` (each verb needs the `BTTEST1 ` prefix). Responses: `BTTEST1
STATE PLAYING|DONE|IDLE|ERROR`, `BTTEST1 STARTED`, `BTTEST1 STOPPED`, `BTTEST1
DONE`, or `BTTEST1 ERROR <reason>`. STARTED means the stream has been armed,
not that analog/USB audio output has been measured. State is polled once a
second, replies time out after six seconds, and shutdown waits at most
12.5 seconds (including an outstanding command). The six-second timeout
allows the existing three LED blinks (4.5 seconds) to finish before firmware
services another command. USB/SD work stays in the
firmware main loop, with fixed-size command buffers and no added heap use.

## Checks and hardware procedure

The idle-boot firmware has passed user-reported manual hardware checks for silent
boot, IDLE handshake, START/restart, STOP and reconnect.

`cmake --build build --parallel 4`, `ctest --test-dir build --output-on-failure`,
and `pio run -e teensy41` build the local UI/tests and firmware without uploading.
On Linux, CTest additionally exercises the serial implementation with PTYs:
fragmented replies, START/STOP ordering, completion/errors, oversized input,
disconnect, changed tty path, heartbeat timeout and handshake timeout.

On R36H/T4.1, verify a fresh boot stays silent without a UI, then connect and
confirm STATE IDLE before START. Use L1/B to start or restart without PROGRAM;
use R1/X to stop and stay, then press R1/X again to exit. Test STOP during each
stage (especially queued/simultaneous streams), and verify that exit after natural
completion preserves three blinks. Unplug/replug USB
while waiting, playing and stopping; verify rediscovery, continued SDL response,
and truthful STOP-unconfirmed logs. Verify actual audio and tty permissions.
Disconnect Teensy and reconnect Wi-Fi to inspect `/tmp/brotracker-arkos.log`.
The log is overwritten at the next application launch, so collect it first.

No firmware upload or console installation is part of the automated checks.

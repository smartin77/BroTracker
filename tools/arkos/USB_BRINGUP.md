# Temporary ArkOS / Teensy WAV control test

This is a disposable `BTTEST1` USB CDC bring-up exchange, not BroTracker's
final communication protocol. Keep `USB_MIDI_AUDIO_SERIAL` selected.
No audio is transported by these commands.

The Linux UI discovers USB VID:PID `16c0:048a` through sysfs and opens the
associated `/dev/ttyACM*` at 115200 with DTR asserted. It retries once a second;
permissions/open errors, transmitted commands, received lines, completion and
timeouts are flushed to `/tmp/brotracker-arkos.log`. The console user must
have read/write permission for the tty. Windows builds show a waiting status;
this temporary CDC implementation is Linux-only.

- Firmware still starts the existing WAV sequence at boot without a UI.
- Connecting reports its state without interrupting autonomous playback.
- The first connected key/controller press restarts the sequence at Test1.
  Presses while waiting are logged and ignored; held-key repeats and duplicate
  joystick events from the mapped controller do not count as another press.
- The next press sends STOP and exits after acknowledgement. If completion or
  an idle/error state is already known, it exits immediately. SDL quit also
  attempts STOP. The UI remains responsive while awaiting replies.
- Rapid presses queue START followed by STOP, even while STATUS is outstanding.
  STOP does not replace an unsent START. The existing shutdown deadline still
  applies; a slow/missing reply is reported as unconfirmed, never as success.
- A disconnect returns to discovery. Commands are never replayed automatically.
  An unsent START is explicitly cancelled and the UI restores its first-press
  action after reconnect. If any START bytes were written without acknowledgement,
  playback is unknown: the next press remains stop/exit, not another START.
  If exiting while disconnected or after a timeout, the log explicitly says
  that stopping is unconfirmed; firmware can continue autonomously.
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

`cmake --build build --parallel 4`, `ctest --test-dir build --output-on-failure`,
and `pio run -e teensy41` build the local UI/tests and firmware without uploading.
On Linux, CTest additionally exercises the serial implementation with PTYs:
fragmented replies, START/STOP ordering, completion/errors, oversized input,
disconnect, changed tty path, heartbeat timeout and handshake timeout.

On R36H/T4.1, manually verify autonomous completion first; then connect the UI
after completion and start again without PROGRAM. Verify first press starts,
second press stops during each stage (especially queued/simultaneous streams),
and exit after natural completion preserves three blinks. Unplug/replug USB
while waiting, playing and stopping; verify rediscovery, continued SDL response,
and truthful STOP-unconfirmed logs. Verify actual audio and tty permissions.
Disconnect Teensy and reconnect Wi-Fi to inspect `/tmp/brotracker-arkos.log`.
The log is overwritten at the next application launch, so collect it first.

No firmware upload or console installation is part of the automated checks.

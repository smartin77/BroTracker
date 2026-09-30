# BroTracker Terminal (Windows bring-up)

Native SDL2 client for the Teensy engine; **BTX** is shorthand, not the
application name. The fixed, non-resizable client area is 640x480. This milestone
has no host audio, scaling, fullscreen, firmware changes or final tracker protocol.

## Build and package

Use the existing MSYS2 **UCRT64 MinGW** compiler, CMake, make and SDL2 development
files. No installer or firmware upload is involved. From the checkout in PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/windows/build-package.ps1 -ToolchainBin D:/dev/msys64/ucrt64/bin
```

The helper uses the existing `build/` CMake layout, enables
`BROTRACKER_BUILD_WINDOWS_TERMINAL`, builds and runs CTest, then copies the checked
executable and its recursively resolved non-system DLL dependencies into
`deploy/windows/`. For a separate cache, pass `-BuildDirectory build/windows-terminal`.
It copies the test tune and both font files from the canonical `assets/` directory,
plus runtime license notices. These package copies are generated outputs, not new
asset sources. Windows supplies its own system/UCRT DLLs (Windows 10/11 baseline).
The default CMake configuration still leaves this target disabled.

Launch, or double-click the executable:

```powershell
.\deploy\windows\BroTrackerTerminal.exe
```

The executable finds packaged assets beside itself, regardless of the initial
working directory. Running `build/BroTrackerTerminal.exe` from the repository root
also works. Keep the DLLs and assets beside the packaged executable.

## Controls and connection

- **Space:** START when ready/stopped/finished; RESTART while playing.
- **Enter:** STOP and stay. While already stopped or waiting, stay without STOP.
- **Ctrl+X / window close:** exit; queued/active playback first receives STOP.
- Auto-repeat, unrelated keys, controller/joystick events and trigger axes do not
  request actions. Pending START/restart duplicates are suppressed. During STOP,
  further commands are suppressed; exit can upgrade it to STOP-then-exit.

Firmware START already stops both streams and restarts Test1, so restart uses the
existing START command. The temporary **BTTEST1** exchange is shared with ArkOS:
HELLO, STATUS, START, STOP, six-second command timeout, one-second reconnect and
status intervals, and the existing 12.5-second overall shutdown bound. A STOP
failure/disconnect is logged as unconfirmed, never acknowledged success.

Windows enumerates present Ports-class device instance identities for USB
`16c0:048a`, reads their assigned COM names, and opens only a unique match. It does
not probe unrelated COM devices and refuses multiple matching Teensys. Discovery,
open and serial I/O run off the SDL thread. Receive storage is bounded to 4096
bytes and a single 32-byte transmit request. A full receive buffer fails closed.
Only connection-local CDC settings (115200 8N1, DTR/RTS) are set; no persistent
USB/device configuration is changed.

On USB removal, the client remains responsive and discovers again. It never
replays START automatically. If a write may have begun, diagnostics say its result
is unknown. Reconnect completes a new handshake; Space starts again without a
PROGRAM-button press. If the port is busy, close the other serial monitor first.

## Shared log

The executable uses the Windows GUI subsystem (no console window). Startup
failures show a native error dialog including the log path; the log also records
its own location, normally:

```text
%APPDATA%\BroTracker\BroTracker Terminal\brotracker-terminal.log
```

All terminal/protocol/transport diagnostics are flushed. Windows connection
stages carry UTC and monotonic millisecond timestamps: discovery, COM open/setup,
HELLO write, boot playback landmark and first STATE reply. Direct runs start a new
log. A future supervisor can initialize this same log and set
`BROTRACKER_APPEND_LOG=1` before launching to preserve earlier bridge lines. No
Windows audio bridge is implemented here.

## Checks

```powershell
ctest --test-dir build --output-on-failure
# Visible error dialogs for missing font and SDL video failure (no serial access):
python tools/windows/check-startup-errors.py
# Explicitly sends test-sequence commands to the connected Teensy through the UI:
python tools/windows/check-terminal.py --hardware
# Also waits for a user-operated USB unplug/replug:
python tools/windows/check-terminal.py --hardware --reconnect
```

The hardware script launches the packaged app with the toolchain removed from
PATH, measures the native client area/style, drives actual Windows key/close
messages, and checks the production log for handshake, START/restart, STOP-and-stay,
repeat suppression and acknowledged shutdown. It also reopens the device without
resetting it, and checks Ctrl+X. It always closes the application afterwards.
Physical replug is separate from reopening a handle. The reconnect test allows
five minutes for each user action and probes the live UI with WM_NULL. It requires
a PLAYING handshake before it sends any START, then exercises normal controls.
The timestamped reconnect transcript is saved to `build/windows-physical-reconnect.log`. This does not measure audio
quality or timing. Python is only a development-test dependency, not a runtime DLL.

Shared control tests run on Windows and Eoan. Existing Linux PTY tests still
exercise the production Linux transport, partial replies, ordering, reconnect,
disconnect and timeout paths. To check ArkOS without changing its package:

```sh
bash tools/arkos/run-in-eoan.sh bash -c 'cmake -S /workspace -B /build/brotracker -DBROTRACKER_BUILD_ARKOS_UI=ON && cmake --build /build/brotracker --parallel 2 && cd /build/brotracker && ctest --output-on-failure'
```

Layout follows `docs/PROJECT_STRUCTURE.md`: Windows host integration in
`ui/windows/`, existing shared UI/protocol code in `src/ui/`, shared SDL presentation
in `ui/sdl/`, scripts here, generated runnable package in `deploy/windows/`.
The Windows target does not compile or include anything under `ui/arkos/`.

## Boot-time reconnect investigation

The previously captured real reconnect log showed COM discovery/open and HELLO
transmission succeeding, followed by the shared six-second reply timeout. The
next HELLO received STATE PLAYING before the first sample finished. It did not
wait for the complete autoplay sequence.

The matching source explains how an early HELLO can be lost: PlatformInit calls
DiagnosticsInitialize, whose TrySyncClockFromHost consumes serial lines for up
to 1500 ms and discards non-epoch input. BTTEST1 is serviced later in KernelRun,
including during playback. This is a boot-time serial-consumer conflict, not a
requirement to finish audio before communication. No firmware, autoplay, clock
settings, shared retry policy or BTTEST1 semantics are changed by this Windows
polish. The timestamps distinguish this reply-loss case from slow enumeration,
COM setup or host transmission on subsequent hardware trials.

On the 2026-09-30 user-operated replug, discovery/open/setup completed in about
4 ms. The first HELLO write completed 1.03 s later, before the boot diagnostics
finished; no STATE reply arrived and the six-second timeout expired. Reopening
and sending HELLO again received STATE PLAYING in the same logged millisecond,
8.11 s after initial discovery. The UI probe maximum was 37.2 ms and no automatic
START was sent. Space/restart, Enter/STOP and both exit paths passed afterward.
The lost-first-HELLO behavior remains visible; this change diagnoses it rather
than modifying firmware's clock-sync consumer or shared retry behavior.

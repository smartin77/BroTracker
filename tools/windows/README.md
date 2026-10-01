# BroTracker Terminal (Windows bring-up)

Native SDL2 client for the Teensy engine; **BTX** is shorthand, not the
application name. The fixed, non-resizable client area is 640x480. This milestone
routes USB audio through a Windows-only WASAPI worker. It adds no scaling,
fullscreen, firmware changes or final tracker protocol.

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

Firmware boots in IDLE with both sample players stopped. HELLO/STATUS report
IDLE until an explicit START; connecting the terminal does not start playback.
There is no terminal-detection delay or automatic playback fallback.

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
HELLO write, playback-arm landmark and first STATE reply. Direct runs start a new
log. A future supervisor can initialize this same log and set
`BROTRACKER_APPEND_LOG=1` before launching to preserve earlier bridge lines. No
external audio bridge process or Python runtime is required; the in-process
WASAPI worker shares this log and is joined before the file closes.

## USB audio routing

Before audible testing, **disable Windows "Listen to this device"** on the
Teensy/BroTracker recording endpoint to avoid duplicate playback. The terminal
never changes Listen, volume, device defaults or persistent audio settings.
Launch the same packaged executable; routing starts automatically, independently
of BTTEST1. Firmware remains IDLE until Space sends START.

The Windows-only `audio_bridge.cpp` worker enumerates active capture endpoints
and matches their container IDs to present USB devices with VID/PID `16c0:048a`.
It logs the underlying USB instance (including serial), container, endpoint ID
and display name. Cached `Teensy MIDI/Audio` and new `BroTracker USB audio` names
both work; names are diagnostic only. No unrelated microphone fallback is used.
Multiple matching capture endpoints are rejected. The output is the current
**default multimedia render endpoint**; an output belonging to a matching
BroTracker USB container is rejected to prevent routing back into the board.
An unverifiable container identity fails closed.

Both clients use WASAPI shared mode with stereo float32 at 44,100 Hz and
`AUTOCONVERTPCM | SRC_DEFAULT_QUALITY`, letting Windows convert sample format,
channel layout and rate to/from the endpoint mix formats. Unsupported
initialization fails explicitly; there is no silent format fallback. The log
reports each mix format, requested client format and actual buffer sizes.
The worker requests 20 ms buffers and waits for WASAPI capture/render events,
using the Windows MMCSS Audio scheduling class when available. Its fixed 8192-frame stereo
queue targets 1323 frames, using occupancy-driven fractional interpolation
(up to +/-3000 ppm) to accommodate independent capture/render clock drift.
These settings are configuration, **not measured end-to-end latency**.

Silent capture packets become zeros. Capture discontinuities clear/re-prime
the queue; starvation inserts silence and re-primes, and overflow discards
oldest frames rather than allowing latency to grow. Counters include non-silent
capture, render submissions, silence-fill, drops, discontinuities and empty
render buffers. A startup discontinuity or initial silence-fill is normal.
The audio processing loop performs no application allocations, discovery or
file writes; the SDL thread periodically logs atomic counters. Discovery and
setup/teardown diagnostics run on the worker outside audio processing.

Disconnect/invalidation, a stalled stream or an endpoint/default-output change
releases both clients and rediscoveries run with interruptible backoff bounded
at 8 seconds (1, 2, 4, 8 seconds after consecutive startup failures). The UI and
CDC controls continue operating. Audio recovery never sends START or changes
engine state. A new default multimedia output is acquired automatically; this
milestone has no endpoint selector. Exiting retains the acknowledged CDC STOP
path, then stops and joins audio before closing the shared log. No worker
process is launched.

Shared-mode conversion and property behavior follow Microsoft's
[stream flags](https://learn.microsoft.com/en-us/windows/win32/coreaudio/audclnt-streamflags-xxx-constants)
and [endpoint properties](https://learn.microsoft.com/en-us/windows/win32/coreaudio/device-properties).

## Checks

Python is an **optional development dependency** for the hardware/UI test scripts
below. BroTracker Terminal does not require Python to launch or run, and the
Windows build/package helper does not invoke these Python tests. Keep the reusable scripts in
`tools/windows/`; their logs and scratch directories under `build/` are generated
test outputs and may be removed between runs.

```powershell
ctest --test-dir build --output-on-failure
# Visible error dialogs for missing font and SDL video failure (no serial access):
python tools/windows/check-startup-errors.py
# Explicitly sends test-sequence commands to the connected Teensy through the UI:
python tools/windows/check-terminal.py --hardware
# Also waits for a user-operated USB unplug/replug:
python tools/windows/check-terminal.py --hardware --reconnect
# Listen must be disabled; sends START/restart/STOP and checks audio counters:
python tools/windows/check-audio.py --hardware
# Start unplugged and verify exit interrupts discovery backoff:
python tools/windows/check-audio.py --hardware --absent-exit
# Start with Teensy unplugged; follow printed connect/replug/output prompts:
python tools/windows/check-audio.py --hardware --physical --default-change
# Start connected and test physical recovery plus output change:
python tools/windows/check-audio.py --hardware --reconnect --default-change
```

The hardware script launches the packaged app with the toolchain removed from
PATH, measures the native client area/style, drives actual Windows key/close
messages, and checks the production log for handshake, START/restart, STOP-and-stay,
repeat suppression and acknowledged shutdown. It also reopens the device without
resetting it, and checks Ctrl+X. It always closes the application afterwards.
Physical replug is separate from reopening a handle. The reconnect test allows
five minutes for each user action and probes the live UI with WM_NULL. It requires
an IDLE handshake before it sends any START, then exercises normal controls.
The timestamped reconnect transcript is saved to `build/windows-physical-reconnect.log`. This does not measure audio
quality or timing. Python is not a runtime requirement for BroTracker Terminal.

The audio test probes UI responsiveness while waiting and verifies non-silent
capture/render flow plus joined-worker/acknowledged-STOP shutdown. It does not
measure acoustic quality or latency. Default-output changes and physical USB
replug are user-operated; the script never changes system settings. The CTest
audio buffer test simulates ten minutes of +/-1000 ppm drift, stereo separation,
silence, starvation and bounded overflow. Physical stereo fidelity and prolonged
hardware drift/quality checks remain listening/manual tests.

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

## Historical boot-time reconnect investigation (autoplay firmware)

The previously captured real reconnect log showed COM discovery/open and HELLO
transmission succeeding, followed by the shared six-second reply timeout. The
next HELLO received STATE PLAYING before the first sample finished. It did not
wait for the complete autoplay sequence.

The then-current source explained how an early HELLO could be lost: PlatformInit calls
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
These measurements describe the older autoplay firmware. The subsequent static
startup handoff preserves early commands while retaining clock synchronization.
Current firmware also boots IDLE and requires an explicit START; the Windows
delays, retry policy and controls remain unchanged. Replug checks now expect IDLE.

## Live USB product-name diagnostic

The audio endpoint label is not the USB product descriptor. To read the actual
connected `16c0:048a` device through a USB hub GET_DESCRIPTOR request, without
opening CDC/audio streams or changing any device settings:

```powershell
$env:PATH = "D:/dev/msys64/ucrt64/bin;$env:PATH"
g++ -std=c++17 tools/windows/check-usb-product.cpp -lsetupapi -o build/check-usb-product.exe
./build/check-usb-product.exe
```

This optional development helper is not shipped in the runtime package. It
prints the product/serial strings and raw descriptor bytes, and reports missing
matches, inaccessible hubs or descriptor failures. It enumerates current hubs;
no USB port topology is hard-coded. Multiple matches are reported separately.

On the post-reboot check for serial `17681760`, a successful descriptor read
returned `BroTracker USB audio` (42 bytes), exactly matching the product string
in `.pio/build/teensy41/firmware.elf`. Windows simultaneously displayed
`Digital Audio Interface (2- Teensy MIDI/Audio)`. This establishes a retained
Windows endpoint label, not an old product name in the running firmware; it
does not establish that every byte of the flashed firmware equals the local
artifact. No registry, driver, serial, VID/PID or firmware change is needed to
make bridge discovery work, because discovery uses USB/container identity.

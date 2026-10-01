# Temporary standalone ALSA live bridge

This command-line experiment is separate from BroTracker's UI and Teensy
firmware. It does not send BTTEST1 commands or start the Teensy sequence.
Use `--auto` for identity discovery or supply both device names explicitly.
No card numbers or USB topology are embedded in the tool.

## Build in the existing Eoan ARM64 root

From the checkout in WSL (no installation or package export):

```sh
bash tools/arkos/run-in-eoan.sh cmake -S /workspace -B /build/alsa-bridge \
  -DCMAKE_BUILD_TYPE=Release -DBROTRACKER_BUILD_ALSA_BRIDGE=ON
bash tools/arkos/run-in-eoan.sh cmake --build /build/alsa-bridge --parallel 2
bash tools/arkos/run-in-eoan.sh cmake -E chdir /build/alsa-bridge ctest --output-on-failure
```

The executable is `$BROTRACKER_EOAN_ROOT/build/alsa-bridge/BroTrackerAlsaBridge`.
ALSA headers/library (`libasound2-dev`) must be present in that root. The new
CMake option defaults OFF; neither Windows nor ArkOS terminal binaries gain
an ALSA dependency. Do not enable it on Windows.

## R36H test (after a separately reviewed transfer)

From the checkout on R36H:

```sh
./deploy/arkos/BroTrackerAlsaBridge --auto
```

At startup ALSA control APIs enumerate current cards and PCM devices/subdevices.
Capture matches ID `MIDIAudio` or card name `BroTracker USB audio` or
legacy `Teensy MIDI/Audio`; playback
matches ID `rockchiprk817co` or name `rockchip,rk817-codec`, explicitly excluding
Teensy. Capture uses discovered `hw:CARD=...,DEV=...,SUBDEV=...` endpoints;
playback uses `plughw:CARD=...,DEV=...,SUBDEV=...`. Here `CARD` is the numeric
index enumerated at startup, not the ALSA ID: two cards can share the same ID.
Device and subdevice numbers are also discovered, not hard-coded. The full discovered card
identity and exact selected PCM string are logged. Only endpoints that open,
accept the bridge format, and pass a post-open identity check are viable.
The winning handles remain open. A listed but vanished/inaccessible/busy
candidate is rejected with its error; another viable match may be used. Zero
viable matches or multiple viable matches fail clearly rather than selecting
an arbitrary device. It never falls back to USB/Teensy playback for speakers.

This discovery happens once. If Teensy disappears during startup and no viable
capture remains, startup fails. A runtime unplug reported by ALSA terminates
the bridge, closes both handles and returns nonzero. There is no rediscovery or
automatic reconnect inside this standalone process; reconnect Teensy and rerun
the command manually. The ArkOS PortMaster launcher separately supervises the
bridge, reaps failed processes and retries fresh `--auto` discovery with capped
backoff while BTX remains open. It never sends START.

Explicit arguments remain available for diagnostics. The previously observed
numeric pair was:

```sh
./deploy/arkos/BroTrackerAlsaBridge hw:1,0 plughw:0,0
```

Recheck numeric assignments after reconnecting. `arecord -L` and `aplay -L`
list PCM names. Explicit mode uses exactly the supplied names, bypassing
automatic identity selection. `hw` requests the hardware format directly;
`plughw` may convert at its slave. The ALSA PCM/slave dump shows both setups.
The application interface remains stereo S16_LE at exactly 44,100 Hz.

Output is opened first. EBUSY is reported explicitly; release the output
manually if needed. The tool never stops EmulationStation or other processes.
Ctrl+C drops queued sound and closes both devices without waiting to drain.
Diagnostics go to stderr and are unbuffered (use `2>alsa-bridge.log` if desired).

## Buffering and interpretation

Each PCM requests a 256-frame period and 2,048-frame buffer; actual negotiated
values and the playback start threshold (half its negotiated buffer) are
printed. A fixed 8,192-frame / 32-KiB queue preserves stereo frame boundaries,
handles short reads/writes, and never grows. Nonblocking I/O retries EAGAIN and
uses a one-millisecond idle wait. These numbers are settings, **not measured
end-to-end latency**.

Overruns/underruns and suspend events are counted and logged. Recovery drops
both PCM queues, discards the application queue, prepares both devices and
restarts capture; discontinuities are explicit. Other errors, including
unplugging, terminate with a nonzero exit rather than hiding failed capture.
Final totals include captured/submitted frames, queue high-water mark and
application frames discarded/remaining. Submitted frames are not proof of
physical playback. A stream reset also discards kernel/device buffering.

There is no adaptive clock-drift correction: independent capture/playback
clocks can eventually exhaust buffering and cause reported xruns. This test
does not establish the cause of the distorted `arecord | aplay` pipeline.
The automated check uses ALSA file/null PCMs to verify stereo bytes, a partial
final block, ring wrap, invalid-device reporting and SIGINT shutdown. Physical
sound quality, both samples staying in sync, long-run drift, device-busy and
unplug behavior, and actual latency still require R36H hardware testing.

Discovery policy tests cover new and legacy USB product names and the legacy
`MIDIAudio` ID, using simulated inventories/open results for renumbered
cards, duplicate identities, vanished/busy candidates, absent endpoints and
rejection of Teensy speaker output. They do not prove physical ALSA enumeration
or hot-unplug behavior on R36H; those still require hardware verification.

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

## Opt-in diagnostic capture recording

Ordinary Terminal launches do not record or create a timeline. **BroTracker
Diagnostics** supplies `BROTRACKER_DIAGNOSTICS_DIR` to the existing supervisor
and bridge; for a deliberate standalone diagnostic, set that variable to an
existing writable directory. No second capture handle is opened.

Each bridge process records exactly the successful capture reads, before queue
or playback processing, as interleaved PCM S16_LE stereo 44100 Hz. Initial
silence is included: the cap counts captured frames starting with the first
read, not the START command or first sound. The maximum is 7,938,000 frames /
31,752,000 PCM bytes / 31,752,044 bytes including the 44-byte WAV header.

A preallocated 2 MiB lock-free SPSC ring carries unchanged bytes to a dedicated
writer. The audio producer does no recording disk I/O, allocation or blocking
lock/wait. The writer handles short writes/EINTR, drains the accepted prefix,
and rewrites/fsyncs the header before closing. On ring overflow it stops
accepting immediately rather than joining PCM across an unreported gap; the
prefix is finalized and marked incomplete. Open, thread or write failures are
logged and disable recording without disabling audio. A failed disk/header
write can leave an invalid WAV; the timeline never marks it complete.

Every recovered bridge instance uses a unique `capture-PID-XXXXXX.wav` path
without overwrite. Failed attempts before capture start create no WAV. Normal
exit and exception unwinding on USB failure join the writer; duration limit
and overflow finalize the WAV while audio continues. The lightweight worker
continues draining xrun event notes until shutdown. WAVs cannot be promised
finalized after SIGKILL, power loss or abrupt storage removal. File storage can
still fail or stall; joining must finish pending writes, so a stuck filesystem
can delay shutdown. Recording can alter system load and needs hardware testing.

`events.tsv` links every WAV to its bridge PID, selected PCMs, source start/end,
limit/overflow/failure and written frame count. Audio xruns are passed via a
bounded 64-note queue, with source time and recorded-frame offset; note loss is
reported explicitly. No timeline writes occur in the audio producer. Multiple
producers serialize entire records using file locks. Tab/newline/backslash
characters in details are escaped. Fields are wall_time (ISO-8601 with zone),
monotonic_time (boot-relative seconds including suspend), source,
bridge_instance, event, details. The writer uses CLOCK_BOOTTIME; shell producers
use `/proc/uptime`. Source events are stamped when generated; existing UI/CDC
log lines are explicitly delayed `collector-observation` events, not claimed
as source timestamps. Their original request, command and response text remains
in details. Finalized runs sort records by monotonic timestamp after producers
have stopped; the live append-only manifest may contain delayed events out of
order. No START is sent as part of recording or recovery.

Each captured WAV is evidence of bytes received by ALSA, not proof of USB packet
cadence, physical audio output, or measured end-to-end latency. A capture xrun
can omit source samples; its event identifies the recorded-frame boundary.

Recorder setup (allocation, file/header creation, setup timeline flush and
thread creation) completes before `snd_pcm_start(capture)`. After successful
capture start only bounded notes are queued for `capture_start` and
`recording_start`, preserving source timestamps without synchronous file I/O.
A capture-start failure produces `capture_start_failure` and
`recording_cancelled`; the unused zero-frame WAV is removed instead of being
presented as a completed capture. Completion rechecks the published queue after
observing the completion flag, so normal end, limit and overflow drain all
accepted bytes even if the initial empty observation was stale.

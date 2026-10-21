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

## Opt-in pattern-click investigation (paired PCM)

This experiment measures the existing USB-capture -> playback path. It does not
change PCM, queue sizes, ALSA settings, clock correction or recovery policy, and
does not diagnose a bridge bug or hardware limit by itself. The looping production
pattern is the primary workload; a continuous tone is only a secondary control.
Physical ArkOS and dArkOS runs remain pending and must be labelled separately.

Local validation (2026-10-20): the bridge and focused test targets build in the
existing Eoan ARM64/QEMU environment. All 11 selected CTest suites pass, including
existing host/serial/input regressions, BTX lifecycle/configuration/display tests,
15 synthetic runner tests, nine Python analysis tests and 9001-frame ALSA file/null
comparisons. Both generated deployment artifacts pass the Eoan ABI/dependency and
capability checks. These tests do not open physical audio hardware or establish
click-free playback.

Set `BROTRACKER_BRIDGE_CLICKS_DIR` to an existing writable evidence base to enable
paired diagnostics. Each process creates a new `bridge-XXXXXX` child without
reusing or overwriting files. When unset/empty, the new worker, files, status
sampling and event/PCM copies are disabled. The older capture-only WAV mode is
independent; the run helper disables it in both baseline and paired runs to avoid
adding a second recording worker.

### Evidence and bounded service work

- `received.s16le`: exactly the positive frame counts returned by `snd_pcm_readi`.
- `accepted.s16le`: exactly the positive frame counts accepted by each
  `snd_pcm_writei`, including partial writes. These are application-format stereo
  S16_LE 44100 Hz bytes, before any playback plugin/slave conversion, not proof of
  DAC output. No silence is deliberately inserted by this bridge.
- `transfers.tsv`: consecutive sequence, CLOCK_MONOTONIC timestamp at observation,
  kind, requested/result frames, absolute source frame, cumulative captured,
  accepted and discarded frames, application queue occupancy, ALSA avail/delay,
  state and recovery step. Counters/occupancy describe the state **before** the
  event is applied; write source = accepted + discarded. Negative/zero/short
  transfers are retained. Times are process observations, not USB packet or
  hardware sample timestamps. Events are not independent clock measurements.
- Kinds: 1 service entry; 2 read; 3 write; 4 capture status; 5 playback status;
  6 recovery operation; 7 application discard; 8 capture start; 9 service end.
  Recovery step 0 contains the original errno and initiating side (`state=1`
  capture, `0` playback), with pre-recovery capture/playback states in avail/delay;
  steps 1/2 drop capture/playback, 3/4 prepare them,
  5 restarts capture. Every operation's actual return value is recorded.
  Drops can also discard already accepted kernel PCM; its exact extent and
  missing upstream USB samples cannot be reconstructed. End result -1 means
  aborted service; zero means orderly signal shutdown. Remaining queued frames
  at shutdown are counted, not pretended to have reached playback.
- `alsa-parameters.txt`: actual PCM names, full application/slave ALSA dumps,
  format/rate/channels, period/buffer, actual software avail_min, start/stop
  thresholds and boundary, obtained before capture starts. `run.json` records
  schema, ALSA version, fixed capacities and a wall/monotonic alignment anchor.
- `completion.json`: record counts, first lost sequence, lost-record count,
  writer failure and explicit incompleteness. Loss reasons: 1 full diagnostic
  queue, 2 diagnostic duration limit, 3 writer failure. Limit/overflow stops
  accepting evidence at the first lost event; audio continues. Later evidence
  is never spliced across that gap. A missing completion file is incomplete.

The audio producer uses a preallocated 4096-slot SPSC queue (about 4.4 MiB),
fixed-size copies and lock-free atomics. Each record holds at most 256 PCM frames.
No new diagnostic formatting, disk I/O, locks, allocation or waits occur in the
service loop. A separate writer formats events and writes both PCM files. In this
mode recovery console logging is replaced by queued records; fatal reporting
occurs after leaving service. ALSA calls remain nonblocking; the existing bounded
1 ms idle poll is unchanged. Allocation/files/thread creation finish before
capture start. Setup failure is reported before service and audio continues.

Service timestamps measure loop gaps; queue occupancy is recorded each iteration.
At most once per 20 ms, both PCMs are sampled with `snd_pcm_avail_delay` and state;
there is no catch-up sampling loop. These samples plus cumulative frames can
expose occupancy trends and scheduling stalls, but ALSA plugins, resets and
scheduling also affect them. No adaptive drift correction or buffer tuning is
introduced. Status-query overhead and asynchronous storage traffic may perturb
the system; matched diagnostics-off runs are required.

Writer failure, overflow, duration limit, malformed/missing/duplicated events,
partial PCM frames, counter disagreement and interrupted finalization must not
pass completeness validation. Files and failed runs are retained. Finalization
joins the writer outside the audio loop; a stalled filesystem can delay process
shutdown. SIGKILL or power loss can prevent finalization. There is no guarantee
that diagnostic evidence survives abrupt storage failure. This mode does not
measure service stalls after the first diagnostic loss.

### Offline unattended Ports procedure

The console needs no network, SSH, extra controller, or powered hub during a run.
The runner is deliberately specific to this experiment; its owned-process,
preflight, evidence and result workflow is the pattern to reuse for later
standalone diagnostics, not a new framework.

#### Delivery and preparation while Wi-Fi is available

The existing Linux policy tracks checked ARM64 deployment artifacts under
`deploy/arkos/` alongside source. Temporary builds and review exports stay under
ignored `build/`. Do not substitute an installed `/roms/ports/brotracker` bridge:
the runner uses only the binaries in this exact checkout and verifies both ELF
hashes, dependencies and executable capability IDs against
`deploy/arkos/bridge-clicks-artifacts.json`. Source and textual package-asset fingerprints normalize CRLF to
LF so Git text conversion cannot invalidate them; ELF/bitmap/evidence hashes use
raw bytes. Windows deployment policy is unchanged.

For developer preparation, build the matching BTX and bridge in the existing
Eoan ARM64 root, run the focused tests, then run:

```sh
# Inside Eoan, with the same build directory used for both binaries:
bash /workspace/tools/alsa_bridge/check-offline-build.sh /build/bridge-clicks
# From the repository root on the development machine, after those checks:
python3 -B tools/alsa_bridge/prepare_offline_artifacts.py \
  --build-dir build/bridge-clicks --package-dir deploy/arkos
```

The preparation helper checks hashes from the ABI report before replacing the
known generated Linux executables and writing their manifest. It performs no
Git operations. The owner reviews and commits/pushes the source, both binaries
and manifest together using the existing repository workflow. Executable modes
must be recorded for the two ELF files when committing from Windows; no bundled
ALSA/SDL library is added. An older bridge/BTX or mixed source/artifact checkout
fails preflight rather than silently running another deployed version.

On the console, update the reviewed checkout using Git while Wi-Fi is connected.
From that checkout, run the single preparation command:

```sh
python3 -B tools/alsa_bridge/prepare_offline.py
```

This idempotently installs/updates only **BroTracker Bridge Clicks** in
`/roms/ports` and records the absolute checkout path. Unchanged entries are not
rewritten; replaced diagnostic entries receive unique byte-preserving backups.
Unrelated Ports entries, persistent INI settings and manual descriptions are
untouched. Python 3.7+, `fuser` (psmisc), `ldd`, system ALSA/SDL libraries and the
usual PortMaster runtime must already be installed during online preparation.
No device queries or network access are required by preparation.

Disconnect Wi-Fi, connect Teensy, then select **Ports > BroTracker Bridge Clicks**.
No manifest, editor, questionnaire, controller START presses or typed workload
identity is required. Offline preflight verifies exact artifact/source fingerprints,
dependencies, device ownership, available space and the actual shared INI parser.
Bridge or absent/empty output is compatible; incompatible selection fails clearly
without rewriting config. Stop existing BTX/bridge sessions normally first.

Read-only preflight automatically saves OS/kernel, available device-tree identity,
USB topology/ancestry, ALSA devices and negotiated parameters, binary/source
fingerprints, available firmware protocol evidence and persistent configuration.
`automatic-workload.json` uses the device's current pattern cells, tempo, gains,
and available sample metadata/checksums; condition readbacks must match. Published
and volatile working patterns must match; nothing replaces a user's working pattern.
Exact firmware build identity and complete sample bindings are unknown where the
existing protocol cannot establish them. Physical wiring, headphones and analog
volume are explicitly unknown; unavailable descriptive facts include reasons and
never gate a run. Compatibility, ownership, workload consistency and evidence
completeness remain strict.

An existing `$HOME/BroTracker/bridge-click-workload.json` (or optional `--workload`
path) is supplementary **unverified user context only**. Its original bytes are
preserved separately up to 16 KiB, including `FILL` or malformed content; missing,
unreadable or oversized context is recorded without blocking. It is never overwritten.
Automatic facts are separate from these descriptions.

Technical success is distinct from listening confirmation. Every summary states
**Audible clicks: NOT YET CONFIRMED** until the user reports back. Listen normally;
a later report of clicks and, if remembered, the phase can be associated with the
unique run directory and automatic monotonic phase boundaries. No live marker,
typed timestamp, questionnaire or external recording is required to complete a run.

The diagnostic display shows **B: EXIT** throughout preflight, measurement and
results using fixed 3x integer text at 640x480. B, mapped controller Back,
Escape or closing the diagnostic window is emergency cancel/exit, including during
bounded setup/serial waits. Cancellation is **CANCELLED**, never technical success.
Only owned children are stopped; serial handles close and a bounded STOP/idle
attempt follows BTX serial release when playback was owned. All evidence remains.
The entire experiment is unattended. Finalized outcomes display briefly (3 seconds)
and return to Ports automatically, without B or Wi-Fi. After publication, B only
dismisses the screen and cannot change the saved outcome. During a running test or
analysis, B cancels with bounded cleanup and preserves partial evidence. Full
paths, errors and tracebacks remain in saved logs. The diagnostic launcher exits
directly instead of invoking PortMaster's external, potentially network-dependent
`pm_finish` return hook; normal application launchers are unchanged.
Normal BTX input and STOP-on-exit behavior outside diagnostic mode are unchanged.
Controller B/Back cancellation arms only after all initial held inputs have been
released for 250 ms; launch events, unknown controller instances, other buttons
and device notifications do not cancel. Escape (non-repeat) and user window-close
remain available. Intentional status-view SIGTERM shutdown is handled separately
from SDL quit, including early initialization. Cancellation source records and
`events.jsonl` retain the producing PID/view, phase, SDL event type and input ID;
the saved reason names the actual trigger. Display failures are reported as
failures rather than user cancellation.


#### One unattended console launch

1. Finish Git transfer and preparation. Disconnect Wi-Fi and connect Teensy
   directly using the intended unchanged test topology. Do not add the unstable
   hub. Keep headphones, power, gains and sample bindings unchanged.
2. From Ports, launch **BroTracker Bridge Clicks**. No controller START presses
   or live edits are required. Preparation, active-phase countdown, BTX-off and
   completed/failed status are displayed using the matching BTX SDL status view.
3. Leave it running for both conditions. The fixed order is diagnostics **on**,
   then **off**. Each has 60 seconds BTX-active and at least 60 seconds BTX-off.
   Setup, serial handoff, observations and finalization are separately timestamped.
4. Do not press B for normal completion. Both 60+60-second condition pairs run,
   followed by finalization; results display briefly and return to Ports automatically.
   B is only emergency cancel/exit if needed. Results stay on disk. Disconnect
   Teensy before reconnecting Wi-Fi
   for retrieval; keep every successful and failed attempt.

Each attempt uses a unique
`assets/hardware_diagnostics/bridge_clicks/<UTC>-offline-XXXXXX/` directory.
`SUMMARY.txt` and `summary.json` name failures, cleanup results, both conditions
and limitations. `STATUS.txt`, `events.jsonl`, per-condition BTX logs/status and
workload files, direct serial RX/TX/readbacks, actual ALSA parameters, hashes and
on-condition paired PCM/analysis remain together. No rerun replaces an attempt.
If a binary/dependency is unusable before SDL can start, the launcher also prints
the failure to the console; the saved result remains the authoritative evidence.
Physical visibility/input behavior still requires separate ArkOS and dArkOS tests.

### Ownership, workload verification and finite phase coverage

There is **no production serial proxy or PTY handoff**. During BTX-active, only
BTX owns CDC and uses its existing audio negotiation, `Start()` and telemetry
parser. Diagnostic behavior is explicitly selected by
`--offline-click-diagnostic <private-directory>`; ordinary startup/controller
behavior and STOP-on-exit remain unchanged. The experiment disables live edits
in that mode. A bounded atomic command file carries only START/RELEASE from the
runner. BTX polls it at most 10 Hz and emits <=2 KiB status snapshots at most
2 Hz plus transitions, on the host main loop, never the Teensy audio callback.

The runner starts each measured phase. It requires bridge readiness, verified
BTX readback, matching bridge acknowledgment, START completion and fresh coherent
advancing telemetry before beginning the 60-second active timer. It requests
release afterwards. BTX waits for a complete-line, command-idle boundary, exits
without STOP only on this explicit diagnostic release, destroys its serial
transport synchronously, and only then publishes released status. The runner
waits for process exit before opening serial. There is no Linux serial worker
to join. Release-report time, observed process-exit time and the first BTX-off
observation are distinct; the handoff gap is not counted as measured coverage.

At BTX-off start, midpoint and end, the runner performs bounded STATUS plus fresh
advancing-position observations. These sparse serial checks are **additional
load**, identical in both on/off conditions, and are not evidence of uninterrupted
playback between observations. The serial FD remains owned but is not serviced
by a background polling worker between bursts; buffered/stale POS lines cannot
satisfy an observation before the STATUS freshness barrier. The independent
bridge remains alive throughout both phases. A separate lightweight SDL status
view renders at 10 Hz during BTX-off; it has no tracker/serial ownership but adds
observer load, identically in both conditions. Off mode intentionally has no PCM
recording, so its evidence is telemetry/normal logs, not a PCM-completeness pass.

Before and after each condition, existing protocol readbacks verify FNV-checked
version-2 published and working pattern cells, tempo and eight channel gains.
Working content must equal published content, and content/gains must match across
all readbacks and BTX's own snapshot. Otherwise the attempt fails before replacing
anything. This does **not** preserve or restore a user-edited volatile working
pattern across BTX restarts. The current readback is the workload; any
change across restarts fails the attempt rather than being replaced. No pattern upload, publication, preview edit or gain SET is sent.
Available WAVHASH/WAVMETA device reports are retained and compared, including
valid reported PCM hashes/frame counts. Missing sample hashes, built-in sample
bytes and a complete runtime slot-to-file map are explicitly unverified; the
manual workload description does not substitute for device verification.

The old 180-second recording default remains for standalone use. The unattended
runner explicitly requests **240 seconds**, within a hard supported maximum of
300, and verifies the actual binary/metadata. Bridge/BTX setup and confirmation
have a separate **60-second deadline**; release/process wait is bounded, as are
serial requests and cleanup. BTX has its own 210-second abandoned-run watchdog.
A larger recording limit never grants unlimited setup. Paired analysis requires
successful read and accepted-write timestamps to span both complete measured
phases, along with normal sequence/count/PCM validation. Timeout, missing/stale
status, early exit, workload mismatch, overflow/limit loss or insufficient phase
coverage fails the attempt. No threshold or completeness check is weakened.

Strict recording analysis runs in an owned, cancellable worker with a 180-second
limit, visible elapsed progress, and analysis/bridge-shutdown/cleanup boundary
events. Timeout fails the attempt. PCM phase bounds come from the same validated
transfer scan; sequence, hashes, PCM equality and phase coverage remain required.
Emergency/failed runs do not analyze partial dumps synchronously before exit:
`analysis-deferred.json` records the explicit offline command and any partial
worker output is retained. Raw evidence is not marked complete by deferral.

The 20261020T204220Z ArkOS attempt remains CANCELLED (0/2 conditions). Its retained
80.21-second recording validates byte-for-byte, but BTX-off was interrupted and
there is no diagnostics-off pair. The user separately confirmed irregular dull
pops while listening. This is an auditory observation, not an automated PCM fault
finding or an identified cause. Historical screen presentation timing and the
reported Wi-Fi message remain unverified; saved status-write timestamps are not
proof of when a screen was seen. Matched physical validation remains pending.


Cleanup first stops/reaps only an owned BTX if needed, then makes bounded
supported STOP/STATUS requests after serial release, finalizes the owned bridge,
and reports STOP/idle uncertainty explicitly. It does not kill other sessions
or reset/program Teensy. A device or storage failure can prevent confirmation;
this remains a failed attempt with whatever evidence survived.

### Offline interpretation and optional external recording

Analysis is automatic after finalization, including partial failed on-condition
traces. To independently recheck a retained original, choose its exact child:

```sh
python3 -B tools/alsa_bridge/analyze_clicks.py \
  assets/hardware_diagnostics/bridge_clicks/ATTEMPT/on/bridge-XXXXXX \
  --output assets/hardware_diagnostics/bridge_clicks/ATTEMPT/reanalysis.json
```

The analyzer refuses to overwrite reports. It checks sequences, timestamps,
completion flags, frame counts, raw hashes and byte equality mapped through
partial writes/application discards. It reports service gaps, occupancy,
shorts/errors, XRUN/recovery results and musical edge statistics separately.
A sharp musical transient alone is not a fault. Accepted PCM is not headphone
output, and clean PCM/no XRUN cannot establish a hardware limitation.

Listening during normal playback is sufficient for the subjective follow-up.
Report afterward whether clicks were audible and, if remembered, during which
phase. An external headphone recording is optional; if one is independently
available, align pattern onset and later events allowing latency/clock drift.
Neither recording nor live markers are required. Technical completion is not a
listening pass. Physical ArkOS/dArkOS validation and click-cause identification
remain pending.

### Optional USB/HCD payload comparison (offline Ports)

This is a separate opt-in observer, not a click fix. It records only the Teensy
ISO audio IN endpoint on its actual single `usbmonN` bus, alongside the
**diagnostics-on** condition. The diagnostics-off condition has no USB recorder;
the extra recorder load must be considered when interpreting that comparison.
Without this option the existing workload, audio parameters and defaults remain
unchanged. No firmware change or live pattern edit is required.

While Wi-Fi is connected, run from the existing console checkout:

```sh
git pull --ff-only
python3 -B tools/alsa_bridge/prepare_offline.py --usbmon
```

Preparation installs the existing
Ports entry and a generated opt-in companion file; no hand-edited manifest or
configuration is required. It checks `sudo -n -k -- <python> <usbmon_capture.py>`
without using a cached password ticket. Only that bounded helper runs as root;
the runner, UI, bridge and BTX remain unprivileged. An existing password-free
sudo policy must permit it. Preparation fails before installing the option if
permission is missing; it does not change sudoers, capabilities or PortMaster.
Do not run the entire launcher as root. To disable the option, run the same
preparation command without `--usbmon`.

Preparation records the installed tcpdump version, linked libpcap dependencies
and hashes, lists supported link types without collecting traffic, and checks
an offline sparse-ISO fixture through the installed filter/savefile path.
It requires `USB_LINUX_MMAPPED` (DLT 220), including its 64-byte header and ISO
descriptor array. This verifies the installed offline semantics, **not** live
payload retention. `-s 0` or a successful tcpdump startup cannot establish that.
The actual capture uses an explicit 262144-byte snapshot, and every completion's
retained bytes, actual lengths and descriptor ranges are checked afterward.

Disconnect Wi-Fi, connect Teensy, then select **Ports > BroTracker Bridge Clicks**.
No network is needed during acquisition or automatic return. Existing emergency
B/Back/Escape cancellation and immutable finalized outcomes remain unchanged.
The helper discovers CDC ancestry and USB descriptors, requires the supported
stereo PCM16/44100 IN interface, and starts before bridge/playback activation.
The single-bus filter retains that endpoint's submissions, completions and
errors; it never selects usbmon0 or a network interface. Device/endpoint changes
are rediscovered on each attempt. Preparation may load the usbmon module.

Limits: 240 seconds and 128 MiB for the pcap, with at least 1 GiB free before
starting. Setup/readiness is bounded; owner-pipe EOF requests shutdown of only
the owned tcpdump, with bounded SIGINT/kill/reap handling. PCM shutdown precedes
USB shutdown. Loss, premature exit, storage/duration limits, missing statistics,
truncation or incomplete ISO records invalidate the USB comparison explicitly;
the matched PCM results and partial evidence remain available. Opt-in analysis
has a separate 360-second deadline (normal analysis remains 180 seconds), runs
off the audio path and remains emergency-cancellable. No audio buffers, recovery
policy or source PCM are changed.

Evidence lives under the existing unique attempt directory:
`on/usb/capture.pcap`, `capture.log`, `target.json`, `capture-result.json`.
The log retains actual commands/versions, readiness, stop result and tcpdump
capture/drop statistics. Derived `replay-*` directories retain every decoded
USB record's timestamps, S/C/E identity, captured/original lengths, per-packet
status/length/offset and reconstructed-byte mapping, plus `usb-audio.s16le`.
The pcap remains the original evidence, including sparse holes. Partial audio
is explicitly invalid: no silence, repair or resampling is inserted.
`on/analysis.json` and the overall summary retain a separate USB validity result;
requested invalid USB comparison makes the overall attempt FAILED even when
both PCM conditions completed. Terminal zero-data cancelled URBs are retained;
cancellation inside the audio stream is invalid.

Content alignment uses independent PCM anchors, allowing separate epochs at
recorded ALSA recoveries. Repeated musical passages and silence can make
alignment ambiguous; ambiguity is UNKNOWN, never a guessed timestamp offset.
Reports include differing frame indices and how many differing received frames
match data 2048 frames earlier. Alignment is not an attribution of cause.
For an independent replay, choose the exact retained PCM child and a new output:

```sh
python3 -B tools/alsa_bridge/analyze_usbmon.py ATTEMPT/on/usb \
  ATTEMPT/on/bridge-XXXXXX --output ATTEMPT/usb-reanalysis.json
```

The [kernel usbmon ABI](https://docs.kernel.org/usb/usbmon.html) observes the
USB driver/HCD boundary, not guaranteed on-wire bytes. The
[libpcap mmap implementation](https://github.com/the-tcpdump-group/libpcap/blob/libpcap-1.9.1/pcap-usb-linux.c)
explains why sparse descriptor offsets and retained lengths matter; this is not
an assumption that the console uses that libpcap version. Full retention must
pass the live run's per-record checks. Matching USB/ALSA bytes narrow the boundary;
corrupted HCD-visible bytes alone do not prove a Teensy defect. No physical USB
capture has yet validated this optional path. Listening confirmation remains a
separate user result; no markers, questionnaire or external recording is required.

### Corrected ISO replay and capture-path boundary (2026-10-21)

The retained run `20261021T130929Z-offline-gpebrcv6` remains historically FAILED;
corrected offline replay does not change its saved result. All 251866 pcap events
are readable and paired (125933 submissions/completions), with no recorded drop.
Eleven zero-data cancelled completions follow the final payload. Successful ISO
completions have aggregate `urb_length=0` but valid 176/180-byte descriptor actual
lengths. This is supported by the kernel representation: `mon_bin_event` keeps
`urb->actual_length` separately while `mon_bin_collate_isodesc` derives the ISO IN
payload extent and `mon_bin_get_isodesc` exports packet actual lengths. See the
[ArkOS-linked 4.4.189 reference](https://github.com/christianhaitian/linux/blob/daf063fc503be035a3df68ad59209ee62c57cfc9/drivers/usb/mon/mon_bin.c#L441)
and [installed libpcap 1.9.1 mmap reader](https://github.com/the-tcpdump-group/libpcap/blob/libpcap-1.9.1/pcap-usb-linux.c#L1206).

Replay accepts zero aggregate length only with retained descriptor/payload,
status, pairing, submitted descriptor bounds and original-length checks intact;
a nonzero aggregate must equal the packet-length sum. Detailed JSONL export is
bounded separately at 128 MiB. Reaching that cap records omitted detail but keeps
validating and reconstructing the entire pcap. A genuinely interrupted scan is
invalid, but does not infer missing completions/count mismatches from its prefix.
Original pcap and historical reports remain unchanged.

Content alignment independently selected offset zero: USB reconstruction has
5553160 stereo frames versus 5551088 received frames (2072 trailing USB frames).
Exactly 1270 frames differ in 98 groups, maximum channel difference 5560. Each
differing received frame matches data 2048 frames earlier in both streams;
outside those frames the aligned streams match exactly. Received/accepted PCM
are byte-identical. These differences affect 67 reads/packets; the application
queue was empty at those reads. The measured ALSA buffer is 2048 frames and our
application ring is 8192 frames. This narrows the path but is not a causal proof.

Our service path reads directly into a bounded nonoverlapping ring span, copies
Read evidence immediately by value, accounts only successful frames, then writes
and copies accepted evidence. The SPSC queue publishes/releases copied records;
the writer does not retain application-buffer pointers. No concrete defect was
reproduced in this path, so no production correction or tuning is applied.
The [vendor reference capture callback](https://github.com/christianhaitian/linux/blob/daf063fc503be035a3df68ad59209ee62c57cfc9/sound/usb/pcm.c#L1282)
updates/unlocks `hwptr_done` before copying into the PCM ring; its pointer callback
can expose that position to a concurrent reader. This is a specific ordering-race
candidate consistent with old ring content, not a verified diagnosis. The source
reports 4.4.189 but its identity with the console's running binary is unverified.
A shared recording-memory fault is still not excluded by two identical exports.
The smallest next discriminating observation is an independent read-return digest
before `Trace::Emit`, checked against its queued/written bytes and aligned USB,
plus exact running-kernel source identity. This is proposed only, not implemented
or captured in this change. usbmon remains HCD evidence, not on-wire certainty.

### Opt-in immediate read-return integrity

From the updated console checkout, while Wi-Fi is available, run:

```sh
python3 -B tools/alsa_bridge/prepare_offline.py --read-integrity
```

This enables the existing USB option plus independent read digests through the
same Ports entry and generated companion setting. It performs the existing
noninteractive sudo/USB preflight; no manual configuration edits are needed.
Disconnect Wi-Fi, connect Teensy, and select **Ports > BroTracker Bridge Clicks**.
The workload, phase timers, emergency B, cleanup and automatic return are unchanged.
Without this explicit option the added read path is disabled. Re-running normal
preparation disables it; `--usbmon` alone retains USB capture without digests.
Matching ARM64 artifacts/fingerprints are required; the bridge advertises the
`read_integrity: 1` capability. No firmware change is needed.

Immediately after every positive `snd_pcm_readi`, before PCM tracing or any other
consumer, the bridge hashes exactly `returned_frames * 4` bytes. It uses FNV-1a
64: offset basis 14695981039346656037, XOR each byte then multiply by
1099511628211 modulo 2^64. It covers every byte in original S16_LE order, with
no normalization or skipped samples. Each observation has a contiguous ID starting
at 1, absolute captured-source frame offset, returned frame count and digest.
Negative/zero reads have no observation. Short reads hash only returned bytes.
The hash is noncryptographic: collisions are possible, especially for adversarial
inputs, and equality does not prove byte equality or exclude common-mode faults.
No guaranteed 2^-64 collision bound is claimed for structured PCM.

A separate fixed 4096-record SPSC queue carries values, never application-buffer
pointers. The audio loop performs at most a 1024-byte digest and fixed publication;
no allocation, I/O, mutex or wait is added there. A separate writer saves
`read-integrity.tsv` and a flushed completion marker beside `received.s16le`.
At most 1,000,000 records are accepted (under 100 MiB text), also bounded by the
existing recording lifecycle. Queue/full-record-limit/writer failure stops later
acceptance and marks lost/incomplete observations; audio continues. Missing or
partial markers are UNKNOWN. This is independent of the existing PCM trace queue,
not independent hardware truth. Hash work and another writer/thread add observer
load only to diagnostics-on; that load is recorded and must be considered.
Audio parameters, PCM, recovery policy and diagnostics-off remain unchanged.

Offline `on/analysis.json` checks contiguous IDs, exact positive-read boundaries
from transfers.tsv, source offsets, frame totals and completion/loss counts,
then independently recomputes every digest from saved received PCM. It requires
complete PCM and aligned full USB coverage before reporting:

- `READ_RETURN_DIFFERS_FROM_USB`: every read digest matches saved PCM, which
  differs from USB. The differing bytes were present at the immediate post-read
  observation, subject to collision/common-mode limitations; this does not name
  ALSA, the kernel or Teensy as the cause.
- `READ_RETURN_MATCHES_USB`: complete matching read/PCM/USB evidence.
- `RECORDER_PATH_MISMATCH`: complete read evidence contains digest mismatches
  against saved PCM. Investigate the subsequent application/recording path.
- `UNKNOWN`: missing, overflowed, malformed or inconsistent evidence, or missing
  aligned USB coverage. Never a successful boundary comparison.

A requested UNKNOWN or recorder mismatch fails the overall attempt while retaining
PCM, USB and digest evidence. Technical acquisition/comparison completion and
listening/cause confirmation remain separate. Independent offline replay:

```sh
python3 -B tools/alsa_bridge/analyze_clicks.py ATTEMPT/on/bridge-XXXXXX \
  --usbmon ATTEMPT/on/usb --read-integrity --output ATTEMPT/read-replay.json
```

Existing metadata now also collects `/proc/version`, a hash of `/sys/kernel/notes`,
loaded module versions/srcversions/build-note hashes and available `modinfo` for
snd_usb_audio, snd_pcm and usbcore. Unavailable/built-in/restricted fields are
unknown with reasons. Installed module information may differ from loaded code;
these identifiers do not verify a match to a source repository. The suspected
kernel publication-before-copy race remains unconfirmed and is not patched.
No physical run of this new read-return observation has been performed.

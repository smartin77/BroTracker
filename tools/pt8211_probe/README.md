# PT8211 empirical pin-loading experiment

## Current wire protocol: version 2

Version 2 replaces all Print::printf/vdprintf emission. Integer-only checked
vsnprintf formats JSON payloads in a 512-byte static buffer; a checked snprintf
builds the sequence/CRC envelope in a separate 768-byte static buffer. CPU
values are formatted from rounded millionths, without floating printf/dtoa.
The payload and frame remain immutable until an exact `ACK <seq> <crc32>`.
Serial writes use actual accepted counts, with at most 64 bytes per attempt.
There are no whole-record retransmissions: short/zero writes retain the original
offset, while missing/invalid ACKs stop the dump. The ACK deadline is 2 seconds;
the whole dump deadline is 60 seconds. A sticky failure stops subsequent records
and attempts an explicit `!ABORT` notification for at most 100 ms. No measurement
is regenerated or silently repaired. These operations occur after collection.

Each wire line has protocol=2, a contiguous sequence starting at zero, a CRC-32
(IEEE/zlib) over the exact payload bytes, and the payload object. The end payload
contains the total record count and cumulative CRC-32 of all preceding payloads
concatenated in order. The host checks these before accepting the dump and checks
pin/control/cadence ordering and control timestamps. Raw bytes remain verbatim
in raw.jsonl; results.json contains decoded measurements. transport.jsonl records
every command/ACK write, including short/zero writes; the end record includes
firmware short/zero-write counters and record_retries=0. Metadata schema=2 and
wire_protocol=2 distinguish these runs from older captures. Older schema-1 data
remain readable, but must never be mixed with schema-2 runs in comparison.
The source identity now covers main.cpp, record_transport.h and select_source.py.

### Corruption investigation

The preserved 20261018T203156.489539Z-795ee202 stream jumps from control 3400 ms
into the middle of control 4800 ms at line 420. Comparing the corresponding
intact control sequence shows a 1536-byte missing span, including whole records;
this is not merely a bad float conversion. The host does not modify received
bytes before writing raw.jsonl. The prior host acknowledged no records, and
count-only validation could not detect all balanced losses/duplicates.

Installed Teensy4 Print.cpp delegates Print::printf to libc vdprintf, whose
_write adapter forwards subwrites to the Print object. The old code ignored
printf's result; its wrapper also did not make failure sticky across records.
USB Serial write returns bytes copied/queued, not end-to-end delivery. Its
120-ms timeout can return a short count or zero. availableForWrite reports an
aggregate free-buffer estimate; it is not a reservation or delivery guarantee.
USB auto-flush uses tx_noautoflush to serialize its callback with foreground
buffer changes. This diagnostic has one foreground serial producer; its Audio
ISR does not format or transmit records. No interrupt-reentrancy defect was
demonstrated by the saved trace.

Installed pyserial serialwin32.py requests a 4096-byte Windows RX queue and calls
ClearCommError in read/in_waiting, but does not surface the returned error flags.
Host scheduling/receive overrun is a plausible explanation for the packet-sized
gap; it is **not proven**, because old captures recorded no driver error flags
or end-to-end ACKs. The trace cannot identify whether loss occurred in firmware,
USB/driver or host delivery. Version 2 removes the unchecked formatting path and
unbounded producer lead, and detects loss end-to-end. The host additionally
requests a 1 MiB Windows RX buffer; correctness relies on flow control/integrity,
not that capacity request. No installed core or pyserial package is modified.

### Pending future scope

- Intended automatic output priority: Audio Shield > PT8211 > USB Audio bridge.
- A manual configuration override remains required.
- Audio Shield diagnostics are pending hardware availability and validation;
  they are neither passed nor evidence of absence.
- Later validation requires repeated attached/detached passes and matched runs
  with and without ADuM3160. No shield probing, thresholds, production routing
  or configuration system is implemented here.

Capture and comparison do not infer module presence. The separate opt-in host
classifier below reports an experimental loading signature. Hardware labels
are supplied by the operator. Production audio routing is unchanged.
GPIO 2 and attachment straps are outside this experiment.

## Experimental host loading classifier

`loading_classifier.py` is an opt-in analysis of one saved, integrity-validated
protocol-2 run. It does not run during capture, change firmware, or select an
audio route. Output is `PT8211_LIKE`, `UNLOADED`, or `UNKNOWN`. PT8211_LIKE means
only a loading signature similar to this tested setup; UNLOADED means the
calibrated unloaded signature, not proven physical absence. Neither verifies
chip identity, module power, signal correctness or DAC health. Other attached
loads could mimic either signature. Audio Shield recognition is pending hardware.

```powershell
python tools/pt8211_probe/loading_classifier.py classify '<one explicit run directory>'
python tools/pt8211_probe/loading_classifier.py evaluate
```

Evaluation selects ONLY the 13 paths in
`pt8211_protocol2_four_conditions_20261018T213803Z-66f1eb0d.json`. The frozen
`loading_profile.json` includes that report's SHA-256, original run paths,
observed extrema, bands, firmware identity and measurement parameters. Evaluation
also checks original capture fingerprints. It creates uniquely named JSON and
Markdown reports under `assets/hardware_diagnostics/`. Existing same-topology
comparison checks remain unchanged; classifier calibration explicitly covers
the two labelled topologies `none` and `adum3160` for this one setup.

Calibration is tied to this Teensy 4.1/module/wiring and the exact protocol-2
metadata, including 600 MHz, source identity
`a9179c5f093605fd51845dbfa8c064604089083de9e3930fcb4805c6504fb805`,
build timestamp and 64 trials/direction. It rejects old/recompiled firmware,
changed timing parameters, invalid wire integrity and unvalidated topology
labels. No frequency rescaling, recalibration or label-dependent decision is
performed during classification. Manual attachment labels are used only AFTER
classification to score evaluation.

The primary decision gates are the three pull-down medians, where separation
is largest. All three must support the same signature. All three pull-up
measurements must corroborate it; they cannot outvote or compensate for failed
pull-down evidence. There is no majority vote or confidence score.

Proposed inclusive **median bands**, in CPU cycles:

| Feature | UNLOADED | PT8211_LIKE |
|---|---|---|
| GPIO 7 down | 477..500 | 709..732 |
| GPIO 7 up | 162..178 | 190..206 |
| GPIO 20 down | 477..493 | 737..753 |
| GPIO 20 up | 162..178 | 190..206 |
| GPIO 21 down | 621..637 | 853..869 |
| GPIO 21 up | 190..206 | 218..234 |

Derivation: for each signature/feature, take minimum/maximum per-boot medians
over both topologies, then expand each endpoint by 8 cycles. Eight is the largest
observed within-label between-boot median spread (7 cycles, GPIO 7 down) plus a
one-cycle engineering guard. It is provisional padding, **not a confidence
interval**. At 600 MHz it is approximately 0.0133 microseconds.

Two further bounds apply independently: p05/p95 must fit the observed minimum
per-boot p05 / maximum per-boot p95 envelope expanded by 8, and EVERY raw trial
must fit the observed raw minimum/maximum expanded by 8. Exact central/raw
bands are in loading_profile.json and the evaluation report. Quantiles use
sorted index floor((n-1)*q), matching existing tooling. Median, p05 and p95 are
robust statistics, but the hard raw guard deliberately fails closed rather than
discarding out-of-profile outliers. Thus arbitrary larger delays are never
classified as PT8211_LIKE.

Any missing/extra trial, timeout, already-at-target initial state, inconsistent
final/settled observation, unsupported profile, mixed pin evidence, gap value
or upper/lower band violation yields UNKNOWN. Zero-margin boundary values are
accepted. Per-feature and per-run margins are distances to the nearest median,
central or raw boundary; they are not probabilities or measured generalization
accuracy.

The original 13-run calibration replay reported 7 PT8211_LIKE and 6 UNLOADED, with minimum
acceptance margins of 8 cycles. This is **in-sample agreement**, since these runs
also defined the bands. Supply/temperature changes, other wiring, devices,
external loads remain unvalidated. Initial held-out validation is completed as
described below; collect further multi-pass data, matched topologies and more cold boots before
considering firmware diagnostics. Production routing/configuration integration
and the required manual override remain separate future work.

## Procedure (PowerShell, repository root)

Close BTX and serial monitors. Use the same USB cable, isolator, power supply,
module wiring and audio load for both conditions. Every physical wiring change,
module removal/attachment or USB topology change requires **all power disconnected**.
Only the powered Teensy and PT8211 input pins may be connected to 7/20/21;
do not connect another device that drives these pins.

```powershell
pio run -e teensy41_pt8211_probe -t upload
$ProbePython = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
& $ProbePython -m serial.tools.list_ports
$ProbePort = 'COM5' # Replace with the diagnostic Teensy's current port.
& $ProbePython tools/pt8211_probe/probe.py capture --port $ProbePort --label attached --isolator adum3160
```

Capture sends GO once; firmware runs once per boot. Do not rebuild or upload
between conditions. Disconnect **all power**, remove the PT8211, restore the same
USB setup and power up. Recheck the COM port, then:

```powershell
& $ProbePython tools/pt8211_probe/probe.py capture --port $ProbePort --label detached --isolator adum3160
New-Item -ItemType Directory -Force build/pt8211-probe | Out-Null
& $ProbePython tools/pt8211_probe/probe.py compare | Out-File -Encoding utf8 build/pt8211-probe/comparison.json
```

For between-boot repeatability, power-cycle and capture at least three runs per
condition. Each capture creates a unique timestamp/UUID directory under
`assets/hardware_diagnostics/pt8211_attached/` or `pt8211_detached/` containing
raw.jsonl, results.json, summary.json and manifest.json. Failed captures retain
raw bytes and an incomplete manifest. Power-cycle before retrying. Comparison
rejects incomplete runs; use `--attached <run-directory>` and
`--detached <run-directory>` to select a particular valid pair, or point at
directories containing only the intended runs. Firmware metadata and isolator
labels must match. Output contains per-run distributions, bitmask frequencies,
timeouts, medians, ranges, standard deviations and between-run repeatability.

If the ADuM3160 prevents upload, upload with a direct USB connection, then power
down and restore the isolator before **both** captures. If capture itself fails
through the isolator, repeat **both** conditions without it, using `--isolator none`.
Compare those specific run directories to avoid mixing USB setups. No firmware
change is needed. PlatformIO's Python normally already includes pyserial; verify
with `& $ProbePython -c "import serial; print(serial.__version__)"`.

## Measurements and limits

The source SHA-256, build timestamp, Teensyduino version, CPU frequency and all
sampling parameters are sent by firmware. Use the identical uploaded build for
both conditions. Keep the compiled ELF/HEX in `.pio/build/teensy41_pt8211_probe/`.

Before any Audio objects are constructed, each pin (7, 20, 21) undergoes 64
trials in each direction. The other two pins remain pulled down. Each trial:

1. Select the opposite internal pull as an input; settle 100 us and record initial level.
2. Timestamp with DWT CYCCNT, switch to the target input pull, poll until the
   target level or a 100 us deadline. Preserve elapsed cycles, final level and timeout.
3. Settle 100 us; record eight digital observations, spaced 10 us apart, as a bitmask.

Only step 2 masks interrupts (previous PRIMASK is restored). The timeout is
bounded by the deadline plus a polling iteration; this occurs before audio.
No GPIO OUTPUT mode or strong charge/discharge pulse is used during probing.
These are weak-pull threshold transition timings, **not capacitance measurements**.
They include pinMode/read overhead, GPIO synchronization, hysteresis, internal
pull variation and loop quantization. Already-at-target initial states are
reported and excluded from successful-transition distributions. Timeouts remain
in raw elapsed distributions and are excluded from successful-transition statistics.
Small differences near the overhead floor may not be resolvable by this method.

Pins return to INPUT without pulls before the PT8211 driver configures the normal
peripheral mux. The graph is placement-constructed once in aligned static storage.
The PT8211 output claims audio updates before AudioMemory(16), avoiding its
dynamically allocated fallback timer. No Audio global constructor runs early.

Control phase: ten seconds, 100 foreground snapshots at 100 ms intervals, with
raw per-update DWT intervals (4096 static entries), callback count, Audio CPU
current/peak and Audio memory current/peak. Audio update instrumentation does
bounded fixed work and never prints/allocates. Initial silence 1512 ms, L440 Hz
250 ms, silence 512 ms, R880 Hz 250 ms, repeat; 10 ms envelopes, amplitude .08.
Afterward noteOff() starts envelope release and the retained records are emitted
immediately; serial emission does not wait for release to complete.
The host reads available USB bytes in bulk and buffers newline framing across
timeouts. Current transfer limits and integrity behavior are specified in the
version-2 section above. Failed raw captures are preserved.
CPU measures AudioStream processing, not total CPU utilization or all DMA work;
memory measures Audio block use, not total RAM. Cadence measures graph updates,
not analog samples. These controls can be identical with a completely absent DAC.

The installed framework inspected was framework-arduinoteensy 1.162.0:
`cores/teensy4/digital.c` implements PUS(3) pull-up/PUS(0) pull-down and input
direction; `AudioStream.cpp` provides update ownership and fallback timer;
`libraries/Audio/output_pt8211.{h,cpp}` starts DMA/SAI in the constructor and
configures pins 7/20/21. These installed sources are not modified.

Physical digital captures and authorized uploads have now been performed.
Analog listening/output quality is not established by these control metrics.
An observed difference requires repeated controlled trials and independent
validation; it does not prove DAC health or establish a reliable detector.

## Offline verification

```powershell
python -m unittest discover -s tools/pt8211_probe -v
pio run -e teensy41_pt8211_probe
```

Decision tests in `test_loading_classifier.py` use explicitly synthetic,
deterministic records derived from the frozen bands. Transport tests use a mocked
serial device and a native C++ transport harness. Synthetic fixtures are never
saved or counted as physical evidence. `test_physical_replay.py` separately
revalidates original captures and the calibration report fingerprint/band
derivation. Missing physical cases are explicitly skipped with missing paths;
skips are not passes. Full 13-run replay remains blocked by missing originals.
Run decision tests alone without any historical captures:

```powershell
python -B -m unittest discover -s tools/pt8211_probe -p test_loading_classifier.py -v
```

## Software-reset repeatability

On this Windows setup, PJRC `teensy_reboot.exe -s` with the Teensy Loader already
configured to program/run the diagnostic successfully started a new boot with
the same HEX. Close capture before running it, wait for COM4 to reappear, then
capture again. The installed `teensy_loader_cli -s` reports that its soft reboot
is not implemented for Win32; it is not the reboot method used for these runs.

```powershell
& "$env:USERPROFILE/.platformio/packages/tool-teensy/teensy_reboot.exe" -s
```

These are separate software-reset boots, not cold power cycles. Three attached,
direct-USB runs were captured on COM4 on 2026-10-18 with the output-retry firmware.
The full results and raw-integrity checks are in
`build/pt8211-probe/attached-three-boots.json` (historical reference, currently
unavailable). The original raw datasets and earlier 21,504-byte failed capture
were subsequently deleted by the user and have not been recovered. The firmware retry fix and host bulk reads resolved the
truncation in these three runs. No detached measurements or analog audio
verification were performed during this capture session.

## Protocol-2 physical validation (2026-10-18)

Three attached/direct-USB COM4 captures from separate software-reset boots
completed with 3930 records each. Every payload CRC, sequence, end count and
cumulative stream CRC passed, alongside raw SHA and decoded-results checks.
There were no whole-record retries and no short/zero writes reported before
the end record. Full run paths, counters and unchanged measurement parameters
are saved in `assets/hardware_diagnostics/pt8211_integrity_v2_validation_20261018.json`.
This historical report and those original baseline/failed captures are currently
unavailable; recovered evidence is listed below. No missing raw data was reconstructed.

The measurement collection code was checked byte-for-byte against the prior
version (normalizing newlines). All collection parameters match. Despite this,
the recompiled firmware has slightly different raw pin cycle medians; code
layout/compiler effects are a possible explanation, not a proven cause. Treat
this as a new firmware baseline; match builds when comparing hardware states.

The true cold-power-up check was subsequently completed and reported for attached
run `20261018T210730.652396Z-fd65f4b4`. Its original capture is now unavailable;
this is a historical reported result, not a currently independently verified pass.

## Held-out validation and surviving original evidence

The frozen classifier was evaluated on three attached and three detached held-out
ADuM3160 boots, outside the 13-run calibration set. Historical predictions were:
attached **PT8211_LIKE, PT8211_LIKE, UNKNOWN**; detached **UNLOADED** three times.
The UNKNOWN must remain recorded. Its reported GPIO 21 pull-down raw trial was
**806 cycles**, below the hard lower bound 853 (margin **-47 cycles**), although
its median was 861 and its median/central margins were +8. The hard all-trial
guard is more sensitive to a single outlier than median/p05/p95 evidence.
Neither this result nor the frozen bands have been changed.

Currently verified original evidence comprises three attached ADuM3160
calibration runs, the first two attached held-out runs, and all three detached
held-out runs. Replaying these eight original captures verifies sequence,
record CRC, cumulative CRC, count, raw SHA and decoded-results agreement:
five PT8211_LIKE and three UNLOADED, each with minimum margin 8 cycles.
The third attached held-out run `20261018T215809.161405Z-93ee62f3` has no available
original raw/manifest/results; its UNKNOWN and 806-cycle trial are previously
reported results that cannot currently be independently revalidated.
The second attached held-out run lacks its original `transport.jsonl`; wire
integrity can still be checked from raw/manifest/results, but host-write audit
cannot be replayed. No transport log was invented.

Available originals were restored byte-for-byte from the preserved review/recovery
ZIPs. Identical files were deduplicated; differing files would be left untouched
and reported. The restored four-condition report matches the profile's frozen
SHA-256, but **full 13-run calibration replay is blocked by ten missing original
capture directories**. A report summary is not an original measurement.

See `assets/hardware_diagnostics/pt8211_evidence_inventory.json` for restored
paths, source archives, fingerprints and conflicts, and
`assets/hardware_diagnostics/MISSING_EVIDENCE.md` for missing paths. The historical
combined report is preserved; the later
`pt8211_heldout_surviving_findings_20261019T165131Z-dc6651db.{json,md}` distinguishes
direct verification from historical reported evidence. Archives and existing
captures remain preserved. Git marks this evidence directory `-text` to avoid
newline conversion invalidating hashes; do not renormalize it.

Production routing/configuration remains unchanged. Future automatic output
priority remains **Audio Shield > PT8211 > USB Audio bridge**, with a required
manual override and visible UNKNOWN state. Shield recognition remains pending
physical hardware; further matched attached/detached passes, topology/cold-start
and environmental validation precede integration. Loading signatures establish
neither verified chip identity nor DAC health.

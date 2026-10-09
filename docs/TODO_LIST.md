# TODO List

This document contains active implementation tasks and deferred implementation, cleanup and refactoring work.

It does not replace the project roadmap or milestone planning.

Items may be added during development to record agreed next steps or work that need not interrupt the current development step.

## Next Development Steps

ArkOS is the priority UI host for BroTracker Terminal (BTX). The current hardware
setup is a bare Teensy 4.1 with the existing USB audio path to the host. These
steps refine the broader roadmap; planned playback behavior still requires
implementation and validation.

### Musical Scheduler Timing

* [x] Implement the sample-timeline foundation: initialization, reset and counting
  processed samples in `libraries/scheduler/src/`, advanced from
  `AudioTestSource::update()`. `tests/test_scheduler.cpp` covers sample counting,
  reset and equivalent totals across partitions; this is not a musical scheduler.
* [x] Implement absolute tick-to-sample conversion in
  `libraries/scheduler/src/musical_timing.*`, with coverage in
  `tests/test_scheduler.cpp`. Internal tempo uses integer `tempo_hundredths`
  (0.01 BPM resolution), independent of the UI's one-decimal display precision.
  Exact rational conversion floors the absolute result without cumulative
  rounding drift; zero tempo/sample rate is rejected, overflow is explicit and
  failures leave the output unchanged. Conversion tests cover fractional tempo
  (127.50 and 127.53 BPM), long-run positions, query-order independence, multiple
  sample rates and overflow. This component is not integrated into firmware.
* [ ] Extend the counter and conversion foundations with musical progression and
  event scheduling per
  [D0029](ARCHITECTURE_DECISIONS.md#d0029---timing-model-and-microtiming) and
  [SCHEDULER.md](SCHEDULER.md): four rows per quarter note and 96 internal ticks
  per row (384 per quarter note), already defined as conversion constants.
  Tick/row progression, scheduled events and sample offsets within processing
  blocks remain unimplemented; retain the conversion's 0.01 BPM internal precision.
* [ ] Implement block event dispatch and verify event timing is independent of
  block partitioning, including fractional BPM, long-run accuracy and events
  exactly on block boundaries (no duplicates or missed events). Existing counter
  and conversion tests do not validate block event dispatch or boundary behavior.
* [ ] Keep realtime scheduling and dispatch bounded and free of heap allocation,
  blocking I/O and dependence on UI refresh or host audio routing; verify the
  processing budget on Teensy after implementation. Conversion is bounded and
  allocation-free, but Teensy processing-budget measurement remains outstanding.

### Minimal Pattern Playback on Teensy

Two active sample channels are the initial test scope; the baseline eight-channel
design remains unchanged.

* [x] Implement RAM sample primitives: `LoadWavSampleFromSd()` loads supported
  16-bit mono 44.1 kHz PCM, and `SamplePlayer::SetSample()` / `Play()` support
  one-shot playback and restarting at the first frame. Evidence is in
  `wav_loader.*` and `sample_player.*`; scheduled pattern behavior is not validated.
* [ ] Prepare short samples in RAM before playback and connect two active channels
  to the musical scheduler. Two players and a mixer already exist in
  `platform.cpp`, but the current SD-streamed WAV test sequence is not pattern
  playback and does not use the RAM loader for this purpose.
* [ ] Implement a bounded Teensy realtime pattern representation and playback of
  one looping 16-row pattern. Host `Pattern`, `Channel`, `Event` and `Tune` data
  already exist, using vectors; they are not a Teensy realtime representation.
  `src/runtime/playback_engine.h` remains a stub.
* [ ] Implement pattern START/STOP with defined reset, loop and channel-silencing
  behavior. Existing `BTTEST1 START` / `STOP` control only the diagnostic WAV
  sequence; pattern transport remains unimplemented.
* [ ] Verify scheduled trigger sample positions, retriggering, simultaneous events,
  row 16-to-row 1 looping and STOP (including stopping active samples and preventing
  further triggers), first in deterministic tests and then on the current hardware.

### Live Playback State in BTX

* [x] Implement diagnostic transport-state exchange and reconnect handling:
  `platform.cpp` reports `BTTEST1` sequence state; `src/ui/bringup_serial.*` handles
  bounded buffers/receive work, HELLO/STATUS, START/STOP and reconnect without
  replaying START. Existing `tests/test_bringup_serial.cpp` and
  `tests/test_bringup_controls.cpp` cover diagnostic command ordering, state and
  reconnect behavior; this exchange has no pattern row reporting.
* [ ] Publish coherent Teensy realtime snapshots of pattern transport state and
  current pattern row safely to non-realtime communication code. Use bounded
  messages and service work; keep serial I/O out of playback processing.
* [ ] Display Teensy-reported pattern state and row in BTX, replacing the fixed
  position visualization in `src/ui/pattern_screen.cpp` (row 13 and fixed POS).
  Diagnostic state is already available through `BringUpSerial`, but live pattern
  state and row display remain unimplemented.
* [ ] Recover authoritative pattern state after reconnect without automatic START;
  verify disconnect/reconnect and slow or irregular UI refresh. Playback timing
  must remain independent of UI refresh rates and communication availability.

### Basic Pattern Editing on ArkOS

* [x] Implement host-side note/instrument pattern data, JSON loading and preview
  rendering. Evidence: `src/core/{pattern,channel,event,tune}.h`, the tune loader,
  `src/ui/pattern_screen.cpp` and `tests/test_tune_loader.cpp`. The preview uses a
  fixed position and does not provide interactive editing or realtime publication.
* [ ] Implement ArkOS row/channel/field navigation and note/instrument entry with
  an edit cursor distinct from the live playback row. Host input and diagnostic
  controls exist; basic pattern editing remains unimplemented.
* [ ] Send validated edit commands to Teensy: validate pattern, row, channel,
  note and instrument values, bound command buffering and handling, and report
  acceptance or rejection to BTX. The current diagnostic exchange has no edits.
* [ ] Define explicit edit-application timing (including edits during playback) and
  safely publish accepted edits to the Teensy realtime pattern without partial
  reads, heap allocation or blocking in playback. Verify invalid commands,
  edits while stopped/playing and application at the defined boundary.

### Later UI Host Compatibility

* [ ] Validate dArkOS compatibility separately after the ArkOS path is established,
  including BTX deployment, input, USB communication and audio routing. This has
  not been validated by the existing ArkOS hardware evidence.

## Deferred Tasks

### Audio Diagnostics

* [x] Resolve recurring Teensy USB audio cold-boot corruption; hardware validation
  run `20261006-220244.Ph7ox3` passed. See the [validation record](../tools/teensy_usb_audio/README.md#hardware-validation-20261006-220244ph7ox3).
* [ ] Investigate intermittent ArkOS audio clicks/pauses and ALSA capture/playback
  xruns as a separate issue. Do not reopen the resolved cold-boot corruption
  without new evidence; see the same validation record for remaining observations.

* [ ] Move the current `AudioTestSource` test path from `firmware/teensy/BroTracker/` to `tools/teensy_diagnostics/`.

  * Keep it available as a Teensy audio hardware verification tool.
  * Keep production BroTracker runtime code separate from diagnostic/test-only code.
  * Update the build/integration path and documentation as necessary.

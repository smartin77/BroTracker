# TODO List

This document contains active implementation tasks and deferred implementation, cleanup and refactoring work.

It does not replace the project roadmap or milestone planning.

Items may be added during development to record agreed next steps or work that need not interrupt the current development step.

## Next Development Steps

ArkOS remains the priority target platform for BroTracker Terminal (BTX), with
follow-up hardware validation. Windows BTX + the bare Teensy 4.1 is the primary
development bring-up/test setup, using the existing USB audio path to the host.
dArkOS compatibility validation remains deferred. These
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
  sample rates and overflow. Used by the opt-in pattern firmware; production
  integration remains open.
* [x] Implement bounded, pull-based tick enumeration over consecutive half-open
  sample blocks in `libraries/scheduler/src/musical_tick_cursor.*`, with coverage
  in `tests/test_scheduler.cpp`. Each tick has an absolute tick index, absolute
  sample position and block-relative sample offset; coincident ticks are preserved
  in tick order. Tests cover partition independence and boundary ownership
  (block start included, block end deferred), empty blocks, reset and explicit
  errors/exhaustion. Invalid operations preserve pending ticks; arithmetic
  exhaustion requires reset/reconfiguration and never wraps counters. The cursor
  preserves Clock / Sync separation per D0035 and `CORE_ARCHITECTURE.md`: it uses
  the caller's logical sample timeline without selecting a physical clock or
  synchronizing clock domains. Used by the opt-in pattern firmware; production
  integration and measured Teensy execution cost remain open.
* [x] Implement stateless logical pattern-position mapping with
  `TickToPatternPosition` in `libraries/scheduler/src/pattern_position.*`, with
  coverage in `tests/test_scheduler.cpp`. Returns absolute row, tick within row,
  looping pattern row and loop index; row-start detection is derived from tick
  within row being zero. Row and loop indices are zero-based. Pattern length is
  explicit (1 through UINT64_MAX rows); zero length is rejected with output
  unchanged, and the full uint64_t tick range is supported. Tests cover row/loop
  boundaries, query-order independence and composition with `MusicalTickCursor`
  across regular and irregular block partitions, preserving sample positions and
  block-relative offsets. This maps positions without reading pattern contents
  or advancing a separate timeline.
* [ ] Integrate the timing, raw row-command and logical channel-state foundations
  with Teensy playback integration and BTX transport control, including realtime firmware
  integration per
  [D0029](ARCHITECTURE_DECISIONS.md#d0029---timing-model-and-microtiming) and
  [SCHEDULER.md](SCHEDULER.md): four rows per quarter note and 96 internal ticks
  per row (384 per quarter note), already defined as conversion constants.
  Tick enumeration, sample offsets within processing blocks and logical looping
  pattern-position mapping are complete; raw row-command generation now exists in
  core playback (see below), as does logical note/instrument continuation.
  Prepared audio-command dispatch/rendering and core mono mixing now exist (see
  below). Instrument lookup and tracker-to-audio command preparation now exist
  for the native-rate sample subset, now orchestrated by `NativeRatePatternPlayer`.
  Opt-in Teensy integration, temporary BTX control and a bring-up fail-stop policy
  exist; production integration, final transport/underrun policy and full hardware
  validation remain open. Retain the conversion's 0.01 BPM internal precision.
* [ ] Integrate production playback-event dispatch and verify event timing is independent of
  block partitioning, including fractional BPM, long-run accuracy and events
  exactly on block boundaries (no duplicates or missed events). Completed cursor
  tests validate tick partition independence and boundary ownership; row-event
  tests validate raw command partition independence. Prepared Trigger/Stop dispatch
  and PCM rendering are tested, including cursor/row-event composition with test-only
  translation. The native-rate sample preparer also has end-to-end mixed-PCM
  tests across partitions and loops, as does the core native-rate pattern player.
  Opt-in firmware integration exists; production integration and hardware timing
  validation remain outstanding.
* [ ] Keep realtime scheduling and dispatch bounded and free of heap allocation,
  blocking I/O and dependence on UI refresh or host audio routing; verify the
  processing budget on Teensy after integration. Conversion, each cursor operation,
  position mapping and raw row-command generation are bounded and allocation-free,
  but draining every configuration within a realtime budget is not established.
  Teensy execution-cost
  and processing-budget measurement remain outstanding.

### Minimal Pattern Playback on Teensy

Two active sample channels are the initial test scope; the baseline eight-channel
design remains unchanged.

* [x] Implement `RealtimePattern` in `src/core/playback/row_events.h`, with coverage
  in `tests/test_row_events.cpp`: initial fixed capacities of 16 rows and 8 channels,
  explicit positive active dimensions within those bounds and empty `Event`
  defaults (`NOTE_EMPTY`, instrument `0xFF`). These capacities are not permanent
  format limits. Consumption assumes an immutable pattern; concurrent editing and
  conversion from the host Pattern/JSON representation remain outside this foundation.
* [x] Implement stateless `GenerateRowEvents` in `src/core/playback/row_events.h`,
  with coverage in `tests/test_row_events.cpp`. Row-start ticks generate raw
  commands for nonempty active cells in ascending channel order, in a fixed batch
  sufficient for all eight channels. Original note/instrument values, including
  `NOTE_OFF`, instrument-only updates and no-instrument-update `0xFF`, are preserved
  without instrument-state resolution or sample triggering. Commands carry channel,
  pattern row, loop index and exact cursor tick/sample positions and block offsets.
  Dimensions are validated on every call; notes are validated only in active
  channels of the consumed row at row-start ticks. Failure leaves the entire batch
  unchanged; success with no commands returns an empty batch. Tests cover raw command
  partition independence at 127.53 BPM across regular and irregular cursor blocks,
  loop boundaries, channel order and validation. The caller supplies each tick once.
* [x] Implement `ChannelPlaybackState` in `src/core/playback/channel_state.h`, with
  coverage in `tests/test_channel_state.cpp`: fixed logical note/instrument state
  for eight channels, initially `NOTE_EMPTY` / `kNoInstrumentUpdate`. Empty fields
  preserve previous state; `NOTE_OFF` preserves the selected instrument, and
  instrument-only updates preserve the note. Explicit repeated note/instrument
  updates remain identifiable through field-presence flags. Application results
  include the original event with timing metadata and before/after snapshots;
  read-only state access has explicit bounds handling. Reset clears all channels;
  state persists across pattern loops without implicit reset. Channel/note inputs
  are validated before mutation, with all state and result output unchanged on
  failure. Tests cover continuation, reset, repeated updates, channel independence,
  invalid input and composition across loops and regular/irregular block partitions.
  Consumption is ordered and single-owner, with each event applied once. Raw
  instrument IDs remain uninterpreted; no default instrument or sound is implied.
* [x] Implement the core `RamSampleVoice` in `src/core/audio/ram_sample_voice.h`,
  with coverage in `tests/test_ram_sample_voice.cpp`: a bounded, allocation-free,
  single-owner one-shot voice for native-rate PCM16 mono samples. Sample views are
  immutable and non-owning; the caller guarantees sample lifetime, valid source
  and destination memory extents and non-overlap. Configure sets the explicit
  output rate and releases playback; trigger/retrigger starts at frame zero,
  including replacement. Segmented rendering copies PCM unchanged, zero-pads
  after completion and releases the sample view at completion or stop. Stop makes
  subsequent output silent; reset also releases playback while retaining the rate.
  Rate mismatches and invalid inputs are explicit errors preserving voice state;
  invalid renders leave the destination unchanged. Render work is bounded by the
  caller-supplied span; zero-length rendering is a no-op. Tests cover completion,
  render partition independence, retriggering, replacement, stop/reset, invalid
  operations and trigger/stop at supplied offsets through split render spans,
  including offset zero and a block boundary. These offset tests split rendering
  in the caller; they do not establish scheduler-driven dispatch or measured
  Teensy execution cost. This core voice is separate from the firmware primitives below.
* [x] Implement `RamVoiceBlockRenderer` in `src/core/audio/ram_voice_block_renderer.h`,
  with coverage in `tests/test_ram_voice_block_renderer.cpp`: eight fixed
  `RamSampleVoice` instances render separate PCM16 mono outputs from a fixed batch
  of up to 16 explicit Trigger/Stop commands. This is an initial implementation
  capacity, not a permanent pattern-format limit. Commands use exact block-relative
  offsets in half-open blocks (end-boundary commands belong to the next block),
  preserving input order at equal offsets. Voices continue across blocks;
  configuration clears playback and reset retains the configured rate.
  Whole-request validation precedes mutation; rejection preserves all voice states
  and destination data. The caller guarantees sample lifetimes, valid memory extents
  and non-overlap. Tests cover exact PCM, boundary ownership, simultaneous commands,
  retriggering, replacement, completion, capacity and rejection behavior. Cursor/
  row-event composition at 127.53 BPM produces identical PCM across regular and
  irregular block partitions, including pattern looping, using test-only translation
  into audio commands. Native-rate sample instrument lookup and tracker-to-audio
  preparation now exist in the separate preparer below. Rendering consumes prepared
  positions without introducing a clock or synchronization policy; Teensy processing
  cost is unmeasured.
* [x] Implement stateless `MixPcm16Mono` in `src/core/audio/pcm16_mixer.h`, with
  coverage in `tests/test_pcm16_mixer.cpp`: allocation-free mixing of eight PCM16
  mono channels using unity-gain `int32_t` summation. Saturation is applied once
  to the final sum, preserving cancellation and channel-order independence.
  Whole-request metadata validation precedes writing; rejection preserves output.
  The caller guarantees valid memory extents, lifetimes and destination non-overlap
  with inputs and request metadata; inputs may share read-only storage. Tests cover
  silence, exact passthrough, input preservation, saturation, cancellation, shared
  inputs, invalid metadata and zero-frame behavior. Composition with
  `RamVoiceBlockRenderer` produces independently expected mixed PCM across regular
  and irregular block partitions. This core mono primitive does not implement final
  volume controls, panning or effects; production playback integration
  and measured Teensy performance remain outstanding.
* [x] Implement bounded, allocation-free `NativeRateSampleCommandPreparer` in
  `src/core/playback/native_rate_sample_commands.h`, with coverage in
  `tests/test_native_rate_sample_commands.cpp`: up to 16 unique sample bindings,
  an initial implementation capacity rather than a permanent instrument limit.
  Bindings contain explicit instrument IDs (0..254), native-rate notes (0..127)
  and immutable, non-owning PCM views matching the configured output rate.
  Logical channel continuation and row-to-audio preparation commit transactionally.
  Instrument-only selection emits no audio command; explicit repeated notes
  retrigger, and row-start `NOTE_OFF` produces Stop without sample lookup.
  Missing/unknown instruments and unsupported pitch are explicit errors.
  Configuration failures preserve existing bindings, rate and logical state;
  preparation failures preserve every channel and the entire output batch.
  Channel and sample offset are preserved, as is input order at equal offsets;
  decreasing offsets are rejected. The caller manages immutable sample lifetime
  while configured and while a renderer retains emitted views, coordinating
  renderer release separately; preparer reset/reconfiguration does not stop it.
  Tests cover validation, continuation, ordering, reset/reconfiguration,
  transactional failures and end-to-end mixed PCM through cursor, row generation,
  preparer, renderer and mixer across regular/irregular partitions and loops.
  The composition fixture uses 100 Hz and 127.53 BPM with independently expected
  output; it does not validate Teensy operation or performance. General instrument
  support, resampling, envelopes, end-of-row note-off, volume controls, panning
  and effects remain outside this completed native-rate subset.
* [x] Implement `NativeRatePatternPlayer` in
  `src/core/playback/native_rate_pattern_player.h`, with coverage in
  `tests/test_native_rate_pattern_player.cpp`: completed core orchestration through
  cursor, row generation, native-rate sample preparation, rendering and mono mixing.
  Fixed pattern/binding metadata is copied; immutable PCM remains caller-owned.
  Start and repeated Start restart at sample/tick zero; Stop clears voices,
  channel continuation and position. Configured stopped rendering produces silence
  without advancing playback. Configuration and rendering are transactional:
  rejection preserves existing configuration/playback state and caller output.
  Initial processing capacities are 128 frames, 16 commands and 512 emitted ticks
  plus one completion pull; excess work is rejected explicitly without truncation.
  Tests cover transport, rollback/retry, capacity limits and independently expected
  PCM from a looping 16-row/two-channel pattern at 44100 Hz and 127.53 BPM across
  regular/irregular partitions, including block boundaries and Stop preventing
  subsequent triggers. The opt-in firmware now uses this core subset; measured
  Teensy realtime performance is still open. `src/runtime/playback_engine.h`
  remains unchanged.
* [x] Implement opt-in `teensy41_pattern` firmware with `BROTRACKER_PATTERN_BRINGUP`:
  static RAM sample fixtures and a 16-row/two-channel pattern feed the core player
  through an audio-owner adapter, with bounded request/status handoff, temporary
  BTTEST1 compatibility and an explicit fail-stop policy. Ordinary `teensy41` and
  `teensy41_usb_trace` paths are preserved. User-performed validation on
  2026-10-10 confirmed build/upload success and pattern audio heard through Windows
  BTX + Teensy 4.1. See [bring-up validation](TEENSY_PATTERN_BRINGUP.md).
  Further validation on 2026-10-10: reported log/capture evidence confirms initial
  IDLE and captured silence, START/RESTART and two STOP acknowledgements, and USB
  unplug/replug while stopped restoring CDC/audio in IDLE and silence without
  automatic START. Normal window close stopping playback is user-confirmed;
  the latest log confirms STOPPED before clean shutdown. After forced BTX termination
  with Teensy still USB-powered, relaunch received STATE PLAYING without START;
  routing resumed approximately 0.906 seconds before the PLAYING handshake reply,
  consistent with independent audio/control paths. The user audibly confirmed
  sequence continuation. Exact sample/phase continuity and restart-at-zero hardware
  timing remain unmeasured. Full hardware validation remains unchecked, including
  legacy regression, ArkOS, MQS and realtime performance.
* [x] Implement firmware RAM sample primitives: `LoadWavSampleFromSd()` loads supported
  16-bit mono 44.1 kHz PCM, and `SamplePlayer::SetSample()` / `Play()` support
  one-shot playback and restarting at the first frame. Evidence is in
  `wav_loader.*` and `sample_player.*`; scheduled pattern behavior is not validated.
* [ ] Prepare short samples in RAM before playback and connect two active channels
  to the musical scheduler. Two players and a mixer already exist in
  `platform.cpp`, but the current SD-streamed WAV test sequence is not pattern
  playback and does not use the RAM loader for this purpose. The opt-in pattern
  path uses static diagnostic PCM fixtures; production sample loading remains open.
* [ ] Complete Teensy playback of one looping 16-row pattern. Bounded realtime
  pattern storage, raw row-command generation and logical channel-state continuation
  are implemented in core playback; prepared native-rate Trigger/Stop commands now
  dispatch to core RAM voices and render at exact offsets, with core mono mixing
  also implemented. Instrument lookup and tracker-to-audio command preparation
  exist for the native-rate sample subset, with core orchestration and Start/Stop
  implemented by `NativeRatePatternPlayer`. Opt-in Teensy integration and temporary
  BTX control are implemented, with build/upload and audible playback confirmed
  on 2026-10-10. Production integration, final transport/underrun policy and full
  hardware validation remain open. Host `Pattern`,
  `Channel`, `Event` and `Tune` data still
  use vectors and have no conversion to the realtime representation yet.
  `src/runtime/playback_engine.h` remains a stub; complete Teensy pattern playback and
  full hardware behavior are not validated.
* [ ] Measure opt-in restart-at-zero hardware timing and complete production
  transport integration.
  Core Start restarts at zero and Stop clears voices, continuation and position;
  opt-in START/RESTART/STOP acknowledgements and normal-close STOP are confirmed
  on 2026-10-10, but exact restart timing remains unmeasured.
  Ordinary firmware still uses BTTEST1 for the diagnostic WAV sequence;
  final pattern protocol integration remains open.
* [x] Implement explicit core Stopped/Playing/Paused transport and opt-in audio-owner
  Pause/Continue requests. Pause silences voices while retaining logical continuation
  and consumed position; Continue uses bounded absolute positioning at the next row
  start, including row zero of the next loop, without replay/catch-up. Windows Space
  uses capability-gated Start/Pause/Continue; Return and numeric Enter request STOP/reset, including
  repeated stopped requests after acknowledgement. Shared parsing accepts PAUSED
  and version-2 position telemetry; reconnect never starts/continues automatically.
  Host tests cover transitions, boundaries, continuation, rollback and protocol
  fallback. ArkOS handheld transport bindings and ordinary diagnostics remain unchanged.
* [x] User-performed Windows BTX + Teensy 4.1 validation on 2026-10-10 confirms
  the revised transport and visual behavior work as agreed.
* [ ] Measure physical pause/continuation timing and realtime processing cost;
  pending-request/backpressure stress, legacy regression, ArkOS and MQS checks
  remain open. The user confirmation does not establish exact sample timing or
  a measured Teensy processing budget.
* [ ] Verify scheduled trigger sample positions, retriggering, simultaneous events,
  row 16-to-row 1 looping and STOP (including stopping active samples and preventing
  further triggers), first in deterministic tests and then on the current hardware.

### Live Playback State in BTX

* [x] Implement diagnostic transport-state exchange and reconnect handling:
  `platform.cpp` reports `BTTEST1` sequence state; `src/ui/bringup_serial.*` handles
  bounded buffers/receive work, HELLO/STATUS, START/STOP and reconnect without
  replaying START. Existing `tests/test_bringup_serial.cpp` and
  `tests/test_bringup_controls.cpp` cover diagnostic command ordering, state and
  reconnect behavior; BTTEST1 itself has no row reporting. The opt-in telemetry
  below supplements it without changing ordinary firmware's command exchange.
* [x] Expose authoritative consumed-tick position in `NativeRatePatternPlayer` and
  publish coherent audio-owner transport/position/dimension snapshots through
  `PatternBringUpControl`. Position is the latest tick consumed in successfully
  rendered audio, not the next block position; it includes validity, absolute tick,
  zero-based row and loop. Rejected/zero-frame renders preserve it, tick-free blocks
  retain it, and Configure/Start/Stop or bring-up faults invalidate it. Evidence:
  `src/core/playback/native_rate_pattern_player.h`, `src/bringup/pattern_control.h`
  and their host tests. Clock / Sync separation and audio-only ownership remain.
* [x] Implement bounded opt-in `BTPATTERN1 POS 1` telemetry and shared strict parsing
  in `src/bringup/pattern_telemetry.h`, `pattern_bringup.cpp` and
  `src/ui/bringup_serial.*`, with coverage in `tests/test_pattern_telemetry.cpp`.
  HELLO/STATUS offer fresh snapshots; periodic updates are approximately 20 Hz.
  Unsent updates coalesce under backpressure while command acknowledgements retain
  FIFO priority/order. Formatting and serial I/O stay outside audio processing.
  Numeric overflow, invalid fields/dimensions and malformed/overlong lines are
  rejected without partial position updates or command acknowledgement.
* [x] Display optional Teensy-reported position in shared Windows/ArkOS BTX via
  `src/ui/live_playback_view.h` and `RenderMainScreen`, replacing fixed playback
  POS/row visualization in connected mode. POS is one-based; LOOP is zero-based.
  Reported dimensions and placeholders until a validated device snapshot arrives
  distinguish the firmware fixture from the host preview. The preview/edit cursor
  is separate and hidden in the device view; standalone preview behavior is preserved. Disconnect,
  pending restart/stop and terminal errors clear live position; 500 ms without
  fresh telemetry removes the highlight without host extrapolation. Legacy
  firmware remains controllable without position telemetry. Host tests cover
  display state, stale data, fragmented/malformed input and command barriers;
  ArkOS hardware validation remains open.
* [x] Validate the completed opt-in live-position scope on Windows BTX + Teensy 4.1
  on 2026-10-10: user-performed firmware build/upload and Windows package build
  succeeded. Reported log review validated all 506 snapshots for consistent
  tick/row/loop mapping, covering all 16 rows. Reconnection to running playback
  restored PLAYING and position without START; RESTART returned to row 0 / loop 0,
  and STOP invalidated position. The user visually confirmed moving playback
  highlighting, changing POS/LOOP and highlight removal after STOP. See
  [bring-up validation](TEENSY_PATTERN_BRINGUP.md). This confirms logical reporting
  and display behavior, not exact physical timing or measured realtime performance.
* [x] Implement and validate read-only opt-in device pattern snapshots in shared
  Windows/ArkOS BTX: bounded capability discovery and identified, ordered transfer
  of actual immutable dimensions, tempo and raw cells; checksum-validated staging
  publishes complete data atomically. Device cells replace placeholders only with
  matching dimensions; internal 0.01 BPM precision and one-decimal display remain.
  Evidence: `src/bringup/pattern_snapshot.h`, `src/ui/bringup_serial.*`, shared
  rendering and `tests/test_pattern_snapshot.cpp`. Windows BTX + Teensy 4.1
  validation on 2026-10-10: log review confirmed capability discovery, GET, BEGIN,
  all 32 ordered cells for 16 rows/two channels and END, with tempo_hundredths
  12753. Independently recomputed FNV-1a32 matched 3128753801. Connection to running
  playback restored position and contents without automatic START;
  START/RESTART/STOP succeeded without reported protocol or pattern errors.
  The user separately confirmed the visual display. This completes read-only
  opt-in transfer/display, not editing, publication or full hardware validation.
* [ ] Validate hardware backpressure/staleness behavior, snapshot timeout/recovery
  and slow or irregular UI service: stale highlights must disappear, fresh position
  must recover, command
  acknowledgements must stay ordered and playback must remain independent of UI
  refresh rates and communication availability. Legacy regression, ArkOS, MQS,
  exact physical sample/phase timing and Teensy realtime performance remain open;
  the Windows opt-in validation does not complete full hardware validation.

### Basic Pattern Editing on ArkOS

* [x] Implement a shared fixed-capacity local draft editor initialized only from a
  validated device snapshot, with independent row/channel/field navigation,
  C-0..G-8 cyclic note entry, instrument-only events, separate local OFF timing,
  comparison-based dirty state, Restore and disconnect cleanup. Windows controls
  include bounded accelerated PgUp/PgDn with renewed endpoint pauses; playback
  telemetry never changes the draft/cursor. Evidence: `src/ui/local_pattern_editor.h`,
  `src/ui/editor_repeat.h` and host/editor/input/display tests. User-performed Windows
  validation on 2026-10-10 confirms the final editor behavior works as agreed.
  Edits are not sent to Teensy; publication and end-of-position OFF playback remain open.

* [x] Implement host-side note/instrument pattern data, JSON loading and preview
  rendering. Evidence: `src/core/{pattern,channel,event,tune}.h`, the tune loader,
  `src/ui/pattern_screen.cpp` and `tests/test_tune_loader.cpp`. The preview uses a
  fixed position and does not provide interactive editing or realtime publication.
* [ ] Implement ArkOS row/channel/field navigation and note/instrument entry with
  an edit cursor distinct from the live playback row. Host input and diagnostic
  controls and the shared local editor exist; ArkOS handheld editor bindings and
  hardware validation remain unimplemented.
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

* [x] Shared right-panel information layout: wrapped/clipped text with at least
  8 px padding; Windows user visually confirmed the preceding panel move on
  2026-10-10. The subsequent host-status/control/pending-message move removes
  Windows/ArkOS bottom overlays and has host containment tests. User confirmation
  on 2026-10-10 covers the final panel layout, `d1` header and all other current
  behavior. User confirmation on 2026-10-10 also covers numeric Enter STOP/reset.
  Decimal one-based `d1` rows retain zero-based
  internal indices. No transport behavior changes are included.
* [x] Validate numeric Enter STOP/reset on Windows: user confirmed on 2026-10-10.
* [ ] Complete ArkOS visual/binding validation; outstanding physical timing,
  realtime performance and other hardware checks remain open.

* [x] Shared live playback styling uses the preview's dark neutral grey row and
  existing font arrow beside the reported row number; fresh valid paused positions
  retain both, while stopped/stale/unavailable positions hide them. Orange field
  editing remains independent. Host rendering tests cover placement/visibility.
* [x] Validate grey playback-row and arrow styling on Windows: user confirmed
  on 2026-10-10. This does not establish physical timing or realtime performance.
* [ ] Validate grey playback-row and arrow styling on ArkOS.

## Deferred Tasks

* [ ] Evaluate the bright edited-channel background as a separate design option;
  current live mode retains only the orange field-edit border.

* [ ] Add row-numbering display options for zero-based and hexadecimal views;
  current `d1` is decimal one-based. No mode switching in the current change.

* [ ] Decide instrument numbering: prefer first visible instrument `01` and
  no-instrument marker `--`; raw-ID/display mapping and capacity remain undecided.
  No instrument-ID or display change is included in this step.

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

### Pattern Visualization Comparison

* [ ] Compare the current fixed pattern view with moving playback highlight against
  classic scrolling pattern visualization on BTX. Evaluate readability, playback
  following, edit-cursor independence and usability on Windows and the ArkOS
  handheld. The existing page/region navigation decision remains in effect pending
  this comparison; no scrolling implementation is requested now.
* [ ] Render completely empty cells/rows at lower brightness, distinguishing them
  from notes and non-note events such as NOTE_OFF or instrument-only updates.
  Preserve readable playback highlighting and the separate edit cursor; no visual
  implementation is included now.

### PCM Playback Validation

* [ ] Retain the diagnostic beeper fixtures and later add one extra channel using
  the existing `test.wav`, `test2.wav` and `test3.wav` PCM samples; overlap is not
  required. Use these for longer-pattern validation once RAM sample loading and
  playback integration are ready. The user expects `test.wav` to span four bars
  and `test2.wav`/`test3.wav` one bar each: verify actual frame counts, sample rates
  and intended meter before deriving BPM. When pitch interpretation/resampling
  is implemented, document the nearest note and signed semitone offset, including
  any fractional residual required for exact tuning at the calculated BPM.
  Duration determines tempo; determining a musical note additionally requires a
  known or measured reference pitch. No sample integration, resampling or tuning
  implementation is included in this task.

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
* [x] Add separate portable `VariableRateSampleVoice` in
  `src/core/audio/variable_rate_sample_voice.h`, tested in
  `tests/test_variable_rate_sample_voice.cpp`: immutable non-owning PCM16 mono,
  positive Q32.32 source-frame step with exact unity, matching source/output rates.
  Separate integer position/fractional phase retain full caller-buffer size support;
  checked advancement safely completes even for maximum steps. Exact int64 linear
  interpolation rounds once to nearest with ties away from zero, clamps the second
  endpoint at the last frame, then releases/zero-pads at completion. Transactional
  validation, retrigger/replacement, Stop/Reset and exact partition independence;
  unity output matches RamSampleVoice. No native-rate API or playback changes.
  Linear interpolation is not band-limited (upward pitch may alias); rate changes
  pitch and duration, not time stretching. Hardware cost is unmeasured.
* [x] Record D0055's agreed sample model: source rate, reference note, signed
  fine tuning and rhythmic tempo remain separate. Preserve explicit tuning;
  otherwise reference internal 60 (Yamaha C-3), zero correction, original speed/
  pitch/duration as a playback anchor, not measured C tuning. Sample BPM does not
  determine pitch. Positive tuning raises playback rate; storage precision remains
  undecided. This is a design decision, not implemented metadata initialization.
* [x] Implement portable `NoteToPlaybackStep` in `note_playback_step.h` with
  checked-in `note_playback_steps.h` constants and exact-integer generation source
  `tools/generate_note_playback_steps.py`: explicit target/reference pitches 24..127,
  positive Q32.32 equal-tempered ratio, exact unity/octaves, full interval range
  -103..103. Reject special/out-of-range notes without changing output. Nearest
  quantization error < half Q32 LSB; relative error < 0.045 ppm. No runtime floating
  point/allocation/I/O. Tests cover all intervals against independent 512-bit exact
  arithmetic, monotonicity, invalid notes and octave voice PCM/partition composition.
  Reference pitch remains explicit; beeper anchors are not measured tuning.
* [ ] Integrate note mapping, tuning metadata and variable-rate rendering/bindings/
  player, firmware/protocol/UI. Existing unsupported-pitch behavior is unchanged.
  Fractional-semitone correction remains a separate future layer. D0055 defines
  source/output ratio * note-interval ratio * fine-tuning ratio. Keep PCM16 mono
  44100 Hz as implemented baseline; preserve future 8000/22050/48000 Hz conversion
  paths without fixing retained-versus-converted storage. Low rate does not imply
  8-bit PCM. Current voice requires matching rates; hardware cost remains unmeasured.
* [ ] Add optional original BPM/quarter-note beat-length metadata and explicitly
  enabled loop-tempo matching (target/original BPM). Derive BPM from duration only
  with known beat count, never infer meter/bar count or reference pitch. Rate matching
  changes pitch and duration; requested semitone offset/fractional residual is speed
  transposition, not a musical root. Define combination with note transposition
  without double application. Analysis suggestions/manual correction, time stretching,
  quality/anti-alias policy and hardware-performance validation remain pending.
  Current beeper C-3/D-3 bindings are trigger anchors, not measured tuning.
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
* [x] Add separate `MixPcm16MonoWithGains` in `src/core/audio/pcm16_gain_mixer.h`,
  tested in `tests/test_pcm16_gain_mixer.cpp`: eight channels, unsigned Q16 gains
  0..65536 (exact unity), default unity; exact int64 accumulation, one nearest
  rounding with signed ties away from zero, then final PCM16 saturation.
  Proven eight-channel full-scale bounds preserve cancellation/order independence;
  unity is bit-identical to unchanged `MixPcm16Mono`. Validate all metadata before
  writes, preserving output on rejection and caller memory/lifetime/non-overlap
  guarantees. Host tests cover unity, silence/partial gain, signed rounding,
  cancellation, saturation, partition independence and invalid requests.
  Player integration is recorded separately below; no firmware/UI controls or
  fixture level changes. Zero gain is an audio
  contribution, not tracker mute/event suppression. Hardware cost is unmeasured;
  gain controls, instrument/master gain, panning, smoothing and final headroom
  policy remain pending.
* [x] Integrate `MixPcm16MonoWithGains` into `NativeRatePatternPlayer` with
  transactional `SetChannelGains` and read-only copied gain inspection. All eight
  channels default to exact unity; values above unity reject without mutation.
  Updates affect the next Render without resetting voices, position, transport,
  deferred OFF or pending working previews. Start/Stop/Pause/Continue and published
  or working replacement retain gains. Successful Configure resets unity; failed
  Configure preserves gains/playback. Render rollback/output guarantees remain.
  Tests cover exact unity/partial/zero PCM, validation, lifecycle preservation,
  deferred OFF and identical absolute gain-change boundaries across partitions.
  Zero gain is not tracker mute; abrupt changes may click. Firmware/protocol/UI
  gain controls, instrument/master gain, smoothing, final headroom policy and
  integrated gain-path Teensy performance measurement remain pending. Device
  protocol implementation is recorded separately below.
* [x] Add opt-in `BTGAIN1 CAP 1` device gain control: strict complete eight-gain
  Q16 SET, copied fixed FIFO ownership, audio-owner application before Render,
  SUBMITTED versus APPLIED, and coherent GET/STATUS applied-ID/gain readback.
  Increasing boot-scoped identities reject duplicates; NEXT=0 means exhausted.
  Reconnect never resets/resends gains; queue rejection consumes no identity.
  Discovery reserves 12 of 16 TX lines, preserving framing/ACK priority and
  telemetry fairness. Portable tests cover parsing, atomic/copied requests,
  transport/preview/publication preservation, readback and ACK backpressure.
  BTX sending controls and hardware validation remain pending. Prior PERF results
  do not measure the gain-aware path; abrupt changes can click, zero is not mute.
* [x] Add shared `HostChannelGains` / `BringUpSerial` coordination: explicit CAP,
  strict dedicated ten-field STATE validation, authoritative gain/ID inspection,
  synchronization phase and complete-set request API. Fixed immutable in-flight
  set plus latest coalesced desired set; no initial unity/default send. SUBMITTED
  and APPLIED remain distinct from exact STATE verification. Requests preserve
  pattern dirty/baseline/history semantics. Complete-line transport priority and
  serialized preview/publication/readback traffic use bounded partial writes.
  Five-second uncertain outcome recovery polls authoritative STATE at most once
  per second; unresolved accepted IDs block newer sends. Reconnect discards all
  old desired gains and requires a fresh explicit request after verified discovery.
  Legacy peers receive no gain commands. Portable tests cover parsing, coalescing,
  errors, exhaustion, recovery, framing and PCM with preview/publication.
  User-facing gain controls/bindings, hardware validation and gain-path Teensy
  performance measurement remain pending.
* [x] Add shared read-only right-panel gain presentation: verified applied Q16
  values/identity, separately labelled desired values, synchronization/rejection
  and unavailable capability/state. Existing wrapping/clipping and padding apply;
  zero remains an audio gain value with no tracker mute markers. Windows-only
  `BROTRACKER_DIAGNOSTIC_CHANNEL_GAINS` optionally parses eight comma-separated
  unsigned Q16 values into fixed storage and submits one explicit request after
  capability and verified synchronization in the initial connection. Missing or
  invalid configuration sends nothing; no retry/reconnect replay. No controls,
  shortcuts, handheld bindings, fixture-level changes or history interaction.
  Host tests cover parsing, one-shot policy, coordination and panel containment.
* [x] Record 2026-10-12 Windows BTX + Teensy terminal-log evidence: one diagnostic
  SET selected channel 3 gain 16384, all other channels 65536; SUBMITTED/APPLIED
  identity 1 and exact STATE readback confirm application. Logged Start,
  Pause/Continue and Stop retained the set; no protocol errors or disconnects.
  Across 160 PERF reports, highest cumulative steady maximum was 55901 cycles
  against 1741496 budget (3.21%), zero reported budget overruns. The maximum
  includes activity since firmware boot before this BTX session; it is not
  exclusively reduced-gain cost or whole-audio-graph cost. Archive the supplied
  validated log separately before further runs, outside disposable build exports;
  the normal AppData terminal log is overwritten on the next launch by default.
* [ ] Complete gain hardware checks: visual presentation, audible comparison,
  reconnect/publication retention, clipping absence and exhaustive stress testing.
  Full-graph cost and instrumentation overhead remain open, as do ArkOS checks,
  smoothing and final gain policy. Removing the diagnostic environment variable
  does not restore device gains; an explicit update or successful configuration/
  reboot is required.
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
  support, resampling, envelopes, firmware end-of-row note-off, volume controls, panning
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
* [x] Implement core-only explicit `NoteOffTiming` metadata in `RealtimePattern`
  and `RowEvent`, separate from instrument IDs, defaulting to Arrival. The
  native-rate player copies/validates metadata transactionally. Arrival stops at
  the OFF row start; EndOfPosition schedules `(absolute_row + 1) * kTicksPerRow`
  through exact floor-rounded conversion at 0.01 BPM precision, with explicit
  overflow errors. Eight fixed per-channel deferred slots and the existing
  16-command/512-tick limits reject excess work without truncation. Deferred Stops
  precede new boundary-row commands, preserving retriggers, instruments and loops.
  Rejected renders preserve pending work/state/output. Start, Stop, successful
  Configure and Pause clear pending Stops; Continue does not replay discarded work.
  Host tests cover independently expected PCM at 127.53 BPM across partitions,
  half-open boundaries, simultaneous channels, loop ordering, pause/continuation,
  metadata validation/copying, rollback/retry, capacity and arithmetic overflow.
* [x] Integrate core OFF timing into negotiated read-only snapshot v2 and the
  shared local draft using `RealtimePattern` metadata. Version 1 remains compatible
  and defaults missing timing to Arrival; v2 validates and checksums timing before
  atomic publication. Draft dirty comparison and Restore use the received baseline,
  including EndOfPosition. Firmware serves its immutable playback configuration
  from the main loop; the current fixture/audio sequence remains unchanged.
* [x] Validate snapshot-v2 pattern display and shared editor on Windows BTX +
  Teensy 4.1: user confirmed on 2026-10-11 the pattern display, OFF timing toggling
  (`¯` / `_`), Restore and unchanged audio.
* [ ] Validate snapshot-v2 OFF timing and shared editor on ArkOS; end-of-position
  hardware timing and realtime performance remain unmeasured.
* [ ] Validate explicit F5 local draft publication separately from the confirmed
  read-only editor; automatic volatile preview is now host-tested but hardware
  validation remains open. Legacy toggles remain NOT SENT TO TEENSY until explicit
  publication. Prior Windows confirmation does not establish publication,
  end-of-position hardware timing or an execution budget.
* [x] Add core-only `NativeRatePatternPlayer::ReplacePattern`: configured/Stopped
  only, retaining tempo, rate and validated sample bindings. Active dimensions,
  notes and OFF timing are checked; two bounded logical passes from cleared state
  reuse sample preparation to reject missing/unknown instruments and unsupported
  native-rate pitches while respecting instrument-only continuation. Success copies
  complete pattern metadata and clears position, voices, continuation and deferred
  Stops; rejection preserves all state. Host tests cover PCM playback/loops,
  validation, Playing/Paused rejection and rollback. Render processing limits and
  arithmetic exhaustion remain explicit runtime checks.
* [x] Add capability-gated opt-in firmware publication via `BTPUBLISH1 CAP 1`
  and versioned BEGIN/CELL/COMMIT. Portable fixed 16x8 staging validates identity,
  ordered cells, dimensions/count and FNV-1a32 including core OFF timing. One
  transaction freezes storage until audio-owner acknowledgement; replacement is
  ordered with transport and accepted only at the actual configured/Stopped state.
  Main-loop readback synchronization precedes APPLIED; snapshot/publication
  exclusion prevents mixed revisions. Five-second unsubmitted timeout/session
  cancellation and increasing per-boot IDs prevent duplicate COMMIT application.
  Submitted timeouts are uncertain outcomes requiring authoritative readback.
  Host tests cover apply/readback, validation, immutable handoff, transport rejection,
  FIFO backpressure, snapshot exclusion and telemetry/ACK interleaving.
* [x] Add Windows F5 explicit stopped-only draft publication with shared portable
  frozen-copy logic. Gate on CAP 1, fresh usable NEXT, validated v2 baseline, dirty
  draft and idle transport/work. Bounded partial lines serialize BEGIN/CELL/COMMIT;
  identities are consumed once and refreshed via STATUS. SUBMITTED is not success:
  APPLIED requests v2 readback, and exact dimensions/cells/OFF timing plus unchanged
  tempo confirm and rebase while retaining the edit cursor. Mutations/Restore freeze,
  navigation remains available; rejected/mismatched results preserve edits/baseline.
  Possible COMMIT delivery on timeout/disconnect remains uncertain until readback;
  no automatic publication retry. STOP/exit finish partial lines and do not cancel
  submitted work. Host tests cover protocol, partial writes, editor and transport.
* [ ] Validate F5 stopped-only publication on Windows + Teensy, then ArkOS; physical
  timing/performance remain unmeasured. ArkOS handheld publication bindings, live
  editing, new sample bindings, sample transfer and persistence remain unimplemented.
* [x] Add core-only `ReplacePausedPattern`: configured/Paused only, unchanged
  active dimensions, retained tempo/rate/bindings and complete two-pass native-rate
  validation. Rebuild new-pattern logical continuation before the reported row
  (cleared-state prefix in loop zero; one full pass plus prefix in later loops).
  Preserve position/loop/next sample, remain silent/Paused, clear deferred Stops;
  no consumed tick leaves cleared state. Continue replays the row exactly once.
  All rejection paths preserve prior state/pattern; fixed work, no elapsed-loop scan.
  Host tests cover selection changes, loops, OFF timing, PCM, validation and rollback.
* [x] Integrate paused publication via separate PAUSEDCAP 1, audio-owner transport
  selection and synchronized APPLIED/readback. CAP 1 alone stays stopped-only.
  Host F5 permits capability-gated Paused, reports specific unmet conditions and
  clears obsolete availability warnings without hiding rejection/uncertainty.
  STOP/exit remain available; Playing publication stays blocked, no auto Continue.
* [ ] Validate paused F5 publication on Windows + Teensy; ArkOS and hardware
  timing/performance remain open.
* [x] Implement explicit core Stopped/Playing/Paused transport and opt-in audio-owner
  Pause/Continue requests. Pause silences voices while retaining logical continuation
  and consumed position; Continue uses bounded absolute positioning to replay the
  same reported row, retaining its loop and consuming its events once without
  catch-up. Before any consumed tick it starts at zero. Windows Space
  uses capability-gated Start/Pause/Continue; Return and numeric Enter request STOP/reset, including
  repeated stopped requests after acknowledgement. Shared parsing accepts PAUSED
  and version-2 position telemetry; reconnect never starts/continues automatically.
  Host tests cover transitions, boundaries, continuation, rollback and protocol
  fallback. ArkOS handheld transport bindings and ordinary diagnostics remain unchanged.
* [x] User-performed Windows BTX + Teensy 4.1 validation on 2026-10-10 confirms
  the then-current next-row transport and visual behavior work as agreed.
  This historical confirmation does not validate the later current-row replay revision.
* [ ] Validate revised current-row Continue replay on Windows + Teensy and ArkOS.
  Paused publication remains a separate open follow-up; no protocol changes here.
* [x] Correct host Pause latency: service Windows input before UI presentation,
  begin transport writes without another service delay, finish partial lines but
  bypass pending STATUS replies. Ignore late STATE during transport and await fresh
  applied telemetry after PAUSED/CONTINUED instead of relabeling old position.
  Host tests cover partial writes and both sides of row boundaries; audio-owner
  application remains block-boundary based, with current-row Continue replay.
* [x] User-confirmed Windows BTX + Teensy pause-latency correction on 2026-10-11.
  This confirms that correction only, not paused publication or measured timing.
* [ ] Measure exact command/output timing and realtime performance; ArkOS validation
  remains open.
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
  Legacy edits remain local until explicit F5 publication; capability-gated volatile
  preview now has separate automatic transfer. Publication
  hardware validation, ArkOS sending bindings and live edits remain open.

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

* [x] Correct Windows publication transport to accept bounded prefixes of protocol
  lines longer than its 32-byte worker buffer. Host regressions cover complete
  16-row/two-channel transfer, deletion/readback and connected pitch rejection.
  Note DELETE clears note/instrument/OFF timing atomically; normal Instrument
  DELETE clears only the instrument. Empty instruments retain `--`, ID 0 displays as `01`.
* [ ] Revalidate F5 deletion apply/readback and unsupported-pitch rejection on
  Windows + Teensy after the transport correction; ArkOS/hardware checks remain open.

* [x] Add shared pattern-derived note entry: first PgUp/PgDn on an empty Note
  field inserts the nearest preceding pitch in the current draft channel unchanged,
  skipping empty/instrument-only/OFF. No preceding pitch initializes C-3 (internal 60); no
  cross-channel search or pattern wrap, no automatic instrument. Subsequent actions
  retain cyclic/repeat behavior. No session memory; edits/deletions, snapshot load,
  Restore and confirmed readback are reflected immediately. Host tests cover both
  directions, channel isolation, skipped cells, fallback, Undo/Redo and held repetition.
  The C-3 empty-note fallback is host-tested; its Windows manual validation remains pending.
* [x] User confirmed on 2026-10-11 that, after clearing both channels, a published
  first-channel C-3 / instrument 01 pattern on rows 01 and 09 plays correctly.
  This does not confirm all paused-publication cases or measured hardware timing.
* [x] User-confirmed Windows pattern-derived note entry on 2026-10-11, including
  after loading an existing pattern.
* [ ] Validate pattern-derived entry on ArkOS.

* [x] Implement optional teensy41_pattern_perf DWT callback diagnostics: separate
  initialization cost, saturating callback/steady counts, steady maximum and budget
  overruns derived from actual CPU clock/sample rate/block size. Includes audio-owner
  transport/replacement; coherent fixed snapshot and bounded once-per-second PERF 1
  main-loop reports skipped under backpressure. Portable arithmetic/format tests
  validate implementation only; no hardware measurements yet.
* [x] Record 2026-10-11 Windows BTX + Teensy performance log evidence: 99 PERF
  reports, final 40362 callbacks / 40361 steady callbacks; initialization 2249 cycles.
  Observed cumulative maxima: stopped before playback 1457, playback before publication
  42354, overall 90158 cycles. Budget 1741496 cycles, zero recorded overruns;
  overall maximum is 5.18% of budget. It first appeared in reporting after publication,
  but is not attributable exclusively to replacement.
* [x] Record paused publication SUBMITTED/APPLIED and exact 32-cell snapshot-v2
  readback on 2026-10-11: initial checksum 176805769, updated 391983933. Paused tick
  28380 / zero-based row 7 / loop 18 stayed unchanged through publication; Continue
  resumed within that row and STOP invalidated/reset position. No protocol errors
  or disconnects occurred in this log. Evidence remains in the terminal log outside
  disposable build exports.
* [ ] Validate full audio-graph cost, instrumentation overhead, stress workloads,
  unsupported-pitch rejection, reconnect and ArkOS. This observed run does not
  complete hardware/performance validation or establish exact physical timing.

* [x] Add portable non-realtime `LoadWavToBuffer` foundation in
  `src/core/audio/wav_buffer_loader.h`: caller-owned PCM storage and bounded reader,
  signed little-endian PCM16 mono/44100 Hz only. Validate RIFF/chunk extents,
  fmt consistency, unique required chunks, padding, complete nonempty frames and
  capacity before PCM copy. Metadata commits only on success; copy-time I/O failure
  may partially modify unpublished storage. No allocations/audio dependencies.
  In-memory host tests cover PCM, capacity, padding, format/container errors and I/O.
* [ ] Integrate the portable loader with Teensy SD/preparation and validate hardware;
  firmware loaders, diagnostic pattern and samples are unchanged. Retain the planned
  extra PCM test channel and test.wav/test2.wav/test3.wav duration/meter validation;
  no sample integration or resampling in this foundation.

* [x] Implement firmware-side `SdWavBufferReader` / `LoadWavBufferFromSd` in
  `firmware/teensy/BroTracker/sd_wav_buffer_loader.h`, reusing real SdReader and
  its D0046 shared read lock throughout validation/copy. Reject bad paths/open/
  directories, oversized seek narrowing and negative reads; RAII releases file/lock
  on every exit. Host tests with an isolated SD fake cover exact PCM, failures and
  writer exclusion using the actual shared guard. PCM is caller-owned; SD internals
  may allocate. This is non-realtime preparation, not audio-callback I/O.
* [ ] Integrate adapter into boot preparation, select RAM placement and sample
  bindings, then validate actual Teensy SD behavior/hardware. No current boot,
  pattern or loader API changes; test.wav/test2.wav/test3.wav remain deferred.

* [x] Remove Windows/ArkOS main-app startup mockup rendering/presentation and
  implicit absent-live-view demo fallback. Main BTX always renders application
  unavailable-data layout while waiting/disconnected; no dummy notes/fake position
  or edit cursor. Explicit RenderPreviewScreen keeps tools/ui_preview BMP export.
* [x] Record user-confirmed Windows startup and reconnect visual validation on
  2026-10-11 after mockup fallback removal.
* [ ] Validate ArkOS startup, disconnect/reconnect and live editor/playback/panel
  presentation after mockup fallback removal.

* [x] Implement opt-in teensy41_pattern_sd_playback, extending sd_prepare: boot
  selection binds the successful immutable RAM2 WAV as raw instrument 2 (03),
  native-rate C-3 on row 01 of a third channel. Original two beeper channels,
  tempo 12753, WAVPREP/PERF and failure fallback are preserved. Host tests cover
  fixture selection, exact completion, loop retrigger, Stop and current-row replay.
* [x] Render the shared application grid as 8 channels x 32 rows (d1), in one
  pass with actual cells or empty --- -- padding. Active dimensions, edit bounds,
  snapshots/publication and playback are unchanged; standalone preview preserved.
* [x] Record user Windows validation on 2026-10-11: all 8 channels and 32 rows
  show actual events or empty placeholders. The SD sample audibly plays alongside
  beepers; louder PCM masks the quiet beepers, while removing the channel-3 note
  confirms they still play clearly. This confirms audible coexistence, not measured
  timing, absence of clipping or completion of the remaining hardware checklist.
* [ ] Complete remaining SD playback checks on Windows + Teensy and validate ArkOS:
  completion, looping, Pause/Continue, stopped/paused publication, failure fallback,
  memory/cache behavior and measured realtime cost. Unity-gain saturation is
  possible with overlapping WAV/beepers; final gain policy remains open.

* [x] Implement portable volatile `ReplaceWorkingPattern` in NativeRatePatternPlayer:
  same dimensions, complete transactional note/OFF/native-binding two-pass validation;
  Stopped/Paused retain replacement semantics, Playing stages one replaceable
  candidate for the next unconsumed row start. Rebuild new logical continuation
  without past audio replay; retain voices/timeline, apply due old deferred Stops
  before new triggers and discard remaining old deferred work. Pause retains the
  candidate for Continue; Stop/Start adopt it while resetting. Host tests cover
  boundary ownership, edits/OFF, loops, partition independence and rollback/retry.
* [x] Integrate the shared Windows volatile WYSIWYG model: accepted local edits automatically
  transfer to working playback; F4 restores the last published baseline in editor
  and playback; F5 promotes the accepted draft to that baseline. Playing publication
  remains blocked; volatile preview is separate and allowed while Playing.
  Automatic host transfer and F4/F5 coordination are host-tested. Preview does not
  rebase publication/clear dirty state; rejection stays visible without retry.
  User confirmed automatic preview on Windows BTX + Teensy on 2026-10-12;
  exhaustive recovery/backpressure checks remain open. No SD persistence.

* [x] Implement opt-in device `BTPREVIEW1 CAP 1` volatile preview transfer and
  audio-owner lifecycle: fixed ordered/checksummed OFF-aware staging, consumed
  identities/NEXT, frozen handoff, ACCEPTED versus actual APPLIED/SUPERSEDED.
  Keep published baseline, applied working and pending candidate metadata separate;
  published GET remains baseline, explicit working/pending readback and STATE
  recover uncertain outcomes. Preview supports all transports; stopped/paused
  publication synchronizes both sources and supersedes previews; Playing rejects.
  Host tests cover validation, ownership, boundary application, transport ordering,
  reconnect retention and partial readback/telemetry arbitration. No hardware result.
* [x] Add shared `HostPatternPreview` and Windows wiring: 75 ms debounce capped
  at 250 ms for continuous edits, frozen in-flight transfer plus latest coalesced
  draft, strict response phases and authoritative readback recovery. F4 queues
  baseline preview; F5 freezes selection and suppresses older previews until exact
  published readback. Transport has complete-line priority; rejected revisions
  remain visible without automatic retry. Tests cover partial writes, held edits,
  dirty state, recovery, races and end-to-end PCM/Restore/publication.
* [ ] Complete exhaustive validation of F4/F5 coordination, rejection, uncertainty/reconnect,
  backpressure and pause latency on Windows + Teensy, then ArkOS. Handheld bindings
  remain deferred. No SD persistence; physical timing/performance remain unmeasured.

## Deferred Tasks

* [ ] Define channel/instrument gain controls and the final mixing headroom policy;
  add host/UI gain control and validate the device protocol without implying mute.
* [ ] For actual muted channels, consider an X formed by two diagonal lines
  inside the channel header, with 16 px padding where geometry permits.

* [ ] Replace the disconnected empty pattern layout with a dedicated
  disconnected-device presentation, using M8 Headless as a visual reference.
  Design and implementation remain deferred.

* [x] Implement opt-in teensy41_pattern_sd_prepare boot loading of Samples/test2.wav
  before g_audio_ready into a static 32-byte-aligned 131072-frame/256 KiB DMAMEM
  buffer. Successful immutable view only; retained failure/status and bounded
  WAVPREP 1 reporting after connection. No retry after activation, no new bindings
  or third-channel playback. Host tests cover preparation and partial diagnostics.
* [x] Record supplied 2026-10-11 teensy41_pattern_sd_prepare build/upload and
  WAVPREP success: 68284 frames at 44100 Hz, 136568 PCM bytes, duration 1.548390 s.
  Address 0x20200820 confirms RAM2 placement and 32-byte alignment. Interpreted as
  one 4/4 bar this implies approximately 155 BPM; intended meter/bar length is unconfirmed.
  Log shows IDLE/STARTED/STOPPED and clean shutdown without protocol errors.
  Ten PERF reports: maximum 41843 / 1741496 cycles (2.40%), zero overruns; this
  measures beeper playback, not the prepared WAV, which is not bound to playback.
* [ ] Validate total RAM/heap headroom, failure paths, reconnect reporting and
  SD/cache behavior on hardware; sample binding/extra-channel playback remain open.
  Preserve supplied evidence outside disposable build exports. Future optional
  PSRAM-backed caller storage remains open; 256 KiB is this environment's choice,
  not a loader limit. Duration does not establish reference pitch.

* [ ] Design tracker-style pitched-note entry with letter keys on desktop and ArkOS
  using an attached keyboard. Key mapping and octave controls remain undecided;
  no implementation yet.

* [x] Implement pattern-context data undo/redo in `pattern_edit_history.h` and
  `LocalPatternEditor`: latest 64 fixed transactions, complete before/after
  note/instrument/OFF timing and coordinates; full-capacity F4 Restore is atomic.
  Navigation/no-ops add no entries, new edits discard redo, oldest entries evict
  deterministically. Cursor and published baseline remain independent; matching
  F5 readback/rebase preserves history. Undo/Redo feed automatic preview, never
  publication. Publication locks mutation but permits navigation. Storage is
  65,804 bytes on the current host ABI (8-byte changes, 128 per transaction).
  Disconnect retains disabled bounded history until recovery, but the protocol
  cannot prove editing-context identity: reconnect conservatively clears it.
  Same-session unchanged readback preserves history; changed recovery clears it.
  `ResetContext` explicitly clears new-project/session context. Host tests cover
  restoration, capacity, locking, baseline/recovery and preview composition.
* [ ] Design undo/redo input bindings within unified ArkOS/keyboard interaction
  and decide held-key transaction grouping; manually validate Windows/ArkOS
  history behavior. No buttons or shortcuts are reserved.
* [ ] Design separate sample-editor and synth-editor histories; lifecycle policy
  and protocol-backed editing-context continuity remain undecided.

* [ ] Evaluate the bright edited-channel background as a separate design option;
  current live mode retains only the orange field-edit border.

* [ ] Add row-numbering display options for zero-based and hexadecimal views;
  current `d1` is decimal one-based. No mode switching in the current change.

* [x] Implement one-based user-visible instrument numbering: raw IDs 0/1 display
  `01`/`02`, 0xFF stays `--`, and numbers 100..255 expand to three digits without
  truncation. Internal bindings/wire IDs remain unchanged; empty editing selects
  `01`, bounds do not wrap, and OFF timing remains separate. Host tests cover
  formatting, editing/Restore and publication/snapshot raw-ID roundtrip.
* [ ] Validate one-based instrument display/editing on Windows + Teensy and ArkOS;
  existing publication/hardware validation remains open.

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

### Pattern Length and Playback Flow

Follow [D0056](ARCHITECTURE_DECISIONS.md#d0056--pattern-length-and-playback-flow):
stored pattern length, playback flow and visible row count are separate. Current
16-row realtime capacity and single-pattern looping remain unchanged.

* [ ] Implement per-pattern stored lengths of 16, 32 or 64 rows and a tune default
  used only for newly created patterns; changing the default must not resize
  existing patterns. Extend bounded core capacities, validation and transfer
  contracts deliberately; preserve four rows per quarter note without LPB changes.
* [ ] Define flow FX names/codes/parameters, same-row conflicts, invalid targets,
  combination rules, end-of-sequence and standalone loop behavior before implementing
  order-list sequencing, Pattern Break and Position Jump. Complete the current row
  before transitions; target order entries rather than pattern IDs. Retain skipped
  stored events, including shorter played sections within a longer stored pattern.
* [ ] Define and test sounding audio/MIDI note, instrument-continuation and deferred
  EndOfPosition OFF behavior at transitions, including paused-row replay interaction.
* [ ] Decide live length changes, resizing/data retention and undo semantics.
* [ ] Design paging/scrolling for 64-row patterns on the current 32-row display;
  preserve independent edit and playback positions and existing navigation decisions.

### Handheld Input Design

* [ ] Design handheld input normalization and action bindings, with ArkOS as the
  primary handheld target and separate dArkOS compatibility validation. Vertical
  versus horizontal layout is an ergonomic distinction, not a reliable identifier
  for raw-button mappings. Separate hardware/SDL normalization from BroTracker
  action bindings; consult the official
  [SDL controller mapping documentation](https://wiki.libsdl.org/SDL2/SDL_GameControllerAddMapping)
  for device GUIDs and button, hat and axis mappings.
  Evaluate verified device profiles and optional calibration; require setup only
  when a usable mapping cannot be established. Account for FN presence/absence,
  face-button labels versus physical positions, shoulder arrangement, hats and
  axis-based triggers. Check which controls/modifier combinations actually reach
  BTX and which OS/emulator hotkeys intercept. Do not assume RetroArch or
  EmulationStation bindings apply to standalone BTX; supplied community hotkey
  examples remain unverified, device-specific examples rather than universal defaults.
  Evaluate guided confirmation of navigation, confirm/back and modifier controls
  before saving. Validate candidate mappings transactionally and provide cancellation
  and recovery without relying solely on newly remapped controls. Removing application
  config or using safe-config restores application defaults but cannot necessarily
  repair system SDL mapping. First-run behavior, profile identification, reset
  workflow and final ArkOS/keyboard bindings remain undecided. Coordinate with the
  pending `config.ini` foundation without changing its scope or reserving controls.

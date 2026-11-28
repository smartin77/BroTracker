# TODO List

## Current task: complete accepted-BTT candidate export

* [x] Review actual legacy session, startup/reconnect, cache and stopped BTT
  implementations; document corrected BTX/Teensy ownership and gaps. Evidence:
  `build/btx-session-ownership/REPORT.md`. That review was documentation only.
* [x] Implement negotiated stopped-only export of the accepted
  complete immutable BTT to a validated BTX-owned candidate, reusing Document/
  ReaderOwner/Validate. Bound credits/chunks, pin lifetime and request/generation
  integrity; retain the old candidate on failure. Components: `tools/btt`, shared
  bringup protocol, Teensy `pattern_bringup.cpp`, host `bringup_serial`, host tests.
  Prove all patterns/order/three FX slots, unavailable/unsupported definitions and
  optional unknown bytes survive; reject stale/short/corrupt/changed-source replies.
  No restore, migration, editor unlocking or automatic session replacement yet.
  Actual exporter/BTX-client tests and Debug/Release regressions, normal Teensy and
  Windows/ArkOS deliveries, production memory comparison and dedicated-card real
  Windows BTX byte verification are recorded in `build/btt-export/REPORT.md`.
  Candidate success remains distinct from durable current-session persistence.
* [ ] **Next:** define/version the shared musical snapshot generation and edit-overlay
  boundary, including legacy one-pattern conversion and working versus applied
  intent. Classify mute/gains and future mixer settings explicitly; do not persist
  playback execution state or assume automatic resume.
* [ ] Add one internal BTX session store with complete musical bytes and separate
  extensible UI state. Test atomic/recoverable promotion and write/sync/close/reopen,
  capacity, corruption and interrupted transaction failures; preserve prior work.
  Host location/envelope/durability primitives remain proposed. Undo is deferred.
* [ ] Add complete stopped restore/import staging with lossless backing retention,
  separate resource/projection admission, real application ACK and full readback.
  Unsupported FX/missing resources must keep saved intent and show a blocked or
  unavailable state; do not silently reduce the tune to the runtime projection.
* [ ] Resolve fresh-host live conflict/adoption, stopped conflict, failed-exit UX
  and standalone Teensy boot policies before automatic lifecycle wiring. Preserve
  running/paused authority, reconnect holds, STOP and existing narrow recovery.
* [ ] Add independent session autosave and confirmed LOAD/SAVE entry, exit and BTT
  Save generation bookkeeping. Track pending/dirty/uncertain per store; never use
  deferred/lost ACK as success. Test equal corresponding musical checkpoints plus
  independent UI restore and later-edit dirtiness.
* [ ] Add protected New Tune/BTT Load replacement and crash reconciliation across
  device ACK/host promotion. Keep prior recoverable bytes on failure/cancellation.
* [ ] Implement versioned legacy session/recovery import and durable ownership
  handoff, then retire legacy long-term autosave/boot precedence. No competing
  authority or silent device-work loss; switch waveform lineage only after commit.

Authoritative scope and proposals: [session ownership](FILE_FORMATS.md#btx-current-session-ownership-contract-2026-11-28).
Project directories, sample copies/collection, project cache packaging and session
undo persistence remain deferred. No filesystem preference or new SD benchmark.

## BTT Foundation implementation record and remaining work

* [x] BTT Foundation ownership documentation draft: define tune ownership, external
  references and lossless preservation of unavailable resources/settings; align
  BTP sample collection and project waveform cache boundaries. This is a draft,
  not implemented persistence. [File formats](FILE_FORMATS.md#btt--brotracker-tune)
  [D0032](ARCHITECTURE_DECISIONS.md#d0032---brotracker-file-and-container-formats)
* [ ] BTT v1 format proposal — awaiting review: stable tune/pattern/instrument
  identity, order list, addressable binary envelope, lossless saved fields,
  reference bases, bounded loading and recoverable saves. Review proposed limits,
  sequencing choices and memory/backing ownership before implementation.
  [Proposal](FILE_FORMATS.md#btt-v1-proposal--awaiting-review)
* [x] Measure bounded pattern access in a native host-only experiment: fixed and
  independently decodable sparse layouts, 8/16-bit numeric FX IDs with three
  slots, repeated order IDs, FX-only/zero/unknown data, direct file access and
  malformed/capacity cases. No application, firmware or FX execution integration.
  [Measurements](FILE_FORMATS.md#host-only-pattern-measurements)
* [ ] Review fixed16 as the pattern baseline and optional smaller sparse16, ID
  namespace/presence rules, bounded seekable section interfaces and current/next
  buffering. Keep every profile limit provisional; validate device admission,
  refill-first scheduling, jump invalidation, dirty-pattern ownership, separate
  FX runtime state, prefetch deadlines and failure policy before integration.
  [Buffering design](FILE_FORMATS.md#proposed-currentnext-pattern-buffering-and-deferred-validation)
* [x] Implement the provisional host-only bounded envelope/TLV
  validator and rewriter: byte-identical no-edit roundtrip with explicit defaults,
  then a BPM edit retaining unknown optional data, unresolved references and
  explicit unsupported values. Separately verify insertion of absent Sample
  C-3/00 tuning defaults, corruption rejection and fake-store write failure.
  All proposed profile limits still await review. Keep UI Load/Save integration
  separate. Preserve all three FX slots and numeric unknown effects on rewrite;
  use bounded file-backed access without a whole-file allocation.
  Caller-owned candidates, owned immutable backing contracts, sequential block
  reads, direct stable-ID materialization, capacity/short-I/O failures and fake
  reopen/compare/promotion checks are covered. Fixed16 remains the baseline;
  decoded pattern storage uses naturally aligned arrays with a size-budget
  regression check: 7,684 bytes per buffer and 15,368 for two on the measured
  host ABI. Codec assignments and device admission remain provisional.
  [Validated host subset](FILE_FORMATS.md#host-only-envelopeschema-foundation)
* [x] Assess Teensy 4.1 without PSRAM using the production teensy41_dev build,
  target layout probes, identical-layout diagnostic map/stack build and isolated
  reservation link. Two compact patterns remain 15,368 bytes. A proposed 64 KiB
  RAM2 BTT arena has static capacity room; reliable runtime admission is still
  unproven. Preserve the existing sample/session/publication/rollback state.
  [Target assessment](FILE_FORMATS.md#teensy-41-without-psram-compilelink-memory-assessment)
* [x] Implement the isolated fixed-capacity FsFile immutable backing/arena
  prototype and actual four-call uint32_t/std::min portability fix. Three fixed
  slots, generation-file mutation exclusion, checked arena lifetimes, zero counted
  bounded-path ownership allocations, host fault tests and real ARM compile/link
  verification are complete. No production handler or playback integration.
  [Prototype](FILE_FORMATS.md#isolated-bounded-backingarena-prototype)
* [x] Isolate candidate resource admission: real singleton path refs, saved stable
  IDs/values and backing spans, initial-pattern-only source requirements, fallible
  ownership, rollback/cancellation and simulated stopped handoff. Host fault and
  real path/source regression tests plus target compile/layout evidence are saved.
  No second path pool or PCM caches, and no production integration.
  [Resource prototype](FILE_FORMATS.md#isolated-candidate-resource-admission)
* [x] Expose a narrow generation-checked candidate source pin API in the existing
  streaming producer, preserving the separate rollback mask and binding/voice
  protections. Real fake-SD tests cover reclaim/discard, failed/cancelled jobs,
  stale reuse, capacity, reset, exhaustion and repeated path/source ownership.
  [API and adapter boundary](SD_SAMPLE_STREAMING.md#candidate-source-pins-2026-11-27)
* [x] Implement the developer-only real SdSampleStreaming SourceService adapter:
  checked pins, asynchronous Pending/Ready admission, owned job tickets,
  refill-first service, scoped cancellation and checked close/quarantine retry.
  Real fake-SD/path tests and measured ARM adapter/lifecycle budgets are saved.
  [Adapter contract](SD_SAMPLE_STREAMING.md#developer-btt-sourceservice-adapter-2026-11-27)
* [x] Place selected cold BTT validation/rewrite/backing mutations in Teensy flash
  with a host-neutral abstraction. Identical-root/object/reservation comparisons
  reduce isolated ITCM by 8,288 bytes, return nine banks to eight and restore
  32,768 DTCM bytes; production hot paths and code/data sections are unchanged.
  Shared materialization/read helpers and asynchronous admission/adapter work
  remain in ITCM. Cache/SD timing and stack/heap peaks remain unqualified.
  [Placement evidence](FILE_FORMATS.md#cold-btt-code-placement-2026-11-27)
* [x] Prepare the opt-in stopped-only `teensy41_btt_diagnostic` image: explicit
  serial RUN, exclusive private files, native deterministic fixtures, real
  FsFile rewrite/reopen validation and cooperative candidate prepare/cancel
  cycles, checked cleanup and scoped duration/memory/ownership observations.
  Compile/host evidence is saved. The first physical run failed during rewrite;
  its logs and untouched on-card artifacts remain evidence.
  [Owner commands and limits](../tools/btt/DEVICE_DIAGNOSTIC.md)
* [x] Diagnose the first physical failure: real SdFat rejects beyond-EOF seek,
  unlike the original fake. Reproduce the rewrite failure, implement bounded
  EOF growth, and separate sticky I/O history from current cleanup resources.
  Preserve genuine quarantine and first-failure evidence. Host regressions pass;
  corrected stopped-only physical run passed both cases, 16 async cycles and
  final cleanup; normal firmware restored/identity checked. [Findings](../tools/btt/DEVICE_DIAGNOSTIC.md#first-physical-failure-and-software-correction)
* [x] Run corrected physical BTT/SD diagnostic once in fresh private directory
  `iofix-261127-b-8d351f`: both cases, 16 async cycles and final cleanup passed;
  complete serial/upload identity evidence saved under `build/btt-device-io-fix/`.
  Existing evidence retained; normal `teensy41_dev` restored and HELLO identity checked.
* [ ] Independently record card/filesystem and off-card output hashes; validate
  physical collision/failure evidence on private artifacts. Stopped-only success
  does not establish production stack/heap/IRQ admission, concurrent playback
  deadlines, media/source policy or FAT power-loss recovery.
* [x] Implement the first stopped-only production BTT load transaction behind
  `BTBTT1 LOAD <stable-pattern-ID> <SD-path>` in `teensy41_dev`: private sealed
  snapshot, Candidate/source admission, stable-ID projection, coherent existing
  audio handoff/ACK, legacy completion acknowledgement and full saved ownership.
  The initial projection refuses FX and unavailable required behavior. Legacy
  session writes/editing are blocked for the ephemeral retained tune. Real
  coordinator/producer fake-SD tests, target build/maps/lifecycle figures and
  read-only device status are under `build/btt-stopped-load/`.
  [Contract and limitations](FILE_FORMATS.md#stopped-only-developer-btt-load-2026-11-27)
* [x] Verify stopped-only production LOAD on an owner-authorized dedicated test
  card (2026-11-28): native sine/no-FX fixture staged/reopened/hash-verified on
  identified FAT32 K:, safe eject, then owner-confirmed Teensy installation.
  Real publication ACK/Complete-ok, pattern 503/BPM 137.50/notes/runtime-slot facts,
  stopped reconnect/ephemeral lineage and invalid-FX rejection with unchanged
  accepted tune passed. Final corrected normal firmware identity/IDLE/Empty was
  verified. Evidence: `build/btt-production-load-verification/card-20261128-b0c00c11a2/`.
  Reset and dedicated-card isolation replaced the original exact-state restoration
  prerequisite; the original card was untouched. Historical 2026-11-27 blocked
  preflight remains preserved in the parent directory. No listening, runtime
  stack/heap/IRQ admission or power-loss recovery claim; test SD evidence retained.
  Owner confirmed powered-off reinstallation of the original card after serial
  closure; no further device commands were issued.
* [x] Correct stopped-load member destruction order after fresh unoptimized host
  tests exposed Candidate leases calling an already-destroyed SdSourceService.
  Keep the service alive through candidate destruction; existing Debug and
  Release stopped-load tests pass. Dedicated-card protocol LOAD subsequently
  passed; original-state restoration was not tested.
* [x] Implement bounded FsFile-backed BTT Save storage transaction, without a
  serial command/UI or production integration: full saved-document/backing pin,
  Rewrite/Save exact comparison, exclusive candidate, checked sync/close/reopen,
  validated canonical/previous promotion, rollback and explicit recovery cleanup.
  Debug/Release actual-adapter fault/regression tests, repeated ownership and
  allocation counts, Teensy compile/link/layout/stack evidence are under
  `build/btt-save-backend/`. Isolated incremental reservation is 8,256 RAM2 bytes;
  normal production reserves nothing for this unintegrated backend.
  [Contract](FILE_FORMATS.md#bounded-fsfile-save-backend-2026-11-28)
* [x] Integrate ordinary BTT Save to the successful active LOAD's original path:
  `BTBTT1 SAVE/SAVEGET/RECOVER` v1, monotonic boot-lifetime request ledgers,
  full immutable saved data through FsSave, stopped/settled dispatch, explicit
  owned recovery and unchanged active playback/resources/ephemeral lineage.
  Reuse LOAD scratch; measured increments are 1,248 RAM2 / 1,024 RAM1 data /
  3,808 ITCM bytes, with eight banks unchanged. Debug/Release coordinator,
  backend and shared-policy regressions and actual-HEAD map builds are saved
  under `build/btt-stopped-save/`. No BTX UI, Save As or tune editing.
  [Protocol and lifetimes](FILE_FORMATS.md#stopped-only-production-ordinary-btt-save-2026-11-28)
* [x] Physically verify the new ordinary Save protocol/output on the dedicated
  FAT32 SanDisk after rejecting an initial nonmatching card without writes and
  the owner's powered-off card swap. CID-checked fresh fixture, production
  LOAD/Save/repeated Save/reload/readback, duplicate and Playing/Paused rejection,
  failed-LOAD destination and ephemeral reconnect passed. Independent read-only
  export exactly matches 7,714 native expected bytes and host CRC checks; no
  recovery siblings or legacy session files appeared. Same current normal image,
  initialized IDLE, empty loader and matching media verified; serial closed.
  This is separate-helper file export, not direct card-reader byte inspection.
  Evidence (including failed preflights): `build/btt-stopped-save/`. Save As/copy
  identity and product UI/edit lifecycle remain separate, undecided work; runtime
  resource admission and interrupted-write/power-loss qualification remain open.
* [x] Fix production loader reuse of an existing backing directory after real
  SdFat rejected repeated mkdir. Validate/open/checked-close the namespace with
  persistent quarantine ownership; never overwrite a regular file. The corrected
  fake reproduces the old failure; 15 stopped-load cases and 14 affected CTest
  entries pass, including foreign-file preservation and directory-close faults.
  Corrected hardware LOAD/reconnect/invalid-FX cleanup passed. Successful snapshots
  remain test evidence; checked in-process retirement of the final active owner
  and post-run off-card inventory remain outside this verification.
* [ ] Complete a production scheduler and lossless Tune Load/Save lifecycle,
  including dirty ownership/recovery and stronger source-change enforcement.
  Physical SD timing and production namespace/media policy remain separate. Validate
  actual stack/IRQ and heap peaks before device admission; 32 KiB stack and
  64 KiB heap remain provisional allowances. SD deadlines, FAT/power-loss recovery,
  playback prefetch, FX execution, UI and transport remain separate work.
* [ ] Extend LOAD/SAVE entry coordination to require STOP/acknowledgement from
  other states that are not confirmed stopped; failed or timed-out STOP keeps
  entry blocked. Confirmed device IDLE with no pending transport command already
  satisfies the stopped precondition and needs no fresh STOP exchange. Review
  the retained-work recovery route separately so its presentation never bypasses
  file-operation guards. Existing Playing/Paused/Done entry already waits for
  STOP and session Save. Add stopped-only guards to every
  future Tune/Project/Module Load/Save entry point, including device handlers;
  Paused and deferred working-session Save are not exceptions for those formats.
  [Transport review](FILE_FORMATS.md#transport-boundaries-requirements-and-current-guards)
* [ ] Reconcile D0030 and general screen-entry wording with the stopped-only
  Tune/Project/Module scope and existing live Instrument exception. Defer seamless
  Tune loading analysis until PSRAM is available, as a prerequisite; then assess
  candidate-state memory, source preparation, safe handoff and playback continuity.
* [ ] Keep guarded Sample/instrument loading available during playback. Make
  future Sample Save unavailable while Playing; leave instrument-file Save during
  playback open for processing, consistent-snapshot and SD I/O cost analysis.

## Instrument and Sample

* [ ] Add Instrument-screen A audition of the acknowledged Sample at default pitch.
  Keep A on Load WAV opening the Sample Browser. [Controls](BTX_CONTROLS.md#instrument)
* [ ] Implement Instrument Name and Description editing and read embedded WAV Title
  for Sample descriptions. Define how metadata and user edits interact.
  [Descriptive fields](INSTRUMENT_FOUNDATION.md#midi-channel-and-descriptive-fields)
* [ ] Physically verify Sample Root Note and Fine Tune on handheld with updated
  BTX and Teensy firmware. Windows BTX and Teensy verified defaults and edits
  on the restored Sample 06, Cancel, and Keep across save and restart. Automated
  checks cover hold acceleration, clamping, source-rate pitch and live triggers.
  [Sample format](SAMPLE_FORMAT.md)
* [ ] Define Sample instrument gain separately from track and master gain.
* [ ] Define Sample start/end behavior and its interaction with slices.
* [ ] Resolve Sample transpose independently of Root Note and Fine Tune.
* [ ] Define Sample loop handling and loop-tempo matching.
* [ ] Define optional original-tempo and musical-length metadata.
* [ ] Add waveform zoom and precise source-frame editing with exact data in the
  visible range. Design loops and persistent non-destructive slicing, including
  Pattern slice selection and both playback modes. FIT visualization and its
  display-only cache already work. [Waveform](INSTRUMENT_FOUNDATION.md#sample-waveform)
  [Slicing decision](ARCHITECTURE_DECISIONS.md#d0060---non-destructive-sample-slicing)
* [ ] Specify Instrument entry from channel headers, NOTE OFF timing fields and
  references outside 01-99. [Screen map](UI_SCREEN_MAP.md#3-instrument-foundation)
* [ ] Design separate Instrument Load/Save and editor undo histories. Extend Synth
  to selectable waveforms and graphical oscillator/envelope/filter controls;
  keep modular Synth2 a separate future engine. [Screen map](UI_SCREEN_MAP.md)
  [Synth foundation](SYNTH_FOUNDATION.md)

## Controls and tracker workflow

* [ ] Implement shared text entry for Tune Name, Save As and future instrument
  names: physical-keyboard input on desktop/handheld and during the controller
  overlay, draft/OK/whole-edit Cancel, orange key focus separate from insertion
  position. Accepted initial on-screen repertoire: ABC/abc, digits 0–9, Space,
  `- _ . ( ) ! & + = @ # $ %`. Accepted bindings: D-pad navigation, A activation,
  Y Backspace and B whole-edit Cancel. Keep physical-keyboard character acceptance,
  per-field length limits, overlay layout, insertion controls and filename edge
  cases open; keep
  title and editable filename separate with filename validation and automatic
  `.btt` handling. This separate task does not block the host-only BTT validator.
  [Text-entry proposal](UI_SCREEN_MAP.md#shared-text-entry-proposal)
* [ ] Move handheld L1+D-pad actions to an ergonomic R1-based layout. Resolve
  collisions with R1+Up/Down beat navigation and R1+Left/Right BPM before changing
  bindings. [Pattern controls](BTX_CONTROLS.md#pattern)
* [ ] Make double activation of Select+Start the handheld exit gesture and align
  practical B/Escape behavior across contexts. Specify the gesture interval and
  test release, held-chord suppression and ordinary Start behavior.
  [Controls](BTX_CONTROLS.md)
* [ ] Design configurable input bindings, SDL device profiles and a recovery path
  that works with a bad mapping. Check device-specific OS hotkeys separately.
  [UI/UX architecture](UI_UX_ARCHITECTURE.md)
* [ ] Design keyboard pitched-note and octave entry, expose Pattern Undo/Redo, and
  decide held-key transaction grouping. [Pattern controls](BTX_CONTROLS.md#pattern)
* [ ] Decide Pattern viewport and 64-row navigation, including off-page playback
  indication; review cursor, edit and mute presentation. Design a dedicated
  disconnected-device presentation. [Screen map](UI_SCREEN_MAP.md)
* [ ] Define mixer controls and final gain/headroom policy. Keep gain, master
  volume and tracker mute distinct. [Audio architecture](TEENSY_AUDIO_ARCHITECTURE.md)
* [ ] Design order-list sequencing, Pattern Break and Position Jump. Specify
  transition ordering, sounding audio/MIDI, deferred NOTE OFF, standalone loops,
  and temporary playback ranges independently of stored pattern length and visible
  rows. [Scheduler](SCHEDULER.md) · [Effect events](EFFECT_EVENT_ARCHITECTURE.md)
* [ ] Specify creation and resize semantics for stored Pattern lengths and apply
  the configured default to new Patterns only. Keep existing 384 PPQ. LPB is a
  deferred project-design discussion. Define MOD effect timing when importer
  work is scoped. [Architecture decisions](ARCHITECTURE_DECISIONS.md)
  [MOD import compatibility](EFFECT_EVENT_ARCHITECTURE.md#mod-import-compatibility)

## Storage, recovery and terminal runtime

* [ ] Follow BTT Foundation with BTT Load/Save, preserving tune-owned instrument
  parameters and unresolved references/settings through loading, playback and
  ordinary saves. [File formats](FILE_FORMATS.md#btt--brotracker-tune)
* [ ] Add unavailable-reference display and explicit relinking for missing/invalid
  samples and unavailable external instruments. Show original paths and saved
  values, failures/unsupported settings, and any effective fallback distinctly.
  [BTT preservation contract](FILE_FORMATS.md#btt--brotracker-tune)
* [ ] Follow BTT Foundation with BTP sample collection, project-relative sample
  references and separate rebuildable project waveform caching. Design Project
  Load/Save and establish durable lineage at the project transition boundary.
  [Project format](FILE_FORMATS.md#btp--brotracker-project)
  [Cache policy](INSTRUMENT_FOUNDATION.md#persistent-terminal-waveform-cache)
* [ ] Add Pattern Save As and filename entry. [File formats](FILE_FORMATS.md)
* [ ] Qualify session recovery with physical interrupted writes and card failures.
  Test separately powered USB removal during playback for continuity and retained
  edits. Verify detection/recovery if the device reboots without an observed
  disconnect; boot facts and durable session lineage already exist.
  [Reconnect continuity](RECONNECT_CONTINUITY.md#reconnect-protection)
* [ ] Verify persistent waveform reuse and exact partial resumption after a
  confirmed Teensy power loss, plus on-device ArkOS cache path and persistence.
  The owner-assisted reconnect check covers a separate case.
  [Persistent waveform cache](INSTRUMENT_FOUNDATION.md#persistent-terminal-waveform-cache)
* [ ] Report another BTX instance owning the serial connection distinctly from an
  absent device. Investigate the recorded handheld port-disable/disconnect without
  assuming a power, cable or EMI cause.
  [MIDI and platform limits](MIDI_FOUNDATION.md#open-issues-and-platform-limits-2026-10-31-updated-2026-11-03)
* [ ] Physically check executable-relative config and safe-config recovery on
  handheld, including configured PT8211/h0, and exercise logging rotation and
  archival failure on installed builds. Qualify differing ArkOS/dArkOS images
  and device mappings separately where behavior differs.
  [Host configuration](LOGGING.md#configuration-and-recovery)
  [Platform deployment](PLATFORM_DEPLOYMENT.md)
* [ ] Verify Linux BTX UTC synchronization on physical handheld, RTC retention
  after power loss, and raw classic FAT timestamps with a terminal offset.
  [Wall-clock exchange](LOGGING.md#teensy-wall-clock-exchange) · [File formats](FILE_FORMATS.md)

## Audio, MIDI and hardware qualification

* [ ] Physically verify `[audio] swap_channels` on the R36H with updated BTX and
  Teensy firmware. Compare off and on with a channel-isolated signal on the same
  output. Direct `aplay` reproduced the reversal. The physical cause and the
  compensation test remain open.
  [Audio architecture](TEENSY_AUDIO_ARCHITECTURE.md#startup-and-reconnect-protocol)
* [ ] Keep residual bridge crackle analysis and optional latency tuning deferred.
  Investigate analog hum separately and measure the effect of charging/power
  connection. [Bridge closeout](BRIDGE_POP_CLOSEOUT_20261021.md)
  [Audio architecture](TEENSY_AUDIO_ARCHITECTURE.md#accepted-bridge-functionalitycompensation-milestone-2026-11-08)
* [ ] Measure internal audio versus external MIDI onset, jitter and drift;
  qualify external MIDI mute, NOTE OFF and transport edge cases on physical
  equipment. [MIDI measurement](MIDI_FOUNDATION.md#physical-measurement-still-required)
* [ ] Follow the approved shared MIDI scheduling and selectable-backend design
  when scoped. Keep USB-host investigation deferred.
  [MIDI backend direction](MIDI_FOUNDATION.md#approved-direction-shared-midi-scheduling-and-selectable-backends)
* [ ] Qualify streaming with fragmented files, other cards, extreme aggregate
  pitch, longer mixed loads and injected physical I/O failure. Investigate the
  recorded SD initialization fault and the separate CDC startup receive overflow.
  [Streaming checks](SD_SAMPLE_STREAMING.md#remaining-checks)
* [x] Run one isolated comparison on the dedicated owner-supplied SanDisk 32 GB
  FAT32 card (2026-11-28): native deterministic fixture preparation/reopened hashes,
  diagnostic baseline, 8 KiB-ring/4 KiB-read low_ram retriggers and full-file EOF,
  safe storage correctness probe, and original 4 KiB/ten-loop DrumLoop benchmark
  passed. Preserve the larger 22.563 ms observed read tail despite lower p99/duty;
  no stale discard was observed. Write throughput remains unmeasured. Normal
  firmware restored and IDLE verified. No production parameter or original-card
  change; full-capacity authenticity, listening and power-loss recovery remain open.
  [Comparison](REFERENCE_SD_CARD.md#dedicated-32-gb-card-comparison-2026-11-28)
  Evidence: `build/sd-card-comparison/sandisk-20261128-e42d943a6c/`.
* [ ] Measure physical transport and event timing, including Pause/Continue,
  restart at zero, simultaneous triggers and deferred NOTE OFF. Stress rapid
  cross-channel working edits, backpressure and stale recovery; distinguish
  edit-to-applied latency from audible onset. Measure callback and full-graph
  headroom under sustained load. [Playback bring-up](TEENSY_PATTERN_BRINGUP.md)
* [ ] Qualify remaining handheld input edge cases, particularly held/reversed
  numeric stepping, OFF timing, release ordering and stressed edit/reconnect.
  Check physical mute and master-volume transitions, clipping and Synth
  attack/decay and rapid retrigger under mixed playback.
  [Controls](BTX_CONTROLS.md) · [Audio architecture](TEENSY_AUDIO_ARCHITECTURE.md)
* [ ] Validate Audio Shield output/input/control and optional codec processing
  on hardware. Before automatic route selection, qualify detection of present,
  absent and conflicting hardware across wiring, supply and cold starts. Keep
  explicit route selection authoritative.
  [Audio Shield](AUDIO_SHIELD_OUTPUT.md) · [Audio architecture](TEENSY_AUDIO_ARCHITECTURE.md)
* [ ] Evaluate candidate audio-graph processing and optional 0/8/16 MiB PSRAM
  layouts for DSP/Sample capacity, copy and CPU cost, and DMA/cache contention.
  [Memory architecture](TEENSY_MEMORY_ARCHITECTURE.md)
* [ ] Investigate the Shield front-panel connector and jack/USB wiring; verify
  electrical and mechanical compatibility before selecting parts.
  [Front-panel investigation](AUDIO_SHIELD_AMPLIFIER.md#future-front-panel-sourcing-and-wiring-investigation)
* [ ] Move the legacy AudioTestSource hardware tool out of the production firmware
  tree while retaining its diagnostic build. [Testing](TESTING.md)

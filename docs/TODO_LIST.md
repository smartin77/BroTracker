# TODO List

## Instrument and Sample

* [ ] Add Instrument-screen A audition of the acknowledged Sample at default pitch.
  Keep A on Load WAV opening the Sample Browser. [Controls](BTX_CONTROLS.md#instrument)
* [ ] Implement Instrument Name and Description editing and read embedded WAV Title
  for Sample descriptions. Define how metadata and user edits interact.
  [Descriptive fields](INSTRUMENT_FOUNDATION.md#midi-channel-and-descriptive-fields)
* [ ] Physically verify the delivered Sample Root Note and Fine Tune controls on
  Windows and handheld with updated BTX and Teensy firmware. Automated checks
  cover steps, hold acceleration, clamping, source-rate pitch, live triggers,
  Keep/Cancel, WAV replacement and session restore. [Sample format](SAMPLE_FORMAT.md)
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

* [ ] Add Pattern Save As and filename entry. Design Tune and Project Load/Save
  and self-contained Sample storage. Establish durable lineage at the project
  transition boundary. [File formats](FILE_FORMATS.md)
  [Cache policy](INSTRUMENT_FOUNDATION.md#persistent-terminal-waveform-cache)
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

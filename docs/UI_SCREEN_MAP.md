# BroTracker UI screen map and implementation direction

## Status and sources

This is a documentation-only proposal, not authorization to implement screens or
change runtime behavior. Screen responsibilities below are proposed unless
explicitly identified as implemented behavior or an established decision. A named
screen, field or action is not evidence that its backend exists.

Read together with [UI/UX Architecture](UI_UX_ARCHITECTURE.md),
[Architecture Decisions](ARCHITECTURE_DECISIONS.md), [TODO List](TODO_LIST.md),
[File Formats](FILE_FORMATS.md), [MIDI Support](MIDI_SUPPORT.md),
[Core Architecture](CORE_ARCHITECTURE.md), [Sample Format](SAMPLE_FORMAT.md) and
[Effect Event Architecture](EFFECT_EVENT_ARCHITECTURE.md). The TODO records
implementation and validation separately; this map does not upgrade either.

Implemented [live tempo control](LIVE_TEMPO_CONTROL.md) uses the existing PAT
header and transport: integer hundredths, one-decimal applied display, bounded
keypad/R1 input and session-only pattern-plus-tempo Publish/Restore. STOP/START
and reconnect retain applied tempo; Restore uses the published baseline. This
adds no settings/project screen or persistence UI; proposed screens below remain
proposals.

Implemented foundations include the shared bitmap pattern renderer, note/instrument
editing, independent edit and reported playback positions, transport coordination,
volatile working-pattern preview and separate published-pattern readback. Bounded
storage/transfer supports 64 rows; the current fixture defaults to 32 active rows
and the display shows 32. Gain primitives, player integration and capability-gated
gain coordination exist; this does not establish a finished Mixer screen or
hardware validation. Sample loading/playback exists for a constrained subset,
not a general sample browser, instrument authoring system or project persistence UI.

Established decisions retained here include eight initial channels; four rows per
quarter note (D0029/D0056); channel mute as pattern-event suppression (D0036);
Sample/MIDI/Synth instrument boundaries (D0042); independent sample rate,
reference pitch and loop tempo (D0055); paused current-row replay (D0054); and
configuration, logging and sparse-preview semantics (D0057–D0059).

## Shared interaction and navigation

- Preserve direct quick access in **OPT / INS / MIX** order. Proposed INS entry
  opens the instrument list, then the selected instrument editor. PAT remains the
  primary workspace. Project, Song, Export and Help access placement is pending;
  they do not displace the established quick-access order.
- Drill down from the selected object: PAT event to Event Detail; instrument
  field to INS/Instrument Editor; sample resource to Browser/Editor. Back restores
  the caller's cursor, selection, page and scroll context. Editors support abstract
  Previous Item / Next Item actions within the relevant collection.
- Use abstract Navigate, Select, Edit, Confirm, Back, Preview and Import actions.
  This map assigns no definitive keyboard or gamepad bindings and does not change
  existing bindings or transport behavior.
- Keep view/navigation state separate from musical/project state. Merely opening
  a page, selecting a tab or browsing a directory must not change musical data.
  Explicit instrument selection and cell assignment must remain distinguishable.
- Propose consistent compact project name/dirty status, transport state, BPM,
  connection/capability status and contextual action hints across screens. Show
  authoritative state, including unavailable/stale/disconnected information;
  never simulate device playback timing on the UI host.
- Retain the same 640x480 logical framebuffer on handheld and desktop, bitmap
  typography, pixel-aligned geometry and integer scaling. No oscilloscope is
  required for basic playback or editing. Four rows per quarter note stay fixed;
  no configurable LPB is introduced.
- Hide unavailable operations or disable them with a concise reason. A visible
  value may be informational without being editable. Controls require supported
  commands, validation and acknowledged results before becoming actionable.

## Screen responsibilities

### 1. PAT — Pattern

Primary workspace: eight visible channels with note/instrument columns, beat
highlighting, distinct edit cursor and playback position. Preserve editing during
playback, existing Start/Stop/Pause/Continue semantics and automatic volatile
preview versus explicit publication. Navigation must not retarget queued edits.

Paging must distinguish stored length, visible page and playback position.
The existing proposal is two 32-row pages for 64 rows, controlled by the edit
cursor, with off-page playback position visible rather than automatic playback
page flips. Paging controls/policy remain pending, not implemented by this map.
Highlight each quarter-note start every four rows regardless of row-number mode.
The shared renderer now extends this foreground treatment across all eight channels:
white beat notes, dimmer ordinary notes, purple instrument levels and independently
dimmed empty fields. Existing selection/playback/edit priorities remain intact;
no new background bands. See [palette and priorities](UI_UX_ARCHITECTURE.md).
Implemented 2026-10-22; manual visual approval pending.

### 2. Event Detail

Use the existing contextual right-side panel concept for common selected-event
edits: note, instrument and supported volume, pan and event parameters. Plan three
FX layers with descriptive effect names, values, valid ranges and contextual help,
including absolute/relative meaning, scope and engine support. Three slots are an
established design direction, not three implemented effect engines or a finalized
event encoding. Preserve Layer 1 import-mapping conventions without restricting
native Layer 1 to volume.

Complex parameters may open expanded editors; ordinary cell edits must not require
full-screen navigation. While editing during playback, retain explicit selected
event context rather than silently editing the advancing playhead's event. Any
optional playback-follow inspection needs a clearly identified target and a
resolved focus policy; see the contradictions below.

### 3. INS — Instrument List

Create/select instruments independently of composing a pattern. Show instrument
number, name, type, resource availability and project usage (references/counts
where known). Missing resources remain visible rather than deleting definitions.
Enter directly through INS or contextually from a pattern instrument field.
Opening an item inspects/edits it; assigning it to a pattern field is an explicit
operation. Creation and usage reporting depend on a real instrument registry and
reference model, not just existing development fixture bindings.

### 4. Instrument Editor

Common identity and navigation: number, name, type, availability, usage, Back and
Previous/Next instrument. Type-specific pages expose supported capabilities:

| Type | Proposed useful controls | Staged capabilities / limits |
| --- | --- | --- |
| Sample | Resource, gain, reference note, fine tuning, playback start/end range | Loops, envelopes, slicing and tempo matching later; expose each only when supported. |
| MIDI | Port/channel, bank/program and device notes | Supported CC mapping and external state capture/restore later; validate device capabilities and route availability. |
| Synth (Native) | Planned three oscillators with waveforms, tuning/detune and levels; envelope/filter pages | Exact synthesis limits and parameters pending; modulation later. No complete synth engine/editor is claimed. |

For samples, display source sample rate independently of reference pitch and
optional original BPM. Preserve explicit tuning metadata; fallback reference is
internal note 60 (Yamaha C-3), zero fine correction, not a measured root-note claim.
Do not infer BPM from duration without a known musical beat count. Tempo matching
requires explicit enablement and defined interaction with pitch transposition.
The current PCM16 mono 44100 Hz baseline does not imply other rates or tuning
metadata integration already work.

MIDI state transfer is capability-dependent, normally before playback; large
device-state transfers do not belong in the realtime event path. SysEx and
MIDI-CI/device-state mechanisms remain future work as documented in MIDI Support.
The [minimal sine Synth foundation](SYNTH_FOUNDATION.md) now replaces the development
beeper: one sine, fixed one-shot envelope and eight monophonic channel voices.
This is not completion of the planned three-oscillator Synth or its editor.
MIDI is an unsupported silent placeholder; modular Synth2 is separate future work.

Optional instrument modules use this same navigation/capability model. Unsupported
types/resources must be reported honestly. D0042's subsystem boundary does not
imply an existing dynamic runtime plugin loader; binary loading/ABI/memory details
remain unresolved in D0043/D0051.

### 5. Sample Browser

Directory navigation, filename/format, sample rate, channels, duration where
validated, availability and explicit Preview / Import operations. Browsing alone
must neither replace the current instrument nor modify the project. Preview is
an explicit audition with a stop/cancel path; import requires explicit destination
and resource validation before assignment.

Show estimated resource/memory implications and storage needs, distinguishing
known measurements from unknown costs. Unsupported, malformed or too-large files
need actionable reasons; do not advertise arbitrary WAV support, guaranteed
streaming or PSRAM. Import conversion and resource ownership remain prerequisites.

### 6. Sample Editor

Waveform with playback start/end and, when supported, loop boundaries and region
audition. Begin with nondestructive playback parameters referencing the source;
displaying boundaries must not imply loop playback is already implemented.
Destructive processing and slicing are later proposals requiring undo, shared
sample/data ownership, copy behavior and durable-save design. Slices belong to
the instrument layer, per Sample Format.

### 7. MIX — Mixer

Eight channel levels and master information. First expose existing gain semantics
through validated capability-aware controls/readback; add pan, meters or master
gain only as supported. Report applied versus pending gain state accurately.

Channel gain/audio silencing differs from BroTracker channel mute: zero gain
silences an audio contribution, whereas channel mute suppresses pattern-generated
note, instrument, FX and other events while playback advances. Supported manual
input can still play/control the selected global instrument. Do not claim the
entire designed mute/manual-input path is implemented merely because gains exist.

MIDI event activity is an event indicator, never measured external audio level.
External audio meters require an actual audio measurement path. Meter snapshots
must be bounded and optional, with stale/unavailable states; metering must not
make UI host refresh or communication responsible for realtime audio timing.

### 8. Song / Arrangement

Proposal pending sequencing and data-model decisions: an ordered sequence of whole
patterns, showing pattern names, stored lengths, repetition and song position.
Repeated entries may refer to the same pattern; order-entry identity differs from
pattern identity. Repetition representation and editing semantics remain pending.
Do not establish per-channel chain/phrase hierarchies or independent channel song
advancement. Existing single-pattern looping remains the implemented baseline.

D0056's future Break/Jump direction completes the current row before moving to
order-entry targets. Conflicts, invalid targets, sequence end, note continuation,
deferred OFF and paused replay interactions must be resolved before sequencing UI
can promise those actions. Arrangement does not redefine stored pattern length.

### 9. Project

Propose New / Load / Save / Save As, project name, BPM, description, pattern
management and missing-resource recovery. Follow the established responsibilities:

- BTT: lightweight tune/instrument definitions with external sample references;
  retain missing instrument definitions/parameters and mark samples **not located**.
- BTP: native project directory containing project data and copies of used samples;
  renaming coordinates the project name and associated directory.
- BTM: ZIP container of a complete BTP for transfer/backup/distribution, not another
  playback representation. Packaging is later work, not currently available Save.

Distinguish local draft/live working state, volatile device-applied preview,
published pattern baseline and durable SD save. Publication/readback success does
not mean saved to SD; dirty-versus-published and unsaved-project indicators must
be separately defined. Show pending/rejected/uncertain operations explicitly.

Before New/Load, replacement, deletion, resizing or overwrite, propose confirmation
that identifies affected data and offers cancel plus save/recovery where supported.
Failed/cancelled load or save must preserve a recoverable current project; success
must be reported only after durable completion. Missing-sample relinking must
validate compatibility before replacing a resource. Save transactions, recovery,
undo and SD read/write exclusivity (D0046) are backend prerequisites, not implemented
guarantees supplied by these screen names.

### 10. OPT — Settings

Keep Program/Interface settings separate from Project settings/operations, even
if Project is reached through OPT. Program settings include display notation, row
numbering, controls, audio selection, MIDI/sync, logging and connection/capability
status. Project-specific BPM, metadata and patterns belong to Project. A default
for newly created patterns must not resize existing patterns.

Preserve current validated INI semantics, precedence, built-ins, startup loading,
safe-config recovery and logging behavior. Normal startup does not automatically
rewrite configuration. Future settings UI must use the same validation and policy,
with explicit apply/save and restart-required feedback where applicable, rather
than inventing a competing settings system. Unsupported settings stay unavailable.

Preserve the mutually exclusive audio-module model. Current production
`[audio] output=bridge/pt8211` selection requests the Teensy route, not a host
playback-device selector; preserve documented defaults, negotiation and restart
requirements in [Teensy Audio Architecture](TEENSY_AUDIO_ARCHITECTURE.md).
MIDI/sync controls must reflect supported roles and capabilities without moving
timing authority to the host or claiming all planned MIDI backends exist.

### 11. Export

Future WAV mix export first; stems and portable BTM packaging later. No export or
offline rendering support is claimed. Define source snapshot, render range,
tail handling, destination and external-MIDI limitations before exposing Export.
MIDI events alone cannot render an external instrument's audio into a WAV.

Propose progress, cancellation and actionable failure reporting, with partial
output cleanup/recovery and no false success. Export requires a render/storage
workflow that respects realtime operation and SD ownership; cancellation semantics
must be designed before offering a Cancel control.

### 12. Help / Status

Contextual parameter/action help, versions, capabilities, connection and resource
status. Explain supported ranges and unavailable actions near the selection.
Normal music screens expose concise actionable status, not automated diagnostic
control panels. Detailed diagnostics remain a separate offline workflow; this
proposal neither changes the paused bridge-pop investigation nor its validation status.

## Incremental implementation direction

Planning sequence only. Each stage requires backend readiness before corresponding
controls become actionable; this is not authorization to implement the features.

| Stage | User benefit | Minimum useful scope | Backend prerequisites and dependencies |
| --- | --- | --- | --- |
| 1. Navigation foundation and instrument list | Reach instruments directly and return without losing editing context. | Abstract screen routing, Back context restoration, previous/next, read-only instrument identity/availability list; creation/selection only where backed. | Separate view state from musical state; instrument registry/identity and capability discovery; define selection versus assignment. Preserve existing editing/transport/preview contracts. |
| 2. Sample browser and minimal sample instrument editor | Choose and audition usable sounds deliberately. | Directory/validated metadata view, explicit preview/import, resource selection and only supported playback parameters. | Stage 1; bounded file inspection/loading, audition ownership, instrument-resource binding, memory/storage validation and supported parameter command/readback. Gain, tuning and ranges need backend support before controls. |
| 3. Project persistence and missing-sample recovery | Save work durably and recover unresolved resources. | Versioned BTT/BTP subset, New/Load/Save/Save As, metadata, dirty visibility and validated relinking. | Stages 1–2 resource/identity model; serialization design (binary layout is not yet fixed), resource copying/path policy, atomic/recoverable writes, SD exclusivity and explicit working/published/saved semantics. |
| 4. Mixer controls over existing gains, then additional supported functionality | Balance channels without confusing audio level and sequencing mute. | Eight supported gain controls with authoritative readback, master information and pending/error status. | Gain-capable player/protocol path and host coordination; capability gating and ownership/application semantics. Later pan/meters/master gain require their own DSP/telemetry support; tracker mute requires its separate event-suppression path. Coordinate persistence with stage 3. |
| 5. Multiple patterns and song arrangement | Build complete songs from reusable patterns. | Pattern management and proposed whole-pattern order list with names, lengths, repeats and position. | Stage 3 project model extended to multiple patterns/order entries; stage 1 navigation/paging; D0056 transition, repeat/end, flow-FX conflict, continuation/OFF and undo decisions. Existing bounded capacity alone is insufficient. |
| 6. FX detail, MIDI instrument and Native Synth editing | Shape events and author additional instrument types through one model. | Common contextual FX editing plus supported MIDI route/program and synth oscillator/envelope/filter parameters. | Stages 1–3 identity/persistence/capabilities; finalized event semantics, three-layer representation and engine mappings; MIDI scheduler/backend support; actual synth DSP and bounded parameter updates. Flow FX additionally depend on stage 5. Basic selected-event panel work may precede full FX support. |
| 7. Advanced sample editing and export | Refine resources and produce shareable audio/projects. | Supported waveform regions/loops first, WAV mix export first; destructive editing, slicing, stems and BTM later. | Stages 2–3 resource ownership/persistence; undo and copy semantics, loop/DSP support, render snapshot/range/tail policy, storage scheduling, cancellation and failure recovery. Stage 5 is needed for whole-song rendering; external MIDI audio capture is separate. |

## Contradictions and remaining design questions

- Older UI/UX Playback/Edit and effect-detail passages imply a stopped-only editor
  and a playback-following panel. Current live editing and independent cursors
  require an explicit focus/inspection policy. How should optional playback follow
  coexist with a pinned selected-event editor without changing its target?
- UI/UX Beat Highlighting uses LPB terminology; D0029/D0056 fix four rows per
  quarter note. Treat that older example as fixed-grid illustration, not a setting.
  Older centered-scroll descriptions also conflict with the current fixed view
  and pending paging comparison. Final paging/follow behavior remains undecided.
- UI/UX calls INS an Instrument Editor; this proposal makes its first level a list
  while preserving direct access and type-specific drill-down. Confirm final
  selection/assignment behavior and editor collection ordering during design.
- D0042 allows compiled modules without dynamic loading, while D0043 proposes
  binary loading and D0051 leaves execution/memory mechanisms undecided. What
  capability/interface contract can be supported first? No loader UI is assumed.
- D0055/Sample Format describe some pitch foundations as pending while newer TODO
  entries record later variable-rate integration. D0056/D0057 also retain old
  16-row implementation wording while TODO records 64-row capacity/32-row fixtures.
  Preserve decisions, consult current implementation/validation records and
  reconcile stale status separately; do not infer complete instrument authoring.
- Resolve order repetition representation, transition/flow conflict rules, sequence
  end and sounding-note/OFF behavior before arrangement implementation. Define
  project dirty baselines, save/recovery transactions, sample sharing and undo
  before persistence/destructive editors. Mute-time instrument-change policy
  remains open in D0036.
- Decide screen access placement beyond OPT/INS/MIX, final parameter ranges and
  display units, resource budgets, audio metering sources, export render scope and
  settings write/apply behavior as their backends mature. These questions do not
  reserve new input bindings or alter existing runtime behavior.

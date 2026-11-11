# Instrument foundation

Implemented on feature/instrument-screen; owner and physical approval pending.
Pattern opens one specific instrument directly (no Inventory) through the shared
STOP/acknowledged IDLE/edit synchronization/session-save transaction. Failed or
uncertain STOP/save retains Pattern. MIX's future playback exception is unchanged.

## Pattern-cell selector

See [controls](BTX_CONTROLS.md#instrument-entry). Entry captures the originating
row/channel/field and its original explicit instrument separately from selection.
Raw IDs 0-98 display as 01-99; 0xff means no explicit update. An explicit source
ID wins; otherwise the nearest preceding explicit ID in the same channel supplies
the initial selection (instrument-only events included). No earlier ID means 01.
There is no wrap, cross-channel lookup or playback-state inference. The nearest
unsupported inherited ID is reported, never skipped. Headers, NOTE_OFF timing
fields and IDs outside 01-99 stay unavailable. Note-only cells are supported.

The header shows the current selection and a turquoise [*NN] marker only
when the source cell had its own explicit ID. Inherited/default selection never
creates an original marker. Browsing only changes selection, not musical data.

Normal Ctrl+Down / Select+Down return confirms selection onto the captured cell,
even if inherited/default selection was never changed. The note, timing metadata
and original Note/Instrument field are preserved; an empty cell becomes an
instrument-only event. Assignment uses the existing editor capture/history and
preview synchronization. A byte-identical assignment creates no history/preview
work. Undo restores the prior cell. Enter remains STOP; closing BTX does not
confirm selection. No playback start/resume or audition is added.

Disconnected, recovery-held, locked/busy or invalid source contexts keep Instrument
visible with a return-unavailable reason; there is no blind replacement or queued
automatic assignment. Retry normal return only when the existing editor/serial
coordination safely accepts it. Recovery rules and retained/live data authority
are unchanged. Entry still uses the existing STOP/session-save workflow;
playback-preserving navigation, live assignment and Type editing remain deferred.

Instrument selector cancellation: Windows **Escape** / handheld **unmodified B**
returns to Pattern without assignment and restores the captured source cursor.
Cancellation remains available while disconnected or held for reconnect recovery;
retained musical work is preserved. Then enter recovery explicitly from Pattern
(Ctrl+Left / Select+Left), review and confirm or cancel using the existing recovery
menu. Cancellation never recovers work automatically or starts playback.
Ctrl+Down / Select+Down confirms assignment and remains blocked when unsafe.
Modified B keeps its existing STOP combinations; Enter remains STOP.
Closing BTX does not confirm assignment.


## Definitions and read-only availability

Teensy is authoritative. `kDevelopmentInstrumentDefinitions` is the single
firmware-initialized definition table: raw0/display01 Sample `Samples/test2.wav`,
02 MIDI, 03 Sine Synth, 04 Sample `Samples/test3.wav`, 05 Sample `Samples/test.wav`.
IDs identify instances, not Types. Definitions carry only Type and an optional
resource reference. The existing playable binding table is derived boot output,
not a definition registry: preparation failure omits playback but retains the same
ID, Type and reference. No replacement instrument is substituted. Existing binding
order, sample ownership, 80% fixture gains, timing and routing remain unchanged.

Preparation/inspection reports supply the immutable boot-preparation snapshot separately;
Prepared is not a guarantee against future streaming I/O errors. Sample status may
be Prepared, No sample selected, Resource unavailable, Preparation failed,
Not prepared or SD unavailable. Missing/open-failed resource is distinguished from
an inspected resource that failed validation/preparation. MIDI definition reports
External MIDI device unknown; neither definition nor this exchange proves an
external receiver is present. Synth reports Ready. No audio callback accesses
files, allocates memory or mutates definitions.

New firmware advertises both `BTINS1 CAP 1` and `BTINS1 CAP 2`. New BTX prefers
`BTINS1 GET 2 raw_id`, receiving exactly
`BTINS1 STATE 2 raw_id type availability reference`. Raw IDs are 0-98;
Type is 0 undefined, 1 Sample, 2 MIDI, 3 existing sine Synth. Availability codes
are 0 undefined, 1 prepared/ready, 2 no resource, 3 resource unavailable,
4 preparation failed, 5 not prepared, 6 storage unavailable, 7 external MIDI
unknown. Resource is a single bounded ASCII path token (maximum 63 characters)
or `-` for none. Lines fit the 96-byte firmware TX entry. Parsers enforce matching
Type/status/reference combinations; undefined is not an unavailable Sample.

`GET 1` / `STATE 1` semantics remain frozen binding facts for older hosts. New BTX
on CAP1-only firmware reports the binding Type with availability/resource Unknown
(legacy firmware); absent legacy binding is Unbound (legacy binding only), never
claimed to prove undefined. Unknown/disconnected/awaiting facts remain distinct.
Selection changes and disconnect clear all cached extension facts. Only negotiated,
valid, matching-ID replies are accepted; late legacy replies cannot replace v2 facts.
The Instrument screen shows Type, Availability and Resource separately.

Definitions remain firmware-initialized and immutable in this step. User creation,
contiguous collection navigation, rotation, Type editing and durable instrument
persistence are deferred. No new instrument parameter set or file format exists.
The development five definitions happen to be contiguous; maximum future collection
size remains 99. There is no New action, renumbering or sample-loading UI.

## Text presentation and deferred work

The production shared renderer uses existing Terminus 14 bitmap text and the
existing translation catalog, with stable Type, Name, Load, Save and Fine Tune
rows. Name, Load, Save and Fine Tune remain unavailable, with no hit targets or
editing handlers. Instrument Load and Save will eventually be separate screens.
Transpose remains undecided. Sample waveform/parameters together, graphical
Synth controls, browser, presets, audition and engine editing are deferred.

## Session snapshot

See [format](FILE_FORMATS.md#instrument-foundation-session-snapshot-2026-11-09).
Instrument entry requires snapshot-capable firmware; older firmware retains Pattern
with an update-required reason, so successful entry cannot silently omit metadata.
BTWS v2 extends the existing candidate/previous atomic writer, preserving the two
musical records and adding host cursor plus audio-owner mute mask. Legacy sessions
load with default cursor/unmuted channels. View data is main-owned and never sent
to the realtime player. Fresh hosts restore saved view; surviving hosts retain
current view and existing reconnect recovery. Boot mute restoration uses SubmitMute
and its audio-owner acknowledgment, never a reconnect write or automatic START.

Representative actual-renderer BMPs, build/test logs, report and complete diff are
in ignored `diagnostics/instrument-foundation/`. Owner should review 640x480 text,
provisional navigation, Windows/handheld chord behavior, snapshot power-cycle and
running reconnect on physical devices. Automated results are not physical proof.

Cursor restoration validates the full stored pattern dimensions, including valid
rows beyond the existing 32-row viewport. It does not resize a pattern or implement
paging; the separate 64-row paging/layout policy remains deferred.

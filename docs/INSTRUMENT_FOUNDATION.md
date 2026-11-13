# Instrument foundation

Agreed Instrument behavior on feature/instrument-screen. Changes to these rules
require explicit agreement with the project owner and a corresponding documentation
update. Physical verification is recorded separately from automated tests.
Pattern opens one specific instrument directly (no Inventory) through the shared
STOP/acknowledged IDLE/edit synchronization/session-save transaction. Failed or
uncertain STOP/save retains Pattern. MIX's future playback exception is unchanged.

## Pattern-cell selector

See [controls](BTX_CONTROLS.md#instrument-entry). Entry captures the originating
row/channel/field and its original explicit instrument separately from selection.
Raw IDs 0-98 display as 01-99; 0xff means no explicit update. An explicit source
ID wins; otherwise the nearest preceding explicit ID in the same channel supplies
the initial selection (instrument-only events included). No earlier ID means 01.
Inheritance lookup never wraps or consults another channel/playback state. The
nearest ID is never skipped. Collection-capable firmware permits reviewing an
out-of-collection reference as Undefined, including received IDs beyond99;
legacy entry still rejects IDs outside01-99. Headers and NOTE_OFF timing fields
stay unavailable. Note-only cells are supported.

The header appends a turquoise * directly after the original selected number
(e.g. Instrument 03*); when browsing another number it shows turquoise [*NN] only
when the source cell had its own explicit ID. Inherited/default selection never
creates an original marker. Browsing only changes selection, not musical data.

Normal Ctrl+Down / Select+Down return confirms selection onto the captured cell,
even if inherited/default selection was never changed. The note, timing metadata
and original Note/Instrument field are preserved; an empty cell becomes an
instrument-only event. Assignment uses the existing editor capture/history and
preview synchronization. A byte-identical assignment creates no history/preview
work. Undo restores the prior cell. Enter activates the focused Instrument field (STOP on Pattern); closing BTX does not
confirm selection. No playback start/resume or audition is added.

Disconnected, recovery-held, locked/busy or invalid source contexts keep Instrument
visible with a return-unavailable reason; there is no blind replacement or queued
automatic assignment. Retry normal return only when the existing editor/serial
coordination safely accepts it. Recovery rules and retained/live data authority
are unchanged. Entry still uses the existing STOP/session-save workflow;
playback-preserving navigation and live assignment remain deferred.

Instrument selector cancellation: Windows **Escape** / handheld **unmodified B**
returns to Pattern without assignment and restores the captured source cursor.
Cancellation remains available while disconnected or held for reconnect recovery;
retained musical work is preserved. Then enter recovery explicitly from Pattern
(Ctrl+Left / Select+Left), review and confirm or cancel using the existing recovery
menu. Cancellation never recovers work automatically or starts playback.
Ctrl+Down / Select+Down confirms assignment and remains blocked when unsafe.
Modified B keeps its existing STOP combinations; Enter activates the focused Instrument field; on Pattern it remains STOP.
Closing BTX does not confirm assignment.


## Definitions and read-only availability

Teensy is authoritative. `kDevelopmentInstrumentDefinitions` initializes the single runtime
`InstrumentDefinitions` authority: raw0/display01 Sample `Samples/test2.wav`,
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

Definitions start with the five firmware development instruments when no session
is restored. Temporary BTM session persistence now stores the entire authoritative
table, including unused and appended definitions; restore preserves slot numbers.
Legacy sessions without a table use the original five. Deletion, renumbering,
existing-Type mutation remains deferred. Sample paths can now be manually relinked
through the transactional workflow below.

A valid definition is structurally complete: Sample requires a valid SD-relative
WAV reference; current MIDI and Sine Synth require only their explicit Type.
Definition validation does not open or prepare WAVs. An unavailable Sample remains
a defined instrument, contributes to N, and retains its WAV reference and Pattern
references. It stops/produces no Sample voice rather than substituting another
instrument. A malformed definition rejects the entire session before activation.
The stopped foreground and boot restorer now prepare selected SD WAVs by saved
path through the shared streaming engine, including non-development assets. Up to
16 cached sources (including three reserved development sources) can be retained
at once; unreferenced dynamic entries are reusable after stopped replacement. Exceeding source capacity rejects a new commit; saved definitions whose
resources cannot be prepared remain defined and unavailable. See the temporary BTM and standalone
Pattern rules in [FILE_FORMATS.md](FILE_FORMATS.md#temporary-btm-session-version-1).

## Text presentation and deferred work

The production shared renderer uses existing Terminus 14 bitmap text and the
existing translation catalog, with stable Type, Name, Load, Save and Fine Tune
rows. Load is enabled for the default Sample draft in the trailing undefined slot and
for defined Samples, including unavailable resources.
Name, Save and Fine Tune remain unavailable. Instrument file Save is deferred.
Transpose remains undecided. Sample waveform/parameters together, graphical
Synth controls, presets, audition and engine editing are deferred.

## Session snapshot

See [format](FILE_FORMATS.md#instrument-foundation-session-snapshot-2026-11-09).
Instrument entry requires snapshot-capable firmware; older firmware retains Pattern
with an update-required reason, so successful entry cannot silently omit metadata.
Temporary BTM reuses the candidate/previous writer and retains host cursor plus
audio-owner mute mask with the working Pattern, BPM and definitions. Legacy BTWS
v2 remains readable. Legacy sessions
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

## Numbered collection and trailing undefined slot

Instrument numbers are 01-99 (wire/raw IDs 0-98). Defined instruments occupy
consecutive slots 01 through N. Teensy reports N through `BTINS1 COLLECTIONCAP 1`,
`BTINS1 COLLECTION GET 1` and `BTINS1 COLLECTION STATE 1 N`. Valid counts are
0-99 inclusive, derived from the definition table, including unavailable Samples.
Pending/unknown is distinct from confirmed zero. Disconnect invalidates the count;
reconnect reacquires it. Repeated capability lines do not repeat queries.

Both Pattern value selection and Instrument browsing use **01 through
min(N + 1, 99)**, inclusive:

| Confirmed N | Defined | Sole trailing undefined slot | Selectable |
|---|---|---|---|
| 0 | None | 01 | 01 |
| 5 | 01-05 | 06 | 01-06 |
| 98 | 01-98 | 99 | 01-99 |
| 99 | 01-99 | None | 01-99 |

This is an ordinary numbered slot, not a transient New Instrument action.
Pattern displays and may store its actual reference through normal history/preview.
An empty Pattern uses the same pool bounds. Numeric editing and assignment wait
while a capable device's count is pending; Clear remains available on otherwise
editable Instrument fields, preserving the note/timing and existing lock/OFF rules.
Legacy firmware retains its 01-99 behavior after snapshot readback.

Ctrl+Up / Select+Up opens the trailing slot from Pattern. Instrument Right from
the last defined slot reaches it and stops at the inclusive maximum; Left from01
wraps to that maximum, otherwise decrements. Its device identity reads **Not
defined yet**. Existing out-of-range references are preserved on load/history and
may be inspected or cancelled; Left selects the maximum and Right selects01.
Only a selection inside the current bounds may replace the source reference.

Notes using undefined valid references (01-99) stop that channel's voice, retain
the logical reference and never fall back to a prior/other instrument. An
instrument-only event updates the logical reference without triggering a note. They are safe in preview,
publication and rendering. Invalid legacy bytes can still be retained in stopped
metadata for inspection/repair; normal pitch/timing/shape validation remains.

## Draft qualification and confirmed return

Browsing changes selection only. An untouched undefined draft stays undefined.
A Sample draft qualifies only with an assigned sample reference; choosing Sample
or changing volume/pan without a sample does not qualify. Resource preparation
success is separate from definition identity. Sample Load opens the Teensy WAV
picker described below; volume/pan editing remains deferred.

**Temporary development exceptions:** explicitly choosing MIDI or Sine Synth
qualifies even with default parameters. An implicit default type never qualifies.
For the selected trailing slot, Windows unmodified **1 / 2 / 3** explicitly selects
Sample / MIDI / Sine Synth; handheld unmodified **X** cycles these draft Types.
Existing defined instruments' Types cannot be changed by these controls.

Ctrl+Down / Select+Down confirms the draft. If ineligible, return normally and
assign the numbered undefined reference; N stays unchanged and N+2 remains
unavailable. If eligible, confirmed return **commits the definition on Teensy**
first. The host advances its confirmed N and exposes the next slot only after
successful acknowledgement, then assigns through the existing Pattern history,
preview and source-cursor restoration paths. Failure retains Instrument, its draft,
the original source cell and the old confirmed count, with an error. Disconnect,
editor and retained-work recovery barriers still apply. A byte-identical Pattern
assignment creates no unnecessary history/transport work.

Escape / unmodified B discards the local draft and returns without a new assignment,
restoring the captured cursor and preserving an already stored reference. This also
works during disconnect/recovery and pending screen entry. It never starts recovery
or playback. An already transmitted definition commit still finishes acknowledgement
coordination, but cannot assign the cancelled source or reopen Instrument. Cancellation
cannot undo a definition that Teensy has already accepted after confirmed return.
Late entry-save replies cannot reopen a cancelled screen. Closing BTX is never
assignment/definition confirmation. Enter activates the focused Instrument field; modified handheld STOP chords retain STOP.

The separate **Save** command is reserved for future file operations and is outside
this workflow. Existing screen-entry STOP/session snapshot safety is retained;
the shared session snapshot now durably includes definitions in temporary BTM.

## Bounded definition commit extension

New firmware advertises `BTINS1 DEFINECAP 1`; older firmware remains usable for
inspection/numbered selection, with unavailable commit reported honestly. The host
sends `BTINS1 DEFINE 1 request expected_count type reference` (63-character bounded
ASCII resource token, or `-`). Teensy requires stopped/storage-ready coordination,
exact expected count, available capacity and a qualifying definition. MIDI/Synth
append their existing binding types. New Sample commits require successful
foreground preparation before the bounded binding/definition append; SD I/O is
never performed under the interrupt guard or in the audio callback. Failed
preparation leaves N and the originating cell unchanged. Boot restore separately
retains valid unavailable definitions. Existing prepared paths share immutable
source ownership.

Reply: `BTINS1 DEFINED 1 request result count`. Result 0 succeeds; 1 malformed,
2 stale/full collection, 3 not ready, 4 invalid definition/resource, 5 binding
installation rejected, 6 WAV preparation/format/read/source-capacity failure. Identical retries of the last request replay its result;
expected-count checking prevents duplicate creation. The host does not advance N
on rejection/timeout, nor automatically retry an uncertain commit. Reconnect
reacquires authoritative facts. Identity CAP1/CAP2 formats remain unchanged.

## Sample Load for the trailing slot

The trailing slot defaults to Sample. Focus **Load WAV**, then activate it with
Windows Enter or handheld A. The shared picker lists regular `.wav`/`.WAV` files
and folders on Teensy SD, starting in `/BroTracker/Samples/` when readable or
its nearest readable parent. It uses the existing bordered list and paging controls. Hosts never browse their local filesystem. Up/Down selects an entry;
Enter/A opens a folder or chooses a WAV and returns to Instrument, setting only the draft SD-relative
path (for example `BroTracker/Samples/kick.wav`). Escape, Backspace or B in the
picker returns to Instrument with the previous draft intact. Escape/B in Instrument
discards the draft and returns to Pattern with its source cell unchanged.

Ctrl+Down / Select+Down confirms return. Teensy validates and prepares the WAV while
stopped, then acknowledges the definition before the host assigns its slot and
exposes the next trailing slot. Unsupported/missing/read-failed WAVs or rejected
binding installation retain Instrument, the draft, old N and the original cell;
the reason is shown. Choosing a filename is not a definition commit or audition.
Enter/A activates the focused actionable field. Defined MIDI/Synth Types remain read-only.

Supported files use the existing RIFF validator: PCM16, mono, 44100 Hz, nonempty
sample data and bounded metadata/chunk traversal. Filenames use 1..40 ASCII letters,
digits, underscore or hyphen plus `.wav` or `.WAV` (44 bytes maximum); paths fit the
existing 63-byte definition resource field. The Teensy browser supports directory navigation; waveform, audition and sample editing remain deferred.
Replacing a saved Sample path requires the explicit confirmation below. Temporary BTM stores the path
unchanged. Boot prepares it again by path; missing or failed resources keep their
slot, definition and Pattern references, report unavailable, and never substitute
another sound. Development resources at `Samples/test*.wav` remain compatible.

## Focused fields and transactional Sample relink

See the exact Windows/handheld [field controls](BTX_CONTROLS.md#instrument-field-cursor-and-sample-relinking).
The trailing slot presents Sample by default while retaining Not defined yet as
status; no explicit Type choice is needed before navigating to Load WAV. An orange
edit frame identifies the focused Type or Load field. Ordinary editor navigation
moves between actionable fields, and editor value controls edit the focused Type.
Existing MIDI/Sine Synth qualification exceptions remain unchanged.

A defined Sample always retains its saved path and displays an orange load warning
if preparation failed. Its Load WAV field allows manual location/relinking. Choosing
a different path requires confirmation displaying saved and candidate paths. Decline
retains the saved path; accept only stages a candidate, shown separately. Confirmed
return prepares it on Teensy outside the audio interrupt guard. Only then does a
bounded stopped transaction replace that Sample's binding and resource. Failed
preparation or installation preserves the old resource, binding/unavailable state,
slot, Pattern references and count. Successful relink also preserves these identities
and existing playable binding reference-note/level settings, replacing only its PCM
source. A previously unavailable Sample uses the established Sample defaults.
Successful relinks schedule normal BTM persistence; boot prepares the new saved path.

`BTINS1 RELINKCAP 1` advertises `BTINS1 RELINK 1 request raw_slot 1 resource` and
`BTINS1 RELINKED 1 request result count`. The resource uses the existing bounded
63-byte SD-relative token. Only existing Sample slots are eligible; Type cannot
change. Result codes and uncertain-commit/reconnect rules match DEFINE. Repeated
identical last requests replay their recorded outcome, including whether the command
was DEFINE or RELINK. New preparation must succeed before the definition or actual
player binding is mutated. Source-cache capacity remains bounded; an unsuccessful
installation discards a newly prepared unused source. Old sources remain intact until candidate preparation and binding installation
succeed. Stopped foreground reclamation then releases only dynamic entries with
no current binding or retained voice snapshot/span. Shared sources stay pinned,
and live descriptors/start buffers never move. Reused vacant slots avoid exhaustion
from historical replacements; a cache of entirely retained sources still requires
a spare entry before replacement can proceed. File closes and reads remain
foreground SD operations, outside the audio interrupt guard.


## Teensy SD directory browser

Load WAV starts at `/BroTracker/Samples` only after Teensy has opened and listed
it. If missing or unreadable, Teensy tries the nearest readable parent, up to `/`.
The UI names the fallback reason and confirmed directory; it never presents the
requested path as though it had been opened. No folders are created by browsing.
Subdirectories and all WAV filenames appear together, folders first. Below
the SD root a visible `..` opens the parent. Enter/A opens the selected folder or
stages the selected file's complete SD-relative path. Escape/B cancels to
Instrument, preserving the previous draft. Modified screen chords retain their
existing meaning. See [exact controls](BTX_CONTROLS.md#teensy-sd-wav-browser).

Empty listings show `No supported samples`. Missing-directory and SD read errors
use the approved single-line folder messages and preserve the last fully listed
directory and selection. WAV metadata and rejection reasons come from the actual
firmware validator; confirmed return still prepares again. A file whose full
SD-relative path exceeds 63 bytes stays visible but cannot be selected. Directory paths must also fit 63 bytes and the existing path
character rules. Browser paging, file opens and directory reads run on Teensy in
stopped foreground code using the existing shared SD read guard. Windows and
ArkOS render the same confirmed listings and never enumerate their own disks.


## Sample Browser validation and stereo import follow-up

The Sample Browser uses the complete host viewport: confirmed SD path at the top,
a scrollable list through the remaining middle area, and exactly one status line
at the bottom. Diagnostics and repeated control hints are hidden. Directory rows
(including `..` below root) normally leave the status line blank. Empty listings
show `No supported samples`; navigation failures use `Folder not found` or
`Unable to read folder`, while retaining the last confirmed directory/list.

All WAV filenames remain visible, including invalid and unsupported files;
unrelated files are excluded. Teensy reads actual metadata with the same bounded
`InspectWavMetadata` validator as streaming preparation, without reading PCM.
Each listing page inspects at most one WAV and at most 64 directory entries;
the existing per-file chunk limit applies. Metadata/results travel with the
correlated listing, so moving the highlight does not issue SD reads. A supported
file displays its actual PCM format, bit depth and sample rate. Rejected files
display the corresponding approved reason on that single line. Enter/A on a
rejected WAV leaves the browser and Instrument draft unchanged. Confirmed return
still revalidates/prepares supported candidates before defining or replacing a
binding; listing metadata is not a preparation acknowledgement.

Current streaming supports PCM16 mono at 44100 Hz, not stereo WAV, IFF or 8-bit
samples. Future stereo import may either downmix to one mono sample or split the
left and right channels into two separate mono samples. Both import options and
their controls are deferred; this change does not implement conversion or splitting.

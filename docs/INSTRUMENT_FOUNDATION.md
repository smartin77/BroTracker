# Instrument foundation

Agreed Instrument behavior on feature/instrument-screen. Changes to these rules
require explicit agreement with the project owner and a corresponding documentation
update. Physical verification is recorded separately from automated tests.
Pattern opens Instrument directly (no Inventory) with Ctrl+Up on Windows or
Select+Up on ArkOS. With current LIVEEDITCAP firmware, entry captures the source
and Pattern baseline in BTX RAM and begins a Teensy visit; it does not issue STOP
or save a session. Pattern playback continues. Legacy stopped entry retains the
STOP/acknowledged IDLE/edit synchronization/session-save path; failed or uncertain
STOP/save retains Pattern. Pattern Load/Save and exit retain their STOP requirements.

## Pattern-cell selector

Entry captures the originating row/channel/field and original explicit instrument.
Raw IDs 0-98 display as 01-99; 0xff means no explicit update. The source cell's
explicit ID wins; otherwise the nearest preceding explicit ID in the same channel
(including instrument-only events) supplies the initial slot, defaulting to 01.
Lookup never wraps or uses playback history or another channel. Headers and
NOTE_OFF timing fields cannot open Instrument; note-only cells can.
Collection-capable firmware permits inspection of out-of-collection references,
including received IDs beyond 99; legacy entry rejects IDs outside 01-99.

An untouched visit shows the resolved slot alone. A star indicates an outstanding
assignment or definition change anywhere in the visit. When the browsed slot
differs from the resolved entry slot, the suffix is ` * [NN]`. Local drafts,
pending writes and acknowledged differences count. Returning to the entry slot
removes the bracketed number. The star clears when no differences remain,
including acknowledgement of any restoration. An error alone adds no marker.
Keep on the resolved entry slot preserves an empty raw assignment.

See [exact controls](BTX_CONTROLS.md#instrument). Entry focuses Type. Up/Down
(or Windows Shift+Tab/Tab) cycles Type, available Load WAV, and Instrument slot;
the orange frame identifies focus. Left/Right on slot clamps to **01 through N**
(maximum 99), without wrapping or selecting N+1. Browsing keeps the original
resolved slot frozen and leaves the captured cell unchanged. Keep assigns the
browsed slot through normal live Pattern editing. Note/timing data and source
field are preserved. An empty cell becomes an instrument-only event. Byte-identical assignment adds no history or
preview work. Slot stepping requires a known collection, synchronized live Pattern
editing and an unlocked editor. Pending file/publication/transport operations,
disconnect/recovery barriers, a retained Type/WAV draft, or unsettled/failed live
Type/WAV acknowledgement block it. With N=0 there is no defined slot to step to.

Left/Right on Type cycles Sample, MIDI and Sine Synth independently of the Pattern
value-direction preference. For existing slots, changes apply to Teensy RAM during
the visit, including built-in slots. Converting to Sample exposes Load WAV; until
a WAV is chosen the previous playable binding remains and confirmed return is
blocked. MIDI and Synth use their development defaults. Enter/A only activates
Load WAV; it does nothing on Type or slot. PgUp/PgDn, L1 value stepping, 1/2/3 and
X do not edit Type.

Ctrl+Down / Select+Down keeps the live source assignment and all applied Type/WAV
changes made during this visit. Escape / unmodified B restores the original source
cell instrument and every touched existing definition/binding, then restores the
captured cursor. Backspace also cancels through the browser-back mapping. Rollback
uses the retained Teensy snapshot, including original unavailable bindings; it
requires acknowledgement rather than immediately leaving the screen. Neither
return starts playback. Modified handheld B retains its STOP combinations.
Closing BTX is not visit confirmation.

Keep/rollback waits for live definition results, verified working-Pattern application
when the assignment changed, and visit-end acknowledgement. Original Sample sources
remain pinned until the visit ends. Disconnection/recovery or uncertain results may
leave Instrument visible with a pending/unavailable reason; cancellation requests
restoration but cannot promise an immediate offline rollback. Recovery remains an
explicit Pattern action, never automatic. A rejected preparation/installation keeps
the old binding; Load can retry or Escape can restore. An unconfirmed outcome is
reported separately and Escape restores the retained snapshot. Do not infer success
from a locally displayed desired Type or path.

Outside the live slot Keep path, a changed reference requires STOP. Keeping an
unchanged resolved entry slot preserves its raw assignment. There is no queued automatic assignment across safety barriers.

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
unknown. This legacy v2 resource is a single ASCII path token (maximum 63 characters)
or `-` for none. Current negotiated long-path facts use GET/STATE 3 and the
[bounded shared path extension](SAMPLE_PATH_STORAGE.md); no path is truncated. Parsers enforce matching
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
Legacy sessions without a table use the original five. Deletion and renumbering
remain deferred. Sample paths can be replaced through the live workflow below.

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
Synth controls, presets and engine parameter editing are deferred. Browser audition
behavior is documented below.

## Session snapshot

See [format](FILE_FORMATS.md#instrument-foundation-session-snapshot-2026-11-09).
Legacy stopped Instrument entry requires snapshot-capable firmware; otherwise
Pattern shows an update-required reason. Current live entry uses the RAM visit
snapshot instead of an entry-time session save.
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

Pattern value selection and confirmed-return validation use **01 through
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

The **Pattern** instrument value range includes N+1 when N<99; select that
reference on Pattern, then Ctrl+Up / Select+Up opens it. In-screen live slot
stepping only selects defined slots 01-N. The trailing slot reads **Not defined
yet** and defaults locally to Sample. Out-of-range stored references remain
inspectable/cancellable, but only an in-bounds reference can be assigned on return.

Notes using undefined valid references (01-99) stop that channel's voice, retain
the logical reference and never fall back to a prior/other instrument. An
instrument-only event updates the logical reference without triggering a note. They are safe in preview,
publication and rendering. Invalid legacy bytes can still be retained in stopped
metadata for inspection/repair; normal pitch/timing/shape validation remains.

## Draft qualification and confirmed return

New-slot editing is a local draft, separate from existing-slot live editing.
An untouched undefined draft stays undefined.
A Sample draft qualifies only with an assigned sample reference; choosing Sample
or changing volume/pan without a sample does not qualify. Resource preparation
success is separate from definition identity. Sample Load opens the Teensy WAV
picker described below; volume/pan editing remains deferred.

**Temporary development exceptions:** explicitly choosing MIDI or Sine Synth
qualifies even with default parameters. An implicit default type never qualifies.
Use the focused Type field's Left/Right controls on both hosts to make an
explicit choice; the retired numeric/X shortcuts do not change Type.

Ctrl+Down / Select+Down confirms the trailing-slot draft while stopped. During
playback it retains the draft with a STOP-required reason. If ineligible, return
normally and assign the numbered undefined reference; N stays unchanged and N+2 remains
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
this workflow. New-slot DEFINE still requires STOP/storage readiness; the shared
session snapshot durably includes definitions in temporary BTM.

## Bounded definition commit extension

New firmware advertises `BTINS1 DEFINECAP 1`; older firmware remains usable for
inspection/numbered selection, with unavailable commit reported honestly. The host
sends legacy `BTINS1 DEFINE 1 request expected_count type reference` (63-character
ASCII token, or `-`), or negotiated DEFINE 2 with an encoded full path. Teensy requires stopped/storage-ready coordination,
exact expected count, available capacity and a qualifying definition. MIDI/Synth
append their existing binding types. New Sample commits require successful
foreground preparation before the bounded binding/definition append; SD I/O is
never performed under the interrupt guard or in the audio callback. Failed
preparation leaves N and the originating cell unchanged. Boot restore separately
retains valid unavailable definitions. Existing prepared paths share immutable
source ownership.

Reply: `BTINS1 DEFINED 1 request result count`. Result 0 succeeds; 1 malformed,
2 stale/full collection, 3 not ready, 4 invalid definition/resource, 5 binding
installation rejected, 6 WAV preparation/format/read/source-capacity failure,
9 bounded shared path-storage capacity exhausted (previous binding retained). Identical retries of the last request replay its result;
expected-count checking prevents duplicate creation. The host does not advance N
on rejection/timeout, nor automatically retry an uncertain commit. Reconnect
reacquires authoritative facts. Identity CAP1/CAP2 formats remain unchanged.

## Sample Load for the trailing slot

The trailing slot defaults to Sample. Focus **Load WAV**, then activate it with
Windows Enter or handheld A. The shared picker lists regular `.wav`/`.WAV` files
and folders on Teensy SD, using the BTX startup chain `/Samples` -> `/BroTracker`
-> `/`. Only missing candidates trigger fallback; an empty directory is valid and
other SD errors remain visible. It uses the existing bordered list and paging controls. Hosts never browse their local filesystem. Up/Down selects an entry;
Enter/A opens a folder or chooses a WAV and returns to Instrument, setting only the draft SD-relative
path (for example `BroTracker/Samples/kick.wav`). Escape, Backspace or B in the
picker returns to Instrument with the previous draft intact. Escape/B in Instrument
discards the draft and returns to Pattern with its source cell unchanged.

Ctrl+Down / Select+Down confirms return. Teensy validates and prepares the WAV while
stopped, then acknowledges the definition before the host assigns its slot and
exposes the next trailing slot. Unsupported/missing/read-failed WAVs or rejected
binding installation retain Instrument, the draft, old N and the original cell;
the reason is shown. Choosing a filename is not a definition commit or audition.
Enter/A activates Load. Existing-slot live editing is described below.

Supported files use the existing RIFF validator: PCM16, mono, 44100 Hz, nonempty
sample data and bounded metadata/chunk traversal. Current filenames may contain ordinary spaces and use any case of `.wav`; the
validated full SD-relative path is bounded at 255 ASCII bytes. Folder navigation,
audition, assignment and persistence use the same bound. Legacy WAVLIST retains
its original filename rules. See [path storage](SAMPLE_PATH_STORAGE.md). The Teensy browser supports directory
navigation; waveform and sample parameter editing remain deferred.
Replacing a saved Sample path requires the explicit confirmation below. Temporary BTM stores the path
unchanged. Boot prepares it again by path; missing or failed resources keep their
slot, definition and Pattern references, report unavailable, and never substitute
another sound. Development resources at `Samples/test*.wav` remain compatible.

## Existing-slot WAV browsing and replacement

Focus Load WAV and press Enter/A. Its transport gate is confirmed stopped
transport or a ready live Instrument visit. The other guards still require
Sample Type, browser capability, no open picker or confirmed-return operation,
no user-action/recovery barrier and no pending file, transport or publication
operation; live-edit firmware also requires no pending definition commit.
A defined unavailable Sample remains eligible and shows a load warning.

Choosing a different path from a current Sample opens an old/new-path confirmation.
Escape/Backspace/B declines it and returns to Instrument with the current binding.
Enter/A accepts the candidate; it is not confirmation of the whole visit. Both
WAV selection and replacement acceptance start live preparation when
`StageLiveSample()` accepts the request. If live staging is unavailable, the
candidate stays local, including with current firmware. It can apply through
stopped confirmation only when the controller reaches that fallback path. If the
visit already has a live assignment or tracked live definition edits
(`live_assignment_ || HasLiveTypes()`), Ctrl+Down / Select+Down rejects an unapplied
Type/WAV draft before reaching the stopped fallback, even after STOP. The screen
asks to discard the draft or cancel before returning. When live staging succeeds, Teensy prepares
incrementally outside the audio interrupt guard, then installs the binding and
definition together.
Failure retains the old definition/binding and count; the screen reports the reason.
Slot identity and all Pattern references remain unchanged. Same-type Sample
replacement preserves reference-note/level; conversion uses Sample defaults.

Pattern transport continues through replacement and restoration. Voices belonging
to the replaced binding are released (including MIDI cancellation); this is not
a Pattern STOP or timeline reset. Future events use the replacement binding.
Ctrl+Down keeps these live changes; Escape restores all touched slots for the visit.

LIVEEDITCAP uses VISIT BEGIN/READY, EDIT/REPLACED, CANCEL, RESTORE and VISIT
END/ENDED coordination. The original definitions, bindings and availability are
retained, and original sources pinned, for rollback. SD work stays in foreground;
source capacity remains bounded and preparation can fail without replacing the old
binding. When live staging is unavailable, candidates remain local. Only when the
confirmed-return guards allow the stopped fallback can RELINKCAP/REPLACECAP
transactions apply them; STOP alone does not bypass the live-visit draft guard.
Their result/uncertain-outcome rules still apply. Temporary
BTM stores the resulting authoritative table and paths; boot prepares by path and
retains unavailable definitions without substituting sounds.

## Teensy SD directory browser

On Sample Browser entry, BTX requests an exact initial listing of `/Samples` on
Teensy SD. A missing-directory result advances to `/BroTracker`, then `/`; no
other error triggers fallback. Empty directories count as successful opens. The
first successful listing is reused as the first progressive batch, without a
second open/probe. The header changes only to a confirmed opened directory.
Progressive continuation and explicit folder navigation do not run this startup
chain. BTX sends the directory-listing request with Teensy's automatic-parent
fallback disabled, so read errors are surfaced rather than hidden. No directories
are created by browsing.

The separate **instrument-file Load** action has no browser implementation yet.
Its intended independent startup chain is `/Instruments` -> `/BroTracker` -> `/`,
also missing-only, accepting empty directories and surfacing other SD errors.
This is distinct from the existing **Load WAV** action and its Sample Browser.

Subdirectories and all WAV filenames appear together in directory scan order. Below
the SD root a visible `..` opens the parent. Enter/A opens the selected folder or
stages the selected file's complete SD-relative path. Escape/B cancels to
Instrument, preserving the previous draft. Modified screen chords retain their
existing meaning. See [exact controls](BTX_CONTROLS.md#sample-browser).

Empty listings show `No supported samples`. Missing-directory and SD read errors
use the approved single-line folder messages and preserve the last fully listed
directory and selection. WAV metadata and rejection reasons come from the actual
firmware validator; applying a candidate still prepares it again. A file whose full
SD-relative path exceeds 255 bytes stays visible but cannot be selected. Directory
paths must also fit 255 bytes and the validated component
character rules. Browser paging, file opens and directory reads run on Teensy in
foreground code using the shared SD read guard. Active live visits use bounded
background browse jobs paced behind streaming refill/headroom; the legacy path
still requires stopped transport. Windows and ArkOS render the same confirmed listings and never enumerate their own disks.


## Sample Browser validation and stereo import follow-up

The Sample Browser uses the complete host viewport: confirmed SD path at the top,
a scrollable list through the remaining middle area, and exactly one status line
at the bottom. Diagnostics and repeated control hints are hidden. Directory rows
(including `..` below root) show the sample count without file metadata. Empty
listings show `No supported samples`; navigation failures use `Folder not found` or
`Unable to read folder`, while retaining the last confirmed directory/list.

All WAV filenames remain visible, including invalid and unsupported files;
unrelated files are excluded. Teensy reads actual metadata with the same bounded
`InspectWavMetadata` validator as streaming preparation, without reading PCM.
Current firmware scans at most 64 directory entries and returns at most eight
rows per batch. Each foreground turn performs at most one directory operation or
bounded metadata read; the existing per-file chunk limit applies. The first batch
is usable immediately, and further batches fill the 24-row viewport and append
without resetting highlight or scroll. BTX does not wait for a full-directory scan,
sort or metadata pass. Only this directory is listed. The footer shows
`NN samples read` as WAV entries arrive, excluding folders and the parent row;
selected-file facts and errors remain alongside it, and the completed count stays
visible. Folder changes and browser exit invalidate outstanding listing replies.
Metadata/results travel with the correlated listing, so moving the highlight does not issue metadata SD reads.
A supported file displays its actual PCM format, bit depth and sample rate. Rejected files
display the corresponding approved reason on that single line. Enter/A on a
rejected WAV leaves the browser and Instrument draft unchanged. Live application
(existing slots) or confirmed return (new slots) revalidates/prepares supported
candidates before defining or replacing a binding; listing metadata is not a
preparation acknowledgement.

Current streaming supports PCM16 mono at 44100 Hz, not stereo WAV, IFF or 8-bit
samples. Future stereo import may either downmix to one mono sample or split the
left and right channels into two separate mono samples. Both import options and
their controls are deferred; this change does not implement conversion or splitting.


## Manual Sample Browser audition

P on Windows / X on ArkOS auditions the highlighted supported WAV only when
Pattern transport is confirmed stopped. Enter/A still chooses a WAV or opens a
directory. List navigation, hover and row selection remain silent. Directories,
unsupported files, paused transport and playing transport cannot start audition.
A fresh press replaces any active/pending audition and restarts at frame zero;
key/button hold and repeat do not retrigger.

Audition uses the existing Teensy SD streaming engine with a separate ninth voice
ring (4096 frames), the bounded immutable start (2048 frames) and bounded refill
reads. It does not load an entire file or truncate playback to a preview duration.
Supported [WAV formats](SAMPLE_FORMAT.md) play through EOF at their original
pitch and duration without instrument tuning or Pattern tempo input. Listening volume
still applies. Audition never changes bindings, source-cell assignments, WAV drafts
or Pattern transport. Leaving the browser or starting Pattern playback cancels
active and pending work; cancelled or superseded preparation cannot start audio.
Unused dynamic sources are reclaimed after the voice releases them.

`BTWAV1 CAP 1` advertises this temporary protocol. BTX sends
`BTWAV1 PLAY 2 request encoded_resource` after BTPATH1 negotiation, or legacy
PLAY 1; `BTWAV1 CANCEL 1 request` is unchanged. Resources use the bounded
255-byte SD-relative path rules described in [path storage](SAMPLE_PATH_STORAGE.md). `BTWAV1 RESULT 1 request result` is
correlated to the latest host request: 0 accepted/success, 1 invalid request,
3 transport/coordination unavailable, 4 preparation/playback setup failed,
9 shared path storage exhausted.
Firmware checks stopped/storage readiness independently of the host gate.
Preparation is serialized with existing live definition jobs and runs outside
the audio interrupt guard. The audio owner only renders prepared streaming PCM.
Auto preview, mouse controls and audition during playback remain deferred.

Manual audition checklist

Windows P and handheld X both trigger audition. Press the same control again
to retrigger. Leave the browser to cancel.

- Stop Pattern; focus a supported WAV. Navigation alone stays silent. Press P/X;
  verify native pitch/rate, complete EOF playback and unchanged Instrument draft.
- Hold P/X: no repeated starts. Release/press while audio plays: restart at frame
  zero. Select another WAV and press again: only the new audition plays.
- Use a long WAV well beyond the 4096-frame ring and 2048-frame start capacity
  (for example, one minute). Verify uninterrupted playback through EOF; retrigger
  while still playing and verify restart from the beginning through EOF again.
- Try directories and unsupported WAVs, and try P/X while Pattern is paused or
  playing: no audition. Enter/A still opens folders or chooses the WAV.
- Cancel/leave during playback and immediately after P/X during preparation;
  verify silence with no late restart. Start Pattern playback during a pending or
  active audition and verify only Pattern audio remains. Check repeated browsing,
  audition and cancellation do not exhaust stream resources.

## MIDI channel and descriptive fields

Channel is editable from 1 through 16 on a defined MIDI instrument with
compatible firmware. Field navigation includes Channel, and Left/Right clamps
at the endpoints. The displayed channel is the acknowledged device value.
Changes use the existing live visit, acknowledgement, Keep and rollback.
Keep retains the channel and schedules a normal session save. Cancel restores
the exact entry channel and binding. A new MIDI definition, including a Type
conversion, starts at channel 1 on Teensy. Successful live MIDI edits return
the applied channel with the acknowledgement so Channel is editable even
while the general instrument readback is pending. Legacy MIDI definitions
without a stored channel migrate to the former default of 12. See
[BTM version 3](FILE_FORMATS.md#btm-version-3-midi-channels). Future Tune and Project
formats must carry each MIDI instrument's channel.

Name is the instrument name. Sample currently derives it from the acknowledged
WAV filename. MIDI and Sine Synth Name values are undefined. Description is
short informational text. Future Sample Description may use WAV Title, and
MIDI/Synth descriptions may use device identity, bank or patch information.
Unavailable values are empty and do not affect modification markers.

BTX caches the filename derived from the existing resource readback, reusing
it until the acknowledged resource changes, becomes unavailable or refreshes
after reconnect. These fields add no metadata polling, SD reads or audio
callback text processing. There is no transfer for empty metadata. See
[D0041](ARCHITECTURE_DECISIONS.md#d0041--host-responsibilities-and-realtime-authority)
for playback priority and terminal assistance.

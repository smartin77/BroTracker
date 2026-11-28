# BTX controls quick reference

**+** means hold the modifier before pressing the other control. Handheld names
refer to the selected controller mapping: D-pad, A/B/X/Y, L1/R1, Select and Start.
Release and press again after changing screens or losing focus.

## Pattern

| Visible action | Windows | Handheld |
|---|---|---|
| Move between rows | Up / Down | D-pad Up / Down |
| Move between Note and Instrument fields | Left / Right | D-pad Left / Right |
| Previous / next channel | Shift+Tab / Tab | L1+D-pad Left / Right |
| Previous / next beat | Shift+Up / Down | R1+D-pad Up / Down |
| Increase / decrease Note or Instrument value | PgUp / PgDn | L1+D-pad Up / Down |
| Enter NOTE OFF | O on Note | X on Note |
| Toggle OFF timing | PgUp or PgDn on OFF timing | L1+D-pad Up or Down on OFF timing |
| Clear the selected cell or Instrument field | Delete | Y |
| Reset OFF timing to Arrival | Delete on OFF timing | Y on OFF timing |
| Enter channel headers / return to row one | Up from first row / Down | D-pad Up from first row / Down |
| Select a header and mute/unmute its channel | Click channel-number header | Move to header; A |
| Toggle selected header mute once per press | PgUp or PgDn on channel-number header | A on header |
| Start / pause / continue | Space | Start |
| Stop and reset | Enter / keypad Enter | B (also L1+B) |
| Decrease / increase BPM | Keypad ? / + | R1+D-pad Left / Right |
| Open Instrument for this cell's slot | Ctrl+Up | Select+D-pad Up |
| Open Pattern Load/Save | Ctrl+Left | Select+D-pad Left |
| Exit and save session | Ctrl+X or close window | L1+Start |

Up/Down and field/channel/beat navigation repeat when held. Note, Instrument and
BPM value controls also repeat. The Pattern value-direction preference affects
PgUp/PgDn and L1+Up/Down, not Instrument editor Left/Right, OFF timing or BPM.
No Undo/Redo or direct typed note/slot shortcut is currently mapped.

Instrument entry preserves Pattern playback with current live-edit firmware and
captures the visit baseline in RAM. It opens the cell's explicit slot, otherwise
the nearest preceding explicit slot in the same channel, otherwise 01. Select a
trailing undefined reference on Pattern before entry; its default Type is Sample.
Pattern Load/Save retains STOP and session-save entry coordination.

## Instrument

| Visible action | Windows | Handheld |
|---|---|---|
| Previous / next actionable field | Up / Down (also Shift+Tab / Tab) | D-pad Up / Down |
| Previous / next defined slot (focus Instrument slot) | Left / Right | D-pad Left / Right |
| Edit MIDI Channel 1?16, on Channel | Left / Right | D-pad Left / Right |
| Edit Sample Root Note or Fine Tune | Left / Right | D-pad Left / Right |
| Cycle Type backward / forward | Left / Right on Type | D-pad Left / Right on Type |
| Open Sample Browser | Enter on Load | A on Load |
| Start / pause / continue Pattern playback | Space | Start |
| Keep visit changes and return to Pattern | Ctrl+Down | Select+D-pad Down |
| Restore visit baseline and return to Pattern | Escape (also Backspace) | B |
| Exit without confirming Instrument draft | Ctrl+X or close window | L1+Start |

The full-width editor groups slot and Type above the Sample WAV section, which
shows the assigned path and opens Sample Browser via Load WAV. In the top group,
Name shows the acknowledged filename. Description stays blank until embedded WAV
title metadata is available. Save is dimmed. The footer shows the current
field's binding alongside keep and rollback. Preparation and errors appear in
the middle frame. Sample shows the acknowledged WAV from Teensy SD when
connected to firmware with waveform support.
Preview, Zoom 1x, Fit, Set Start, Set End and the orange endpoints are visual
placeholders. See [Sample waveform](INSTRUMENT_FOUNDATION.md#sample-waveform).

Entry focuses Type. The orange frame marks focus; field navigation cycles Type,
Sample Root Note and Fine Tune when available, Load WAV or MIDI Channel as
applicable, and Instrument slot. Root Note steps by semitone from C-0 to G-8.
Fine Tune steps by one cent, accelerates when held, and clamps at 00 and 99.
Left/Right on slot clamps to 01-N (at
most 99). Browsing leaves the captured Pattern cell unchanged. Keep assigns
a different browsed slot. Keeping the resolved entry slot preserves its raw
assignment, including an empty source cell. `*` marks outstanding visit changes.
` * [NN]` also identifies the resolved entry slot when browsing a different slot.
It cannot select N+1. Slot changes wait for known collection and live Pattern
synchronization, an unlocked editor, no pending file/publication/transport work,
no recovery barrier, no retained Type/WAV draft and settled live acknowledgements.

Type cycles Sample, MIDI and Sine Synth for existing and new slots, including
built-in slots. Existing-slot Type/WAV changes apply live in Teensy RAM. Sample
conversion retains the previous playable binding until Load supplies a valid WAV.
Load requires confirmed stopped transport or a ready live Instrument visit,
subject to the existing Sample/browser, pending-operation and recovery guards.
Replacing a binding releases its voices while
Pattern transport continues. Failures retain the previous binding and show a reason;
unconfirmed outcomes require reconciliation/restoration, not assumed success.

Ctrl+Down / Select+Down keeps live changes; Escape / unmodified B restores the
original source assignment and every touched existing definition/binding. Return
waits for definition, working-Pattern and visit-end acknowledgements as applicable;
disconnection/recovery can keep it pending. Both restore the captured cursor.
Browser cancellation only leaves the picker, preserving previous Instrument work.
Acknowledged Sample tuning edits affect subsequent triggers during playback.
Keep retains both values for normal session saving and Cancel restores both
entry values. The pitch of an already sounding voice stays at its trigger setting.

A trailing undefined slot is a local draft, not a live-created instrument. Confirmed
return requires STOP: without a WAV its default Sample stays undefined and its
reference is assigned; explicit MIDI/Synth or a selected WAV can qualify for DEFINE.
Only successful acknowledgement advances N and exposes the next trailing slot.
Failure retains the draft, source cell and count. Escape/B discards the draft.
An untouched inherited/default selection keeps its empty raw reference on return.

PgUp/PgDn, L1 value stepping, 1/2/3 and X do not change Instrument Type. Ctrl/Select
is used only for confirmed return within this editor (Ctrl+X remains global exit).
Modified Left/Right does not change slots. Enter/A on Type or slot does nothing.

## Sample Browser

| Visible action | Windows | Handheld |
|---|---|---|
| Move selection | Up / Down | D-pad Up / Down |
| Audition highlighted supported WAV (Stopped, Paused, Playing) | P | X |
| Open folder, including `..` | Enter | A |
| Choose supported WAV and return to Instrument | Enter | A |
| Cancel and retain previous Instrument draft | Escape / Backspace | B |
| Cancel via existing screen-navigation chord | Ctrl+Left / Right | Select+D-pad Left / Right |

Production accepts PCM8 and PCM16 mono/stereo at 7.040, 8, 8.363, 10, 11.025,
14.019, 16, 17.5, 20, 22.05, 32 and 44.1 kHz with current firmware.
Audition plays once at the file's original pitch and speed. Press P/X again to
restart the highlighted WAV, including while preparation is queued. Navigation
is silent, and leaving the browser cancels audition. Pattern transport continues
independently. Pattern SD reads take priority, so audition can wait for headroom
or stop if its own buffer runs dry. Older audition-capable firmware supports
this shortcut only while Stopped.

The confirmed SD directory is at the top. The list fills the middle; one bottom
line shows selected-file metadata, rejection, empty state or folder error. All
WAVs remain visible. Enter/A on an unsupported WAV or invalid resource path keeps
the browser and draft unchanged. Rows appear in directory scan order as bounded
batches arrive; you can navigate and choose confirmed rows while loading continues.
New batches append without moving the highlight or scroll position. The footer
shows `NN samples read` for WAV entries in this directory only (including rejected
WAVs), excluding folders and `..`, alongside selected-file metadata or useful errors.
The final count remains after loading. Changing folders or leaving ignores old replies.

In both file lists, Home highlights the first entry, and End highlights the
last entry currently shown by the list. Sample Browser can receive more entries
after End without moving the highlight. Mouse-wheel steps move the highlight
and keep it visible. A left click highlights a visible entry. A double-click
opens the folder or selects the file through the same validation and
confirmation as Enter/A. Keyboard and mouse controls also work with peripherals
connected to ArkOS.
Paths up to 255 ASCII bytes, including ordinary spaces in folder/file names, are
supported with long-path firmware. Long directory headers wrap to show the full
path. Over-limit paths and exhausted shared storage are rejected without replacing
the previous assignment; see [path storage](SAMPLE_PATH_STORAGE.md).

On the first Sample Browser opening of an Instrument visit, BTX starts in the
selected instrument's assigned WAV directory, when available. With no assignment,
it tries `/Samples`, then `/BroTracker`, then `/`. A missing preferred directory can
fall back through these roots. An empty directory counts as success, and other
SD errors remain visible. Explicit navigation does not run the startup chain.

The last successfully opened directory is remembered in BTX until the Instrument
visit ends. Reopening after choosing a WAV or cancelling the browser uses that
directory. A new visit resolves the initial directory again. No setting is saved.
The header displays the actual opened directory. `..` is absent at SD root.
Choosing a different path
for an existing Sample opens the replacement confirmation below. Choosing a WAV
starts live preparation when `StageLiveSample()` accepts it. If live staging is
unavailable, the candidate stays local, including with current firmware. Stopped
application is possible only if confirmed return reaches the fallback path. If
the visit already has a live assignment or tracked live definition edits,
Ctrl+Down / Select+Down can reject the unapplied draft even after STOP.

Press P (Windows) or X (ArkOS) in Sample Browser to audition the highlighted
supported WAV. Current firmware supports this while Stopped, Paused or Playing.
A new press restarts at frame zero. Leaving the browser cancels audition.
Navigation remains silent, and audition is independent of Pattern transport
and Instrument assignments.

The separate instrument-file Load action has no browser yet. Its intended,
independent startup chain is `/Instruments` -> `/BroTracker` -> `/`, with the same
missing-only fallback and empty-directory/error rules. Current Load WAV uses the
Sample Browser chain above.

## WAV replacement confirmation

| Visible action | Windows | Handheld |
|---|---|---|
| Accept the displayed new path | Enter | A |
| Keep the saved path | Escape / Backspace | B |

Review both paths. Accepting starts live preparation when `StageLiveSample()`
accepts the request; successful preparation applies the replacement and awaits
acknowledgement. If live staging is unavailable, the candidate stays local,
including with current firmware. It can apply through stopped confirmation only
when the controller reaches that fallback path; a live assignment or tracked live
definition edits can make Ctrl+Down / Select+Down reject the unapplied draft even
after STOP.
Ctrl+Down / Select+Down later keeps the whole visit; Escape / unmodified B in
Instrument restores the visit baseline. Declining this prompt only keeps the current
path and returns to Instrument.

## Pattern Load/Save

| Visible action | Windows | Handheld |
|---|---|---|
| Select Load, Save or a filename | Up / Down | D-pad Up / Down |
| Activate action / confirm file replacement | Enter | A |
| Cancel replacement confirmation | Escape / Backspace | B |
| Back from file list to action menu | Backspace / Ctrl+Left | B / Select+D-pad Left |
| Return to Pattern | Escape / Ctrl+Right | B from action menu / Select+D-pad Right |

Save writes the source of the last successful Pattern Load; it is unavailable
without a known source. Load stays stopped and returns to Pattern after success.
Load shows its sorted filenames once the scan completes. Home, End, wheel and
click navigation apply to that file list.

## Recovery, warnings and listening volume

| Visible action | Windows | Handheld |
|---|---|---|
| Review retained work after reconnect | Ctrl+Left from Pattern | Select+D-pad Left from Pattern |
| Select recovery / confirm replacement | Enter, then Enter again | A, then A again |
| Cancel recovery confirmation / leave recovery | Escape / Backspace | B |
| Stop playback while device state is uncertain | Enter | B |
| Exit during device/storage warning | Ctrl+X or close window | L1+Start |
| Raise / lower internal listening volume | No keyboard binding | Physical volume + / ? when captured by BTX |
| Cancel an opt-in diagnostic run | Escape / close window | B / Select |

Recovery never starts playback automatically. Physical volume keys remain OS
controls when BTX shows `OS keys`.

Exit stops active or paused playback, waits for confirmed STOP and storage readiness,
synchronizes the latest edits, and closes only after session SAVE acknowledgement.
If STOP or session SAVE fails, BTX logs the specific failure and closes automatically.
Pattern Save remains a manual action.

The preceding exit behavior describes the current legacy device-session path.
Required target: BTX saves its one internal current session (complete recoverable
music plus screen/cursor/waveform zoom and view context), independently of manual
BTT Save. LOAD/SAVE activation must continue to wait for confirmed STOP/IDLE,
settled edits and successful session persistence; failure or uncertainty retains
the caller and prior recoverable session. BTT is tune-only on Teensy SD, without
UI or undo history. New Project means New Tune and replaces the one session;
session undo persistence is deferred. Failed-exit keep-open/retry versus explicit
unsaved-exit UX is proposed, not implemented. See the
[ownership and transaction contract](FILE_FORMATS.md#btx-current-session-ownership-contract-2026-11-28).

Details: [Instrument editing](INSTRUMENT_FOUNDATION.md),
[session and file rules](FILE_FORMATS.md), [reconnect recovery](RECONNECT_CONTINUITY.md),
[tempo](LIVE_TEMPO_CONTROL.md), [mute](CHANNEL_MUTE.md),
[handheld mapping](../tools/arkos/README.md) and
[technical verification records](TEENSY_PATTERN_BRINGUP.md).

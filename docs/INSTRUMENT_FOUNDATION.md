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


## Read-only identity

The variable-rate Teensy advertises `BTINS1 CAP 1`. BTX requests
`BTINS1 GET 1 raw_id` for the currently inspected instance. The reply is
`BTINS1 STATE 1 raw_id type`, with 0 unbound, 1 Sample, 2 MIDI, 3 Sine Synth.
The bounded helper reads the actual frozen `g_selected_bindings` table, including
only resources actually bound by boot preparation. An absent binding is unbound;
this does not invent a missing instrument definition or availability information.
The discovery FIFO reserve grows by exactly three lines for identity CAP and
session CAP/view, retaining the existing one-line headroom. There is no
name/preset registry, type mutation or DSP change.

BTX clears facts on selection change/disconnect and accepts only valid matching-ID
replies. Unsupported firmware, disconnected devices or no response show unknown;
unbound is shown explicitly. The host contains no fixed ID-to-type mapping. The
current Synth binding represents the existing sine engine. Future Synth engines
will need an explicit identity extension rather than assuming all Synth is sine.

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

# BTX controls

Single current reference for desktop and handheld controls in standalone
BroTracker Terminal (BTX). Protocol/design pages and dated implementation records
provide context; use this page for current bindings and validation scope.
**+ means hold the modifier while pressing the other control.** Row means an
editable pattern position; field means Note or Instrument (OFF timing when the
note is OFF); header means the channel-number box. Playback does not move the
edit cursor. Unmodified keys are intended unless a modifier is shown.

## Pattern Load/Save navigation foundation (2026-11-05)

Shared ScreenLeft enters a dedicated Pattern Load/Save screen to the left of PAT;
ScreenRight returns to the same working pattern and edit cursor within the same
BTX run. Returning does not resume playback; the next Play starts at pattern row
zero, independently of the edit cursor. Cursor position remains host view state;
it is not persisted by the existing working-session format.

Leaving Pattern for another screen stops active playback and saves the working
session before switching. The future MIX screen is the exception and will allow
playback to continue; MIX is not implemented here.

Entry automatically requests STOP for Playing/Paused and naturally completed
DONE, waits for acknowledged STOPPED/IDLE, then uses the existing working-session
SAVE path. Already IDLE skips STOP. Only a successful save permits entry. Repeated
entry requests coalesce; failures remain on PAT with right-panel status. File
browsing, Load and Save to the original file are available below; Save As remains deferred.

The screen first offers clearly labeled **Load** and **Save** actions. Up/Down
selects an action and Enter (Windows) / A (handheld) activates it. Load opens
the existing sorted browser. Save writes the original file associated with the
last successfully applied and synchronized browser Load. Save is unavailable
when the source is unknown, or when the firmware lacks PATTERNSAVECAP 1. The
source association is host state for this run, cleared on disconnect or successful
replacement by another loading path; it is not session metadata across restart.
Cancelled or rejected browser Load leaves that association intact.

The plain list reads compatible `.btdev` files from the Teensy SD directory
`/BroTracker/development/`, including patern1.btdev through patern4.btdev and
other valid files. The host reads every internal LIST page through its `done`
marker, counts the matching files and sorts all filenames ascending before
selection is enabled. No total-count field or new firmware command is needed.
Up/Down selects one file in the continuous list; Left/Right has no list action.
Enter (Windows) or
A (handheld) opens confirmation, then a second press confirms replacement.
Escape/B cancels confirmation back to the selected file. Outside confirmation,
Escape returns directly to Pattern; B returns from list to menu, then Pattern,
with the original work and cursor. Ctrl+Right/Select+Right also returns.
An in-flight Load cannot be undone; wait for its result/synchronization.

`[pattern] confirm_load_replace=true` is the default, including when absent.
Setting it to `false` skips only the replacement confirmation. STOP, validation,
application acknowledgement and host synchronization remain required. Restart
BTX after editing config/config.ini beside the executable; the example is not
loaded automatically. Load succeeds only after the new baseline and working
pattern synchronize, then returns to Pattern at channel 1, first row, Note field.
Validation/application rejection retains the original work and cursor; uncertain
transport/readback outcomes retain the existing recovery/input warning barriers.
No Load starts or resumes playback. This list requires LISTCAP 1 firmware;
legacy firmware keeps the screen's return path and shows listing unavailable.

The agreed BroTracker bindings are Ctrl+Left/Right on Windows and
Select+D-pad Left/Right on handheld (Select is SDL BACK). Hold Select before
pressing the direction. Each direction triggers once per physical press and is
consumed without moving the edit cursor. L1/R1 directional bindings take
precedence when a shoulder is held. Select+Start is consumed without a pattern
action; the legacy file/Publish/Restore shortcuts are retired. STOP and Exit remain.

ElkTracker Manual, pages 5 and 7 (Controls / Navigation), supplies the directional
screen-navigation reference: its Select+Left/Right has context-dependent back/dive
behavior and otherwise switches main screens. Its desktop screen keys are 1-8.
BroTracker bindings above were explicitly supplied by the owner, not inferred
from that manual.

The owner physically confirmed Pattern Load/Save on Windows BTX and the tested
dArkOS handheld. On that dArkOS setup, the first transition from Pattern during
playback after BTX startup took about one second or slightly longer. Later
transitions were faster, and the owner accepted this behavior. This confirmation
is scoped to those tested setups; it does not establish validation for other
handhelds, mappings or configurations.

## Instrument entry

Pattern **Ctrl+Up** / **Select+D-pad Up** opens one specific Instrument directly.
There is no Inventory screen or menu. Entry captures the edit target, automatically
STOPs Playing/Paused/DONE, waits for acknowledged IDLE and synchronized edits,
and requires successful working-session persistence. Already IDLE skips STOP.
Failures retain Pattern and recoverable work. Future MIX retains its playback exception.

An explicit source-cell ID selects that instrument. Otherwise the nearest explicit
ID in preceding rows of the same channel is selected, including instrument-only
events; with none, select 01. Inheritance lookup never wraps or uses another
channel/playback. The nearest reference is never skipped. Note-only and empty
ordinary Instrument fields are supported; headers and NOTE_OFF timing fields
remain unavailable. Collection-capable firmware allows an outside reference to
be reviewed as Undefined; legacy entry still rejects IDs outside01-99. Entry
itself never assigns a cell.

The header shows Instrument NN* when viewing the original, or a turquoise [*NN]
marker when viewing another selection. Markers exist only for the source
cell's original explicit ID. Normal return confirms the selected instrument onto
that captured cell, retaining its note/timing and original field. Empty cells
become instrument-only events. Unchanged assignments create no extra history or
preview. Existing undo/history and preview apply. Return while blocked retains
Instrument and selection with a reason until safely accepted; it never triggers
automatic recovery. Enter remains STOP; closing BTX is not assignment confirmation.

Instrument selector cancellation: Windows **Escape** / handheld **unmodified B**
returns to Pattern without assignment and restores the captured source cursor.
Cancellation remains available while disconnected or held for reconnect recovery;
retained musical work is preserved. Then enter recovery explicitly from Pattern
(Ctrl+Left / Select+Left), review and confirm or cancel using the existing recovery
menu. Cancellation never recovers work automatically or starts playback.
Ctrl+Down / Select+Down confirms assignment and remains blocked when unsafe.
Modified B keeps its existing STOP combinations; Enter remains STOP.
Closing BTX does not confirm assignment.


Bindings inside Instrument (qualification rules: [Instrument foundation](INSTRUMENT_FOUNDATION.md#draft-qualification-and-confirmed-return)):

| Action | Windows | Handheld |
|---|---|---|
| Previous / next (collection bounds; legacy 01-99) | Ctrl+Left / Right | Select+Left / Right |
| Confirm eligible definition, assign selection and return in its original field | Ctrl+Down | Select+Down |
| Explicit draft Type: Sample / MIDI / Sine Synth (trailing undefined slot only) | Unmodified 1 / 2 / 3 | Unmodified X cycles |
| Cancel selection and return without assignment, including recovery hold | Escape | Unmodified B |

Select precedes D-pad; L1 value-edit and R1 beat/tempo precedence remain.
Chords consume once per physical arrow/direction press, suppress held repeat and
retain focus-loss/controller-removal handling. Browsing performs no session save,
assignment or automatic playback resume. Existing Type is device-authoritative; trailing undefined slots permit explicit draft Type selection, with separate
Availability and Resource lines. Missing/failed Sample preparation retains its
Type/reference. MIDI definition does not prove an external device is present.
Older firmware retains binding-only Type with honestly unknown availability.
Load, Save and Fine Tune remain unavailable placeholders. Transpose is deferred.

## Desktop controls

| Function | Key / button combination | Context / behavior |
| --- | --- | --- |
| Pattern Load/Save screen (left) | Ctrl + Left | From Pattern: automatically STOP Playing/Paused/DONE and wait for confirmation (skip STOP if IDLE), save the working session, then switch only on success. One request per press; consumes Left. |
| Return to Pattern (right) | Ctrl + Right | From Pattern Load/Save: return with the same pattern and editor cursor. One request per press; consumes Right. |
| Select pattern file | Up / Down in Load/Save | One step per press; does not move the pattern cursor. |
| Load / confirm replacement | Enter or keypad Enter in Load/Save | Confirmation defaults on. After success and synchronization, return with reset cursor; playback stays stopped. |
| Cancel | Escape in Load/Save | Confirmation: back to list. Action menu or list: return to Pattern, retaining current work/cursor. |
| Start / Pause / Continue | Space | Stopped: start at pattern row zero. Playing: pause. Paused: replay the reported paused row from its beginning, then continue. |
| STOP/reset | Enter or numeric keypad Enter | Stop/reset to pattern start; remain in BTX. A fresh press can request STOP again. |
| Exit | Ctrl + X or close the window | Synchronize available edits and request session persistence, then exit without STOP. Playing writes are explicitly deferred; failures/timeouts are logged. |
| Previous / next row | Up / Down | Bounded by active rows. Up from first row enters headers; Down from a header returns to first row. Hold repeats. |
| Previous / next field | Left / Right | Note <-> Instrument, crossing into adjacent channels at field edges; stops at outer edges. In headers, moves to adjacent header without wrapping. Hold repeats. |
| Previous / next channel | Shift + Tab / Tab | Wraps active channels; selects Note in row context, preserves header context. Hold repeats. |
| Previous / next beat | Shift + Up / Down | Strictly preceding/following highlighted beat (zero-based 0,4,8,...), within active visible rows 0..31; no wrap. Previous from first row enters header; next from header returns to first row. Hold repeats. |
| Increase / decrease value | Page Up / Page Down | Selected Note or Instrument; hold repeats with acceleration. Direction preference can reverse these requests. No value editing in headers. |
| Enter NOTE OFF | O | Note field only; sets Arrival timing. |
| Select OFF timing | Page Up or Page Down in the second field of an OFF cell | Either toggles Arrival (row start) / EndOfPosition (following row boundary). Direction preference does not affect this toggle. |
| Clear | Delete | Note field: clears the whole cell (note, instrument and OFF timing). Numeric Instrument field: clears only instrument. OFF timing field: resets Arrival. No effect in headers. |
| Select header and mute / unmute channel | Left-click inside a channel-number header | Selects that header and toggles its session mute. Border/outside clicks do not activate it. No desktop keyboard mute binding is implemented. |
| Decrease / increase tempo | Numeric keypad - / + | Independent of Num Lock; main-keyboard +/- are unbound. Immediate 0.05 BPM step; hold accelerates (below). Independent of value-step direction; works while playing when tempo control is available. |

## Handheld controls - R36H horizontal layout

Use the D-pad, L1/R1 shoulders, Start and A/B/X/Y names below according to the
active controller mapping. These are BTX's named controls, **not verified printed
labels/physical positions on every R36H**. The launcher obtains PortMaster's SDL
controller mapping; BTX uses the selected mapped controller, including its mapped
button/hat fallback. Unmapped joysticks and raw-axis fallback are not supported.

| Function | Key / button combination | Context / behavior |
| --- | --- | --- |
| Pattern Load/Save screen (left) | Select + D-pad Left | From Pattern: automatically STOP Playing/Paused/DONE and wait for confirmation (skip STOP if IDLE), save the working session, then switch only on success. Hold Select first; one request per press; consumes Left. |
| Return to Pattern (right) | Select + D-pad Right | Return with the same pattern and editor cursor. Hold Select first; one request per press; consumes Right. L1/R1 directional bindings retain precedence. |
| Select pattern file | D-pad Up / Down in Load/Save | Without modifiers; one step per press, no pattern editing. |
| Load / confirm replacement | A in Load/Save | Without modifiers. Confirmation defaults on; successful synchronized Load returns with reset cursor, stopped. |
| Cancel | B in Load/Save | Without modifiers: confirmation to list; file list to menu; menu to Pattern. L1+B remains STOP. |
| Start / Pause / Continue | Start | Stopped: start at row zero. Playing: pause. Paused: replay the reported paused row from its beginning, then continue. |
| STOP/reset | B | Stop/reset and stay in BTX, including with L1 held. A fresh press can request STOP again. |
| Exit | L1 + Start | Session persistence exit workflow; playback remains running. Also exits with Teensy disconnected. Start action occurs on release. |
| Previous / next row; enter / leave headers | D-pad Up / Down | Active row bounds; Up from first row enters headers, Down from header returns to first row. Hold repeats. |
| Previous / next field or header | D-pad Left / Right | Row: move Note <-> Instrument across channel edges, bounded at outer edges. Header: adjacent header without wrapping. Hold repeats. |
| Previous / next channel | L1 + D-pad Left / Right | Wraps active channels; selects Note in row context, preserves header context. Hold repeats. |
| Previous / next beat | R1 + D-pad Up / Down | Strictly preceding/following beat at zero-based 0,4,8,... within visible active rows 0..31; no wrap. Previous from first row enters header; next from header returns to first row. Hold repeats; R1 takes precedence over L1 value editing. |
| Increase / decrease value | L1 + D-pad Up / Down | Selected Note or Instrument; hold repeats with acceleration. Direction preference can reverse requests. No value editing in headers. |
| Enter NOTE OFF | X | Without L1, Note field only; sets Arrival timing. |
| Select OFF timing | L1 + D-pad Up or Down in the second field of an OFF cell | Either toggles Arrival / EndOfPosition; not reversed by value-step preference. |
| Clear | Y | Without L1: Note field clears the whole cell and OFF timing; numeric Instrument clears only instrument; OFF timing resets Arrival. No effect in headers. |
| Mute / unmute selected channel | A in a header | Consumes A, including with L1 held. Session mute only; no pattern/tempo change. |
| Decrease / increase tempo | R1 + D-pad Left / Right | Immediate 0.05 BPM step, accelerated hold. Independent of value-step direction; available during playback with supported synchronized tempo. |
| Internal master volume | Physical volume + / - | Verified evdev profile only: 5-point steps over0..100%, default100%. Hold repeats after400 ms, every100 ms; kernel repeats ignored. Teensy smooths changes; external MIDI unchanged. Requires supported synchronized firmware and exclusive volume-only key acquisition. `OS keys` means BTX did not acquire them; they retain system behavior. |
| Cancel opt-in diagnostic run | B or Select; Escape on an attached keyboard; window close | Diagnostic mode only: normal editing/controller actions are disabled. B/Select arm only after all controls are released for 250 ms. Select+Start has no pattern action. This is cancellation, not a normal playback shortcut. |

**Vertical R36S differences:** repository evidence does not verify an R36S-specific
printed A/B/X/Y layout, shoulder arrangement or alternate binding profile. No
model-specific swap is implemented. The same actions use that device's selected
SDL/PortMaster mapping; orientation alone cannot establish which physical button
produces A, B, X or Y. Verify the device mapping before relying on printed labels.
Do not assume EmulationStation/RetroArch hotkeys apply to standalone BTX. Existing
handheld confirmations do not establish a separate R36S physical mapping test.

Hold L1 before a normal L1 chord. Header A still mutes/unmutes with L1 held.
B requests STOP on Pattern, including with L1 held. Select maps to SDL `BACK`, Start to SDL
`START`, including verified raw fallback from that controller's SDL bindings.
Start transport/exit fires on release so either Select+Start press order consumes
it safely. A held chord does not repeat and must fully release before rearming.
Physical PortMaster mappings still need confirmation on each device; emulator
hotkeys do not establish BTX's mapping.

BTX selects an SDL GameController by instance ID. If controller-event delivery
cannot be enabled, its verified raw fallback uses only that opened controller's
SDL button/hat bindings; controller duplicates and raw axes are ignored. Hat
transitions release old directions before pressing new ones. Removal releases
the selected handle; a newly added mapped controller can be selected without
transport actions. Layout/orientation and emulator settings do not establish the
BTX mapping. Configurable profiles, calibration and final bindings remain future
work; broader device and OS-hotkey checks remain open.

The volume keys are discovered by identity/capabilities, never by an event number.
BTX checks that no other keys share the device before acquiring it. Focus loss,
disconnect and opposing volume keys cancel holds; release before rearming. Normal
ArkOS mixer handling resumes outside BTX. Physical evdev delivery is user-confirmed;
the user confirmed button response, displayed updates and PT8211 listening volume
on 2026-10-25. Hold-repeat, exact silence, OS-control restoration, mixer-exclusion
and USB-bridge listening checks remain unconfirmed. Normal display is just `Vol n%`
without a pending-change asterisk; this is listening volume, not the future mixer.
Windows shows authoritative volume but has no new volume keyboard shortcut.

## Editing and hold behavior

- Value direction defaults to normal. `[editing] value_step_direction=reversed`
  swaps Page Up/Down and L1+Up/Down for notes/numeric instruments, not navigation,
  tempo, explicit OFF or OFF timing. Changes take effect on application restart.
  The deprecated `note_step_direction` alias remains accepted; specifying both
  names rejects the configuration.
- Notes cover C-0..G-8 (raw 24..127), cycling through OFF at the endpoints. Empty
  Note entry reuses the nearest preceding pitched note in that channel, otherwise
  C-3, in either direction; it does not insert an instrument. Increase cycles
  G-8 -> OFF -> C-0; decrease cycles C-0 -> OFF -> G-8. Entering/leaving OFF sets
  Arrival. Instrument numeric choices use the confirmed collection bounds described in
  [Instrument foundation](INSTRUMENT_FOUNDATION.md#numbered-collection-and-trailing-undefined-slot); legacy firmware uses 01..99 (raw 0..98) through empty `--`:
  99 -> -- -> 01 or 01 -> -- -> 99. Fresh empty entry starts at 01 in either
  direction. On legacy firmware, higher received IDs remain intact but cannot be stepped. Empty
  notes retain an ordinary instrument field; instrument-only events are valid.
  In an OFF cell, `?` means Arrival (row start), `_` means EndOfPosition (next
  row boundary); either timing-step direction toggles them.
- Navigation repeats after 350 ms, then every 100 ms, accelerating to 60 ms after
  one second and 40 ms after two seconds. Value repeats start after 400 ms, then
  100/50/25 ms at those same one/two-second thresholds. Value endpoints/OFF/empty
  renew the initial pause. Tempo repeats start after 400 ms, every 100 ms: 0.05 BPM,
  then 0.25 after 1.5 seconds and 1.00 after three seconds. No catch-up bursts.
- Releases, opposing directions, modifier changes, focus loss, disconnects and
  relevant locks cancel held actions; release and press again to rearm. Value
  and navigation holds share one active editor stream. No Undo/Redo shortcut or
  direct typed-note/number entry is currently mapped. OS/kernel repeat does not
  create extra actions. Each repeat service emits at most one action and drops
  catch-up work after stalls. Controller removal, exit and Clear/Restore/OFF also
  cancel value holds; navigation does not edit data or create history.

Beat navigation changes only the edit cursor, preserving channel/field and
musical data. Examples: row 3 next -> 4; 4 next -> 8; 5 previous -> 4; 4 previous
-> 0. At the last reachable beat, next stays put. Previous from the first row
enters its header instead; next from a header returns to the first row. No paging
or playback-range controls are implemented. R1 can precede a beat direction or
take over an already held direction (after any earlier plain movement); takeover
starts a fresh beat hold. R1 vertical chords take precedence over L1 editing.
Releasing R1 blocks held directions until physical release, preventing navigation
leakage. R1 tempo chords support both modifier orders and work in headers too.

Internal recovery establishes the device's **in-memory published pattern and tempo baseline**
after authoritative acknowledgment/readback. Automatic working preview does not
change that baseline. No Publish/Restore shortcut is mapped; tempo remains
separate from cell-edit history. Supported
edits automatically preview, including at eligible row boundaries while Playing.
Preview changes working playback, not the published baseline or dirty comparison;
rejected edits remain visible with status. Legacy peers without preview support cannot synchronize working edits through
the production UI; use current firmware. Neither working nor published device RAM survives
power loss merely because a readback succeeded. See [storage operations](FILE_FORMATS.md).

Editing requires a validated device pattern. During initial device synchronization or explicit file-load recovery
(including uncertain application/readback), note/instrument/OFF edits and preview remain
blocked until authoritative baseline and working recovery synchronize. Publication
locks also block mutation/Restore, while navigation may remain available. START,
Publish, tempo and mute requests have their own connection/capability/pending-state
checks; watch the status panel. STOP and exit remain usable. Legacy firmware without
pattern transport uses Start/Restart instead of Pause/Continue.

Mute releases the channel's current voice. Unmute does not trigger a note; later
pattern events can sound normally. Mute is session state, not a pattern edit.
Crossed turquoise channel numbers indicate confirmed mute; orange marks the
selected header. Pending/rejected/unsupported states are separate from that
confirmed display. BPM displays applied tempo rounded to one decimal, with
pending desired hundredths in status; this is separate from cursor navigation.

Implementation references: [desktop input](../ui/windows/input.h),
[handheld input](../ui/arkos/input.h), [editor](../src/ui/local_pattern_editor.h),
[transport dispatch](../src/ui/bringup_controls.cpp),
[host synchronization](../src/ui/bringup_serial.cpp) and
[launcher](../tools/arkos/port/BroTracker%20Terminal.sh).
Bindings and edge cases are covered in the existing
[desktop](../tests/test_windows_input.cpp), [handheld](../tests/test_arkos_input.cpp),
[editor](../tests/test_local_pattern_editor.cpp) and
[startup recovery](../tests/test_startup_storage.cpp) tests. This manual was checked
against source/test definitions; that inspection is not new physical validation.

## Reconnect recovery hold

See [reconnect continuity](RECONNECT_CONTINUITY.md). After power loss, BTX can
recover a compatible retained draft automatically through working preview while
Teensy is stopped. Complete readback and tempo confirmation unlock editing and
transport; the edit remains unpublished. Cursor row/channel/field/header survive
when valid, with safe clamping on context installation. No manual Publish or
automatic START is required. This also applies to a normal session launch
without a manual pattern command. Enter/STOP remains usable during recovery, but a STOP
acknowledgment alone does not complete the required working-state verification. A RAM-only playing edit was not necessarily on SD
before the interruption, even when BTX can recover it from retained host RAM.

A running device, newer working revision, changed baseline/dimensions, failed or
incomplete readback or rejected recovery keeps available work protected with an
explicit status. STOP remains available. Open Pattern Load/Save with Ctrl+Left
or Select+D-pad Left to review **Recover retained host work**. Enter/A first
shows confirmation; a second press explicitly replaces device RAM with retained
host work through the internal recovery coordinator. Cancel dismisses the prompt
or leaves recovery without discarding either side. It does not load/overwrite a
pattern file or start playback. Uncertain application offers readback first,
never an automatic replay. Disconnected/SD warning barriers retain priority.
No fixture LOAD or START is replayed on reconnect.

## Recorded confirmations and validation limits

These are dated observations, not approval of every later change or a measured
latency/performance result. This consolidation involved no physical testing.

| Recorded evidence | What it establishes / remaining limits |
| --- | --- |
| 2026-10-15: user confirmed ArkOS controls/workflows and added held navigation; shared held navigation confirmed on Windows and ArkOS | Supersedes the earlier held-navigation-pending note. Does not establish dArkOS validation, physical timing/realtime performance, every mapping/profile or later shortcuts. See the [navigation/controls records](TEENSY_PATTERN_BRINGUP.md) and [completed tasks](TODO_LIST.md). |
| Windows live-tempo manual checklist confirmed, including requested BPM pending-color indication | Preserve that recorded confirmation; no additional cases are inferred. See [tempo evidence/checklist](LIVE_TEMPO_CONTROL.md). |
| 2026-10-22: user confirmed smooth handheld R1+Left/Right tempo changes and R1+Up/Down beat navigation during playback; supplied handheld control test passed | Other unreported boundary/modifier/hold cases remain unconfirmed. Does not establish audio quality, MIDI alignment or a separate dArkOS/R36S physical mapping test. Windows Shift+Up/Down manual beat-shortcut checks remain pending; ordinary shared navigation confirmation does not cover them. |
| 2026-11-08 reconciliation: Windows mixed streaming/Synth/editing/mute/OFF/transport behaved correctly and sounded clean | Scoped owner listening/workflow acceptance; device and host counter evidence is in [production streaming](SD_SAMPLE_STREAMING.md). Does not validate handheld external MIDI controls or all failure/recovery cases. |
| Windows USB bridge with H4MIDI/WASP: owner accepts 82 ms compensation; sine/MIDI and sample/MIDI recordings exist | Listening acceptance, not quantified onset/jitter or universal latency; current handheld 25 ms integration remains pending. See [MIDI setup](MIDI_FOUNDATION.md#configuration-and-acknowledged-setup). |
| Windows mouse mute, header navigation and enlarged channel numbers user-confirmed | Physical handheld header/mute UX and listening remain unconfirmed. No desktop keyboard mute binding exists. See [mute evidence/checklist](CHANNEL_MUTE.md). |
| 2026-10-25: physical volume-key response, display updates and PT8211 listening level user-confirmed | Hold-repeat, exact silence, OS-control restoration, mixer-exclusion and USB-bridge listening remain unconfirmed, as detailed above. |
| 2026-10-28: Windows reconnect retained edited note 61/C#3; user explicitly pressed legacy F5 Publish; complete 256-cell readback verified the edited RAM publication | This was Publish, not legacy F4 Restore or an SD session restore. Then-current reconnect-hold F4 behavior and interrupted/rejected/uncertain recovery are host-test covered, not physically verified. Powered-only USB interruption and dArkOS fix validation remain open. See [captured evidence and receive-buffer limitation](RECONNECT_CONTINUITY.md). |

Synthetic tests cover selected-controller filtering, verified raw button/hat
normalization without duplicates, normal/reversed value holds and OFF timing,
repeat thresholds/stalls/timestamp wrap, chord precedence/cancellation, publication
capability gates, header A consuming Publish, STOP/exit and reconnect retention.
Test/ARM64 build success does not prove physical handheld UX, electrical/audio
behavior or realtime performance. Printed face-button/shoulder placement and
OS-hotkey interception still require verification for each device/profile.

Implementation/test sources for repeat and tempo details:
[navigation timer](../src/ui/navigation_repeat.h),
[value repeat](../src/ui/editor_step_input.h),
[tempo repeat](../src/ui/tempo_repeat.h),
[preview/reconnect tests](../tests/test_host_pattern_preview.cpp),
[publication tests](../tests/test_host_pattern_publication.cpp) and
[tempo tests](../tests/test_live_tempo.cpp).

## Session and file behavior

A valid Teensy working session takes precedence at startup, restoring published
baseline, unpublished working edits and tempo, stopped. If absent or invalid,
BTX uses existing `[development] startup_pattern`. A filename loads that file;
`none`/empty uses `[btx] new_pattern_length` (16, 32 or 64 rows), eight channels
and 125.00 BPM. Invalid config filenames warn and use the configured blank length.
Config syntax and installed user settings are unchanged. Unknown I/O or interrupted
storage outcomes are not treated as an absent session. Fallback runs once per
BTX start, never as a reconnect replay. Playback never starts automatically.

Working edits and tempo automatically mark the Teensy session pending. It saves
as soon as synchronization, transport, MIDI, SD ownership and storage conditions
permit, without Publish, Restore, Exit or screen navigation. Playing/blocked
storage retains pending intent; Pause/STOP permits it when safe. Failed writes
retain pending intent and an explicit error without a tight retry loop; a new
mutation or session-save request re-arms saving. Pattern-file Save is separate.
An invalid canonical session is preserved as `current.btsession.invalid` before
fallback may persist. Existing quarantine/candidate/previous files or failed
preservation keep automatic writes blocked for owner recovery; no retained file
is silently overwritten.

Pattern Load/Save is the only manual pattern-file workflow. The temporary
`tune.btdraft` commands are retired and existing draft files are untouched.
F3/F4/F5, Shift+F4/F5, Select+Start, row A, L1+Y, L1+X and row L1+A have no legacy
pattern action. Header A still mutes, browser A confirms, and B retains STOP or
contextual Cancel. Source association and edit cursor are host state; cursor
position is not restored across restart. Successful browser Load resets the
cursor; Pattern Save and ordinary in-process return retain it.

If Teensy rejects the pre-load READY check (for example, MIDI cleanup has not
completed), no file Load is sent. BTX retains the current work/cursor and releases
the Load barrier for an existing editor; STOP and normal controls remain
available. The status shows why Load was not attempted. Retry Load after
the device condition clears. Idle, cancelled MIDI quarantine no longer blocks
file operations; MIDI fault/cleanup uncertainty remains visible. Active MIDI
cleanup or DMA still blocks them. Uncertain outcomes after a submitted Load still
require verified recovery. This does not clear or bypass MIDI faults.

MIDI status is separate from transport: ArkOS shows the H4 route subscription and
USB-path errors independently of Teensy firmware health. A subscription or healthy
USB endpoint does not verify external delivery. For a MIDI fault, follow the shown
action: explicitly STOP, wait for STOPPED and cleanup=0, then explicitly START to
request recovery. BTX never sends those transport commands automatically. Missing
or stale keeper/firmware status is shown as unavailable/unknown; internal audio and
editing remain independent. No button bindings changed.

Pattern Load/Save does not save or load channel mutes. It retains the live Teensy mute mask
while the device remains powered; device initialization clears that mask.

Pattern Load/Save physical confirmation is scoped to Windows BTX and the tested
dArkOS handheld described above. Existing dated observations retain their original
scope, including the previous startup/F4/F5 behavior where recorded.

## Blocking device/storage warnings

Windows and handheld BTX display one centered warning over the production Pattern
screen when Teensy is disconnected or its boot-time SD initialization explicitly
failed. All normal user actions, including STOP, edits, navigation, playback,
mute, tempo, volume, publication and file actions, are blocked in these states.
Exit remains available (Ctrl+X/window close on Windows; existing L1+Start on
handhelds). Exit with confirmed unavailable SD skips session persistence.
Communication and reconnection servicing continue; held actions are cancelled.
At initial BTX startup, the disconnected overlay is suppressed for 5000 ms to
allow discovery/boot/enumeration. The named code constant
`kInitialTeensyDiscoveryGraceMs` is not a config setting. This nonblocking grace
never relaxes input readiness. Any observed connection ends initial grace, so a
subsequent genuine disconnect shows its warning immediately.

Opening a serial port is not synchronization. Connected synchronization/recovery
shows synchronization/recovery information only in the existing right-side status.
Once connected there is no disconnected or synchronization overlay; normal actions
remain blocked until authoritative state is usable. Connected STOP and Exit remain available.
Existing explicit retained-work Restore/Publish recovery paths remain available
under their existing safety checks. This never displays an invented SD failure. Existing retained-work recovery protections remain in
place; no automatic START or new overwrite policy is introduced.

The optional `[btx] language=en_US` selects `assets/translations/en_US.ini`; another
`language_REGION` locale (for example `sk_SK`) selects `<locale>.ini` there.
Absent language defaults to `en_US`; `en`/`en_us` are accepted English aliases.
Files are UTF-8 INI with a `[warnings]` section and stable `teensy_heading`,
`teensy_body`, `sd_heading`, `sd_body` keys. A literal `\n` in a value means an
explicit line break at native spacing; soft wrapping uses 1.5 native line spacing.
Absent/unreadable files, missing/empty keys and invalid language selections fall
back to built-in English. Only these warnings are translated. `WarningText::Select`
reloads/selects independently of rendering for future runtime switching; no
selection screen or hotkey exists.

`kWarningFont` in `src/ui/warning_renderer.h` selects native Terminus 8x14
Regular/genuine Bold (default), or native 6x12 Regular for both text roles.
The bundled BDFs and license live in `assets/fonts/terminus`; the Pattern BTF
font and grid geometry are unchanged. Layout constants in that header are code
settings, including 18px top/bottom padding reduced from the accepted mockup.

### Runtime warning artwork

`assets/icons/font-preview-warning.svg` is the runtime vector source, loaded once
from the executable's assets directory independently of the launch directory.
Host-only `SvgImage::Load`/`Draw` wraps vendored NanoSVG (zlib license in
`third_party/nanosvg/LICENSE.txt`; packaged as `licenses/NanoSVG.txt`). Parsed
geometry stays resident; CPU rasterization antialiases paths, fills and rounded
strokes into an RGBA workspace at the requested size. Straight-alpha source-over
blending affects just the icon's pixels. No generated bitmap/header replaces SVG,
and no full-frame filtering is applied. Missing/unusable icon files leave the
warning text, panel and blocking behavior available. Native Terminus text and
both size choices remain unchanged.

Pattern Load uses a bordered vertical list with its Terminus 8x14 title above
the frame and Terminus 6x12 filenames. The selected row is highlighted across
its full width. Windows Up/Down and handheld unmodified D-pad Up/Down move one
item. Down from the bottom visible row jumps to the next block with its first
row selected at the top; Up from the first visible row selects the previous
block's last row. Up at the first file and Down at the last file retain selection.
The white vertical indicator shows the visible matching-file proportion and
viewport position in the complete sorted list, including a partial final block.
There are no manual page controls. Windows Enter / handheld A opens confirmation
and confirms on the second press; Escape / B dismisses confirmation to the list. Outside confirmation, B returns
to the action menu from the file list; Escape returns directly to Pattern. Ctrl+Right / Select+Right
returns directly. While a Load is in flight, wait for its result; successful
synchronized Load automatically returns to Pattern with the reset edit cursor.
The existing `BTSTORE1 LISTCAP 1` firmware remains sufficient. MOD import, sample
loading and Save As remain separate work.

## Pattern Save to original file

Windows Up/Down and Enter, or handheld D-pad Up/Down and A, select **Save** on
Pattern Load/Save. It waits for working-edit and tempo synchronization, then
writes the original `.btdev` file, including unpublished edits, tempo and
instrument numbers. No Publish is required. It does not start or resume playback
or move the edit cursor. Confirmed success automatically returns to Pattern with
the cursor intact. Failure retains the working pattern and reports the storage
error in the right-side status area. An uncertain timeout is not retried
automatically; inspect the device files before retrying.

Escape cancels a Load replacement prompt back to the file list; outside a prompt
it returns to Pattern. B goes back one level (file list to action menu).
Ctrl+Right / Select+Right also returns. In-flight Load/Save waits for its result.
Session persistence remains separate from Pattern Save. Save requires the new
`teensy41_dev` firmware advertising `BTSTORE1 PATTERNSAVECAP 1`; LISTCAP alone
supports Load but not Pattern Save. Save As, filename entry, handheld character
map, MOD import and sample loading remain separate tasks.

| Action selection | Windows | Handheld |
|---|---|---|
| Select Load or Save | Up / Down, then Enter | D-pad Up / Down, then A |
| Save original | Select Save; no Publish required | Select Save; no Publish required |

## Browser Back and Save feedback

Windows **Backspace** or **Ctrl+Left**, and handheld **B**, go back one level:
from the file list to the Load/Save action menu; from Load confirmation to the
same selected file. Reopening Load keeps that file selected. Hints say
`Backspace / Ctrl+Left: Back` or `B: Back`; during confirmation the action is
labeled **Cancel**. From the action menu, Back returns to Pattern. Escape keeps
its existing Windows behavior: dismiss confirmation to the selected file, or
return directly from the file list/action menu to Pattern. Ctrl+Right and
Select+D-pad Right still return directly to Pattern. B on Pattern remains STOP.

Only a matching acknowledged successful Save returns to Pattern and shows
`Pattern saved: <original filename>` in the right-side status panel, including
`.btdev`. The message remains after changing screens, and the edit cursor is
unchanged. Pending, stale replies and failures never show this success message;
existing failure/recovery behavior is preserved.

## Collection bounds and undefined drafts

The agreed rules are in [Instrument foundation](INSTRUMENT_FOUNDATION.md#numbered-collection-and-trailing-undefined-slot).
With confirmed count N (0-99), Pattern selection and Instrument browsing offer
01 through min(N+1,99). The last slot is undefined when N<99, displayed as its
ordinary number in Pattern and **Not defined yet** in Instrument. Pending count
holds numeric selection/assignment; Clear remains available when otherwise editable.
Existing out-of-range references remain inspectable and cancellable.

Ctrl+Up / Select+Up opens the trailing slot; Right browsing reaches it from N.
Ctrl+Down / Select+Down confirms: an untouched or unassigned Sample draft returns
with the numbered undefined reference. An eligible draft commits on Teensy first;
only its success acknowledgement advances the count and permits assignment/return.
Windows unmodified 1/2/3 selects Sample/MIDI/Sine Synth for this undefined draft;
handheld unmodified X cycles these Types. Explicit MIDI/Sine Synth qualification
is a temporary development exception. Failure keeps the editor open with the
original source cell intact. Escape/unmodified B discards local draft changes
without assignment; an already transmitted commit may finish, but cannot assign the cancelled cell.
Save is reserved for future file operations, separate from confirmed return.
Behavior changes require explicit owner agreement and a documentation update.

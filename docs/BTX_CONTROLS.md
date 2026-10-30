# BTX controls

Single current reference for desktop and handheld controls in standalone
BroTracker Terminal (BTX). Protocol/design pages and dated implementation records
provide context; use this page for current bindings and validation scope.
**+ means hold the modifier while pressing the other control.** Row means an
editable pattern position; field means Note or Instrument (OFF timing when the
note is OFF); header means the channel-number box. Playback does not move the
edit cursor. Unmodified keys are intended unless a modifier is shown.

## Desktop controls

| Function | Key / button combination | Context / behavior |
| --- | --- | --- |
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
| Publish to RAM | Shift + F5 | publish draft plus applied tempo while stopped, or paused if firmware supports it; never stops or continues automatically. No-op/unavailable without changes or required synchronization/capabilities. |
| Restore RAM baseline | Shift + F4 | Restore the entire draft pattern and published tempo baseline, subject to readiness/locks. Normally sends supported working preview; local-only during a reconnect hold (below). Does not Publish. Works from row or header context. |
| Temporary Save | F5 | Save working tune to the single tune.btdraft file; no Publish. Playing: freeze one RAM snapshot, report pending, write after Pause/Stop. |
| Temporary Load | F4 | Require STOP, validate tune.btdraft, then replace baseline/working state. Failure retains current work; no fallback. |
| Load development pattern | F3 | Require STOP; explicitly load development.startup_pattern. Never runs on normal startup/reconnect. |
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
| Restore RAM baseline | L1 + X | Restore entire draft pattern and published tempo baseline, including from headers; subject to readiness/locks. Local-only during a reconnect hold (below). Does not Publish. |
| Publish to RAM | L1 + A in a row | publish draft plus applied tempo while stopped/eligible paused; does not auto-Stop/Continue. Required capability/synchronization checks apply. |
| Temporary Save | A in a row | Without L1: save working tune to tune.btdraft, including unpublished edits available on the device. |
| Temporary Load | L1 + Y | Load validated tune.btdraft after STOP; preserve current work on failed read/validation. |
| Load development pattern | Select + Start | One request per chord; both controls must be released before rearming. Either press order; consumes Start. Two separate presses do not exit. |
| Mute / unmute selected channel | A in a header | Consumes A instead of Save/Publish, including with L1 held. Session mute only; no pattern/tempo change. |
| Decrease / increase tempo | R1 + D-pad Left / Right | Immediate 0.05 BPM step, accelerated hold. Independent of value-step direction; available during playback with supported synchronized tempo. |
| Internal master volume | Physical volume + / - | Verified evdev profile only: 5-point steps over0..100%, default100%. Hold repeats after400 ms, every100 ms; kernel repeats ignored. Teensy smooths changes; external MIDI unchanged. Requires supported synchronized firmware and exclusive volume-only key acquisition. `OS keys` means BTX did not acquire them; they retain system behavior. |
| Cancel opt-in diagnostic run | B or Select; Escape on an attached keyboard; window close | Diagnostic mode only: normal editing/controller actions are disabled. B/Select arm only after all controls are released for 250 ms. Normal Select+Start loads the development pattern. This is cancellation, not a normal playback shortcut. |

**Vertical R36S differences:** repository evidence does not verify an R36S-specific
printed A/B/X/Y layout, shoulder arrangement or alternate binding profile. No
model-specific swap is implemented. The same actions use that device's selected
SDL/PortMaster mapping; orientation alone cannot establish which physical button
produces A, B, X or Y. Verify the device mapping before relying on printed labels.
Do not assume EmulationStation/RetroArch hotkeys apply to standalone BTX. Existing
handheld confirmations do not establish a separate R36S physical mapping test.

Hold L1 before a normal L1 chord. Header A still mutes/unmutes with L1 held;
row A saves, L1+A publishes, L1+X restores the RAM baseline and L1+Y loads the
saved tune. B requests STOP with L1 held. Select maps to SDL `BACK`, Start to SDL
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
  Arrival. Instruments cycle displayed 01..99 (raw 0..98) through empty `--`:
  99 -> -- -> 01 or 01 -> -- -> 99. Fresh empty entry starts at 01 in either
  direction. Higher received IDs remain intact but cannot be stepped. Empty
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

Publish establishes the device's **in-memory published pattern and tempo baseline**
after authoritative acknowledgment/readback. Restore returns to that baseline;
automatic working preview does not Publish. Neither control is explicit SD session
SAVE/RESTORE, and no keyboard/controller shortcut for those tooling operations is
implemented. Restore is one undoable pattern-data transaction, but no Undo/Redo
shortcut is mapped; tempo remains separate from cell-edit history. Supported
edits automatically preview, including at eligible row boundaries while Playing.
Preview changes working playback, not the published baseline or dirty comparison;
rejected edits remain visible with status. Legacy peers without preview support
retain explicit publication. Neither working nor published device RAM survives
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
without F3/Select+Start. Enter/STOP remains usable during recovery, but a STOP
acknowledgment alone does not complete the required working-state verification. A RAM-only playing edit was not necessarily on SD
before the interruption, even when BTX can recover it from retained host RAM.

A running device, newer working revision, changed baseline/dimensions, failed or
incomplete readback or rejected recovery keeps available work protected with an
explicit status. STOP remains available. Shift+F4 / L1+X restores the retained RAM
baseline; a resulting verified match can unlock without Publish. Shift+F5 / row
L1+A is an explicit publication option for an unresolved mismatch, never a file
Save. Uncertain publication requires readback before retry. F3/F4/F5 file actions
remain unavailable during the hold. No fixture LOAD or START is replayed.

## Recorded confirmations and validation limits

These are dated observations, not approval of every later change or a measured
latency/performance result. This consolidation involved no physical testing.

| Recorded evidence | What it establishes / remaining limits |
| --- | --- |
| 2026-10-15: user confirmed ArkOS controls/workflows and added held navigation; shared held navigation confirmed on Windows and ArkOS | Supersedes the earlier held-navigation-pending note. Does not establish dArkOS validation, physical timing/realtime performance, every mapping/profile or later shortcuts. See the [navigation/controls records](TEENSY_PATTERN_BRINGUP.md) and [completed tasks](TODO_LIST.md). |
| Windows live-tempo manual checklist confirmed, including requested BPM pending-color indication | Preserve that recorded confirmation; no additional cases are inferred. See [tempo evidence/checklist](LIVE_TEMPO_CONTROL.md). |
| 2026-10-22: user confirmed smooth handheld R1+Left/Right tempo changes and R1+Up/Down beat navigation during playback; supplied handheld control test passed | Other unreported boundary/modifier/hold cases remain unconfirmed. Does not establish audio quality, MIDI alignment or a separate dArkOS/R36S physical mapping test. Windows Shift+Up/Down manual beat-shortcut checks remain pending; ordinary shared navigation confirmation does not cover them. |
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

See [working-session storage](FILE_FORMATS.md#working-session-separate-from-tunesprojects)
and [reconnect continuity](RECONNECT_CONTINUITY.md). Temporary Save does not change
the published baseline or dirty comparison. RAM Restore never reads tune.btdraft;
Publish never writes it. File Load intentionally establishes a new baseline and
working copy only after validation/application. No Save As or file browser is
implemented. Power loss restores the latest available disk session stopped, while
a powered reconnect synchronizes with the actual device without STOP or LOAD.

F4 Load and session resynchronization retain the known BTX edit cursor, including
its field/header selection, clamped to the loaded pattern dimensions. Explicit
F3 / Select+Start Test loads reset the cursor. A fresh
editor with no previous cursor starts at the first row/channel. Cursor position
is host state; it is not stored in tune or SD session files.

If Teensy rejects the pre-load READY check (for example, MIDI cleanup has not
completed), no file Load is sent. BTX retains the current work/cursor and releases
the Load barrier for an existing editor; STOP and normal controls remain
available. The status shows why Load was not attempted. Retry Load/Test after
the device condition clears. Idle, cancelled MIDI quarantine no longer blocks
file operations; MIDI fault/cleanup uncertainty remains visible. Active MIDI
cleanup or DMA still blocks them. Uncertain outcomes after a submitted Load still
require verified recovery. This does not clear or bypass MIDI faults.

F5/F4 do not save or load channel mutes. They retain the live Teensy mute mask
while the device remains powered; device initialization clears that mask.

New bindings are automated-test coverage, not a new physical handheld confirmation.
Existing dated observations used the previous startup/F4/F5 behavior.

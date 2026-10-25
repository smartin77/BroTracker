# BTX controls

Current implemented controls for the standalone BroTracker Terminal (BTX).
**+ means hold the modifier while pressing the other control.** Row means an
editable pattern position; field means Note or Instrument (OFF timing when the
note is OFF); header means the channel-number box. Playback does not move the
edit cursor. Unmodified keys are intended unless a modifier is shown.

## Desktop controls

| Function | Key / button combination | Context / behavior |
| --- | --- | --- |
| Start / Pause / Continue | Space | Stopped: start at pattern row zero. Playing: pause. Paused: replay the reported paused row from its beginning, then continue. |
| STOP/reset | Enter or numeric keypad Enter | Stop/reset to pattern start; remain in BTX. A fresh press can request STOP again. |
| Exit | Ctrl + X or close the window | Request STOP when needed, then exit after acknowledgment; disconnect/exit timeout can leave STOP unconfirmed. |
| Previous / next row | Up / Down | Bounded by active rows. Up from first row enters headers; Down from a header returns to first row. Hold repeats. |
| Previous / next field | Left / Right | Note <-> Instrument, crossing into adjacent channels at field edges; stops at outer edges. In headers, moves to adjacent header without wrapping. Hold repeats. |
| Previous / next channel | Shift + Tab / Tab | Wraps active channels; selects Note in row context, preserves header context. Hold repeats. |
| Previous / next beat | Shift + Up / Down | Strictly preceding/following highlighted beat (zero-based 0,4,8,...), within active visible rows 0..31; no wrap. Previous from first row enters header; next from header returns to first row. Hold repeats. |
| Increase / decrease value | Page Up / Page Down | Selected Note or Instrument; hold repeats with acceleration. Direction preference can reverse these requests. No value editing in headers. |
| Enter NOTE OFF | O | Note field only; sets Arrival timing. |
| Select OFF timing | Page Up or Page Down in the second field of an OFF cell | Either toggles Arrival (row start) / EndOfPosition (following row boundary). Direction preference does not affect this toggle. |
| Clear | Delete | Note field: clears the whole cell (note, instrument and OFF timing). Numeric Instrument field: clears only instrument. OFF timing field: resets Arrival. No effect in headers. |
| Publish | F5 | With no modifiers: publish draft plus applied tempo while stopped, or paused if firmware supports it; never stops or continues automatically. No-op/unavailable without changes or required synchronization/capabilities. |
| Restore | F4 | Restore the entire draft pattern and published tempo baseline, subject to readiness/locks. Sends supported working preview; does not Publish. Works from row or header context. |
| Select header and mute / unmute channel | Left-click inside a channel-number header | Selects that header and toggles its session mute. Border/outside clicks do not activate it. No desktop keyboard mute binding is implemented. |
| Decrease / increase tempo | Numeric keypad - / + | Immediate 0.05 BPM step; hold accelerates (below). Independent of value-step direction; works while playing when tempo control is available. |

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
| Exit | L1 + Start | STOP/acknowledgment exit workflow; an unconfirmed STOP on disconnect/timeout is not successful cleanup delivery. |
| Previous / next row; enter / leave headers | D-pad Up / Down | Active row bounds; Up from first row enters headers, Down from header returns to first row. Hold repeats. |
| Previous / next field or header | D-pad Left / Right | Row: move Note <-> Instrument across channel edges, bounded at outer edges. Header: adjacent header without wrapping. Hold repeats. |
| Previous / next channel | L1 + D-pad Left / Right | Wraps active channels; selects Note in row context, preserves header context. Hold repeats. |
| Previous / next beat | R1 + D-pad Up / Down | Strictly preceding/following beat at zero-based 0,4,8,... within visible active rows 0..31; no wrap. Previous from first row enters header; next from header returns to first row. Hold repeats; R1 takes precedence over L1 value editing. |
| Increase / decrease value | L1 + D-pad Up / Down | Selected Note or Instrument; hold repeats with acceleration. Direction preference can reverse requests. No value editing in headers. |
| Enter NOTE OFF | X | Without L1, Note field only; sets Arrival timing. |
| Select OFF timing | L1 + D-pad Up or Down in the second field of an OFF cell | Either toggles Arrival / EndOfPosition; not reversed by value-step preference. |
| Clear | Y | Without L1: Note field clears the whole cell and OFF timing; numeric Instrument clears only instrument; OFF timing resets Arrival. No effect in headers. |
| Restore | L1 + Y | Restore entire draft pattern and published tempo baseline, including from headers; subject to readiness/locks. Does not Publish. |
| Publish | A in a row | Without L1: publish draft plus applied tempo while stopped/eligible paused; does not auto-Stop/Continue. Required capability/synchronization checks apply. |
| Mute / unmute selected channel | A in a header | Consumes A instead of Publish, including with L1 held. Session mute only; no pattern/tempo change. |
| Decrease / increase tempo | R1 + D-pad Left / Right | Immediate 0.05 BPM step, accelerated hold. Independent of value-step direction; available during playback with supported synchronized tempo. |
| Cancel opt-in diagnostic run | B or Select; Escape on an attached keyboard; window close | Diagnostic mode only: normal editing/controller actions are disabled. B/Select arm only after all controls are released for 250 ms. Select is otherwise unassigned. This is cancellation, not a normal playback shortcut. |

**Vertical R36S differences:** repository evidence does not verify an R36S-specific
printed A/B/X/Y layout, shoulder arrangement or alternate binding profile. No
model-specific swap is implemented. The same actions use that device's selected
SDL/PortMaster mapping; orientation alone cannot establish which physical button
produces A, B, X or Y. Verify the device mapping before relying on printed labels.
Do not assume EmulationStation/RetroArch hotkeys apply to standalone BTX. Existing
handheld confirmations do not establish a separate R36S physical mapping test.

## Editing and hold behavior

- Value direction defaults to normal. `[editing] value_step_direction=reversed`
  swaps Page Up/Down and L1+Up/Down for notes/numeric instruments, not navigation,
  tempo, explicit OFF or OFF timing. Changes take effect on application restart.
- Notes cover C-0..G-8 (raw 24..127), cycling through OFF at the endpoints. Empty
  Note entry reuses the nearest preceding pitched note in that channel, otherwise
  C-3; it does not insert an instrument. Instruments cycle displayed 01..99 and
  empty; fresh empty entry starts at 01. Higher received IDs cannot be stepped.
- Navigation repeats after 350 ms, then every 100 ms, accelerating to 60 ms after
  one second and 40 ms after two seconds. Value repeats start after 400 ms, then
  100/50/25 ms at those same one/two-second thresholds. Value endpoints/OFF/empty
  renew the initial pause. Tempo repeats start after 400 ms, every 100 ms: 0.05 BPM,
  then 0.25 after 1.5 seconds and 1.00 after three seconds. No catch-up bursts.
- Releases, opposing directions, modifier changes, focus loss, disconnects and
  relevant locks cancel held actions; release and press again to rearm. Value
  and navigation holds share one active editor stream. No Undo/Redo shortcut or
  direct typed-note/number entry is currently mapped.

Publish establishes the device's **in-memory published pattern and tempo baseline**
after authoritative acknowledgment/readback. Restore returns to that baseline;
automatic working preview does not Publish. Neither control is explicit SD session
SAVE/RESTORE, and no keyboard/controller shortcut for those tooling operations is
implemented. See [storage operations](FILE_FORMATS.md).

Editing requires a validated device pattern. During configured SD startup recovery
(including failed/uncertain loading), note/instrument/OFF edits and preview remain
blocked until authoritative baseline and working recovery synchronize. Publication
locks also block mutation/Restore, while navigation may remain available. START,
Publish, tempo and mute requests have their own connection/capability/pending-state
checks; watch the status panel. STOP and exit remain usable. Legacy firmware without
pattern transport uses Start/Restart instead of Pause/Continue.

Mute releases the channel's current voice. Unmute does not trigger a note; later
pattern events can sound normally. Mute is session state, not a pattern edit.

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

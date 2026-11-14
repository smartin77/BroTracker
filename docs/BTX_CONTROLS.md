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

Opening another screen stops playback and saves the session before entry. Instrument
opens the cell's explicit slot, otherwise its preceding slot in the same channel,
otherwise 01. Select another slot on Pattern before opening Instrument again. The single trailing
undefined slot, when available, opens with Sample as its default Type.

## Instrument

| Visible action | Windows | Handheld |
|---|---|---|
| Previous / next actionable field | Up / Down (also Shift+Tab / Tab) | D-pad Up / Down |
| Previous / next focused value | Left / Right | D-pad Left / Right |
| Cycle Type backward / forward | Left / Right on Type | D-pad Left / Right on Type |
| Open Sample Browser | Enter on Load | A on Load |
| Confirm draft and return to Pattern | Ctrl+Down | Select+D-pad Down |
| Discard draft and return to Pattern | Escape (also Backspace) | B |
| Exit without confirming Instrument draft | Ctrl+X or close window | L1+Start |

The orange frame marks the focused field. Type cycles Sample, MIDI and Sine Synth
for existing and new slots, including built-in slots. Sample exposes Load. A new
Sample needs a prepared WAV to become defined; without a staged WAV, confirmed
return assigns its still-undefined slot and keeps the next slot unavailable.
Existing changes keep the same slot,
Pattern references and defined count; failed preparation keeps the editor open
and the original instrument intact. Confirmation waits for Teensy acknowledgement.

PgUp/PgDn, L1 value stepping, 1/2/3 and X do not change Instrument Type. Ctrl/Select
is used only for confirmed return within this editor (Ctrl+X remains global exit).
Modified Left/Right does not change slots. Enter/A on Type does not change it.

## Sample Browser

| Visible action | Windows | Handheld |
|---|---|---|
| Move selection | Up / Down | D-pad Up / Down |
| Open folder, including `..` | Enter | A |
| Stage supported WAV and return to Instrument | Enter | A |
| Cancel and retain previous Instrument draft | Escape / Backspace | B |
| Cancel via existing screen-navigation chord | Ctrl+Left / Right | Select+D-pad Left / Right |

The confirmed SD directory is at the top. The list fills the middle; one bottom
line shows selected-file metadata, rejection, empty state or folder error. All
WAVs remain visible. Enter/A on an unsupported WAV or invalid resource path keeps
the browser and draft unchanged. Ordinary folder rows leave the footer blank.

The preferred folder is `/BroTracker/Samples`; missing/unreadable folders fall
back to a readable parent. `..` is absent at SD root. Choosing a different path
for an existing Sample opens the replacement confirmation below.

## WAV replacement confirmation

| Visible action | Windows | Handheld |
|---|---|---|
| Stage the displayed new path | Enter | A |
| Keep the saved path | Escape / Backspace | B |

Review both paths. Accepting stages the candidate; confirmed return from
Instrument prepares it before changing the saved definition and playback binding.

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

Details: [Instrument editing](INSTRUMENT_FOUNDATION.md),
[session and file rules](FILE_FORMATS.md), [reconnect recovery](RECONNECT_CONTINUITY.md),
[tempo](LIVE_TEMPO_CONTROL.md), [mute](CHANNEL_MUTE.md),
[handheld mapping](../tools/arkos/README.md) and
[technical verification records](TEENSY_PATTERN_BRINGUP.md).

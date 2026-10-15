# Temporary ArkOS controls

Current bindings come from [the implemented input handler](../ui/arkos/input.h)
and shared pattern editor. On **2026-10-15**, the user confirmed that the current
controls and workflows work on ArkOS. This does not establish dArkOS validation,
measured physical timing or realtime performance.

## Controls

Hold L1 before pressing a chord's other control. Chords consume their component
actions. Editing requires a validated device pattern; playback never moves the
edit cursor.

| SDL semantic control | Action |
| --- | --- |
| D-pad Up / Down | Previous / next edit row, bounded to the active pattern |
| D-pad Left / Right | Previous / next field across channels; stops at outer edges |
| L1 + Up / Down | Increase / decrease the selected value, subject to direction configuration |
| L1 + Left / Right | Previous / next active channel, wrapping and selecting Note |
| Start | Start from zero when stopped; Pause when playing; Continue by replaying the reported paused row from its beginning |
| B | Stop/reset and stay in BTX; another physical press can request another STOP |
| X | Enter NOTE_OFF in the Note field, initially Arrival |
| Y | Clear the selected field (details below) |
| L1 + Y | Restore the published baseline locally and to working playback through preview |
| A | Publish the selected draft while Stopped/Paused, with required device capabilities |
| L1 + Start | Clean exit through the existing STOP/acknowledgement workflow |
| L1 alone / Select / R1 | Unassigned |

X and A have no action with L1 held. B still requests Stop/reset with L1 held.
Legacy firmware without pattern-transport capability uses Start/Restart rather
than Pause/Continue; publication and automatic preview require their capabilities.

## Values, holds and OFF timing

Optional schema-1 configuration:

```ini
[editing]
value_step_direction=normal
```

Normal: L1+Up increases, L1+Down decreases. Reversed swaps these requests for
Note and numeric Instrument fields, including holds. The deprecated
`note_step_direction` alias is accepted; specifying both names rejects the config.
OFF timing toggles, navigation and explicit NOTE_OFF entry do not invert.

- Pitched notes cover C-0..G-8 (raw 24..127). Increase cycles G-8 -> OFF -> C-0;
  decrease cycles C-0 -> OFF -> G-8. Entering/leaving OFF sets Arrival timing.
- An empty Note field first reuses the nearest preceding pitched note in that
  draft channel, or C-3 if none, unchanged in either direction. No instrument is inserted.
- Numeric instruments cover displayed 01..99 (raw 0..98), cycling through empty
  `--`: 99 -> -- -> 01, or 01 -> -- -> 99. Fresh empty entry selects 01 in either
  direction. Higher received IDs remain intact; stepping them is rejected.
- For OFF, the second field is timing: `¯` Arrival stops at row start;
  `_` EndOfPosition stops at the following row boundary. Both directions toggle
  the modes. An empty note retains a normal instrument field.

A value press acts immediately. Holding repeats after 400 ms, initially every
100 ms, then 50 ms after one second and 25 ms after two seconds. Directional
endpoints and entry into OFF/empty renew the 400 ms pause. Release, navigation,
Clear/Restore/OFF, focus loss, controller removal, disconnect or editor locking
cancel holds. Changing L1 while a direction is held requires releasing that
direction before rearming it. Opposing directions cancel; there is no catch-up burst.

Held navigation is a newer change, **hardware-validation pending** and not covered
by the 2026-10-15 confirmation above. Plain D-pad row/field navigation and
L1+Left/Right channel navigation act immediately, repeat first after 350 ms,
then use 100 ms intervals before one second held, 60 ms from one second and
40 ms from two seconds. Timing is measured from the initial press. Bounds and
channel wrapping are unchanged; navigation does not edit data or create history.

The platform-independent navigation timer is shared with Windows arrows and
Tab/Shift+Tab. Both platform integrations await manual validation of the new
shared implementation; previous controls confirmations remain unchanged.

Navigation and value editing share one active repeat stream. Each service emits
at most one action, dropping catch-up work after a stall. Releases, modifier
changes, opposition, focus loss, controller removal, device disconnect and exit
cancel navigation holds; release before rearming after modifier changes.
Publication locking permits navigation repeat while blocking/cancelling value
mutation. The 400/100/50/25 ms value-edit schedule and its endpoint pauses above
remain unchanged. No paging or playback-range controls are implemented here.

## Clear, Restore and Publish

**Y / Clear:** Note clears note, instrument and OFF timing together. A numeric
Instrument field clears only the instrument; an OFF timing field resets Arrival.
Explicit instrument-only events remain valid.

**L1+Y / Restore:** replace the draft with the last published baseline and queue
it as working preview when supported. This is one undoable data transaction;
Undo/Redo have no handheld bindings yet. Restore does not publish.

**A / Publish:** explicit stopped/paused publication; Playing remains blocked.
It never automatically Stops or Continues. Exact authoritative readback must
confirm the selected revision before rebasing the editor. Publication locks
mutations/Restore but permits navigation; B and clean exit remain available.

Supported edits automatically preview, including while Playing at an eligible
row boundary. Preview does not clear dirty state or change the published baseline.
Rejected edits remain visible with status. Legacy firmware retains explicit
publication behavior. Neither preview nor publication persists to SD; both device
patterns are volatile RAM state. Reconnect does not automatically Start or publish.

## Mapping and future profiles

Labels identify SDL semantic controls, not guaranteed printed face-button labels
or raw indices. BTX filters the selected controller's instance; its raw fallback
uses only that controller's SDL button/hat bindings, without duplicate events.
Unmapped devices and raw-axis fallback are unsupported. Horizontal/vertical layout
and RetroArch/EmulationStation settings do not establish a BTX mapping.

These bindings are temporary. Configurable profiles, calibration, final controls
and broader device/OS-hotkey validation remain future work. See the
[bring-up notes](TEENSY_PATTERN_BRINGUP.md) for implementation and protocol details.

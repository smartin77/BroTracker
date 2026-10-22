# Live tempo control (session only)

Implemented on `feature/live-tempo-control`. Engine and Tune tempo is an integer
number of hundredths of BPM, with a 12500 / **125.00 BPM** development default.
Interactive requests accept 3200-30000 (32.00-300.00 BPM) transactionally. Existing
fractional regression inputs such as 12753 remain intentional and unchanged.
The existing decimal JSON input is converted once to integer hundredths; its
format is unchanged. No SD persistence or config.ini rewrite is added.

The timing convention is unchanged, as confirmed by the user: **96 ticks per
row, four rows per quarter note, 384 ticks per quarter note**. Tempo belongs to
the tune/transport, not to pattern cells. Sample 01, silent unsupported MIDI 02,
Synth 03, retained Sample 04, gains and output negotiation remain unchanged.
USB_BRIDGE remains the development default; bridge diagnostics remain paused.

## Controls and display

| Platform | Decrease | Increase |
| --- | --- | --- |
| Windows | Numeric keypad - (`SDLK_KP_MINUS`) | Numeric keypad + (`SDLK_KP_PLUS`) |
| Handheld | R1 + D-pad LEFT | R1 + D-pad RIGHT |

Main-keyboard +/- are unbound. Keypad control is independent of Num Lock. The
handheld consumes the chord in both SDL controller and verified raw button/hat
paths. R1 may be pressed before or after the direction; when direction precedes
R1, earlier plain navigation has already happened, then repeat is cancelled and
the tempo chord takes over. Releasing R1 blocks held directions from resuming
editor navigation until their physical release. Existing L1 editing, transport,
raw stream selection and value-step-direction preference are preserved; the
preference never reverses tempo directions.

Each physical press changes 0.05 BPM immediately. Application-owned repeat ignores
OS key repeat: after 400 ms, 0.05 every 100 ms; after 1.5 s held, 0.25 every 100 ms;
after 3 s, 1.00 every 100 ms. A stalled frame performs at most one repeat, without
replaying a backlog. Release, opposite directions, focus loss, disconnection and
controller removal cancel repeat. Reversing starts a fresh hold. Range clamping
uses widened signed arithmetic to avoid overflow.

BPM displays one decimal place using integer nearest rounding (half up). Hidden
hundredths are retained: 125.00 -> 125.05 -> 125.10 displays 125.0 -> 125.1 -> 125.1.
Two taps always change the desired value by 0.10 BPM away from a clamp. The BPM number uses the editing frame orange while the existing pending
indication is active, and white otherwise (no asterisk or additional timer).
Status text distinguishes last applied tempo from the desired value;
an uncertain outcome is labelled while authoritative device state is read.
Older firmware remains usable with tempo controls disabled.

## Timing and ordering

`MusicalTickCursor` retains its constant-tempo Configure/Reset/conversion API and
adds explicit segment anchoring. At a live change, the next unconsumed row-start
tick is first located using the existing segment. The new segment starts at that
actual absolute sample and musical tick. Future samples are:

`boundary_sample + floor((tick - boundary_tick) * rate * 6000 / (tempo * 384))`.

Elapsed time is never recomputed using the new tempo. The preceding row finishes
at its old timing. Half-open audio blocks leave end-boundary ticks for the next
block; each tick and boundary event is consumed exactly once. Row events,
telemetry and deferred musical events use this same cursor. Deferred NOTE OFF
entries retain their target tick as well as their sample deadline; future
deadlines are recalculated against a new segment. Stops already due at the
boundary precede that boundary's new triggers.

At each audio block, accepted transport/publication/tempo FIFO operations are
processed in order (bounded to eight). For a render boundary the order is:

1. Flush old deferred NOTE OFFs due at that sample, ordered by channel at a tie.
2. Apply the latest pending tempo and establish its timing segment.
3. Apply a pending working-pattern preview and reconstruct logical continuation.
4. Emit that row's note/OFF events in the existing order, then render normally.

Publication remains stopped/paused only. A successful publication installs its
frozen pattern and explicit tempo together through the audio owner. Playing
publication is rejected before mutation. Transport requests that run before
rendering can stop/pause/restart before a boundary is consumed. STOP cancels all
already-submitted tempo work, including queued requests, and clears pending
application; a cancelled value cannot appear after restart. IDs have separate
monotonic domains for tempo, gains, publication and preview.

A live tempo change does not reset/reconfigure audio voices, sample interpolation,
Synth tuning, phase or envelopes. Synth duration remains measured in samples /
seconds. Samples retain pitch/playback rate; no stretching or loop matching is
added. Rendering/handoff storage remains fixed and bounded, without allocation,
logging, file I/O, locks or host timing in the render path.

## Retention, Publish and Restore

| Action | Tempo behavior |
| --- | --- |
| Tempo input during playback | Latest pending desired value applies at the next unconsumed row start. |
| Tempo input while stopped/paused | Applies through the audio-owned bounded handoff without moving position or starting audio. |
| Pause / Continue | Retain applied tempo; Continue replays the reported current row using the new tempo, preserving its loop identity. Unchanged tempo preserves its existing rational timing phase. |
| STOP / START | Retain latest applied/acknowledged tempo; STOP cancels unacknowledged pending work. START resets transport position, not tempo. |
| BTX reconnect | Read device-authoritative tempo and published baseline; no automatic START or replay of an uncertain old SET. |
| Publish (F5 / A) | Freeze coherent pattern + applied tempo as the new published baseline; requires synchronized tempo and stopped/paused transport. |
| Restore (F4 / L1+Y) | Return the working pattern and desired tempo to the published snapshot baseline, using normal preview/tempo handoffs. Does not force 125.00 or change the published baseline. |
| Device reboot / firmware reinitialization | Session ends; development fixture starts at 125.00. No disk persistence is implied. |

Restore's tempo goal coalesces with an already in-flight adjustment. Its value
comes from the checksum-verified published snapshot, so it also works immediately
after publication readback, before a later status poll. Cell-only previews never
contain a tempo field and cannot restore or reset tempo accidentally. Pattern
history remains separate from tune tempo; tempo does not become a cell edit.

## Capability-gated protocol

Existing BTTEST1, snapshot, preview and output contracts remain available.
Additional capabilities:

```text
BTTEMPO1 CAP 1
BTPUBLISH1 TEMPOCAP 2
```

Tempo requests and responses (integer hundredths, unsigned decimal):

```text
BTTEMPO1 SET 1 identity tempo
BTTEMPO1 SUBMITTED 1 identity
BTTEMPO1 APPLIED 1 identity
BTTEMPO1 CANCELLED 1 identity
BTTEMPO1 REJECTED 1 identity
BTTEMPO1 GET 1
BTTEMPO1 STATE 1 settled applied_id next_id pending_id applied_tempo published_tempo
```

SUBMITTED is receipt, not application. APPLIED is emitted after the audio owner
has committed the boundary change. The completion/readback slot is coalesced;
STATE is authoritative and includes the highest settled identity even for
cancellation. IDs do not wrap; zero next_id means exhausted. Invalid range,
identity, malformed input, publication conflict or full handoff is rejected
without changing tempo. The host keeps one frozen in-flight SET and one latest
desired value. Taps accumulate from desired, not stale applied values. Traffic is
bounded by one in-flight request and GET recovery polling at most once/second.
Timeout starts at the first attempted write, including asynchronous zero-progress
writes. Timeout/reconnect reads state and discards uncertain old goals; it never
blindly retransmits a possibly delivered command. STOP retains line framing and
outranks tempo/status traffic.

Tempo-aware publication uses a version-2 BEGIN:

```text
BTPUBLISH1 BEGIN 2 identity rows channels count tempo
```

CELL 1 and COMMIT 1 retain their existing note/instrument/timing payload/framing.
For BEGIN 2, FNV-1a32 hashes rows, channels, four little-endian tempo bytes, then
cell note/instrument/timing triples (the existing snapshot header hash). Invalid
tempo is rejected before staging; checksum mismatch rejects the entire tuple.
Legacy BEGIN 1 still publishes pattern while retaining current applied tempo;
new BTX requires TEMPOCAP 2 when live tempo is supported, explicitly refusing an
inconsistent partial-capability combination. Old firmware without live tempo
continues to use the existing publication flow.

Existing snapshot version 2 already carries/checksums tempo. Published readback
now freezes the published tempo with its pattern; working readback freezes the
applied working tempo with its working pattern. Sender tempo cannot change during
an active transfer. No new cell format or on-disk format is introduced.

## Validation and manual checks

See [actual validation report](../build/live-tempo/validation.md),
[hardware transcript](../build/live-tempo/hardware.log),
[hardware summary](../build/live-tempo/hardware.json) and
[complete actual diff, including new files](../build/live-tempo/complete.diff).
Windows package/eight suites, clean GCC 13 Release/six CI suites, ARM64 Eoan/seven
suites and application ABI/dependencies, and all six Teensy builds passed.
Development upload and bounded physical tempo/transport/publication/readback checks
passed on COM3 / Teensy 17681760. Final applied/published tempo is 125.00 BPM,
playback stopped and serial closed.
These generated artifacts remain ignored. Automated results do not establish
listening/UX approval, manual rapid-retrigger approval or a console-pop solution.
The earlier user NOTE OFF confirmation through O/value stepping is preserved.

From the repository root, exact Windows BTX commands:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/windows/build-package.ps1 -ToolchainBin D:/dev/msys64/ucrt64/bin
.\deploy\windows\BroTrackerTerminal.exe
```

Windows manual checklist confirmed by the user, including the BPM pending color
indication working exactly as requested. Physical handheld checks and Teensy
reboot/default-tempo restoration remain pending:

- Tap keypad +/- and check fine adjustment, including hidden hundredths and the
  first-tap rounding behavior; confirm main +/- do nothing to tempo.
- Hold/release to check acceleration; test focus loss and R1/direction release
  on handheld later, confirming chords do not move the cursor or change cells.
- Change tempo while playing; confirm there is no restart and no missed row.
- Verify Synth tuning stays unchanged; existing drum-loop samples change neither
  pitch nor playback rate (there is no tempo stretching).
- Verify Pause/Continue current-row replay, STOP/START and reconnect retain the
  acknowledged tempo. Reconnect must not start playback automatically.
- Publish a changed tempo while stopped/paused, adjust it again, then Restore:
  Restore returns to that published tempo and pattern, not the fixture default.

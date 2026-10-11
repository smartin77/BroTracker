# Opt-in native-rate pattern bring-up

Windows BTX + a bare Teensy 4.1 is the primary development bring-up setup.
ArkOS remains the priority target platform, with follow-up hardware validation;
dArkOS validation is deferred. The opt-in firmware implementation is complete
and has host policy tests. User-performed validation on **2026-10-10** confirmed
that `teensy41_pattern` built and uploaded successfully and pattern audio was
heard through Windows BTX + Teensy 4.1.

Further user-performed Windows BTX + Teensy 4.1 validation on **2026-10-10**:

- Reported log/capture evidence confirms the initial IDLE handshake and captured
  silence, correctly acknowledged START, RESTART and two STOP operations.
- USB unplug/replug while stopped restored CDC/audio, remaining IDLE and silent
  without automatic START.
- The user confirmed normal window close during playback stops playback; the
  latest log also confirms STOPPED before clean shutdown.
- After forced BTX termination with Teensy still USB-powered, relaunch received
  STATE PLAYING without sending START. Audio routing resumed approximately
  0.906 seconds before the PLAYING handshake reply, consistent with independent
  audio and control paths. The user audibly confirmed sequence continuation.

The live-position implementation is complete for the opt-in firmware and shared
Windows/ArkOS BTX UI. User-performed Windows BTX + Teensy 4.1 validation on
**2026-10-10** additionally confirmed:

- Firmware build/upload and Windows package build succeeded.
- Reported log review validated **all 506 telemetry snapshots** for consistent
  tick/row/loop mapping, covering all 16 rows.
- Log review confirmed reconnection to running playback restored PLAYING and
  position without START; RESTART returned to zero-based row 0 / loop 0, and
  STOP invalidated position.
- The user visually confirmed moving playback highlighting, changing POS/LOOP
  and highlight removal after STOP. These are visual observations, separate from
  the log evidence above.

The read-only device pattern snapshot implementation is complete for the opt-in
firmware and shared Windows/ArkOS BTX. Windows BTX + Teensy 4.1 validation on
**2026-10-10** confirmed:

- Log review confirmed capability discovery, GET, BEGIN, all **32 ordered cells**
  for the **16-row/two-channel** pattern and END. Snapshot tempo was **12753
  hundredths BPM** (127.53 BPM internally; 127.5 on the one-decimal display).
- An independently recomputed **FNV-1a32 checksum matched 3128753801**.
- Connection to already-running playback restored position and pattern contents
  without automatic START. START/RESTART/STOP exchange succeeded without reported
  protocol or pattern errors.
- The user confirmed the visual display, separately from the log/checksum evidence.

User-performed Windows validation on **2026-10-10** confirmed that the final local
editor behavior works as agreed, including navigation, OFF timing toggling and
cyclic note editing with endpoint repeat pauses. This editor confirmation is
separate from the subsequent transport confirmation: user-performed Windows
BTX + Teensy 4.1 validation on **2026-10-10** confirms the revised transport and
visual behavior work as agreed. Exact physical timing and realtime performance
remain unmeasured; this does not close the remaining hardware checks.

These records distinguish reported log/capture evidence from user listening and
visual observations;
exact sample/phase continuity and restart-at-zero hardware timing remain unmeasured.
Full hardware validation remains open, including hardware backpressure/staleness
and snapshot timeout/recovery checks, legacy regression, ArkOS, MQS and realtime
performance. Logical telemetry consistency and visible restart at row 0 / loop 0 do not measure exact physical
sample/phase timing. The checklist below retains these outstanding checks.

From the repository root in PowerShell, build and upload explicitly:

```powershell
pio run -e teensy41_pattern -t upload
```

Launch the existing Windows client:

```powershell
.\deploy\windows\BroTrackerTerminal.exe
```

The default `teensy41` environment and `teensy41_usb_trace` are unchanged.
`teensy41_pattern` inherits the ordinary USB audio preparation script, including
the guarded zero-fill and unused-RX ownership corrections. It does not enable
USB trace instrumentation. The installed framework is not modified.

## Windows BTX manual checks

### Temporary local editor controls

After a complete validated device snapshot arrives, BTX creates a separate local
draft. The device snapshot remains read-only. The orange field border is the edit
cursor; the teal row highlight remains the reported playback position. The draft
is labeled **NOT SENT TO TEENSY**: changing it does not change audible playback.
Device BPM/POS/LOOP remain authoritative.

- Up/Down move previous/next row within active dimensions. Right moves Note ->
  second field -> next channel's Note; Left reverses this sequence. Horizontal
  arrows stop at the outermost fields without wrapping and preserve the row.
  Tab/Shift+Tab jump to the next/previous active channel's Note field, wrapping.
- Page Up/Down increment/decrement the selected numeric value. An empty Note field
  inserts the nearest preceding pitched note from this draft channel unchanged,
  or C-0 (raw 24) if none exists.
  OFF initializes to C-0; pitched values cycle through C-0..G-8
  (24..127): incrementing G-8 wraps to C-0, decrementing C-0 wraps to G-8.
  Empty instrument fields initialize to raw ID 0 (displayed 01); instrument bounds do not wrap.
- O enters NOTE_OFF in the Note field, defaulting to arrival-time `¯`. For OFF,
  the second field selects timing: `¯` stops at the current position's beginning,
  `_` stops at its end, whose duration follows musical timing/BPM. Both PgUp and
  PgDn toggle to the other mode; neither performs numeric instrument editing.
  Delete in this timing field restores `¯`. For pitched/empty notes the second
  field remains an instrument, so instrument-only events remain representable.
  Timing uses core draft metadata; raw instrument IDs are preserved separately
  and never encoded or reinterpreted as timing. Changing the Note field resets
  timing to arrival mode.
- Delete in the Note field clears both note and instrument to NOTE_EMPTY and
  0xFF, including OFF, and resets timing to Arrival. Delete in a normal Instrument
  field clears only that instrument; explicit instrument-only editing remains supported.
  Empty instruments display `--`; instrument ID 0 remains a valid ID displayed as `01`.
- F4 discards local edits by restoring the device baseline, retaining the cursor.
- Space uses the capability-gated transport described below; Return and numeric Enter STOP/reset,
  Ctrl+X and window close remain independent of editing. OS repeats are ignored.
  PgUp/PgDn alone repeat under application
  control: immediate action, first repeat at 400 ms, then every 100 ms, accelerating
  to 50 ms after 1000 ms held and 25 ms after 2000 ms. Each action changes one value
  or timing mode; stalled frames perform at most one repeat, without catch-up.
  Reaching a pitched-note endpoint in the held direction resets acceleration:
  wait a fresh 400 ms before wrapping, then resume normal repeat/acceleration.
  A new physical press at an endpoint wraps immediately on keydown; if that press
  becomes a hold, its first repeat waits 400 ms. Keydown cannot distinguish a tap
  from a future hold, so this immediate-press rule also applies when beginning a
  hold at an endpoint.
  Release, focus loss, disconnect/unavailability, navigation, Restore/Clear/OFF
  and incompatible modifiers cancel repeat; the opposite key replaces it. Held
  edits never resume after reconnect. Shift+Tab is the supported modified editor
  key. No piano keyboard mapping is provided.

NOTE_OFF timing is now core `NoteOffTiming` metadata copied from the validated
read-only device snapshot into the draft, separate from instrument IDs. Version 2
can carry EndOfPosition; legacy version 1 defaults to Arrival. Local changes remain
**NOT SENT TO TEENSY** until explicit F5 stopped-only publication and confirmed
readback, described below. Live editing is not implemented. The current
bring-up fixture retains Arrival timings and its audio sequence is unchanged.

Dirty state compares active raw cells and OFF timing with the baseline; restoring
original values clears it. Telemetry never moves the edit cursor or overwrites edits.
Disconnect disables editing and discards the connection-scoped draft. Reconnect
requires a fresh complete snapshot; edits are never replayed or published.
The fixed shared model/actions and draft rendering are reusable by ArkOS, but
handheld editor bindings and hardware validation remain outstanding. Existing
handheld transport bindings are unchanged. There are no edit uploads, persistence,
sample preview or automatic transport actions; standalone preview and page/region
behavior remain unchanged. Scrolling and empty-cell brightness are deferred.

### Playback checks

1. Close serial monitors, disable competing Windows Listen routing, upload the
   opt-in firmware, then launch BTX. Check an IDLE handshake and silence before
   pressing Space, including a cold boot with the board attached.
2. Press Space: check STARTED/playing and two distinct quiet diagnostic bursts.
   The immutable 16-row, two-channel pattern loops at 127.53 BPM, approximately
   every 1.88 seconds. It includes separate triggers, adjacent-row retriggers,
   simultaneous triggers at row 8 and row-start NOTE_OFF commands. In particular,
   channel B's row-14 NOTE_OFF cuts its row-13 burst short. Rows here are zero-based.
3. With the new transport capability, press Space while playing: expect PAUSED,
   silence and retention of the reported position. Space while paused continues
   at the START of the NEXT row, including row zero of the next loop after the
   last row; no remainder of the paused row or skipped-event burst is played.
   Press Return or numeric Enter: expect STOPPED/idle, reset position and silence while staying in
   BTX. Repeat Enter after acknowledgement: another STOP/reset is sent. Space
   while stopped starts at row zero. User confirmed the agreed behavior on
   2026-10-10; exact timing remains unmeasured. Older firmware without capability
   keeps Space START/RESTART.
4. Check several loops with Windows USB audio routing and, where connected, MQS.
   Mono PCM is routed identically to USB left/right and the existing MQS channel.
   No SD card or legacy WAV sequence is needed or played.
5. Stop, unplug/replug and let BTX reconnect: expect IDLE and silence until a new
   Space press. Reconnect while playing as a separate check: no START is sent by
   HELLO/reconnect; separately powered hardware may continue an already-running
   pattern, while a power-cycled board boots stopped. Inspect the authoritative
   STATUS reply rather than assuming disconnect stopped playback. Reconnect while
   paused must restore PAUSED and its position without START/CONTINUE. Previously
   accepted explicit commands are not replayed by BTX or replaced in the FIFO.
6. Inspect logs for `BTTEST1 ERROR pattern ...`. Any render/allocation fault must
   leave playback stopped. Allocation faults include a cumulative `alloc` count.
   Successful explicit START clears the active error latch and restarts at zero;
   it retains diagnostic counters. There is no automatic recovery/retry.
7. Regression: upload `pio run -e teensy41 -t upload` and verify the ordinary SD WAV
   diagnostic, boot silence and existing BTTEST1 Start/Stop/reconnect behavior.
   Separately upload `pio run -e teensy41_usb_trace -t upload` and verify its existing
   trace/diagnostic behavior. These are user-operated board checks, not completed
   validation claims. Follow with ArkOS hardware checks; leave dArkOS deferred.

## Ownership, startup and temporary protocol

The statically allocated `PatternBringUpControl` contains the native-rate player.
Its AudioStream source alone configures the player on its first ready callback
and owns Start/Stop/Render. Samples are immutable compile-time PCM16 mono arrays
at 44100 Hz (8192 frames each, peak magnitudes at most 600 and 512); these are
diagnostic fixtures, not a synthesis engine. Pattern/binding metadata is fixed.
The output is sent to MQS and both USB channels. No AudioTestSource, legacy
Scheduler, streaming player or legacy mixer is instantiated in this graph:
AudioTestSource's ordinary `AdvanceSamples(128)` does not run here, and the
pattern timeline advances only once per successful player Render.

MQS retains the existing update-responsibility arrangement. A readiness flag
published under interrupt exclusion prevents pre-setup callbacks from using
uninitialized AudioMemory. The opt-in startup does not run SD diagnostics or
RTC/host-time synchronization; the ordinary startup path is preserved. Playback
boots stopped and HELLO/reconnect never enqueues START. Logical sample scheduling
does not select a physical clock or implement synchronization/tempo correction.

BTTEST1 is a temporary compatibility bridge for current Windows/ArkOS BTX, not
the final tracker protocol. HELLO/STATUS report a coherent applied snapshot as
IDLE, PLAYING, PAUSED or ERROR. Applied acknowledgements are queued only after the audio owner
applies the request. Main-loop parsing is limited to 128 input bytes and one
complete command per service; USB writes are outside audio processing and limited
to 64 available bytes per service. Optional live position telemetry is described
below; it does not change the ordinary firmware's BTTEST1 exchange.

### Capability-gated Pause/Continue transport

After HELLO, the revised opt-in firmware advertises `BTPATTERN1 TRANSPORTCAP 1`.
Only after this exact connection-scoped capability may shared BTX send the new
requests. Ordinary firmware is unchanged and receives no PAUSE/CONTINUE requests.
Windows Space sends START from Stopped, PAUSE from Playing, CONTINUE from Paused.
Without capability, it retains diagnostic START/RESTART. ArkOS handheld transport
bindings remain L1/B START/RESTART and R1/X STOP/exit; no handheld remapping is included.

Exact LF-terminated command/response lines:

```text
BTTEST1 PAUSE
BTTEST1 CONTINUE
BTTEST1 PAUSED
BTTEST1 CONTINUED
BTTEST1 STATE PAUSED
```

PAUSED/CONTINUED acknowledge audio-owner application, not submission. Existing
START/STARTED and STOP/STOPPED remain supported. Return and numeric Enter both request STOP/reset
without starting or exiting; pending STOP suppresses repeats, and a fresh Enter
after acknowledgement sends another STOP. Close/Ctrl+X queues STOP in order before
exit, including during paused or pending transport. Accepted requests and applied
acknowledgements retain bounded FIFO order; formatting/I/O remain main-loop-only.

The core explicitly distinguishes Stopped/Playing/Paused. Pause is applied at the
next audio block boundary, silencing/releasing active voices and retaining logical
note/instrument continuation, consumed position and next-sample position. Paused
renders are silent and do not advance. Continue transactionally converts the currently reported paused
absolute row-start tick to its sample position and restarts a cursor segment there;
it performs no catch-up scan. That same row and all its events are replayed once
on rendering, with normal absolute rational timing thereafter. If paused before
any tick is consumed, row zero is still pending and is processed first. Stop clears
voices, continuation and position; Start always starts anew at zero. Arithmetic
or render rejection preserves core state/output; the firmware's existing fail-stop
render/allocation policy and explicit START recovery remain unchanged. The 128-frame,
16-command and 512-tick processing limits still apply. No new physical clock or
synchronization policy is introduced. Snapshot requests never change transport.

User-performed Windows BTX + Teensy 4.1 validation on **2026-10-10** confirms
revised transport and visual behavior work as agreed. Exact physical pause latency,
sample/phase and boundary timing, pending-command/backpressure stress, ordinary
firmware regression, ArkOS/MQS and measured realtime execution cost remain open.

### BTPATTERN1 live position telemetry

The opt-in firmware emits this explicitly versioned ASCII line (LF terminated):

```text
BTPATTERN1 POS 2 <state> <valid> <tick> <row> <loop> <rows> <channels>
```

Fields are unsigned decimal integers separated by exactly one ASCII space;
no signs, tabs, extra fields or trailing spaces are accepted. The literal `2`
is the revised message version. `state` is 0=Stopped, 1=Playing, 2=Paused;
`valid` is 0 or 1. The shared parser also retains the earlier version-1 grammar
with a 0/1 `running` flag (no paused state). `tick`, `row` and `loop`
support the full uint64 range. Active dimensions must be 1..16 rows and 1..8
channels, the current implementation capacities. A valid position requires
Playing or Paused, `row = (tick / 96) % rows` and `loop = (tick / 96) / rows`.
When invalid, tick/row/loop are all zero; Playing/Paused can be reported before the first
tick has been rendered. Row and loop indices are zero-based in the protocol;
BTX displays the row as one-based POS and keeps LOOP zero-based.

Position is the **latest musical tick actually consumed within successfully
rendered audio**, including empty rows. It is not the next sample/block position
or a measurement of physical output latency. Rejected/zero-frame renders retain
the prior core position; tick-free blocks retain it too. Configure, Start,
repeated Start and Stop invalidate it until another tick is consumed. The
bring-up fail-stop policy invalidates it on any fault. Pause retains the consumed
position and highlight; it never extrapolates a row. Continue retains the previous
consumed position until its first successful render consumes the replayed row.
Transport, position and
dimensions are copied together under the existing interrupt guard. No formatting,
serial writes, allocation or blocking are added to the audio callback.

HELLO/STATUS queue the existing BTTEST1 state reply and offer a fresh coherent
snapshot. After HELLO, periodic updates are offered every 50 ms (approximately
20 Hz) while CDC is connected. Command acknowledgements retain their separate
FIFO and priority. Telemetry holds at most one partial line and one latest
snapshot; unsent intermediate updates are replaced under backpressure. A partial
line finishes before another line starts, including command replies. Explicit
command application replaces unsent pre-command telemetry. Formatting is
main-loop-only in a checked 128-byte buffer, sufficient for maximum uint64 fields;
the existing 64-byte-per-service write limit remains. Disconnect discards TX
telemetry only; reconnect/HELLO never resets the player or queues START. Telemetry
does not select a physical clock, extrapolate playback or change MIDI scheduling.

Windows and ArkOS share strict parsing and display state. Malformed/overlong
lines do not change position or acknowledge commands. Disconnect, pending
START/restart/STOP and terminal errors clear the highlight. No valid telemetry
for **500 ms** hides the position as stale; the host never predicts the next row.
Fresh telemetry restores it. Legacy firmware remains controllable and displays
position as unavailable. The connected screen uses reported dimensions and
unknown-cell placeholders until the read-only snapshot below completes: the loaded
host preview tune is not the firmware fixture. Its fixed preview/edit cursor is
hidden in this device view, not moved
with playback; standalone/disconnected preview retains the existing layout and
cursor. UI refresh only renders snapshots and never advances playback.

### Read-only immutable pattern snapshot (version 1)

This opt-in exchange transfers the actual active pattern used for playback. It
does not edit/upload patterns, persist data, transfer PCM or define a BTM format.
The main loop references the same immutable `kPattern` and `kTempoHundredths`
used by the audio owner's configuration. It never reads mutable player state or
copies pattern data under interrupt exclusion; audio processing is unchanged.

Exact LF-terminated ASCII grammar, with single spaces and unsigned decimal fields:

```text
device -> host: BTPATTERN1 SNAPCAP 1
host -> device: BTPATTERN1 GET 1 <id>
device -> host: BTPATTERN1 BEGIN 1 <id> <rows> <channels> <tempo_hundredths> <count>
device -> host: BTPATTERN1 CELL 1 <id> <index> <note> <instrument>
device -> host: BTPATTERN1 END 1 <id> <count> <checksum>
device -> host: BTPATTERN1 SNAPERR 1 <id>
```

`SNAPCAP 1` is advertised after the BTTEST1 HELLO state reply. BTX requests a
snapshot only after this exact capability; ordinary firmware and older opt-in
firmware receive no unsupported requests. Each host instance uses increasing
nonzero uint32 transfer IDs across reconnects, rejecting exhaustion rather than
wrapping. A busy sender rejects a second GET without replacing the active transfer.
Malformed GETs produce SNAPERR with ID 0 when no valid identity can be extracted;
this is diagnostic rejection, not a BTTEST1 transport error.

Initial capacities are 16 rows, eight channels, 128 active cells and one transfer.
Rows/channels are positive within those bounds; tempo is nonzero uint32 in integer
hundredths of BPM. `count = rows * channels`. Each CELL is one chunk, in strict
row-major order, index 0 through count-1: row = index / channels, channel = index
% channels. Supported pattern notes are 24..127, NOTE_OFF (254) or NOTE_EMPTY (255); instruments are
raw 0..255, with 255 meaning no update. No instrument resolution is performed.
Fields cannot have signs, tabs, extra fields, numeric overflow or trailing spaces.
Snapshot lines use a 96-byte buffer including LF/NUL (sufficient for maximum
uint32 fields); GET fits the host's existing 32-byte TX buffer. CRLF is accepted
by line framing, but embedded CR/NUL and overlong lines are rejected.

Completion requires the exact identity, cell count and FNV-1a32 checksum. Start
at 2166136261; for each byte compute `(hash XOR byte) * 16777619` modulo 2^32.
Hash rows as one byte, channels as one byte, tempo as four little-endian bytes,
then every active raw note/instrument pair in transmitted row-major order.
Transfer identity is checked separately; the checksum is consistency validation,
not authentication. Host staging and committed snapshots use fixed storage;
only a valid END after all cells atomically publishes metadata/cells. Duplicates,
missing/out-of-order cells, invalid notes/dimensions, identity mismatches,
malformed messages and bad completion reject staging without altering transport,
position or previously committed data. Data outside an active request is ignored.

Both sides have a five-second total transfer deadline: host timing starts when
GET is queued; device timing starts when GET is accepted. No automatic retry
backlog is created. Timeout cancels staging; a partially transmitted device line
finishes intact before the sender stops. Host overlong input conservatively
cancels an active staging transfer. Disconnect clears all connection-scoped
device data and cancels transfer; reconnect discovers capability and requests a
fresh identity without START, Stop or player reconfiguration.

Transmission formats at most one cell/line at a time, retains one snapshot line,
and writes at most 64 available bytes per service. Command replies retain FIFO
priority; already partial lines always finish first. Snapshot and position lines
alternate when both are ready, so neither starves the other. Position updates
still coalesce; transfer lines never fill the command FIFO. Sustained command
traffic or USB backpressure can cause a transfer timeout, leaving controls usable
and cells unavailable rather than accumulating a queue.

BTX replaces placeholders only for a complete snapshot whose active dimensions
match reported position dimensions. It displays device tempo to one decimal while
retaining integer hundredths, and uses the existing note/instrument formatters.
OFF remains a note-off command; its second field displays timing while raw instrument IDs remain preserved in the snapshot/draft.
Live playback highlighting stays independent of read-only cells and the hidden
host edit cursor; standalone host preview remains unchanged. Windows/ArkOS share
assembly and rendering. The completed read-only opt-in scope has host tests and
the Windows BTX + Teensy 4.1 snapshot validation recorded above. Hardware
backpressure, timeout/recovery, legacy regression, ArkOS/MQS and realtime
measurement remain open; editing and publication are not implemented.

Remaining Windows BTX + Teensy 4.1 checks **are not covered by the validation above**:

- Exact physical row-boundary timing and restart phase remain unmeasured;
  logical position, snapshot contents/tempo/dimensions and visual display are
  confirmed above.
- Check pending START/restart and terminal-error highlight clearing under delayed
  communication; STOP invalidation and highlight removal are confirmed above.
- Pause/slow host service or apply CDC backpressure: stale positions disappear,
  fresh positions recover, acknowledgements remain ordered and audio continues.
- Exercise snapshot timeout/recovery on hardware: incomplete transfers must not
  expose partial cells or disrupt controls, and reconnect must obtain fresh data.
- Repeat legacy firmware and ArkOS checks. Hardware row/sample timing, MQS and
  Teensy execution cost remain open; the 2026-10-10 Windows opt-in validation
  does not complete full hardware validation.

Main-loop request submission, acknowledgement extraction and status copying use
short PRIMASK save/disable/restore sections with compiler memory barriers. The
single audio callback cannot interleave with these operations; `volatile` alone
is not used as synchronization. No serial/SD work happens under these guards or
in the audio callback. Both request and applied-acknowledgement FIFOs have eight
entries. Request overflow is explicitly rejected with `BTTEST1 ERROR request-overflow`.
Acknowledgement saturation leaves requests pending in order; the main-loop
16-line TX FIFO backpressures RX and acknowledgement extraction rather than
overwriting them. STATUS describes applied state, not pending intent. Hardware
interrupt/USB behavior under backpressure and worst-case execution time remain
unmeasured.

## Bring-up failure policy

A rejected render overwrites the entire allocated output block with zeros,
stops/resets the player, latches its error and publishes status for main-loop
reporting. Subsequent blocks stay silent until explicit successful START; there
is no repeated retry at a frozen position and no stale PCM transmission.

AudioStream allocation failure transmits no block, performs no rendering or
timeline advancement, stops/resets playback and latches Allocation with a
saturating counter. The verified USB shortage zero-fill patch remains in place;
missing-block behavior at the physical outputs still requires hardware validation.
Start/Stop requests are applied at the callback boundary before the allocation
result is handled, so a STARTED acknowledgement may be followed by an explicit
fault report if that same block fails. This deliberate fail-stop bring-up policy
does not define the final firmware underrun/recovery policy.

No sample allocation/freeing, SD loading, resampling, effects, MIDI changes,
generic instrument ABI or runtime PlaybackEngine integration is included.

### Shared informational panel

Playback/paused/stale status, local draft / NOT SENT TO TEENSY / dirty status,
pattern loading/unavailability and rows/channels/loop information use the existing
right-hand content panel in both Windows and ArkOS. Text wraps within the actual
workspace bounds, with at least 8 px internal padding and pixel clipping to the
content interior. Pattern cells, field cursor, playback highlight, OPT/INS/MIX
header and bottom channel-navigation frame remain intact. User visually confirmed
this preceding panel change on **2026-10-10**.

The subsequent layout update also places caller-supplied connection status
(waiting/idle/playing/paused/errors), host-specific transport hints and pending
STOP / STOP-then-EXIT messages in this shared panel. Connection and pending
transport information are drawn first, including before device connection.
Windows and ArkOS no longer draw a bottom overlay over pattern rows; transport
behavior and logging are unchanged. This new update has host rendering checks
and user confirmation on **2026-10-10** covers the final panel layout, `d1`
header and all other current behavior. User also confirmed numeric Enter STOP/reset
on **2026-10-10** after the mapping correction. Both Enter keys share modifier
handling (Ctrl/Alt/GUI suppress the action) and ignore OS-repeat events.
ArkOS and all outstanding hardware timing/performance checks remain open.

The row-numbering header is `d1` in device and standalone modes: decimal,
one-based displayed rows (01..16 for the fixture), with zero-based internal
indices. Planned zero-based and hexadecimal display options remain deferred;
no numbering-mode switch is implemented.

Instrument numbers are one-based decimal: raw ID 0 displays as `01`, raw ID 1
as `02`; `00` is never an editable/displayed instrument number. Empty remains
`--` (raw 0xFF). PgUp/PgDn from an empty instrument selects `01`; editing is
bounded, without wrapping. The minimum two-digit format expands to three digits
for 100..255 within the existing field width; there is no 100-instrument limit.
Internal IDs, bindings and snapshot/publication wire values remain raw and unchanged.
OFF timing glyphs remain separate. This display change is host-tested; Windows
hardware and ArkOS validation remain pending.

### Live playback-position styling

The shared Windows/ArkOS renderer now uses the prototype/standalone preview's
neutral dark grey current-row colour (RGB 30/30/30), without a bright edited-channel
background. The row-number column reuses the existing custom arrow glyph at
U+00A6 (BFM entry 166), positioned clear of the number and separator. Both the
row highlight and arrow follow the same authoritative reported row, including
fresh valid paused positions; stopped, stale, invalid or unavailable positions
hide both. The orange field-edit border remains independent.

Host rendering checks cover colour, exact arrow bitmap/placement and visibility.
User confirmed the grey playback-row and arrow styling on Windows on
**2026-10-10**. ArkOS styling validation remains open, as do unmeasured physical
timing/realtime performance and the other outstanding hardware checks. A bright
edited-channel background remains a deferred design option; transport and editing
are unchanged.

### Snapshot v2 OFF timing compatibility (2026-10-11 implementation)

New firmware advertises `BTPATTERN1 SNAPCAP 2` before the preserved `SNAPCAP 1`
line after HELLO. BTX requests the highest advertised version known when queuing
GET; ordinary firmware without snapshot capability receives no GET. Legacy hosts
request v1 and receive exactly v1 payloads. No version upgrade occurs mid-transfer.
A late v2 capability may leave the current transfer at compatible v1; reconnect
requests a fresh snapshot without transport actions.

```text
host -> device: BTPATTERN1 GET 2 <id>
device -> host: BTPATTERN1 BEGIN 2 <id> <rows> <channels> <tempo_hundredths> <count>
device -> host: BTPATTERN1 CELL 2 <id> <index> <note> <instrument> <timing>
device -> host: BTPATTERN1 END 2 <id> <count> <checksum>
device -> host: BTPATTERN1 SNAPERR 2 <id>
```

Timing is unsigned decimal `0` (Arrival, `¯`, row-start Stop) or `1`
(EndOfPosition, `_`, next-row-boundary Stop), meaningful only for NOTE_OFF.
All active timing values are validated; instruments remain raw IDs. V2 FNV-1a32
uses the same header bytes as v1, then note/instrument/timing byte triples in
ordered row-major cells. V1 retains its byte-pair checksum and missing timing
initializes to Arrival. Transfer identity and version must match the request;
invalid timing, mismatched version, missing/out-of-order cells or checksum errors
cancel staging without replacing the accepted snapshot. Existing 16x8 capacity,
96-byte line storage, five-second timeout and disconnect cancellation remain.
The sender reads the same immutable core pattern used to configure playback;
formatting/transmission stay outside the audio callback with existing mux priority.

The editor stores timing directly in the core draft. Both PgUp/PgDn toggle OFF
modes; instrument-only empty-note editing remains normal. Dirty comparison and
Restore include the actual received timing baseline, including EndOfPosition.
The read-only device view also displays the received timing glyph. Host tests
cover v2/legacy negotiation, roundtrip, checksum tampering, malformed/incomplete
atomic rejection and baseline/Restore behavior. User-confirmed Windows BTX +
Teensy 4.1 validation on **2026-10-11** covers pattern display, OFF timing toggling
(`¯` / `_`), Restore and unchanged audio. Windows validation of this read-only
snapshot/editor scope is complete. ArkOS validation, end-of-position hardware
timing and realtime performance checks remain open; local toggles are still
NOT SENT TO TEENSY and do not establish deferred Stop timing at physical outputs.

### Stopped-only pattern publication (host-tested; hardware pending)

This opt-in firmware foundation advertises a separate `BTPUBLISH1 CAP 1` after
HELLO. A future sender must discover this capability before using publication;
legacy firmware receives no new commands. Windows BTX now has explicit stopped-only F5 publication and confirmed-readback
rebasing, as described below. There is no automatic publication/retry; unsubmitted
local edits remain NOT SENT TO TEENSY. The boot fixture, tempo 12753, 44100 Hz, native-rate bindings and ordinary
firmware/USB trace paths are unchanged. Only an explicitly submitted replacement
can change the active pattern.

```text
device -> host: BTPUBLISH1 CAP 1
device -> host: BTPUBLISH1 NEXT 1 <next_id>
host -> device: BTPUBLISH1 BEGIN 1 <id> <rows> <channels> <count>
host -> device: BTPUBLISH1 CELL 1 <id> <index> <note> <instrument> <timing>
host -> device: BTPUBLISH1 COMMIT 1 <id> <count> <checksum>
device -> host: BTPUBLISH1 SUBMITTED 1 <id>
device -> host: BTPUBLISH1 APPLIED 1 <id>
device -> host: BTPUBLISH1 REJECTED 1 <id> <reason>
device -> host: BTPUBLISH1 ERROR 1 <id> <code>
```

Lines use exact single spaces and unsigned decimal uint32 fields, ending in LF
(optional trailing CR on input). No signs, extra fields, embedded NUL, overflow or
unsupported versions. ID is nonzero and strictly increases across HELLO/disconnect
sessions during one board boot: reuse is rejected, including after cancellation or
lost ACK. HELLO reports `BTPUBLISH1 NEXT 1 <next_id>` immediately after CAP 1;
STATUS also refreshes NEXT after its transport reply. Before any consumed BEGIN,
next_id is 1. It is the highest BEGIN identity accepted into staging plus one,
including subsequent cancellation, timeout, malformed-cell rejection, queue or
core rejection. Invalid BEGINs that never enter staging consume no identity.
Discovery does not apply a pattern or change transport and survives HELLO/disconnect.
A newly launched sender can use this discovery instead of assuming ID 1; it must
still handle BUSY and uncertain prior outcomes with authoritative readback.
After UINT32_MAX is consumed, NEXT explicitly reports 0 (exhausted, never a usable
ID), without wrapping. No further transaction can begin until an explicit reboot;
never automatically reboot or retry. HELLO reserves six bounded TX lines for the
state, existing capabilities and discovery. ERROR uses ID 0 when malformed input cannot
be attributed safely. BEGIN dimensions are 1..16 rows and 1..8 channels; count is
exactly rows*channels (maximum 128). Cells are ordered, zero-based row-major,
exactly once; note is 24..127, 254=OFF or 255=empty, instrument is raw 0..255,
timing is 0=Arrival / 1=EndOfPosition. Timing is separate from instruments and
meaningful only for OFF. Inactive storage defaults empty/Arrival.

Checksum is FNV-1a32, initial 2166136261, byte update `(hash XOR byte)*16777619`
modulo 2^32: rows byte, channels byte, then every note/instrument/timing triple in
row-major order. Tempo, sample rate, bindings, PCM and ID are not transferred.
COMMIT requires all cells and exact count/hash. Partial or rejected staging never
alters the active pattern. BEGIN/CELL have no success ACK; SUBMITTED confirms queue
acceptance only. ERROR codes: 2 malformed/unsupported/overlong, 3 transaction busy,
4 stale/invalid identity, 5 invalid dimensions/count, 6 invalid/duplicate/out-of-order
cell, 7 incomplete/count/checksum COMMIT, 8 timeout, 9 snapshot busy, 10 request queue
full, 11 no HELLO connection. REJECTED reasons are `playing-or-paused`,
`not-configured`, `missing-instrument`, `unknown-instrument`, `unsupported-pitch`,
`invalid-pattern`; `handoff-error` reports an internal synchronization inconsistency.
Rejection does not STOP or mutate the existing core configuration/pattern.

One fixed staging transaction has a five-second total deadline from valid BEGIN,
with no extension per cell. New HELLO or disconnect cancels only unsubmitted work.
Before COMMIT submission, main freezes staging; only a const pointer and identity
cross the existing interrupt-protected request FIFO. The audio owner calls
ReplacePattern at a block boundary in FIFO order with START/STOP/PAUSE/CONTINUE,
checking actual state rather than an earlier main-loop status. Playing
reject; no automatic STOP. Core validation reuses retained sample bindings and
two logical passes (up to 256 events / 4096 binding comparisons), with fixed storage,
no allocation/serial I/O. This adds bounded work but no measured realtime budget.
Successful replacement remains stopped at position zero with continuation, voices
and deferred Stops cleared, unless a later explicit transport request changes it.

Submitted storage stays immutable through timeout, disconnect and HELLO until the
audio-owner acknowledgement is extracted. A full applied FIFO delays processing
without dropping requests. Main copies successful staging to the snapshot source
outside interrupt exclusion, then releases storage and queues APPLIED. Rejection
leaves the old snapshot source intact. GET is rejected while publication is
receiving/submitted; publication is rejected while a snapshot (including a partial
line) remains active. Subsequent GET therefore describes the applied revision,
never a mixture or the original fixture after replacement.

ACKs retain the existing bounded 16-line priority queue; position and snapshot
lines alternate fairly and partial lines finish before switching lanes. RX handles
at most 128 bytes/one line per service, with 96-byte line storage; TX writes at most
64 bytes per service. Queue saturation backpressures RX. Publication has no dynamic
queue, formatting or I/O in audio processing. After SUBMITTED, a host timeout or
lost ACK is an **uncertain outcome**, not cancellation: do not resend COMMIT or
publish automatically. Reconnect/HELLO and authoritative GET readback resolve the
actual pattern once the pending transaction finishes; GET may report SNAPERR while
busy. No transaction status persistence across reboot is provided.

Portable tests cover complete application/readback with both OFF modes, malformed
and overflowing input, ordering/count/checksum failures, timeout/session cancellation,
frozen storage/duplicate COMMIT, actual transport/dimension rejection, retained old-pattern
playback after rejection, stopped-zero success, queue saturation, snapshot exclusion
and ACK/telemetry interleaving under partial writes. Hardware validation of publication
is pending for Windows + Teensy and ArkOS. Windows sending/rebasing is implemented
below; live edits, sample transfer and persistence remain unimplemented, with
physical timing/performance checks open.

### Windows F5 explicit publication (host-tested; hardware pending)

F5 publishes the current dirty draft only on an unmodified physical keydown
(Ctrl/Alt/GUI/Shift and OS-repeat events are ignored; lock-key state is harmless).
It requires connected CAP 1 firmware, fresh nonzero NEXT discovery, an accepted
snapshot v2 baseline, a ready changed draft, authoritative IDLE/Stopped (or Paused
with PAUSEDCAP 1), and no
pending transport, snapshot or publication. It never STOPs automatically.
Space START/PAUSE/CONTINUE is blocked during publication/verification; Enter and
numeric Enter STOP/reset, Ctrl+X and window close remain available. ArkOS shares
portable serialization/parsing/editor state but has no publication binding yet.

Submission copies fixed pattern metadata. BEGIN, row-major CELLs and COMMIT use
the documented FNV-1a32 and bounded 96-byte partial-write storage, one line at a
time; no UI blocking or protocol-line interleaving. An identity is consumed locally
once. STATUS refreshes NEXT before another attempt; if an unsent attempt consumed
a local identity the sender may choose a larger fresh ID than the reported minimum.
There is no automatic publication retry. Sending/application-wait/verification,
confirmed success, rejection reason and uncertain outcome appear in the right
information panel. Publication freezes note/instrument/OFF mutations and Restore,
and cancels held value repetition; row/channel/field navigation remains available.

SUBMITTED only confirms queue acceptance. Strict version/identity/phase parsing
ignores wrong, duplicate or malformed responses. APPLIED requests a fresh v2 GET;
only exact active dimensions, every raw cell and OFF timing, with unchanged tempo,
confirm success and rebase the local baseline. Cursor is retained. Rejection or
mismatched readback retains the draft and baseline; firmware remains the authority
for native instrument/pitch resolution. Initial bindings support instrument 0 at
raw note 60 (C-3) and instrument 1 at raw note 62 (D-3), both at 44100 Hz. Other
instrument/pitch combinations used by a pitched note are rejected by firmware;
instrument-only selection and
OFF preserve their existing semantics. No sample preview/resampling is added.

A six-second send/application deadline does not prove cancellation once a COMMIT
write may have reached the device. Timeout/disconnect then reports uncertain;
readback is required before another publication, never a resent COMMIT. APPLIED or
an uncertain connected outcome initiates one readback; a failed/busy/timeout GET
requires explicit F5 to request another authoritative readback only. Disconnect
still discards connection-scoped editor data. Reconnect requests a fresh snapshot,
without START/CONTINUE/publication, to resolve retained uncertainty. Partial output
lines finish before STOP switches lanes. STOP/exit cancels only an unsubmitted
transfer; submitted work and its uncertain outcome remain independent of STOP.
Clean exit after STOP may leave publication unresolved: read back on the next host
launch rather than assuming STOP cancelled it. No automatic retry or persistence.

Windows hardware checklist (not yet confirmed):

1. Connect using updated opt-in firmware and BTX; confirm v2 snapshot, CAP 1 and
   NEXT. While IDLE, change an OFF timing or supported note/instrument cell, then
   press F5. Check sending, SUBMITTED, APPLIED, exact readback and clean draft;
   cursor/BPM remain unchanged. Press F5 again while clean: no publication.
2. Publish an unsupported pitch/instrument: verify explicit rejection with dirty
   draft/baseline preserved. A later explicit F5 uses refreshed NEXT, not a retry.
3. While Playing or Paused without PAUSEDCAP 1, or while transport/snapshot work is pending, F5 must not
   send BEGIN or STOP. During sending/verifying, edits and F4 are frozen, navigation
   works, Space cannot start; Enter/close still request STOP safely.
4. Exercise partial writes/backpressure, timeout and disconnect around COMMIT;
   confirm uncertain indication, fresh readback and no automatic resend. Relaunch
   uses discovered NEXT and accepted pattern without reboot or auto START.
5. Start only after confirmed publication; verify OFF behavior/loop playback.
   Exact end-of-position hardware timing and realtime cost remain unmeasured.

No Teensy build/upload or hardware validation was performed for this change.

### Windows publication transport correction (hardware revalidation pending)

The 2026-10-11 Windows log shows CAP 1/NEXT discovery succeeded, followed by
completed BEGIN and CELL index 10, then a blank disconnect reason before COMMIT.
The last received message was stopped position telemetry. The Windows worker's
32-byte write limit rejected the longer CELL index 11 without setting an error.
This was host transport reopening, not a logged firmware rejection or ACK timeout;
NEXT advanced across HELLO sessions, supporting retained board state rather than reset.

Windows now accepts bounded write prefixes and the shared scheduler finishes each
line before sending another. Host regressions exercise the full 16-row/two-channel
transfer with asynchronous 32-byte writes, deletion apply/exact readback, and pitch
rejection retaining connection, draft and baseline. Existing protocol ERROR/REJECTED
handling does not disconnect; timeout and uncertain-COMMIT rules remain unchanged.
Revalidate F5 deletion/readback and C#3 unsupported-pitch rejection on Windows +
Teensy. No hardware success is inferred from these host tests.

### Current-row Continue revision (host-tested; hardware pending)

Continue now replays the reported paused row from its beginning, preserving its
absolute loop index; it no longer advances to the next row. With no consumed tick,
it starts at tick/sample zero. Pause retains logical instrument continuation but
silences voices and clears deferred Stops. Replayed notes retrigger; instrument-only
rows do not trigger, Arrival OFF stops at row start, and EndOfPosition OFF schedules
a fresh Stop at its next absolute row boundary. The cursor uses exact rational
conversion and bounded positioning; rejected continuation preserves state.

Existing ACK/telemetry barriers and authoritative BTX position remain unchanged.
Start and Enter STOP/reset are unchanged. The 2026-10-10 transport confirmation
covered the former next-row behavior, not this revision. Windows + Teensy and ArkOS
validation of current-row replay remain pending; physical timing/performance and
paused-publication follow-up remain open.

### Pause latency correction (hardware revalidation pending)

Log investigation found non-row-boundary PAUSED ticks (including 5332, 6670,
8052 and 9815), confirming no musical-row wait in audio-owner application. One
request displayed tick 10687 (row 15/loop 6) before PAUSE and received paused tick
10760 (row 0/loop 7): elapsed command/telemetry latency can cross a real boundary.
The old log lacks per-command timestamps; it cannot establish physical latency.

Windows now services queued input before rendering/presentation; shared serial
scheduling starts PAUSE/CONTINUE writes in that service. Transport finishes any
partial informational line but does not wait for STATUS acknowledgement. Late
STATE replies cannot override a pending transport request. PAUSED/CONTINUED ACKs
invalidate pre-command position freshness until the following coherent telemetry;
the host does not relabel old playing telemetry as the applied paused position.
Timestamped Windows transport input diagnostics support later measurement.

The audio owner already applies PAUSE before rendering the earliest available
block after receipt, preserving its actual last-consumed tick, silencing voices
and clearing deferred Stops. Continue still replays that same row. No row subtraction,
rewind compensation or host timing authority is introduced. Host tests cover
partial STATUS writes, late replies and pauses on either side of a row boundary.
Windows hardware latency/visual revalidation and timing/performance remain open.

### Core-only paused pattern replacement (host-tested)

`NativeRatePatternPlayer::ReplacePausedPattern` accepts only configured Paused
state and unchanged active dimensions. It retains tempo, rate and sample bindings,
validates the whole pattern and two logical passes, then rebuilds continuation
from the new pattern immediately before the paused row. Loop zero uses cleared
state plus preceding rows; later loops use one full pass plus preceding rows,
independent of elapsed loop count. No consumed tick means cleared continuation.
The paused row is not consumed, audio is not triggered and the cursor is not moved.

Success retains reported tick/loop and next sample, remains Paused/silent and clears
deferred Stops. Continue replays that row once using rebuilt selections, including
new OFF timing scheduling. Failure preserves all state and the original pattern.
Storage/work are fixed and bounded (at most 63 logical rows). Existing stopped-only
replacement and firmware BTPUBLISH1 behavior are unchanged. Paused F5 integration and capability negotiation are now implemented as described
below; hardware validation remains open. Playing publication
remains blocked. No hardware confirmation applies to this new core foundation.

### Capability-gated paused publication (observed run validated; remaining checks open)

HELLO now additionally advertises `BTPUBLISH1 PAUSEDCAP 1`. Existing `BTPUBLISH1 CAP 1`
remains unchanged and alone authorizes stopped-only host publication. Discovery
reserves seven queue slots for STATE, SNAPCAP 2/1, TRANSPORTCAP, CAP, PAUSEDCAP and
NEXT; telemetry remains coalesced outside that ACK queue. BEGIN/CELL/COMMIT, IDs,
checksums, frozen staging, timeout/uncertain outcomes and exact v2 readback are unchanged.
At audio-owner application, Stopped calls ReplacePattern, Paused calls
ReplacePausedPattern, and Playing rejects without mutation. Paused dimensions
must match; success stays silent/Paused at the same reported row/loop and next
sample. The main-loop readback source is synchronized before APPLIED.

Windows F5 now accepts Paused only with PAUSEDCAP 1; Playing remains blocked.
The capability is cleared on disconnect. Specific unmet-condition messages replace
the generic availability warning and clear when that condition changes. Rejection
and uncertain outcomes remain visible. STOP/exit stay available; publication never
starts or continues automatically. This section supersedes earlier stopped-only
integration limitations. ArkOS parsing is shared; handheld publication bindings
remain deferred.

User confirmation on 2026-10-11 validates the Windows pause-latency correction.
That confirmation does not validate paused publication or exact hardware timing.
Manual Windows checklist: pause in a later loop, edit a supported native-rate note,
F5 and verify readback/clean draft with unchanged PAUSED row/loop; Continue must replay
the replaced row once. Try unsupported pitch and verify connection/draft/baseline
remain intact. Verify Playing F5 is blocked, STOP/close still work, and older CAP-only
firmware permits only stopped F5. Hardware validation and realtime cost remain open.

### Pattern-derived pitched-note entry

On the first PgUp or PgDn in an empty Note field, search backward from the
preceding row in the current draft channel. Insert the nearest pitched note
unchanged, skipping empty, instrument-only and NOTE_OFF cells. With no preceding
pitch, initialize C-0. Never search another channel or wrap across the pattern
boundary. No instrument is inserted. Subsequent actions use normal direction,
cyclic bounds and held-key acceleration; existing pitched notes change immediately,
and OFF still initializes C-0. Lookup reflects draft edits/deletions and works
immediately after snapshot initialization, Restore and confirmed publication.
There is no session-local remembered pitch or extra publication metadata.

User confirmation on 2026-10-11: after clearing both channels, a published first-channel
pattern with C-3 / instrument 01 on rows 01 and 09 plays correctly. This confirms that
specific publication/playback case, not all paused-publication checks, hardware timing
or realtime performance. Pattern-derived entry hardware validation remains pending.
Tracker-style letter-key pitched entry on desktop/ArkOS attached keyboards is deferred;
key mappings and octave controls are not yet designed.

### Optional realtime cycle diagnostics (implementation only; results pending)

Use `teensy41_pattern_perf` (extends `teensy41_pattern`, adds
`BROTRACKER_PATTERN_PERF`) to enable DWT CYCCNT instrumentation. Ordinary pattern,
USB trace and legacy firmware retain their existing behavior. No firmware build or
hardware measurement was performed for this change.

The first memory-ready callback, including one-time player initialization, records
its cost separately. Subsequent callbacks track count, maximum elapsed cycles and
counts strictly exceeding the floored block budget derived from F_CPU_ACTUAL,
AUDIO_BLOCK_SAMPLES and AUDIO_SAMPLE_RATE_EXACT. Unsigned subtraction handles a
counter wrap provided a callback takes less than one full 32-bit counter period.
Counts saturate rather than wrap. Pre-memory-ready callbacks are excluded.

The measured span includes allocation/failure handling, audio-owner transport and
pattern replacement, rendering, transmit and release. Counter reads and instrumentation
branches add overhead; post-span budget calculation/counter publication and the final
function return are excluded. Higher-priority interrupt preemption contributes elapsed
cycles; these are elapsed callback costs, not isolated player instruction counts or
proof of end-to-end latency. Frequency changes within a callback are not modeled.

The existing interrupt-protected snapshot carries coherent counters to the main loop.
At most once per second while connected it may emit this LF-terminated line:

```text
BTPATTERN1 PERF 1 callbacks steady_callbacks initialization_cycles maximum_cycles over_budget budget_cycles
```

All six fields are unsigned 32-bit decimal values; maximum_cycles/over_budget refer
only to steady callbacks. Formatting uses a fixed 96-byte buffer (including worst-case
values). BTX logs the line without UI changes. Reports are skipped when ACKs, partial
lines, snapshot transfer, pending serial input or insufficient TX capacity exist;
there is no diagnostic backlog. Playback ownership and clock/sync policy are unchanged.

Hardware checklist: build/upload the optional environment manually, retain the log
and record CPU/sample-rate/block configuration. Observe stopped silence, then playing
loops and Continue/retrigger. Pause, publish a supported replacement while Paused,
then Continue; compare maxima/over-budget counters with idle operation. Exercise STOP
and reconnect under serial load and verify acknowledgements remain prompt. Record
initialization cost separately. The observed results below cover one run; broader
timing/performance validation remains open.

User confirmation on 2026-10-11: pattern-derived note entry works on Windows,
including after loading an existing pattern. ArkOS entry validation remains open.

### 2026-10-11 Windows BTX + Teensy hardware log evidence

The supplied terminal log contains **99 PERF reports**. The final report records
**40362 callbacks / 40361 steady callbacks**, **2249 initialization cycles**, a
**1741496-cycle budget** and **zero recorded overruns**. Observed cumulative steady
maxima were **1457 cycles** while stopped before playback, **42354 cycles** during
playback before publication, and **90158 cycles** overall (**5.18%** of budget).
The overall maximum first appeared in reporting after publication; the cumulative
counter does not identify its originating callback, so it must not be attributed
exclusively to pattern replacement.

Paused publication received SUBMITTED and APPLIED, followed by exact **32-cell v2
readback**. Checksums were **176805769** initially and **391983933** after replacement.
The paused position stayed at **tick 28380, zero-based row 7, loop 18** through
publication. Continue resumed within that same row; STOP invalidated/reset position.
No protocol errors or disconnects occurred in the supplied log. These are log
observations, not a new claim about physical output phase or timing accuracy.

Preserve the original evidence at
`C:\Users\smartin\AppData\Roaming\BroTracker\BroTracker Terminal\brotracker-terminal.log`
outside disposable build/review exports; archive it before another terminal run
can overwrite it. This documentation update does not move or modify the log.
Full audio-graph cost, instrumentation overhead, stress testing, unsupported-pitch
rejection, reconnect testing and ArkOS validation remain open. Earlier pending-result
statements describe implementation-time status; this run supplies initial hardware
results without completing the broader validation checklist.

### Portable WAV-to-buffer preparation (host-tested; SD integration pending)

`LoadWavToBuffer` accepts a non-owning reader (size, absolute seek, bounded read),
caller writable PCM storage and frame capacity. It supports signed PCM16 mono,
44100 Hz only and explicitly decodes little-endian values. A complete container
scan validates RIFF/WAVE extents, unique fmt/data, PCM metadata/alignment/byte rate,
unknown chunks/odd-byte padding, nonempty complete frames and capacity before PCM
copy. Chunk additions are bounded by the checked container end; trailing bytes
outside RIFF are ignored. fmt is 16-byte PCM or has a consistent extension length.
Short reads/seek/size failures and unsupported formats return explicit statuses.

Success returns a non-owning Pcm16MonoView. All failures preserve output metadata;
I/O failure during copying may modify part of the destination. Callers must load
into unpublished storage with no active playback views and guarantee extents,
lifetimes and non-overlap. This fixed-scratch, allocation-free operation scales
with file/data size and must never run in an audio callback. Reader implementations
must likewise avoid allocation if required. No Arduino/SD/audio dependency is added.

Host fixtures validate decoding, padding, capacities, malformed/overflowing chunk
sizes, truncation, unsupported formats and partial-copy I/O failures. Teensy SD
adapter/preparation integration and hardware validation remain open. Existing firmware
WAV loaders and playback environments are unchanged. Retain the diagnostic beepers
and the deferred extra PCM channel using test.wav/test2.wav/test3.wav, with actual
frame counts, rates and intended meter checked before deriving duration-based BPM.
No new samples, pattern changes, pitch interpretation or resampling are introduced.

### Teensy SD WAV buffer adapter (host-tested; hardware pending)

`SdWavBufferReader` implements the portable reader through the existing `SdReader`.
`LoadWavBufferFromSd(path, pcm, frame_capacity, output_view)` returns explicit
Success/InvalidPath/OpenFailed/Directory/LoadFailed status; portable load_status is
meaningful for Success/LoadFailed. Null/empty paths, failed opens and directories
are rejected. SD initialization remains the caller's responsibility.

RAII holds the D0046 shared SD read lock throughout container validation and PCM
copy, closes the file and releases ownership on every exit. Seek checks uint64
before narrowing to uint32; negative reads become zero/error rather than unsigned
large counts. There is no direct SD.open bypass, PCM ownership allocation or logging.
The SD library may allocate internally, so caller-buffer loading is not evidence
that the complete SD stack is allocation-free. Never use this helper in audio processing.

Portable metadata/partial-destination contracts remain unchanged: unpublished PCM
storage must have no active playback views, and callers guarantee extents, lifetime
and non-overlap. All failures preserve metadata; a later copying I/O failure may
modify an already copied prefix. Host tests compile the actual adapter/SdReader/shared
guard against an isolated fake SD API, covering exact PCM, open/directory/short/negative
read/seek failures, oversized seek rejection and writer exclusion/file/lock release.
They do not compile against or validate actual Teensy SD headers/drivers; no Teensy
build/upload was performed. Boot preparation, RAM placement, sample binding integration
and hardware validation remain pending. Existing WAV APIs, firmware environments,
boot pattern and deferred test.wav/test2.wav/test3.wav plans are unchanged.

### Opt-in boot SD preparation (success observed; remaining hardware checks open)

teensy41_pattern_sd_prepare extends pattern_perf with BROTRACKER_PATTERN_SD_PREPARE.
Only this environment reserves a static, 32-byte-aligned DMAMEM buffer of 131072
PCM16 frames (256 KiB). Built-in SD initialization and Samples/test2.wav loading
finish before g_audio_ready. Early callbacks return without reading preparation
data. Only successful loading publishes a valid immutable view. Missing SD/file,
unsupported format, capacity and I/O failures retain invalid metadata and status.
No retries or storage mutation occur after activation; beeper pattern/bindings
and stopped boot remain unchanged. No third-channel sample playback is added.

Main-loop retained results are reported after HELLO via a fixed 160-byte partial
line, behind acknowledgements and existing partial lines. Disconnect clears pending
TX but not the preparation evidence; reconnect does not reload the sample.

```text
BTPATTERN1 WAVPREP 1 attempted sd_ready sd_status load_status frames rate capacity address
```

All fields are unsigned decimal; address is uintptr_t and capacity is frames.
Statuses follow SdWavBufferStatus/WavBufferStatus; load_status is meaningful for
Success/LoadFailed. Failed SD initialization reports sd_ready=0, OpenFailed/IoError.
No formatting, logging or SD I/O occurs in audio processing.

No test2.wav was available in the implementation-time repository inspection. The
supplied hardware evidence below now establishes its duration; intended meter/bar
length remains unconfirmed, and duration does not establish musical pitch. The buffer persists for the entire boot,
including failure, reserving half the nominal 512 KiB RAM2. Remaining memory must
support other buffers, SD internals/heap and player/stack/code requirements.
The reported address below confirms RAM2 placement/alignment; total RAM/heap
headroom and SD/cache behavior still require board validation.
Per TEENSY_MEMORY_ARCHITECTURE, optional PSRAM remains open: caller storage keeps
WAV parsing/playback independent of placement. No PSRAM detection/allocation is
implemented; this 256 KiB reservation is not a permanent loader limit.

Hardware checklist: manually build/upload the new environment; inspect linker map
and reported address/capacity. With valid test2.wav, check successful metadata,
silent stopped boot and unchanged beeper playback. Try missing card/file,
unsupported and oversized WAVs; verify failure metadata and working controls.
Reconnect and verify retained reporting without reload. Validate RAM/cache/SD
headroom before future sample binding and extra-channel integration. Host tests
use fake SD headers; no actual Teensy compilation/hardware result is claimed.

### 2026-10-11 SD preparation hardware evidence

User-supplied evidence records successful **teensy41_pattern_sd_prepare build/upload**
and **WAVPREP success** for Samples/test2.wav: **68284 frames**, **44100 Hz**,
**136568 PCM bytes**, duration **1.548390 seconds**. Buffer address **0x20200820**
confirms RAM2 placement and **32-byte alignment**. If interpreted as one 4/4 bar,
that duration implies approximately **155 BPM**; intended meter and bar length
still require confirmation. No reference pitch is inferred from duration.

The log shows **IDLE, STARTED, STOPPED and clean shutdown**, with no protocol errors.
**Ten PERF reports** record maximum **41843 cycles / 1741496-cycle budget (2.40%)**
and **zero overruns**. This still measures the existing beeper playback; the loaded
WAV is not bound to playback and these counters do not validate sample playback.

Total RAM/heap headroom, missing/unsupported/oversized/I/O failure-path hardware tests,
reconnect result reporting, sample binding/playback and cache behavior remain open.
Retain the supplied original log/build evidence outside disposable build exports;
this documentation update does not move, replace or delete it. Initial preparation
success does not complete the broader hardware checklist or constrain future PSRAM
storage choices.

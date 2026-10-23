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

The primary development firmware environment is `teensy41_dev`, the default
in `platformio.ini`. It inherits the complete `teensy41_pattern_variable_rate`
configuration, including SD sample playback and PERF diagnostics, without changing
firmware behavior. From the repository root in PowerShell, build and upload it:

```powershell
pio run -t upload
```

Launch the existing Windows client:

```powershell
.\deploy\windows\BroTrackerTerminal.exe
```

Upload includes building. For an explicit environment, use
`pio run -e <environment> -t upload`; all existing names remain usable for
regression/diagnostic testing. The ordinary legacy `teensy41` and diagnostic
`teensy41_usb_trace` configurations are unchanged.
`teensy41_pattern` inherits the ordinary USB audio preparation script, including
the guarded zero-fill and unused-RX ownership corrections. It does not enable
USB trace instrumentation. The installed framework is not modified.

## Windows BTX manual checks

### Temporary local editor controls

After a complete validated device snapshot arrives, BTX creates a separate local
draft. The device snapshot remains read-only. The orange field border is the edit
cursor; the grey row highlight/arrow remains the reported playback position.
With preview capability, edits automatically request volatile working playback;
the panel reports synchronization and unpublished dirty state. Legacy firmware
keeps **NOT SENT TO TEENSY** until explicit publication. Device BPM/POS/LOOP remain
authoritative.

- Up/Down move previous/next row within active dimensions. Right moves Note ->
  second field -> next channel's Note; Left reverses this sequence. Horizontal
  arrows stop at the outermost fields without wrapping and preserve the row.
  Tab/Shift+Tab jump to the next/previous active channel's Note field, wrapping.
- Page Up/Down increment/decrement the selected numeric value. An empty Note field
  inserts the nearest preceding pitched note from this draft channel unchanged,
  or C-3 (raw 60) if none exists.
  Logical stepping cycles G-8 -> OFF -> C-0 on increase and
  C-0 -> OFF -> G-8 on decrease; other pitches step by one semitone.
  Empty instrument fields initialize to raw ID 0 (displayed 01); numeric instruments cycle 99 -> -- -> 01 and 01 -> -- -> 99.
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
- F4 restores the last published baseline, retaining the cursor. With preview
  capability it also sends a volatile working preview; it does not publish.
- Space uses the capability-gated transport described below; Return and numeric Enter STOP/reset,
  Ctrl+X and window close remain independent of editing. OS repeats are ignored.
  Arrows and Tab/Shift+Tab use shared application-controlled navigation repeat:
  immediate action, first repeat after 350 ms, then 100 ms intervals before
  1000 ms held, 60 ms from 1000 ms and 40 ms from 2000 ms. At most one action
  occurs per service call, without catch-up. Modifier changes and opposing arrow
  directions cancel until release; releasing an older key preserves a newer hold.
  Navigation and PgUp/PgDn share one stream. Navigation remains available during
  publication locking; value mutation remains blocked. Focus loss, disconnect
  and exit cancel holds without automatic recovery/rearming. The user confirmed shared navigation repeat on Windows and ArkOS on
  2026-10-15; this does not validate the later fixture/load-display changes.
  PgUp/PgDn alone repeat under application
  control: immediate action, first repeat at 400 ms, then every 100 ms, accelerating
  to 50 ms after 1000 ms held and 25 ms after 2000 ms. Each action changes one value
  or timing mode; stalled frames perform at most one repeat, without catch-up.
  Reaching a pitched-note endpoint in the held direction resets acceleration:
  wait a fresh 400 ms before entering OFF; entering OFF renews that pause before
  proceeding to the opposite pitched endpoint.
  A new physical press at an endpoint enters OFF immediately on keydown; if that press
  becomes a hold, its first repeat waits 400 ms. Keydown cannot distinguish a tap
  from a future hold, so this immediate-press rule also applies when beginning a
  hold at an endpoint.
  Release, focus loss, disconnect/unavailability, navigation, Restore/Clear/OFF
  and incompatible modifiers cancel repeat; the opposite key replaces it. Held
  edits never resume after reconnect. Shift+Tab is the supported modified editor
  key. No piano keyboard mapping is provided.

NOTE_OFF timing is now core `NoteOffTiming` metadata copied from the validated
read-only device snapshot into the draft, separate from instrument IDs. Version 2
can carry EndOfPosition; legacy version 1 defaults to Arrival. With explicit
BTPREVIEW1 capability, local changes now automatically update volatile working
playback, subject to device validation and synchronization status. Legacy firmware
retains **NOT SENT TO TEENSY** and explicit F5 publication. F5 is stopped/paused
only where supported; Playing publication remains blocked. No SD persistence.
The boot fixture is unchanged; only accepted previews change working contents.

Dirty state compares active raw cells and OFF timing with the baseline; restoring
original values clears it. Telemetry never moves the edit cursor or overwrites edits.
Disconnect disables editing and clears the connection-scoped editor. Preview-capable
sessions retain a bounded recovery copy of local edits until authoritative readback;
it cannot be sent before recovery. Reconnect
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
   The default immutable 32-row, two-channel pattern loops at 127.53 BPM, approximately
   every 3.76 seconds. It includes separate triggers, adjacent-row retriggers,
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
now uses the temporary bindings documented below: Start invokes this same
capability-gated toggle, B requests Stop/reset, and L1+Start uses clean STOP/exit.

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
support the full uint64 range. Active dimensions must be 1..64 rows and 1..8
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

Current capacities are 64 rows, eight channels, 512 active cells and one transfer.
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

Row-numbering defaults to `d1` (decimal one-based). Host startup config now accepts
`[display] row_numbering=d1/d0/h1/h0`, including decimal/hex zero-based labels and
an accurate mode indicator. Standalone previews default to d1. This changes only
presentation; internal indices remain zero-based. No runtime mode selector.

Instrument numbers are one-based decimal: raw ID 0 displays as `01`, raw ID 1
as `02`; `00` is never an editable/displayed instrument number. Empty remains
`--` (raw 0xFF). PgUp/PgDn from an empty instrument selects `01`; editing is
limited to 01..99 with cyclic stepping through --. Received higher IDs are preserved;
numeric stepping rejects them instead of clamping. The minimum two-digit format expands to three digits
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
timing and realtime performance checks remain open. That historical validation
covered local-only toggles, not later automatic preview or physical Stop timing.

### Stopped-only pattern publication (host-tested; hardware pending)

This opt-in firmware foundation advertises a separate `BTPUBLISH1 CAP 1` after
HELLO. A future sender must discover this capability before using publication;
legacy firmware receives no new commands. Windows BTX now has explicit stopped-only F5 publication and confirmed-readback
rebasing, as described below. There is no automatic publication/retry; automatic
volatile preview is separate and never changes the published baseline. Legacy
local edits remain NOT SENT TO TEENSY until explicit publication. The boot fixture,
tempo 12753, 44100 Hz, native-rate bindings and ordinary firmware/USB trace paths
are unchanged. Validated working previews may change volatile playback contents.

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
be attributed safely. BEGIN dimensions are 1..64 rows and 1..8 channels; count is
exactly rows*channels (maximum 512). Cells are ordered, zero-based row-major,
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
pitch, initialize C-3 (raw 60). Never search another channel or wrap across the pattern
boundary. No instrument is inserted. Subsequent actions use normal direction,
cyclic bounds and held-key acceleration; existing pitched notes change immediately,
and OFF proceeds to C-0 on increase or G-8 on decrease. Lookup reflects draft edits/deletions and works
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

### Main-app startup and unavailable pattern presentation

Main Windows/ArkOS BTX no longer renders or presents a startup mockup. Missing
connection uses the normal layout with unavailable pattern/position indicators,
without dummy notes, fixed playback position or fake edit cursor. Live snapshots,
drafts, highlight/arrow, edit cursor and padded information panel remain unchanged.
RenderPreviewScreen is an explicit standalone-only preview API used by
 tools/ui_preview/main.cpp; its assets and BMP export remain available.
User confirmed Windows startup and reconnect visual behavior on 2026-10-11
after mockup fallback removal. ArkOS visual validation remains pending.
Transport, publication, audio and firmware behavior are unchanged; outstanding
physical timing and realtime performance checks remain open.

A dedicated disconnected-device presentation replacing the empty pattern layout
is deferred, using M8 Headless as a visual reference. No UI implementation is
included in this documentation change.


### Opt-in prepared SD sample playback (partial Windows hardware validation)

`teensy41_pattern_sd_playback` extends `teensy41_pattern_sd_prepare` with
`BROTRACKER_PATTERN_SD_PLAYBACK`. Before audio activation, successful preparation
selects three active channels and a third binding: internal instrument 2 (display
03), native-rate note 60/C-3, with one row-01 trigger. The two beeper instruments
and all their events are unchanged. C-3 is an explicit trigger reference, not a
measured musical pitch. No resampling, pitch analysis or tempo matching occurs.
Failure retains the two-channel beeper pattern without binding 03, and WAVPREP
still reports the failure. Existing environments retain their behavior.

PCM stays immutable in the existing caller-owned 256 KiB aligned RAM2 buffer;
selection copies only fixed metadata before g_audio_ready. The audio owner uses
that selected pattern/bindings; snapshots, telemetry and publication validation
therefore describe the actual configuration. No audio-time SD/loading/logging.
Future optional PSRAM-backed caller storage needs no parser/playback API changes.
Stopped/paused publication remains available; Playing publication remains blocked.

The observed test2.wav duration is 68284/44100 = 1.548390 seconds. The current
32-row loop at 127.53 BPM spans eight quarter notes, approximately 3.763820 seconds
(165984 samples for the first loop). The WAV completes before its next loop trigger;
this does not establish intended meter or sample pitch. Unity-gain mono summation
may saturate when the PCM overlaps beepers; final volume policy remains undecided.
WAVPREP and PERF remain enabled; prior beeper measurements do not validate WAV cost.

Manual checklist: verify successful WAVPREP and 32-row/three-channel readback;
start, hear sample completion and loop retrigger; Stop, Pause/current-row Continue;
try stopped and paused publication (Playing must reject); test preparation failure
fallback; inspect PERF, RAM headroom and cache behavior. These remaining hardware
checks and ArkOS validation remain open.

User listening validation on **2026-10-11** confirms that the SD sample audibly
plays alongside the beepers. Louder PCM masks the quiet beepers; removing the
channel-3 note confirms they still play clearly. This establishes audible
coexistence, not measured timing, absence of clipping or completion of the
remaining hardware checklist.

### Display grid versus active pattern dimensions

Main Windows/ArkOS BTX draws 8 numbered channels and 32 decimal one-based rows
(d1). Each cell is drawn once: actual device/draft data inside active dimensions,
otherwise `--- --` in existing empty styling. Unavailable data also uses empty
padding, with availability explained in the right panel. A 16-row/two-channel
pattern remains exactly that size: padding cannot be edited, published or played.
Live highlight/arrow still use only authoritative position; edit navigation stays
within active dimensions. The standalone mockup and right panel are unchanged.
This supports varying loaded contents without adding new-pattern creation or
incremental rendering. User confirmed on **2026-10-11** that Windows displays all
8 channels and 32 rows with actual events or empty placeholders. ArkOS visual
validation remains pending.

Deferred: define channel/instrument gain controls and the mixing headroom policy.
For actual muted channels, consider an X formed by two diagonal lines inside the
channel header, with 16 px padding where geometry permits. Neither gains nor the
mute-marker UI are changed by this documentation update.


### Volatile working-pattern core foundation (integration pending)

Agreed eventual WYSIWYG behavior: accepted local edits automatically update the
working playback pattern; F4 restores the last published baseline in both editor
and playback; F5 promotes the accepted draft to that baseline. Playing publication
remains blocked. Volatile preview is a separate operation allowed while Playing,
and does not imply SD persistence. This step implements only portable core support;
firmware protocol and shared Windows automatic host transfer/F4/F5 coordination
are now implemented in subsequent steps below. Hardware validation remains pending;
earlier validation records do not establish this new model as validated.

`NativeRatePatternPlayer::ReplaceWorkingPattern` accepts only unchanged active
dimensions and validates all active notes/OFF timing plus native-rate resolution
through the initial pass and next-loop continuation. Stopped and Paused use their
existing replacement semantics. Playing copies one pending candidate, superseded
by a newer accepted candidate, and applies before the next unconsumed row-start
(including tick zero). Rejected candidates/renders preserve all state/output and
pending work; a failed render can be retried without consuming the application.

At application, reconstruct logical continuation from the NEW pattern immediately
before that absolute row: cleared-state prefix in loop zero; one complete pass plus
prefix in later loops. No elapsed-loop scan, old events replay or reconstruction
audio occurs. Keep timeline/loop and sounding voices. Deleting a note is not an
implicit Stop. Old EndOfPosition Stops due at the boundary execute before new row
commands, so new triggers remain audible; discard remaining old deferred audio work.
New OFF events use the existing exact musical boundary scheduling.

Pause silences voices/clears deferred Stops but retains a pending candidate for
Continue's current-row replay boundary. Stop/Start adopt a pending candidate while
resetting playback; successful Configure or published replacement supersedes it.
A paused working replacement instead uses existing paused reconstruction immediately.
The player owns no separate published baseline: later integration must manage it.
Storage adds one fixed pattern candidate; acceptance validates at most 32 logical
rows, application reconstructs at most 31 once per Render, with existing 128-frame,
512-tick and 16-command limits unchanged. No allocation, I/O, locks or new clock.
Host tests cover boundary/loop ordering, simultaneous triggers, deletion, instrument
continuation, both OFF modes, Pause/Continue/Stop and transactional capacity retry.
These are not Teensy performance or hardware results.


### Device volatile preview protocol (host/hardware integration pending)

Only opt-in pattern firmware advertises `BTPREVIEW1 CAP 1` on HELLO. Clients must
wait for that capability; Windows BTX now sends automatic previews only when it
is advertised. HELLO
and STATUS also return `BTPREVIEW1 NEXT 1 <next_id>` and
`BTPREVIEW1 STATE 1 <published_id> <working_id> <pending_id>`.
All integers are strict unsigned decimal, no signs/overflow/extra fields.
IDs are uint32; NEXT=0 means exhausted until reboot. Preview and publication
have separate identity domains. Published ID zero denotes the boot baseline;
working ID zero denotes that published revision, otherwise an applied preview ID.
Pending ID zero means none. STATE comes from the coherent audio-owner snapshot.

Complete-pattern upload, LF-terminated (optional CR before LF):

```text
BTPREVIEW1 BEGIN 1 <id> <rows> <channels> <cell_count>
BTPREVIEW1 CELL 1 <id> <index> <note> <instrument> <off_timing>
BTPREVIEW1 COMMIT 1 <id> <cell_count> <checksum>
```

Dimensions must match the working pattern at audio-owner validation. Capacity is
64 rows x 8 channels; cells are strictly ordered row-major indices 0..count-1.
Notes preserve 24..127/254 OFF/255 empty; instruments preserve raw 0..254/255
no-update. Timing is 0 Arrival or 1 EndOfPosition, separate from instrument IDs.
Checksum is FNV-1a32: rows byte, channels byte, then note/instrument/timing bytes
for every ordered cell (same publication hash). Tempo/rate/bindings stay unchanged.
One preview OR publication transfer at a time; snapshot TX also excludes uploads.
Receiving/ready staging times out after 5000 ms, using unsigned elapsed arithmetic.
IDs increase from the highest staged BEGIN, including cancelled/rejected work;
HELLO/disconnect cancel only unsubmitted staging and never reset the high-water mark.

Replies distinguish each phase:

```text
BTPREVIEW1 RECEIVING 1 <id>
BTPREVIEW1 SUBMITTED 1 <id>
BTPREVIEW1 ACCEPTED 1 <id>
BTPREVIEW1 APPLIED 1 <id>
BTPREVIEW1 SUPERSEDED 1 <id>
BTPREVIEW1 REJECTED 1 <id> <reason>
BTPREVIEW1 ERROR 1 <id> <code>
```

RECEIVING acknowledges BEGIN; SUBMITTED acknowledges frozen handoff, not validation.
ACCEPTED means core validation succeeded and copied the candidate. Playing may
remain pending until a row-start; only an actual change of working metadata emits
APPLIED. A newer accepted candidate or successful publication emits SUPERSEDED
for the old pending ID. Invalid candidates preserve pending/applied metadata.
REJECTED reasons use the publication compatibility reasons; ERROR numeric codes
reuse PublicationStatus (malformed/busy/identity/dimensions/cell/completion/timeout/
snapshot-busy/queue-full/not-connected). Neither rejection disconnects transport.
Duplicate COMMIT never reapplies; frozen transactions reject further transfer lines.

Authoritative readback (explicit selector 0 applied, 1 pending):

```text
BTPREVIEW1 GET 1 <read_id> <selector>
BTPREVIEW1 READ 1 <read_id> <selector> <published_id> <revision_id>
BTPREVIEW1 BEGIN 2 <read_id> <rows> <channels> <tempo_hundredths> <cell_count>
BTPREVIEW1 CELL 2 <read_id> <index> <note> <instrument> <off_timing>
BTPREVIEW1 END 2 <read_id> <cell_count> <checksum>
```

Read IDs must be nonzero; they are correlation IDs, not consumed preview identities.
Readback uses the exact snapshot-v2 checksum including tempo and OFF metadata.
`BTPREVIEW1 READERR 1` rejects invalid/busy/not-yet-synchronized requests or absent
pending data. Retry readback explicitly after STATUS; never blindly resend COMMIT.
Existing BTPATTERN1 GET continues to return the last published baseline, not preview.
A frozen per-readback main-loop copy prevents revisions mixing if playback applies
a candidate mid-transfer. Validate READ identity/revision and complete END before
using cells. An outcome timeout after submission is uncertainty, never cancellation.
After reconnect, discover STATE/NEXT and read applied/pending contents to resolve it;
no automatic START, baseline restore or resend occurs.

Main loop owns two fixed transaction staging objects, applied/pending metadata and
one frozen readback source. Only one transfer can freeze at a time. Audio exclusively
validates/replaces the player; staging stays immutable until its acceptance/rejection
ack is consumed. ACCEPTED releases transfer storage after copying pending metadata;
APPLIED copies that accepted metadata to working readback. Successful stopped/paused
publication updates published and working sources before its APPLIED response and
supersedes pending preview. Playing publication remains rejected. Preview follows
core Pause/Continue and Stop/Start semantics, without clock/sync changes or persistence.

RX is bounded to 128 bytes/one complete line per main-loop service; TX to 64 bytes,
16 fixed lines, and unchanged complete-line arbitration with ACK priority and
alternating telemetry/readback. Discovery now reserves 12 lines (including preview CAP/NEXT/STATE
and gain CAP/STATE). Formatting buffers cover uint32 extrema. The audio ACK FIFO retains
ordered lifecycle events; requests wait for sufficient space, and one completion
notification slot prevents loss if rendering finishes under ACK backpressure.
No formatting, serial I/O, allocation or blocking occurs in audio processing.

Portable tests cover strict identities/order/checksum/timing, timeout/cancellation,
frozen ownership, actual boundary application, supersession, reconnect retention,
Pause/Continue/Stop, publication exclusion/rejection and partial readback writes
interleaved with telemetry. Teensy compilation/hardware behavior remains unvalidated;
Windows sending/F4 restore/F5 promotion is implemented below, with hardware pending.


### Windows automatic volatile preview (host-tested; hardware pending)

Shared `HostPatternPreview` coordinates complete drafts with BTPREVIEW1 CAP/NEXT,
strict identity/version/phase parsing and exact v2 working/pending readback. Windows
uses it automatically for accepted local edits while Stopped, Paused or Playing;
Playing application is still exclusively the device's next-row-boundary decision.
ACCEPTED means staged, never synchronized playback. The panel distinguishes pending
transfer, device validation, waiting for application, readback verification,
synchronized state and rejection. Preview-capable drafts show UNPUBLISHED relative
to the published baseline, rather than unconditional NOT SENT TO TEENSY.
Unsupported notes/instruments stay visible with the rejection reason and no
automatic retry of that same revision. Editing remains responsive during preview;
playback highlighting and transport retain their authoritative device semantics.

Fixed storage keeps immutable in-flight bytes separate from one coalesced latest
desired draft, accepted state and bounded readback staging. Debounce is 75 ms after
a change, capped at 250 ms from the first edit in a continuous burst (when the link
is eligible). Held edits cannot postpone transfer indefinitely; backpressure and
in-flight work may add latency. Serial work remains bounded, with partial writes
and complete-line framing. Transport outranks the next preview line; a partially
written line finishes first. Informational STATUS never delays PAUSE until its reply.

F4 restores the published baseline locally and queues it as volatile preview,
without publishing. During an in-flight preview, the restored revision remains the
latest goal and is sent after that frozen transfer; an older draft cannot become
the final requested working state. F5 freezes its selected revision and locks edit
mutations/Restore while allowing navigation. It waits for existing preview/recovery
work, suppresses later preview sending, then publishes only while Stopped or
capability-supported Paused. Exact published v2 readback is required before baseline
rebase and dirty-state clearing. STOP/exit stay available; submitted replacements
are not cancelled by STOP. Start/Pause/Continue are blocked during publication.
Legacy peers retain explicit F5-only behavior and receive no preview requests.

Submitted timeout or disconnect is uncertainty. Recover published baseline through
its normal GET, then STATE and exact applied/pending GET readback before selecting
another revision. Never resend COMMIT, START or baseline restore automatically.
The editor is disabled/cleared on disconnect; a private fixed recovery copy retains
local edits. Reconnect reconstructs against the verified published baseline and
verified working/pending contents, retaining recoverable local edits. A clean,
synchronized session follows fresh device working state. A last attempted revision
not found on the device remains visible as unapplied and is not automatically
retried; a newer coalesced revision may send only after recovery resolves ordering.
Editing during readback is captured before recovery installation, so it cannot be
overwritten by an older snapshot. Dirty comparison remains against publication,
not preview. No storage/persistence or automatic transport is introduced.

Windows hardware checklist (all pending for this new scope):

1. Confirm CAP/NEXT/STATE and initial published/working readbacks; edit a supported
   native-rate note/instrument while stopped, paused and playing without F5.
   Check pending versus APPLIED/readback and next-row audible changes.
2. Hold PgUp/PgDn, make rapid edits and toggle OFF timing; verify responsive editing
   and latest complete draft. Unsupported pitch must stay visible, connected and
   rejected without repeated transfers or false synchronization.
3. Press F4 during partial/in-flight preview; verify baseline playback eventually
   returns while baseline/dirty comparison stays correct. Press F5 during preview
   while stopped/paused; verify selected revision, exact published readback and
   no later old preview overwrite. Playing F5 must remain blocked.
4. Exercise Space/Enter and clean exit during transfer/backpressure; verify PAUSE
   remains prompt and lines are not interleaved. Disconnect around COMMIT, reconnect
   to running/paused playback and verify recovery without START or blind resend.
5. Verify panel labels, independent edit/playback cursors, unchanged BPM and legacy
   firmware fallback. Temporary ArkOS handheld publication/editor bindings now reuse
   the shared code; ArkOS hardware validation remains open.

Host tests include a partial-write simulated device using real parser/hash,
audio-owner control and core PCM rendering: edits alter PCM, F4 restores playback,
and F5 confirms a new baseline. These do not measure USB latency, Teensy realtime
cost, physical row timing or hardware performance.

### Automatic preview Windows confirmation (2026-10-12)

The user confirmed automatic draft preview works correctly on Windows BTX + Teensy. This does not establish exhaustive reconnect/backpressure behavior, physical timing or realtime performance. ArkOS validation remains open.

### Bounded pattern edit history (portable foundation; manual validation pending)

History belongs to the LocalPatternEditor pattern context, not the visible screen
or an input binding. It survives screen changes when that context is retained.
Undo/Redo are exposed as actions and methods with availability queries; no keyboard
shortcuts or held-key grouping have been added. Each accepted data edit records
coordinates and complete before/after note, instrument and OFF timing. Combined
Note Delete and F4 Restore are atomic; Restore captures all changed active cells
and the baseline at invocation, unaffected by subsequent publication.

The latest 64 transactions use fixed storage, each capable of 512 changed cells.
Each change is 8 bytes; transaction storage plus ring counters is 262,412 bytes on
this host ABI, plus the editor's existing pattern/snapshot storage and one bounded
pattern copy during edit recording. Single-cell transactions use one change;
unused slots remain reserved so full-pattern Restore never allocates. Work is
bounded by 512 cells per edit/Undo/Redo. No navigation/no-op history entries;
new edits after Undo discard redo, and capacity evicts the oldest transaction.

Undo/Redo preserve cursor and published baseline. Dirty comparison uses the
current baseline, so Undo after confirmed F5 makes the draft dirty against that
new baseline. Exact matching publication readback preserves history. Unchanged
preview readback also preserves history; Undo/Redo/F4 use the normal automatic
preview path and never publish. Publication locks Undo/Redo/Restore, not navigation.

Temporary disconnect disables editing and retains history privately in bounded
storage. The present protocol has no editing-context/session identity that safely
proves continuity, so reconnect initialization conservatively clears history even
when dimensions, tempo or contents match. Recovery that changes draft contents
also clears incompatible history. No partial or unrelated readback may reattach
history. ResetContext is the explicit new-pattern/project or session-termination
boundary; it assumes no final file/project identity format. Destruction ends the
session. Sample/synth editor histories are separate future work with undecided
lifecycle policies. Host tests cover atomic/full-pattern restoration, branch
invalidation, capacity, locking, baseline transitions, recovery and automatic
preview after Undo/Redo; manual validation and input integration remain pending.

### Portable per-channel gain mixer (host-tested)

`MixPcm16MonoWithGains` is separate from the unchanged unity mixer and existing
playback. Eight unsigned Q16 gains span 0..65536; 65536 is exact unity and the
fixed gain metadata defaults every channel to unity. It retains exact fractional
PCM contributions in int64_t, rounds the accumulated sum once to nearest integer
with ties away from zero, then saturates to PCM16. Signed division defines both
polarities explicitly; no implementation-dependent negative shifts are used.
Eight full-scale weighted channels sum within [-17179869184,17179344896];
rounding adds at most 32768 magnitude, safely within int64_t. Cancellation is
independent of channel order, and unity output is bit-identical to MixPcm16Mono.

Whole-request gain/pointer/count validation occurs before writing. Invalid gains
are rejected even for zero frames; valid zero-frame requests permit null pointers.
Positive spans require all eight input pointers, even at zero gain, and a valid
destination. Frame/byte representability is checked before pointer arithmetic.
Rejection preserves output. Caller guarantees valid memory extents/lifetimes,
immutable metadata and destination non-overlap with inputs and request metadata;
inputs may share read-only storage. Processing is allocation-free and O(8*frames).
Zero gain is silent audio contribution, not tracker mute, event suppression or
muted-header styling. Tests cover unity equivalence, partial gains and both
rounding signs, cancellation/order independence, saturation, invalid requests,
input preservation and partition-independent output. No hardware cost was measured.

Core player integration is recorded below; firmware protocol, UI and diagnostic
fixture levels remain unchanged. Gain controls, instrument/master gain, panning, smoothing and final mixing
headroom policy remain separate work. Undo/Redo input bindings remain deferred
until unified ArkOS/keyboard interaction design; no buttons or shortcuts are
reserved, and held-key transaction grouping remains undecided.

### Core player channel gain integration (host-tested; hardware pending)

NativeRatePatternPlayer now uses MixPcm16MonoWithGains. All eight channel gains
start at exact unity, preserving bit-identical current playback. SetChannelGains
validates the entire Q16 set before mutation and returns InvalidGain for values
above unity. GetChannelGains returns a read-only snapshot by value. The single
owner may set gains before configuration; successful Configure resets unity,
while every failed Configure retains gains and playback.

Updates affect the next Render, including already-sounding voices, without
resetting transport, position, continuation, deferred OFF or pending previews.
Start, Stop, Pause/Continue, published replacement and volatile working replacement
retain gains. Rendering keeps tentative playback state and internal scratch;
rejection preserves caller output and all playback state. Zero gain changes only
audio contribution, not tracker mute or event processing. Abrupt changes may click;
smoothing is future work.

Host tests cover exact unity/partial/zero PCM, complete-set validation rollback,
transport/replacement preservation, Configure semantics, deferred OFF and
partition-independent output with gain changes at identical absolute sample
boundaries. No gain commands or UI controls, fixture-level changes, instrument or
master gain, or smoothing are added. Firmware/UI integration, final headroom policy
and Teensy performance measurement of this gain-aware render path remain pending;
prior unity-mixer hardware measurements do not measure the new path.

### Device channel gains: BTGAIN1 (implemented; hardware validation pending)

Only opt-in pattern firmware advertises `BTGAIN1 CAP 1` during HELLO. Ordinary
legacy/USB-trace firmware is unchanged. Future clients must wait for that explicit
capability; no BTX gain controls or bindings are implemented here.

Exact newline-terminated grammar (decimal unsigned integers, single spaces):

```text
BTGAIN1 CAP 1
BTGAIN1 SET 1 <id> <g0> <g1> <g2> <g3> <g4> <g5> <g6> <g7>
BTGAIN1 SUBMITTED 1 <id>
BTGAIN1 APPLIED 1 <id>
BTGAIN1 REJECTED 1 <id>
BTGAIN1 GET 1
BTGAIN1 STATE 1 <applied_id> <next_id> <g0> <g1> <g2> <g3> <g4> <g5> <g6> <g7>
BTGAIN1 ERROR 1 <id_or_zero> <reason>
```

Gains are Q16 0..65536 (exact unity), in internal channel order 0..7. No signs,
extra/missing fields, trailing spaces, overflow or out-of-range gains are accepted.
RX accepts LF or CRLF with existing framing. ERROR reasons are `malformed`,
`not-connected`, `queue-full`, `identity` or `invalid-gain`; malformed payloads
report identity zero. Invalid requests preserve gain/playback state and connection.

IDs are nonzero uint32 and strictly greater than the highest previously accepted
SET identity for this device boot. Each accepted request consumes its identity,
including across HELLO/disconnect; duplicate or older SET is rejected, never
reapplied. Queue-full rejection consumes no identity. NEXT is highest accepted+1,
or zero after UINT32_MAX (exhausted until reboot). IDs are a separate domain from
preview/publication. APPLIED ID zero means configured unity defaults. Successful
Configure resets gain values and applied ID, but does not reset identity protection.

SET copies all eight values into the existing eight-entry request FIFO; there are
no retained parser pointers or sample transfers. Main-loop Submit/TakeApplied and
snapshot reads use interrupt exclusion. The audio owner applies FIFO requests at
the earliest available block boundary before rendering, including while Stopped,
Paused or Playing. SUBMITTED proves queue acceptance only; APPLIED is formatted
by the main loop only after owner completion. ACK saturation leaves requests
queued rather than losing acknowledgements. The complete set applies atomically;
multiple queued sets keep input order, with the last applied set used for rendering.
No formatting, serial I/O, allocation or blocking occurs in the audio callback.

GET and HELLO/STATUS emit coherent applied values/identity and NEXT without changing
playback. NEXT may reflect accepted-but-unapplied work; it is not an application
acknowledgement. After a lost ACK/reconnect, obtain readback instead of blindly
resending; if a submitted ID is not yet applied, wait/read again. No reconnect
reset, automatic resend or automatic transport is defined. Readback is current
state, not a historical log of every intermediate gain set.

Worst-case STATE (two maximum uint32 IDs and eight unity gains) fits 96-byte line
storage. HELLO reserves 12 of 16 fixed TX entries; STATUS needs five. Existing
128-byte/one-line RX and 64-byte TX service bounds remain. Gain replies use the
command FIFO, retain acknowledgement priority, and finish partial lines before
switching lanes; snapshot/telemetry arbitration is unchanged. Request entries add
32 copied gain bytes each (256 bytes for eight slots), plus fixed status metadata.
Transport/preview/publication remain ordered through the same owner queue.

Default unity and fixtures are unchanged. Transport, preview and publication
retain gains; zero contribution does not suppress events or indicate tracker mute.
Abrupt changes may click; smoothing, instrument/master gain and final headroom
policy remain separate. Portable tests cover strict parsing, bounded formatting,
copied ownership, application/readback, identity exhaustion, reconnect-style
readback, queue/ACK backpressure and transport/preview/publication composition.
Teensy compilation/upload and hardware validation were not performed. Prior PERF
reports do not validate the gain-aware rendering cost. Later hardware checks must
cover all transport states, partial writes, backpressure and reconnect retention.

### Shared host gain coordination (implemented; no controls yet)

BringUpSerial exposes RequestChannelGains for one complete Q16 set, capability and
availability queries, read-only authoritative values/applied ID/NEXT, local desired
values and synchronization phase/status. HostChannelGains is portable fixed-storage
logic for Windows and future ArkOS callers. No widget, shortcut, handheld binding,
level change, smoothing or instrument/master gain is added.

A dedicated strict ten-field STATE parser accepts applied ID zero and exhausted
NEXT zero, validates all gains, exact fields/spacing and uint32 overflow, and rejects
inconsistent nonzero NEXT <= applied ID. It does not expand or weaken existing
snapshot/transfer parsers. Malformed/overlong/version-mismatched messages leave
accepted gain state unchanged and do not disrupt transport or pattern handling.
Only explicit BTGAIN1 CAP 1 enables sends; legacy peers receive no gain traffic.
Discovery retains initial device gains, including non-unity values, and never
implicitly queues unity defaults.

An explicit valid request updates one latest desired set. Sending freezes eight
gains and consumes a fresh identity; later requests coalesce without modifying
that line. Transport outranks the next unsent gain line, while partial lines finish
before switching. Gain SET/GET are serialized against active publication and
snapshot/preview transfer/recovery work; gain replies have their own parser and
cannot acknowledge transport or alter draft/baseline/history. Stop, pattern preview,
publication and F4/F5 do not reset or resend gains. Legacy and ordinary pattern
controls retain their semantics. Gain requests are not pattern edits or tracker mute.

SUBMITTED only proves receipt. APPLIED initiates GET verification; synchronized
status requires matching authoritative applied ID and all eight values. Local
newer desired values remain pending until the previous identity is resolved.
Known ERROR/REJECTED responses retain authoritative values, consume the attempted
host identity and never automatically retry the rejected set. A fresh explicit
request may retry with a new identity. Wrong/duplicate/phase-invalid replies do not
change state. NEXT zero prevents sends; counters never wrap or reuse identities.

After five seconds from the first attempted SET write, the outcome is uncertain.
GET polls at most once per second; no blind SET resend. STATE with accepted but
unapplied identity blocks newer work until application is authoritative. Discovery
also detects prior-session accepted-but-unapplied work from NEXT versus applied ID.
If it cannot resolve (for example, configuration reset after an accepted identity),
it remains uncertain rather than assuming cancellation. Disconnect clears capability,
verified availability and all previous-session desired/in-flight values. Reconnect
reads authoritative state without resetting gains or replaying desires; callers
must issue a fresh request after resolution. No automatic Start or baseline restore.

Host tests cover extrema/malformed ten-field parsing, non-unity discovery,
immutable/coalesced requests, partial writes, rejection/exhaustion, timeout and
reconnect recovery, complete-line PAUSE priority, and host/device PCM composition
with preview/publication. Windows target builds and portable tests do not establish
hardware performance. User-facing controls, ArkOS integration/validation, hardware
backpressure/reconnect checks and gain-path Teensy measurement remain pending.

### Read-only gains and Windows diagnostic request (hardware pending)

Windows and ArkOS use the shared padded/clipped information panel to show verified
applied channel gains in Q16 and their applied identity, separate desired values,
and synchronization/error status. Unsupported capability or unavailable state is
explicit. Values are numbered by channel 1..8; zero is not tracker mute and draws
no X/header marker. Connection, transport and pattern preview/publication messages
retain their priority and remain separate. No interactive mixer controls are added.

Windows alone accepts the optional `BROTRACKER_DIAGNOSTIC_CHANNEL_GAINS` process
environment variable. Supply exactly eight comma-separated unsigned decimal Q16
values 0..65536. Signs, spaces, empty/extra/missing fields, overflow and out-of-range
values reject the whole configuration. Parsing uses fixed storage and at most
87 bytes (ten decimal digits per uint32 field, allowing leading zeroes). Invalid
configuration sends nothing and reports a clear panel/log diagnostic. With no
variable set, no gain request is made and current device gains are preserved.

A valid configuration waits for CAP 1 and verified synchronized gain state, then
submits one complete set through RequestChannelGains in the initial connected
session. No repeat request, uncertain-outcome retry, reconnect replay or overriding
of later device changes occurs. If the first session ends before discovery, it
will not submit on reconnect. If validation/identity recovery prevents availability,
it remains waiting rather than resetting playback or gains. This diagnostic is
independent of draft data, F4/F5 and Undo/Redo history; no shortcut or handheld
binding is introduced.

Example from the repository root, after packaging the updated Windows executable:

```powershell
$env:BROTRACKER_DIAGNOSTIC_CHANNEL_GAINS = '65536,65536,16384,65536,65536,65536,65536,65536'
.\deploy\windows\BroTrackerTerminal.exe
Remove-Item Env:BROTRACKER_DIAGNOSTIC_CHANNEL_GAINS
```

This lowers channel 3 to one-quarter unity and retains unity on every other
channel. Removing the variable prevents a request on the next launch; it does
not reset the device's retained gains. A separate explicit all-unity launch can
restore unity; reconnect within an existing launch never replays its configuration.
Abrupt changes may click. Smoothing and final gain/headroom policy remain future
work; diagnostic values do not establish fixture levels or a final mixer design.

Windows hardware checklist (application/transport and limited PERF evidence below;
remaining checks pending):

1. Launch without the variable, then with malformed data: verify no SET and no
   gain/playback change; malformed configuration must be visible in panel/log.
2. Use the channel-3-only reduction above; compare PCM/beeper audibility and verify
   APPLIED plus exact STATE readback. Desired and applied labels must stay distinct.
3. Exercise Start/Stop/Pause/Continue, draft preview, F4 and stopped/paused F5.
   Verify gains persist without changing pattern dirty/baseline/history semantics.
4. Unplug/replug or reconnect BTX; verify authoritative gains are restored without
   another diagnostic SET or automatic transport action. Check panel containment,
   unavailable state and rejection/recovery reporting under backpressure.
5. Capture PERF during stopped/playing/paused publication/Continue for the new
   gain-aware render path. Earlier PERF reports do not measure this implementation.

Portable parsing/one-shot and shared rendering tests are software evidence only.
ArkOS visual validation and full gain-path stress/performance validation remain
open; the limited Teensy callback measurements below are separate evidence.

### Windows gain log evidence (2026-10-12)

The supplied latest `brotracker-terminal.log` records one diagnostic SET:

```text
BTGAIN1 SET 1 1 65536 65536 16384 65536 65536 65536 65536 65536
BTGAIN1 SUBMITTED 1 1
BTGAIN1 APPLIED 1 1
BTGAIN1 STATE 1 1 2 65536 65536 16384 65536 65536 65536 65536 65536
```

Exact readback confirms channel 3 at quarter unity and every other channel at
unity. Subsequent STATE lines retain these values through the logged Start,
Pause/Continue and Stop operations. No protocol errors or disconnects were recorded.
This is log evidence, not user listening or visual confirmation.

Across 160 PERF reports, the highest reported steady callback maximum was
55901 cycles against a 1741496-cycle budget (approximately 3.21%), with zero
reported budget overruns. The maximum is cumulative since firmware boot,
including activity before this BTX session; it cannot be attributed exclusively
to reduced gain. These callback measurements do not establish whole-audio-graph
cost or isolate instrumentation/preemption overhead.

Audible comparison, reconnect retention, publication retention, clipping absence,
exhaustive stress validation and ArkOS validation remain open. The normal terminal
log in AppData is overwritten on the next launch by default. Archive the supplied
validated log separately before further runs, outside disposable review exports;
the AppData log path is not permanent evidence storage. This documentation update
does not modify or archive the log. Removing the diagnostic environment variable
only prevents a new launch request: device gains remain until an explicit update
or successful configuration/reboot restores them. Smoothing and final gain policy
remain pending.

### Portable variable-rate PCM voice (host-tested; not integrated)

VariableRateSampleVoice is a separate PCM16 mono one-shot using the existing
immutable non-owning Pcm16MonoView. The caller retains PCM and valid memory extents
until Stop/Reset, valid Configure/retrigger, completion or destruction releases it.
Destination spans must be disjoint from source PCM, the voice and request metadata.
No sample ownership, allocation, I/O, locks, clock or floating-point work is added.
Existing RamSampleVoice/native-rate player APIs and audible fixtures are unchanged.

Configure explicitly sets the output rate and stops playback; zero rate rejects
without mutation. Reset stops/releases while retaining configuration. Trigger
accepts a positive uint64 Q32.32 source-frame step per output frame (2^32 is exact
unity). Samples must still match configured rate; note-to-rate and general
sample-rate conversion are separate future work. Valid triggers restart at zero
and replace playback. Invalid triggers preserve the previous voice.

Source integer position and fractional phase are stored separately, so the packed
step does not restrict sample length to 2^32 frames. Caller-buffer byte limits
match RamSampleVoice. Advancing uses fractional carry and compares the whole-frame
increment with remaining frames before adding; maximum steps safely complete
without wrapping or scanning skipped frames. Each output uses exact fixed-point
linear interpolation and one nearest rounding, signed ties away from zero, using
signed division rather than negative shifts. Weighted sums lie within
[-2^47, 2^47-2^32]; rounding headroom fits int64. While position is inside the
sample, interpolation's second endpoint clamps to the last source frame. Reaching
or passing frame_count releases the view and zero-pads subsequent output.

Render validates destination/count before mutation; rejection preserves state and
output. Zero-length calls are no-ops permitting null. Silent/unconfigured voices
render zero. Work is O(output frames), independent of sample length/step; arbitrary
span partitioning produces exactly identical PCM and phase. Unity output and
completion match RamSampleVoice bit for bit. Tests cover fractional/half/double
steps, signed rounding/extrema, final/single-frame behavior, maximum-step and
large-position arithmetic, retrigger/replacement, Stop/Reset and failure contracts.

Linear interpolation is an initial quality choice, not band-limited resampling;
upward transposition may alias. Changing playback rate changes both pitch and
duration; this is not time stretching. No sample reference pitch is inferred from
duration. Reference-note mapping is now provided below. Renderer/player/binding,
firmware/protocol/UI integration, broader conversion and quality policy remain
pending. Teensy execution
cost is unmeasured; host tests are not hardware evidence.

### Portable reference-note interval mapping (host-tested; not integrated)

`NoteToPlaybackStep(target_note, reference_note, output)` accepts explicit pitched
pattern values 24..127 and returns positive Q32.32 `2^((target-reference)/12)`.
Empty/OFF/out-of-range target or reference rejects before subtraction, preserving
output. Equal notes produce exact unity (2^32); every supported whole-octave
interval produces exact representable powers of two. All intervals -103..103 are
supported without counter overflow or arbitrary octave restrictions.

The 207 immutable uint64 constants (1656 bytes) in `note_playback_steps.h` are
reproducible with `tools/generate_note_playback_steps.py`. Generation uses exact
Python integers: binary search the twelfth root of 2^(384+interval), then compare
its exact half-integer boundary to round to nearest Q32.32, ties upward. No floating
point/pow, allocation, I/O or independent clock occurs in the runtime API; runtime
work is fixed validation and one lookup. Absolute ratio error is strictly below
2^-33 (half a Q32 LSB); over the supported range relative error is below 4.5e-8
(0.045 ppm). Whole octaves/unity have zero quantization error.

Host tests independently verify all intervals against 512-bit integer bounds
`(2*q-1)^12 < 2^(396+interval) < (2*q+1)^12`, using a fixed-limb reference distinct
from the generator's root search. This establishes nearest quantization without
floating-point reference tolerances. Additional tests cover all target/reference
pairs for monotonicity/exact octaves, directions/extrema, invalid-output preservation,
and independently expected octave playback/partition independence composed with
VariableRateSampleVoice. To check checked-in generation without rewriting:

```text
python tools/generate_note_playback_steps.py --check
```

Reference pitch is explicit metadata/playback anchor, never inferred from WAV
duration or BPM. Existing C-3/D-3 beepers are not relabelled as physically tuned.
Fractional-semitone tuning has a separate future composition boundary; no detune
or source/output-rate conversion is implemented here. This completes D0055's
next mapping foundation, not binding/player integration. Existing native-rate
unsupported-pitch behavior, fixtures, firmware/protocol/UI are unchanged.
Playback integration, tuning metadata and hardware validation/performance remain
pending; linear interpolation aliasing and pitch/duration limitations still apply.

### Optional host configuration and recovery

Windows and ArkOS load an optional `config.ini` from SDL's per-user preference
path, never the launch directory. See [D0057](ARCHITECTURE_DECISIONS.md#d0057--host-configuration-and-safe-recovery)
for exact paths and grammar. Copy `config/config.ini.example` manually if needed.
Schema 1 exposes only `new_pattern_length` (16/32/64, default 32) for future
creation; current device patterns/playback and input controls are unchanged.
Missing config is normal. Invalid/unreadable/oversized config falls back to built-ins
with a diagnostic in the startup log; user files are never rewritten/deleted.

Run Windows `.\deploy\windows\BroTrackerTerminal.exe --safe-config` or the ArkOS `BroTrackerTerminal --safe-config` launcher command to
bypass external configuration. Manual startup/path/recovery checks remain pending
on both platforms. This recovery affects application settings, not system SDL
mappings; configurable controls and project creation are not implemented.

### Variable-rate block rendering foundation

`VariableRateVoiceBlockRenderer` owns eight fixed `VariableRateSampleVoice`
instances and accepts up to 16 prepared Trigger/Stop commands (initial implementation
capacity). Triggers carry caller-owned immutable PCM views and explicit positive
Q32.32 steps; source/output rates must match. Separate mono channel outputs use
exact half-open offsets, preserve input order at equal offsets and continue voices
across blocks. Valid Configure clears voices; Reset retains the rate. Whole-request
preflight rejects invalid metadata/steps before touching any voice or output.
Caller guarantees sample lifetimes/extents and non-overlap with outputs and request
metadata. Work is bounded by eight channels, supplied frames and 16 commands;
no allocation, I/O, exceptions or independent clock. Linear interpolation remains
an initial non-band-limited choice, changing pitch and duration together.

Host tests cover exact and fractional PCM, partition independence, simultaneous
commands, retriggers, replacement, Stop, boundary ownership, capacity and rollback.
NoteToPlaybackStep remains a separate mapping primitive; note/instrument preparation,
pattern-player integration, firmware/UI and hardware performance remain pending.
Existing native-rate playback and fixture levels are unchanged.

User confirmation on 2026-10-12: the C-3 empty-note fallback works on Windows BTX.
Preceding-note lookup and OFF initialization remain unchanged; ArkOS validation
is still pending.

### Variable-rate sample command preparation (host-tested; not integrated)

`VariableRateSampleCommandPreparer` copies metadata for up to 16 unique instrument
bindings (initial implementation capacity), each with raw ID 0..254, explicit
reference note 24..127 and immutable non-owning PCM view. Source/output rates must
match. Pitched events resolve through `NoteToPlaybackStep(target, reference)` into
explicit Q32.32 Trigger commands; reference pitch is never inferred from duration
or BPM. No source-rate conversion, finetune, tempo matching, envelopes or defaults.

The adapter reuses logical channel continuation: instrument-only events select
without triggering, empty fields continue state, repeated notes retrigger and
Arrival OFF emits Stop without lookup. EndOfPosition OFF updates logical state
but emits no immediate command; deferred Stop scheduling remains player-owned.
Offsets and equal-offset input order are preserved; decreasing offsets reject.
Configure/Prepare validate transactionally, preserving all state and the complete
output on failure. Reset retains bindings and configuration. Caller keeps PCM
valid/immutable while configured and while any renderer retains views, coordinating
renderer release separately from adapter reset/reconfiguration.

Fixed storage and bounded table lookup, allocation-free/noexcept, without rendering,
I/O or clock ownership. Host tests cover validation, note/reference extrema,
unity/octaves, continuation, retriggering, Stop, ordering and rollback; composition
with the variable-rate renderer and unity mixer verifies independently expected
fractional PCM and partition independence. Native-rate playback remains unchanged.
Variable-rate pattern-player, firmware/protocol/UI integration and hardware timing/
performance validation remain pending.

### Variable-rate pattern-player core (host-tested; not integrated)

`VariableRatePatternPlayer` is a separate portable core player. It reuses the
musical cursor, row generator, `VariableRateSampleCommandPreparer`,
`VariableRateVoiceBlockRenderer` and gain-aware mono mixer. Fixed pattern/binding
metadata is copied; PCM remains immutable and caller-owned with valid extents
through configuration lifetime. Source/output rates must match. Explicit reference
notes map to Q32.32 transposition; no finetune, tempo matching or source-rate
conversion. Linear interpolation may alias and changes pitch and duration together.

The native-rate player, its API and current firmware playback are unchanged.
The separate player exposes `VariableRatePatternPlayerStatus`,
`VariableRatePatternTransportState` and `VariableRateConsumedPatternPosition`.
Configure validates active dimensions/notes/OFF metadata and binding/tempo/rate
requirements transactionally, stops playback and restores unity gains on success.
Like the native player, initial Configure leaves instrument lookup to Render;
replacement validation checks two complete logical passes before acceptance.

Start/repeated Start restarts zero. Stop clears voices, continuation, deferred
Stops and reported position. Pause silences voices and clears deferred Stops,
retaining continuation/position; Continue uses exact bounded cursor positioning
to replay that same absolute row once, retaining the loop. Arrival OFF stops at
row start; EndOfPosition stops at the absolute next-row boundary before new
triggers there. Position reports the latest tick consumed by successful rendering.
Channel gains are transactional, default to exact unity and persist through
transport/replacement; zero gain is audio attenuation, not tracker mute. Abrupt
gain updates may click; smoothing remains pending.

Stopped replacement resets playback; paused replacement reconstructs continuation
from the new pattern before the paused row without consuming it. Same-dimension
volatile working replacements stage one supersedable candidate while Playing,
apply before the next unconsumed row's events and preserve timeline/sounding voices.
Due old deferred Stops precede new triggers; remaining old deferred work is cleared.
Reconstruction uses a cleared prefix in loop zero or one full pass plus prefix in
later loops. Pause retains the candidate; Stop/Start adopt it while clearing playback.
Successful Configure or published replacement supersedes it. No baseline/persistence
or transfer protocol is owned by this core.

Initial limits remain 128 output frames, 16 audio commands and 512 emitted ticks
plus one completion pull. Fixed scratch/state, bounded processing, allocation-free
and noexcept. Whole-render preflight uses tentative state and internal PCM scratch;
rejection preserves transport, cursor, continuation, voices/fractional phase, gains,
position, pending Stops/candidate and caller output. Callers must handle rejection;
no firmware underrun policy or measured Teensy budget is established.

Host tests cover independently expected unity/octave/fractional mixed PCM and
partition independence through loops, Pause/current-row replay, both OFF modes,
deferred Stop/new-trigger ordering, gains, all replacement modes, pending-candidate
handling, validation, processing limits and rollback/smaller retries. Direct unity
comparison with native-rate playback covers equivalent patterns/bindings through
transport and replacements. Core orchestration is complete; opt-in firmware
integration is described below. Hardware timing/performance validation remains pending.

### Opt-in variable-rate firmware (host-tested; hardware validation pending)

`teensy41_pattern_variable_rate` extends `teensy41_pattern_sd_playback`, retaining
SD preparation/WAVPREP, RAM2 storage and PERF diagnostics. It adds only
`BROTRACKER_PATTERN_VARIABLE_RATE`. Other environments retain their native-rate
player and behavior. Compile-time aliases select one player/control instance;
a fixed facade translates core status/position types into the existing control
interface, without virtual dispatch, heap allocation or a second player instance.
The bounded audio-owner FIFO, applied ACK ordering and serial protocol implementation
are shared. No discovery syntax, queue capacities or identity rules change.

Boot pattern notes, 12753 hundredths BPM, unity gains and active dimensions are
unchanged. Beeper PCM uses explicit reference notes 60 and 62, preserving original
unity output. Successful SD preparation binds the same immutable caller-owned
`Samples/test2.wav` at reference 60/internal instrument 2 (display 03); failure
retains the two-beeper fixture and WAVPREP failure report. Preparation and selected
metadata construction finish before audio activation. RAM2 placement stays a
caller-storage choice, preserving future optional PSRAM. References are playback
anchors, not measured physical tuning. Source/output rates must match (current
44100 Hz fixture); no finetune, tempo matching or lower-rate WAV support.

Supported pitched notes 24..127 resolve through variable-rate mapping for Render,
preview and publication validation. Existing native-rate environments still reject
unsupported pitch changes. Start/Stop, Pause/current-row Continue, both OFF timings,
gains, telemetry and snapshots retain their semantics. Stopped/paused publication
remains allowed; Playing publication rejects. Volatile preview works while Playing
and applies at the next row boundary. Frozen transfer ownership, accepted versus
applied notifications, supersession and authoritative baseline/working readback
remain unchanged. Reconnect never starts playback or resets gains/patterns.

All player mutation remains audio-owner-only. Main-loop parsing/formatting and
boot-time SD I/O stay outside audio processing. Render rejection zeroes the block,
Stops and latches a fault; allocation failure stops/resets without advancing and
increments its diagnostic counter. PERF includes audio-owner transport/replacement
work, with the same wrap-safe counters and budget calculation. The 128-frame,
16-command and 512-tick-plus-completion limits remain explicit. Linear interpolation
is not band-limited: upward transposition may alias; rate changes affect pitch and
duration together, without time stretching. Unity-gain summation may saturate;
final gain/headroom policy and smoothing remain future work.

Host validation runs both native regressions and `BroTrackerVariableRateBringUpTests`,
compiled with the same selection flag as firmware. It covers reference/semitone/
octave PCM, full supported note boundaries, SD success/fallback, transport/OFF order,
gains, preview/publication rejection and readback, frozen storage, identity/reconnect
handling, bounded ACK backpressure and render/allocation faults. These portable
checks do not establish Teensy compilation, memory headroom or realtime performance.

Windows BTX + Teensy hardware checklist (all pending for this environment):

- Build/upload the new environment; inspect linker RAM1/RAM2/stack headroom and
  confirm WAVPREP success or the unchanged beeper fallback, with stopped/silent boot.
- Compare each instrument at its reference, one semitone above, one octave above
  and one octave below; confirm pitch/duration coupling rather than tempo matching.
- Check automatic preview and F4 restoration; publish with F5 while Stopped and
  Paused, verifying readback and retained paused row/loop. Playing F5 remains blocked.
- Verify Pause/Continue replays the current row; exercise both OFF modes and a new
  note at the deferred Stop boundary. Exact physical timing remains unmeasured.
- Reconnect during playback and while paused without automatic Start/Continue;
  verify applied/pending preview, baseline and gain readback without blind retries.
- Capture PERF while Playing, previewing, publishing while Paused and Continuing.
  Prior native-rate PERF results are not validation of the variable-rate path.
  Inspect memory usage and stress/backpressure behavior separately; ArkOS remains open.


### Working-preview corrections and evidence (2026-10-13)

The user confirmed beeper/PCM transposition, an audible distinction between
Arrival and EndOfPosition OFF, and playback continuation after forced Windows
BTX termination/relaunch while Teensy remained powered. The supplied
`brotracker-terminal(2).log` records PLAYING and `BTPREVIEW1 STATE 1 0 4 0`,
with no START or publication writes. Unpublished working playback retention is
intentional; it is neither publication nor SD persistence. Archive validated logs
separately before another launch overwrites the normal terminal log.

The earlier review summary reports valid edits becoming audible after 3–4 loops
and a D-3 display/C-3 boot-fixture mismatch after USB reset. Missing-instrument
rejections are separate validation failures, not evidence explaining every delay.
Neither log establishes precise local-edit-to-application latency.

Host tests reproduce one-line-per-service transfer pacing and stale local-draft
retention after device identity reset. Shared serial service now performs at most
64 bounded passes per Tick, rechecking transport priority between complete lines
and stopping on partial/blocked writes. Each pass retains its 1024-byte receive
limit. The Windows worker holds a full bounded protocol line instead of imposing
32-byte fragmentation. No service waits for I/O completion; elapsed transfer time
still depends on service cadence and backpressure. Core application remains at
the next eligible row boundary, and a changed row already passed naturally waits
for its next occurrence.

On reconnect, a lower device NEXT high-water mark invalidates retained draft
context; complete verified working readback supplies the displayed draft. A
powered-device reconnect with unchanged identities retains recoverable local
edits and authoritative working/pending readback. A fresh host adopts verified
working data, not the boot fixture. No automatic START or publication is sent.
Identity rollback is not a general boot/session UUID; where reset cannot be
established, retained unmatched edits remain explicitly unsynchronized.

Rebuild Windows BTX for these host corrections; firmware is unchanged. Hardware
revalidation remains open: rapid valid edits, supersession/readback delays, USB
reset reconciliation and powered-device reconnect. Physical timing, exhaustive
backpressure/recovery and ArkOS validation remain open. Configurable PgUp/PgDn
inversion (including held acceleration) was deferred at this stage; the later
Note-field direction implementation is documented below. OFF toggling stays unchanged.


### Working-preview latency and STOP regression review (2026-10-13)

The user reports improved preview responsiveness after the prior correction.
The alleged Enter STOP failure did not recur in subsequent user testing, and
neither supplied log captures that defect. Keep regression tracking; no Enter
fix is claimed. An edit appearing visually ahead of the highlight does not prove
that its device row-start event is still pending. After a power-cycle, boot-fixture
restoration is expected: published and working patterns currently live only in RAM.
Powered-device BTX relaunch retention remains intentional, without automatic START
or publication.

Evidence is archived locally in `diagnostics/preview-validation-20261013-221030/`
outside build exports. This ignored local path is not a repository deliverable.
The archive contains both supplied logs, the previous current log, pre-test executable,
source revision/status, scripts, exact tested executable SHA256, readbacks and
measurement captures. The expected environment is `teensy41_pattern_variable_rate`;
the existing firmware was not rebuilt/uploaded and its binary identity cannot be
verified through the current protocol. The packaged Windows executable was built
with the host timing diagnostics. Original logs remain unchanged; archive the
normal AppData terminal log before another launch overwrites it.

The supplied logs contain 25/78 preview BEGINs, 19/73 APPLIED replies and 6/4
REJECTED replies respectively, plus successful STOP replies. Missing-instrument
rejections remain explicit validation outcomes, not explanations for every valid
edit delay. Those logs lack local-edit timestamps, so they cannot establish
local-edit-to-application latency. New main-thread diagnostics attach monotonic
host millisecond ticks to draft-checksum changes and serial TX/RX. They do not log
inside audio processing or alter debounce, repeat or scheduling policy.

With BTX closed and COM3 exclusively owned by the bounded serial harness, six
48-cell previews were applied while Playing, interspersed with Pause/Continue.
Host-observed timing was:

| Preview ID | BEGIN -> COMMIT | COMMIT -> ACCEPTED | COMMIT -> APPLIED |
| --- | ---: | ---: | ---: |
| 1 | 34 ms | 3 ms | 68 ms |
| 2 | 27 ms | 2 ms | 105 ms |
| 3 | 50 ms | 19 ms | 19 ms |
| 4 | 31 ms | 8 ms | 170 ms |
| 5 | 53 ms | 6 ms | 6 ms |
| 6 | 31 ms | 13 ms | 113 ms |

These timestamps include USB/host buffering and observation delay; they are not
exact audio-owner or audible-onset measurements. The first changed row-zero note
was APPLIED with the next observed position in row 6, so hearing that note on its
following row-zero occurrence is normal. A 16-row loop at 127.53 BPM lasts about
1.882 seconds. This small run did not show multi-loop application delay, but does
not resolve every reported local-edit delay or separate all telemetry lag.
Original working cells/OFF metadata were restored and matched the final 48-cell
readback exactly. Published baseline was not changed; device was left Stopped.
Direct serial STOP reset was acknowledged. No automatic START/publication occurred
on reconnect; STARTs in the harness were explicit test operations.

All seven configured Windows host test targets pass, including both Enter mappings,
clean/dirty STOP during preview transfer/recovery, held editing and framing/priority
coverage. The initial new input test incorrectly assumed Enter cancels a separately
held PgUp; that assertion was corrected rather than changing the repeat policy.
Actual BTX window-message injection could not obtain foreground focus and aborted
before sending keys. BTX was closed through its normal window-close path, and the
attempt logs were retained. Neither direct serial testing nor synthetic SDL mapping
tests establish physical keyboard behavior. No new transport/scheduling defect was
established; audible timing, exhaustive recovery/backpressure and ArkOS remain open.

Human validation still needed:

1. Run the packaged BTX; verify a ready device pattern and explicit instrument.
2. While Playing, edit a pitched note several rows ahead, then a row just passed;
   retain timestamped draft, BEGIN/COMMIT, ACCEPTED/APPLIED and POS records, noting
   audible change separately from the highlight. Repeat with rapid edits.
3. Press Return and keypad Enter while clean/dirty, during preview/readback and
   while holding PgUp/PgDn. Verify STOP/reset without F5, draft preservation and
   connection retention; release held keys and confirm no automatic restart.
4. Relaunch BTX with Teensy powered, then separately power-cycle Teensy. Verify
   authoritative working retention in the first case and boot restoration in the
   second. Archive each log before the next launch.


### BTX host logging modes and startup retention

See [BTX logging](LOGGING.md) and D0058. Optional schema-1 `[logging]` settings are
`mode=basic|debug` and `preserve_last=0..20`; defaults and `--safe-config` are Basic/0.
Existing `[btx]` requirements are unchanged. Debug records detailed protocol/edit
and audio diagnostics; Basic keeps lifecycle, applied transport, errors/rejections
and recovery, filtering CELL/POS/PERF and periodic statistics. UTC/local-offset and
monotonic timestamps describe host observation only.

Debug/N rotates exact logger archives once at startup; append mode skips rotation.
Failure preserves current by appending with a diagnostic. Windows retains the SDL
per-user log location, ArkOS `/tmp/brotracker-arkos.log`. Archive hardware evidence
separately before overwrite-mode launches. Normal startup never rewrites config;
the authorized Windows user file now selects Debug/5 and new-pattern length 32.

This is host-only implementation, separate from manual Windows/ArkOS validation.
No firmware timestamp, formatting, telemetry/PERF rate or audio-callback work changes.
Existing hardware records and outstanding timing/backpressure checks are preserved;
the postponed hardware scenario was not run. Final controls/playback remain unchanged.


### Captured STOP write-ownership failure (2026-10-13)

The current supplied log now captures the previously unconfirmed failure. Local
23:14:21.827 completes preview 68 CELL 10; Enter arrives at 23:14:21.829 and STOP
is queued at 23:14:21.833. There is no STOP TX completion or STOPPED; playback POS
continues. Preview ERROR 1 68 8 at 23:14:26.563 is staging timeout after the transfer
stalled, not evidence of an SDL input failure. STOP becomes unconfirmed at
23:14:27.836. Host reopening restores PLAYING through HELLO at 23:14:29, without
proving a board reset. PAUSED at 23:14:32.004 and STOPPED at 23:14:32.750 demonstrate
that later transport requests succeed. Earlier user reports of non-recurrence are
retained as historical observations, not proof this failure never existed.

Original logs/config were copied unchanged to
`diagnostics/stop-write-ownership-20261013-232532/` before testing. The manifest
records revision/status. Source originals remain untouched. This ignored archive
is a local evidence path, not a repository deliverable.

A deterministic asynchronous worker fixture reproduces the cause: Write copies
and owns a preview CELL but returns zero pending completion. The scheduler used
`tx_offset==0` as permission to discard that line when STOP arrived. The worker
then returned the old CELL completion to the new STOP buffer. With completion
larger than STOP, reported offset exceeded its length; STOP was never actually
written and its ACK timer expired. Partial completion can instead break framing.
This reproduces the ownership and missing-STOP path; it does not identify every
individual USB packet from the original log.

The host now tracks a write attempt separately from reported byte offset. Once
attempted, bytes remain immutable through pending/partial completion; STOP gains
priority after that complete line, not by replacing an owned request. The same
rule guards preview, publication and gain cancellation and the immediate transport
write path. No debounce, firmware, serial wire protocol or SDL mapping change.

Tests cover STOP while an asynchronous CELL is owned at zero reported offset,
backpressure, full/partial completion, preserved draft/connection, and real disconnect
recovery without automatic START, STOP replay or publication. A fresh explicit STOP
still works after recovery. Host tests establish this defect and correction; physical
keyboard and Windows + Teensy correction validation remain pending. No hardware
success is inferred from the historical log.

Retest:

1. Launch the updated Windows package, START and hold PgUp/PgDn to generate previews.
2. Press Return and keypad Enter during CELL transmission. Verify the owned line
   finishes, STOP TX completes and STOPPED follows; retain draft and connection.
3. Release held keys; confirm stopped/reset position and no automatic restart/F5.
   Repeat under slow/backpressured transfer and retain timestamped logs separately.


### Latest-draft preview delivery (2026-10-14)

The latest supplied logs confirm 11 Enter inputs followed by STOPPED and no
STOP-unconfirmed. Preserve the asynchronous ownership/framing regressions; this
change does not replace or weaken the STOP fix. In the 2026-10-13 log, draft changes
start at local 23:46:13.406. Frozen preview 103 sends cell 36 as note 60 at
23:46:14.310, commits at 23:46:14.566 and is APPLIED at 23:46:14.672. Preview 104
sends that cell as note 51 at 23:46:15.435 and is APPLIED at 23:46:15.715. These
are successive valid frozen revisions; they do not demonstrate incorrect pitch
mapping. The user hearing the older pitch is a listening observation separate
from these protocol timestamps.

Avoidable host latency came from UI-paced per-line completion polling, the
worker's unconditional 5 ms idle sleep, and recovery/readback gating before
sending newer work after an obsolete revision was APPLIED. A pre-fix deterministic
regression reproduces that readback gate. Frozen bytes themselves must never be
mutated to accelerate delivery.

Windows now services serial in eight fixed 2 ms slices during its existing 16 ms
UI idle span; no extra input-repeat ticks or rendering are performed. Worker
submission wakes its condition-variable wait; receive polling still has a bounded
5 ms idle wait. STOP retains complete-line priority and every attempted/partial
write keeps immutable ownership. A matching SUBMITTED/ACCEPTED/APPLIED chain may
release an obsolete intermediate readback gate when a newer draft is already
ready. This never labels the draft synchronized: final exact readback is retained.
A readback already started must drain before newer transfer, because the device
serializes snapshots and transfers. Uncertain COMMITs/timeouts/disconnects still
require authoritative recovery, never blind resend. Current/latest storage remains
bounded; no per-key revision queue, debounce/repeat, musical scheduling or protocol
change is introduced. ArkOS retains its normal loop; shared coalescing applies.

Observed comparison, using `mono_ms` and matching draft/COMMIT checksums:

| Capture/revision | Draft -> BEGIN | BEGIN -> COMMIT | COMMIT -> APPLIED | Draft -> APPLIED |
| --- | ---: | ---: | ---: | ---: |
| Supplied 103 | 98 ms | 1062 ms | 106 ms | 1266 ms |
| Supplied 104 | 71 ms | 1041 ms | 34 ms | 1146 ms |
| Updated BTX 1 | 77 ms | 240 ms | 84 ms | 401 ms |
| Updated BTX 2 | 37 ms | 251 ms | 7 ms | 295 ms |

The after run uses the actual packaged BTX editor, shared coordinator, Windows
worker and connected Teensy through targeted synthetic Windows messages to BTX's
own SDL window. Four PageDown edits produced two frozen transfers; final working
readback synchronized. Both injected Enter requests received STOPPED. This is not
a direct-serial timing substitute, but also not physical keyboard or listening
validation. Scenarios, row positions and device session differ from the supplied
log; this is an observed comparison, not a controlled benchmark or audible-onset
measurement. Local observation and USB buffering contribute to timestamps.

Original logs/config, pre-test executable, manifest, targeted-input script,
working-before/restored readbacks and timing summary are preserved locally in
`diagnostics/latest-preview-20261014-172848/`. This ignored local archive is not a
repository deliverable. Authoritative working data was saved before testing and
restored exactly afterward; published baseline was not changed. One serial owner
was used at a time, BTX exited cleanly, and final transport is Stopped. No firmware
build/upload occurred. The expected variable-rate environment has no independently
readable firmware binary identity; reference-note mapping is unchanged.

All seven host CTest targets pass. Async coverage includes repeated edits during
transfer, partial completion, delayed readback, latest convergence, STOP priority
and disconnect recovery. The header/service constants preserve fixed work bounds.
Only Windows BTX requires rebuilding/packaging for this correction. Hardware
physical-key/listening checks, exhaustive stress/backpressure/reconnect, ArkOS and
physical timing/performance remain open. Even faster delivery cannot update a row
already consumed: an upcoming row can use the older applied revision, and that
row's new event then waits for its next occurrence. No instantaneous WYSIWYG claim.

Retest:

1. START, enter/change a note several rows ahead with an explicit instrument, and
   repeat rapidly; archive draft checksums, BEGIN/COMMIT, APPLIED and readback.
2. Repeat just before and just after the target row. Note audible changes separately
   from highlight position and correlate authoritative tick/row/loop.
3. Press Return/keypad Enter during transfer and held editing; verify STOPPED,
   draft retention and no automatic restart. Test powered reconnect separately.


### User acceptance of preview latency (2026-10-14)

The user confirmed substantially improved preview responsiveness. Edits sufficiently
ahead of playback were reflected reliably in the user's test. Near the playback
position, initial/default-note playback can precede application of the subsequently
edited pitch. The user accepts this behavior for the current development stage and
considers the latency correction closed. These are user validation/listening
observations, not new sample-accurate hardware measurements or proof that every
timing case is resolved. No zero-latency claim is made.

Further local-edit-to-applied optimization, especially near row boundaries, remains
deferred. Classic scrolling and a centered edit cursor are undecided UI options.
Stopped-only editing is a possible future interaction policy, not an adopted
restriction; current live editing remains supported. This confirmation supplies
no new Enter, log-rotation or ArkOS validation. Existing regression coverage,
backpressure/reconnect, physical timing/performance and other pending checks remain.


### Note-field OFF cycle and configurable input direction

Logical increase follows G-8 (127) -> OFF -> C-0 (24); decrease follows
C-0 -> OFF -> G-8. Other pitched values step by one semitone. Instruments are
preserved; entering or leaving OFF sets Arrival timing. O remains direct OFF
entry, and both PgUp/PgDn in the OFF second field still toggle Arrival/EndOfPosition.
Empty Note entry still inserts the nearest preceding pitched note in this draft
channel, or C-3 (60) if none, unchanged on its first action and without an instrument.

Optional schema-1 config (normal is the built-in and safe-config default):

```ini
[editing]
value_step_direction=reversed
```

Normal maps PgUp to increase and PgDn to decrease; reversed swaps Note and numeric Instrument
requests once at the shared input interpretation boundary, including held repeat.
OFF timing toggling, navigation, explicit note assignment and
future letter-note entry are unaffected. Held editing keeps the existing endpoint
acceleration reset and renews a 400 ms pause on entering OFF; the next action
leaves OFF in the selected logical direction. Opposite keys replace the hold and
key release/cancellation retain their existing behavior. Delete, Restore, dirty
comparison, Undo/Redo, preview/publication and live editing are unchanged.

Implementation and host tests cover cycles, reversal, metadata/instruments,
initialization, held timing, Windows key release and transactional config validation.
Windows packaging is updated; manual normal/reversed Windows validation remains
pending. The shared model is reusable for handheld input, but no ArkOS bindings
are added and ArkOS runtime validation remains open. No user config rewrite,
firmware change/upload or hardware test is performed for this step.


### 32-row bring-up fixture (implementation; hardware validation pending)

User confirmation: cyclic Note stepping through OFF and
`note_step_direction=reversed` work on Windows. ArkOS validation remains open.

The firmware fixture now explicitly defaults to 32 active rows through
`BROTRACKER_PATTERN_FIXTURE_ROWS` (override choices 16, 32 or 64), exposed as
`kPatternBringUpFixtureRows`. All existing beeper and optional SD channel events
in rows 01..16 are preserved; rows 17..32 have NOTE_EMPTY, instrument 0xFF and
Arrival timing. Tempo remains 12753 hundredths BPM; bindings/gains are unchanged.
The 32-row loop lasts eight quarter-note beats, approximately 3.763820 seconds.
The prepared test2.wav remains 68284 frames / 1.548390 seconds at native rate;
its duration is independent of this longer loop. There is no tempo matching.

Core storage is 64 rows by eight channels, independent of active length and the
32-row display. The legacy implicit RealtimePattern constructor length stays 16
for API compatibility; firmware selects its length explicitly. Existing generic
core validation still accepts positive lengths within capacity. D0056's named
fixture choices are 16/32/64. No sequencing/flow FX, 64-row UI paging, new-pattern
creation or persistence is implemented. Host config `new_pattern_length` does
not resize the device. The standalone mockup is unchanged.

Snapshot v1/v2, preview and publication keep their wire grammar/checksums; bounded
staging now holds up to 512 ordered cells. Both players, editor and history use
that capacity. Host tests cover rows 16->17 and 32->1, 64-row looping, independently
expected PCM and partition independence, complete upper-row transfers (including
index 511), upper-row editing/Undo/Redo/Restore and rejected replacement rollback.
Rendering limits remain 128 frames, 16 commands and 512 ticks plus completion pull.

Storage audit (Windows x64 host ABI, not Teensy linker measurements):

| Object | Previous 16-row storage, bytes | Current 64-row storage, bytes |
| --- | ---: | ---: |
| RealtimePattern | 386 | 1538 |
| NativeRatePatternPlayer | 4208 | 6512 |
| VariableRatePatternPlayer | 4272 | 6576 |
| PatternBringUpControl (native selection) | 4960 | 7264 |
| PatternSnapshotAssembler | 828 | 3132 |
| PatternPublication staging | 416 | 1568 |
| PatternEditHistory | 65804 | 262412 |
| LocalPatternEditor | 66608 | 265520 |

Each full pattern copy adds 1152 bytes. Transactional player copies add 2304
bytes per player; they retain fixed audio scratch and now reserve two larger
pattern stores. Additional firmware snapshot/preview/publication copies also grow.
History reserves 64 transactions with room for 512 changes each: +196608 bytes,
with single-cell edits still recording only one change. Host edit operations have
a bounded full-pattern stack copy; player configuration/replacement/rendering use
existing bounded tentative copies. The RAM2 PCM reservation remains 256 KiB.
Actual Teensy RAM1/stack/linker headroom and callback cost must be measured after
building the selected firmware; old PERF results are not evidence for this change.

Hardware checklist (pending): verify boot snapshot dimensions, empty rows 17..32,
playback crossing row 16 into 17 and looping after 32; edit an upper-row cell and
check preview, stopped/paused F5 and authoritative readback. Check both player
environments, transport/gains, memory usage and PERF. No firmware or BTX application
build/package was performed for this step. Historical validation records above
refer to their then-current fixtures and remain unchanged.


### Numeric instrument stepping and unified direction (hardware validation pending)

Schema-1 `[editing] value_step_direction=normal|reversed` applies once to Note and
numeric Instrument input, including holds. Built-in and safe-config use normal.
The deprecated `note_step_direction` alias accepts the same values and now applies
to both fields; configurations containing both names reject transactionally.
Effective startup logging reports `value_step_direction`. No user config is rewritten.

Numeric editing covers displayed 01..99 (internal 0..98). Increase cycles
99 -> -- -> 01; decrease cycles 01 -> -- -> 99. Fresh empty entry inserts 01
unchanged for either direction. A small interaction flag outside pattern/history
preserves cycling through empty across individual presses and held repeats.
Navigation, explicit field edits, Restore, Undo/Redo and context replacement reset
it. Holds renew the 400 ms pause at the directional endpoint and on entering --.
OFF's second field still toggles timing in either direction, without inversion.
Instrument edits preserve the note; higher device IDs are preserved and displayed
using the existing formatter, but unsupported numeric stepping/assignment rejects.

User confirmed 32-row editing and playback progression beyond row 16. This does
not confirm row-32 loop timing, physical sample timing or performance. Windows unified directions, boundary pauses and OFF timing are user-confirmed
below; remaining numeric edit edge cases and ArkOS validation remain open.
Test fresh entry, both cycles with taps/holds, reversed mode, OFF timing,
Restore/history and preservation of notes and received higher IDs.


### Temporary ArkOS handheld pattern-editor bindings

See [ArkOS controls](ARKOS_CONTROLS.md) for the authoritative user reference and
complete table, value direction, repeat, OFF timing and preview/publication semantics.
On **2026-10-15**, the user confirmed the current controls and workflows work on
ArkOS. This confirmation does not cover dArkOS, measured timing or performance.
Final configurable profiles remain future work.

Chords consume component actions. No legacy L1/B Start or R1/X Stop/exit mapping
remains. Value holds reuse shared repeat/acceleration/cyclic boundary pauses;
OFF timing toggles do not invert. Direction/modifier release, focus loss,
controller removal, disconnect and editor locking cancel repeats. Changing L1
while a direction is held never turns that hold into another action: release
and press that direction again. Opposing directions cancel and require release;
there is at most one value repeat stream. Navigation remains independent from
playback, and remains available during publication locking. Publication freezes
mutations, while B and clean exit retain shared STOP priority/recovery semantics.

Edits are observed by BringUpSerial before processing further recovery replies,
using existing automatic preview/coalescing. Restore does not publish; A uses
exact readback before rebase, never auto-Stops/Continues. Dirty comparison and
history remain shared. Controls/status use ArkOS labels in the existing wrapped,
clipped, eight-pixel-padded right panel; no bottom overlay or pattern-cell labels.

Mapping mechanism: discovery opens an SDL GameController and filters by its
joystick instance ID. Mapped events use SDL semantic buttons, not raw indices
inferred from horizontal/vertical layouts. Unmapped raw devices remain ignored.
If controller event delivery cannot be enabled, the supported raw fallback copies
button/hat bindings from that opened controller via
`SDL_GameControllerGetBindForButton`; it ignores controller duplicates and raw
axes. Hat changes release old directions before pressing new ones. Normal mode
ignores all raw joystick/hat duplicates. Removal releases the selected handle;
a subsequently added mapped controller can be selected without transport actions.

Physical A/B/X/Y labels, L1 placement, mapping correctness and OS/emulator hotkey
interception still require actual-device verification. Axis-only raw fallback
controls are unsupported; standard mapped controller axes remain SDL's mapping
responsibility. RetroArch/EmulationStation bindings are not assumed to apply.
See [SDL controller binding documentation](https://wiki.libsdl.org/SDL2/SDL_GameControllerGetBindForButton).

Validation: synthetic SDL input/coordinator tests cover navigation, chords,
release ordering/opposition, normal/reversed holds and OFF timing, removal,
instance filtering, verified raw button/hat normalization, publication gates and
STOP/clean exit. Shared rendering tests check handheld labels stay inside the
panel. Available Windows GCC/SDL C++17 syntax checks cover ArkOS main/input;
these are not ARM/Linux linking or deployment checks. No application binary was
built/packaged, no device accessed and no firmware changed.

The current ArkOS controls/workflows are user-confirmed above. Broader device
mapping and OS-hotkey coverage, exhaustive partial-transfer/reconnect/removal
checks, physical timing and performance remain open. Final profiles, calibration,
Undo/Redo bindings and dArkOS checks remain separate future work.

### Development fixture and measured callback display (2026-10-15)

The user confirms accelerated navigation on Windows and ArkOS separately from
this new fixture/load display, whose hardware validation remains pending.
The variable-rate development environment now selects eight active channels at
the configured 16/32/64-row length (default 32). CH1 cycles raw notes
60,64,67,72,67,64,62,65 with explicit instrument 0; CH2 cycles
62,65,69,74,69,65,64,67 with explicit instrument 1. Every active row has both
beeper events; there are no OFF events. CH3..CH8 are genuinely empty, including
instrument 0xFF and Arrival timing. Historical native-rate environments retain
their existing fixture. Instrument selection follows cells, not channel numbers.

Requested WAV identities are display 03/raw 2 = Samples/test.wav,
04/raw 3 = Samples/test2.wav, and 05/raw 4 = Samples/test3.wav, all reference 60
(C-3 anchor, not measured pitch). The current prepared test2.wav binding is now
raw 3 in the development environment and has no boot trigger. IDs 2 and 4 remain
unbound: three-file preparation is **blocked on exact metadata and safe storage**.
No sample substitution, truncation, lower quality, PSRAM assumption or dynamic
PCM allocation was introduced. Existing WAVPREP still describes test2.wav only;
it does not establish opening or existence of the other two exact paths.

The samples are on the Teensy's SD, not in the checkout. Previous hardware evidence
measures test2.wav at 68284 frames / 136568 PCM bytes / 44100 Hz. Historical notes
mention a larger test.wav but do not establish current file/chunk metadata;
test.wav and test3.wav must be measured before computing a complete reservation.
The installed pattern protocol cannot inspect arbitrary SD files. No upload or
SD writes were performed. Fake SD failure tests do not establish real file existence.

The development firmware compilation/linker check reports RAM1 variables 69280,
ITCM code 105480, padding 25592, leaving 323936 bytes for locals/stack; RAM2
variables 282912, leaving 241376 bytes for malloc/new. RAM2 includes the existing
262144-byte PCM reservation. These linker figures do not measure runtime stack,
heap or SD/cache headroom. Additional complete-sample RAM requirements remain
unknown; no new reservation is made. Storage-independent caller-owned views keep
future optional PSRAM possible without changing parsing/playback APIs.

BTX now strictly parses existing PERF v1 reports without changing firmware message
frequency or callback instrumentation. The live header uses CBpk (rounded percent)
and the panel identifies Callback peak/budget (steady, cumulative), using
maximum_cycles / budget_cycles, never initialization_cycles. This is player/audio
owner callback cost including its control work, **not total CPU or whole-audio-graph
load**; neither whole-graph nor whole-CPU measurement is available here. Missing,
initialization-only, disconnected or reports at least 3000 host ms old display
unavailable. Malformed reports preserve the accepted measurement without refreshing
its age. Host observation time does not establish physical audio timing.

Windows and ArkOS applications need rebuilding to include the shared display;
no application binaries were built or packaged in this step. Pending hardware
checks include eight-channel/every-row fixture, corrected 04 selection, measured
load freshness, complete three-file metadata/storage, and real per-file loading.

### Read-only boot WAV metadata inspection (implementation; hardware capture pending)

Historical source `firmware/teensy/BroTracker/platform.cpp` establishes the exact
paths Samples/test.wav, Samples/test2.wav and Samples/test3.wav; its streaming
call sites open all three. The earlier SD diagnosis record confirms they were
copied into Samples, and the user confirms those card contents are unchanged.
They are established existing samples, not guessed or presumed absent.
The September benchmark files in `_backyard/benchmarks/` measure library paths:
AmenVN_4barOrig.wav has 613984 file bytes / 613940 PCM bytes (mono PCM16 44100 Hz),
and bbeat-01 variants have 136612 file bytes / 136568 PCM bytes. Historical discussion
attributes the larger recording to test.wav, but the benchmark paths alone do not
identify all three root aliases. The 2026-10-11 test2.wav WAVPREP evidence directly
measures 68284 frames / 136568 PCM bytes. Exact root-file metadata for the complete
three-file total is still needed; neither expected bar counts nor BPM establish it.

SD-preparation environments now inspect all three exact paths once after SD.begin,
before existing test2.wav loading and before g_audio_ready. Each file uses SdReader
and holds its shared read lock through validation, then closes on every exit.
No PCM data is read by inspection, no SD writes are made, and no new PCM buffers
are reserved. The existing fixture, bindings, CBpk presentation and loaded sample
lifetime are preserved. The portable loader and inspector share checked RIFF/fmt/
data validation; the inspector limits traversal to 1024 chunks per file and reads
at most 18 fmt bytes plus fixed headers per chunk. Unknown payloads and padding
are skipped, not scanned. The loader retains its previous full-RIFF traversal.
Missing files, directories, I/O, malformed/truncated/duplicate/missing chunks,
unsupported formats and chunk-budget exhaustion have explicit statuses.

Main-loop diagnostics retain immutable boot results and emit these four lines
once after HELLO, including reconnect, through bounded 64-byte partial writes:

```text
BTPATTERN1 WAVMETA 1 <path> <sd_ready> <open_status> <validation_status> <flags> <file_bytes> <format> <channels> <rate> <bits> <data_bytes> <frames> <duration_us> <required_pcm_bytes>
BTPATTERN1 WAVTOTAL 1 <valid> <required_pcm_bytes>
```

Three WAVMETA lines use the exact paths in the order listed above. Fields are
unsigned decimal except the fixed path. Flags: 1=file size known, 2=fmt fields
read, 4=data extent read, 8=frame count/duration derived from a consistent PCM
layout. On rejection, known fields are diagnostic evidence, not a validated view;
zero with a missing flag means unavailable. Format is the raw WAVE encoding
(1=PCM). Duration is floor(frames*1000000/rate), in microseconds; it is not BPM
or reference pitch. Required bytes are available only for validated PCM16 mono
44100 Hz, equal to data bytes, and are otherwise zero/unavailable.

Open statuses: 0=success, 1=invalid path, 2=open failed, 3=directory (4=loader
failure is unused here). Validation statuses retain WavBufferStatus numbering:
0=success, 1=I/O, 2=invalid destination, 3=invalid capacity, 4=invalid container,
5=truncated, 6=invalid chunk, 7=duplicate, 8=missing, 9=invalid fmt,
10=unsupported, 11=invalid data, 12=insufficient capacity, 13=chunk limit.
Destination/capacity statuses are unused for inspection. If SD is unavailable,
sd_ready=0/open-failed/I/O identifies that no file inspection occurred.
WAVTOTAL valid=1 requires all three successful supported inspections; its checked
sum gives the combined complete PCM requirement. valid=0/bytes=0 is unknown,
not a zero-size allocation. No decision about three-file storage/PSRAM follows
until that valid result and memory headroom are reviewed.

The report holds one 256-byte line, never queues unbounded data, and performs
formatting/serial writes only in the main loop. ACKs and existing position/snapshot
traffic take priority at line boundaries; partial lines finish without interleaving.
No request for arbitrary filesystem access is added. Fake-reader tests verify exact
paths, header-only reads, supported/unsupported/missing/truncated cases, lock/file
release, isolated file failures, chunk limits and bounded reporting; they do not
establish new actual SD metadata or hardware behavior.

Compilation/linking of teensy41_dev passed: RAM1 variables 70304 bytes, ITCM 107784,
padding 23288, locals/stack free 322912; RAM2 variables 282912/free 241376. Compared
with the preceding fixture build, RAM1 variables increase by 1024 and RAM2 is
unchanged. Runtime stack/heap/cache headroom is not measured by these figures.

After the user uploads the compiled firmware, connect with BTX Debug logging,
without requiring START, and capture all three WAVMETA lines plus WAVTOTAL,
existing WAVPREP, and any errors. Archive the log separately before another launch
overwrites/rotates it. Confirm filenames and statuses, then use the valid total
for the RAM sizing decision. Reconnect should report the retained boot inspection
without new reads. No upload, hardware-session interruption or application
packaging was performed during implementation; real-card validation remains open.

### Automated device validation and SD sizing (2026-10-15)

Task-specific authorization covered building/uploading teensy41_dev and isolated
Windows-side protocol testing. The board was identified as Teensy 4.1 on COM3
(16c0:048a, Serial+MIDI+Audio); no BTX session was interrupted. Original terminal
logs and pre-upload published/working/gain readbacks were archived first. The old
unedited 32x3 boot fixture was replaced by the requested new 32x8 firmware fixture
at upload; the protocol test did not send preview/publication/gain mutations.

All three exact SD paths opened and validated successfully as encoding 1, PCM16
mono, 44100 Hz, flags 15. Actual read-only inspection returned:

| Path | File bytes | PCM/storage bytes | Frames | Duration (reported seconds) |
| --- | ---: | ---: | ---: | ---: |
| Samples/test.wav | 613984 | 613940 | 306970 | 6.960770 |
| Samples/test2.wav | 136612 | 136568 | 68284 | 1.548390 |
| Samples/test3.wav | 136612 | 136568 | 68284 | 1.548390 |

WAVTOTAL valid=1 confirms **887076 PCM bytes**. WAVPREP independently reports
successful existing test2.wav loading: 68284 frames, 44100 Hz, capacity 131072,
address 538970144 (0x20200820). No three-file PCM reservation was added.
The complete total exceeds current internal-memory capacity: even reclaiming the
262144-byte PCM reservation and combining reported RAM1 locals/stack free 322912
with RAM2 malloc/new free 241376 gives only 826432 bytes, a 60644-byte shortfall
before retaining any stack/heap margin. Those regions are not one contiguous
allocation; test.wav alone exceeds either 512 KiB bank. This is a measured blocker
for the present contiguous complete-sample internal-RAM approach, not permission
to truncate, substitute, assume PSRAM or change the playback/storage design.

The device snapshot checksum and all 256 ordered cells were validated: 32 rows,
eight channels, tempo 12753, explicit beeper sequences on every CH1/CH2 row,
no OFF, genuinely empty CH3..CH8 and Arrival metadata. Reported playback covered
all zero-based rows 0..31 and the row31/loop0 -> row0/loop1 transition. START,
Pause/current-row Continue and STOP/reset were acknowledged; the post-test
published/working snapshots and all gains exactly matched the post-upload initial
values. Final STOP was confirmed and COM3 released. No SD writes occurred.

Three PERF reports were valid: cumulative maximum 60272 cycles / budget 1741496
(3.46093%), initialization 5599 cycles, zero reported overruns. These are cumulative
callback measurements since boot, not total CPU, whole-audio-graph cost or stress
validation. An isolated Windows test fed those actual lines through production
BringUpSerial/PlaybackView, checking parsing, freshness available at 2999 ms,
unavailable at 3000 ms and disconnected unavailability. This Windows test is not
execution of the ARM64 ArkOS application. CBpk visual layout/readability, physical
audio timing and listening remain manual checks.

Raw logs, source/executable snapshots and manifests are local untracked evidence
under diagnostics/dev-wavmeta-20261015-fa7f9b2f03/, not repository deliverables.
The captured terminal log originals remained byte-identical. The authorization was
one-time; future application builds/firmware uploads still require authorization.

The existing Eoan ARM64 environment was reused incrementally (GCC 9.2.1, glibc
2.30, SDL 2.0.10). All 10 CTest targets and the established architecture,
interpreter, symbol-version, RPATH, dependency and relocation checks passed for
BroTrackerTerminal and BroTrackerAlsaBridge. All six package files were copied
and compared to the validated package in deploy/arkos, including navigation and
CBpk. This is ARM64 emulated build/test validation, not handheld deployment or
visual/listening confirmation. No dependencies were installed and no Windows
application was packaged.

### Two manual-test WAV bindings (current contiguous mapping; hardware validation pending)

The variable-rate development environment now prepares only Samples/test2.wav and
Samples/test3.wav for playback, independently, before audio activation. test2.wav
is display 03/raw 2; test3.wav is display 04/raw 3. Both use reference 60 (C-3
playback anchor). Samples/test.wav remains unbound/deferred; no extra samples,
substitution, tempo matching, normalization or resampling are added. Inspection
of all three established paths remains unchanged. CH1/CH2 beeper events are
unchanged, and CH3..CH8 remain genuinely empty. Select either WAV instrument in
any channel through the existing editor/preview workflow; there is no automatic
boot trigger or channel-to-instrument routing.

The development build replaces the old 131072-frame reservation with two separate
32-byte-aligned DMAMEM arrays of 68284 frames each: 136568 bytes per sample,
273136 PCM bytes total. No unnecessary old buffer remains in this build. The ELF
confirms addresses 0x20200820 and 0x20221DA0 and sizes 0x21578 each. Current linker
usage: RAM1 variables 70464, ITCM code 109832, padding 21240, locals/stack free
322752; RAM2 variables 293920, malloc/new free 230368. The RAM2 increase of 11008
bytes over the previous build includes alignment. These are linker figures, not
runtime stack/heap/cache headroom measurements. Historical native-rate preparation
and playback environments retain their previous single-buffer behavior.

Each load retains its own result and publishes a non-owning immutable view only
on success. Missing/unsupported/oversized/I/O-failed files remain unbound without
preventing the other sample or beepers from working. Buffers are caller-owned,
static for the boot lifetime and never rewritten after audio activation. Caller
storage keeps future PSRAM options independent of WAV parsing/playback; none is
assumed or allocated here.

Both development files now use path-qualified WAVPREP v2 reports, followed by
WAVHASH records. Historical native-rate environments retain WAVPREP v1. Capacity
remains 68284 frames per development sample:

```text
BTPATTERN1 WAVPREP 2 Samples/test3.wav <attempted> <sd_ready> <sd_status> <load_status> <frames> <rate> <capacity> <address>
```

Statuses retain existing SdWavBufferStatus/WavBufferStatus values. Reports are
main-loop only, finish partial lines intact, and defer to command acknowledgements
at line boundaries. PERF and metadata inspection remain outside audio processing.
Host tests cover independent success/failure/capacity, exact paths, no retry,
reference bindings, empty spare channels and exact sample rendering from either
instrument on CH1. Fake SD tests are not new real-file or hardware evidence.
Firmware compilation/linking passed; this revision was not uploaded. Manual
loading of both views, instrument 03/04 preview/publication, audio output and new
performance/memory behavior remain pending user validation. Unity summation can
saturate when full-level PCM overlaps beepers; final gain/headroom policy is still
separate work.

### Current sparse phrase and PCM content diagnostics

This revision supersedes the earlier dense/every-row development fixture and its
reserved instrument gap. Historical validation records above describe the firmware
that was actually tested; they do not validate this new phrase or checksum reporting.

| Display / raw ID | Source | Reference note |
| --- | --- | --- |
| 01 / 0 | Existing beeper 1 | 60 (C-3) |
| 02 / 1 | Existing beeper 2 | 62 (D-3) |
| 03 / 2 | Samples/test2.wav | 60 (C-3) |
| 04 / 3 | Samples/test3.wav | 60 (C-3) |

There is no reserved playback ID for test.wav; playback of it remains deferred,
while metadata inspection still opens all three established paths. Independent
loading, buffer addresses/lifetimes, failure isolation and native sample rates are
unchanged. Missing files leave their own IDs unbound; the other WAV is never
renumbered or substituted.

The eight-channel, default-32-row pattern uses this one-based phrase, with an
explicit beeper instrument on each listed cell:

- CH1 / 01: 01 C-3, 05 E-3, 08 G-3, 11 E-3, 17 F-3, 21 A-3, 24 C-4, 29 G-3.
- CH2 / 02: 01 C-2, 09 G-2, 13 B-2, 17 F-2, 25 C-3, 29 G-2.

B natural is raw 59 (German H-2), not B-flat/raw 58. Current international
formatter tables spell it B-2; no global note naming behavior changes here.
All other cells are NOTE_EMPTY/instrument 0xFF/Arrival, including CH3..CH8;
there are no OFF events. Length 16 uses the first half; length 64 repeats the
32-row phrase. Tempo, gains and transport semantics are unchanged.

The reported audible similarity is not established as a software routing defect.
The inspected route is exact SD path -> separate decoded PCM buffer -> explicit
binding ID -> selected view in prepared Trigger -> per-channel voice. Distinct
positive/negative fake PCM fixtures prove independently expected samples through
that whole path, including same-channel replacement, same-sample retrigger and
simultaneous mixing. Regular/irregular partition outputs match. Preview tests
verify UnknownInstrument rejection for failed/unbound samples, unchanged working
pattern/audio, retained raw draft data, and explicit host rejection without false
APPLIED or automatic retry. The former ID gap is corrected; these tests do not
establish that the gap caused the user's audible observation.

Successful boot loading now computes FNV-1a32 once over every decoded PCM16 frame,
low byte then high byte (two's-complement unsigned representation), starting at
2166136261 and multiplying by 16777619 after each byte. It excludes WAV headers,
metadata and unused buffer capacity. Work is bounded by the caller frame capacity
and runs before audio activation; there is no added audio-callback work or SD read.
The main loop retains and sends this line after each path-qualified WAVPREP:

```text
BTPATTERN1 WAVHASH 1 <path> <valid> <frames> <fnv1a32>
```

Path is Samples/test2.wav or Samples/test3.wav; numbers are unsigned decimal.
Only valid=1 means a successful load/checksum; a failed load reports valid=0,
frames=0, hash=0. Zero hashes alone must never be compared as content evidence.
FNV is a diagnostic fingerprint, not cryptographic proof. The bounded sender ends
Data spans at each newline, allowing ACK/telemetry priority between status/hash
lines while finishing any partially written line intact.

No actual new WAVHASH records were captured: this revision was compiled but not
uploaded, and no hardware or SD writes were performed. User validation should
capture both successful WAVPREP/WAVHASH pairs, compare valid PCM checksums rather
than lengths, test displayed instruments 03 and 04 explicitly at C-3 on the same
channel, and check the sparse phrase on 16/32/64 rows. Matching checksums would be
content-fingerprint evidence; differing checksums would establish different PCM
fingerprints, not explain audibility by themselves. Preserve the SD files as-is.

Firmware compilation/linking passed: RAM1 variables 70656, ITCM code 108936,
padding 22136, locals/stack free 322560; RAM2 variables 293920, malloc/new free
230368. The two 136568-byte aligned PCM reservations are unchanged. New hardware
checks, subjective listening and measured timing/performance remain pending.

### Development-only beeper level adjustment

User confirmation on 2026-10-15 covers Windows BTX functionality of the sparse
fixture and displayed WAV instruments 03/04. It predates the new level adjustment;
ArkOS confirmation of this fixture/instrument workflow and the new level balance
remain pending. This does not supply actual WAVHASH comparison evidence or resolve
the earlier identical-sound report by itself.

Only teensy41_dev now enables the named development amplitude multiplier 4,
approximately +12.04 dB. Historical teensy41_pattern, perf, SD prepare/playback and
explicit teensy41_pattern_variable_rate environments retain their original beeper
PCM. The generator multiplies the original quantized PCM exactly, preserving both
waveforms, 8192-frame duration, linear decay and zero tail. References stay 60/62;
the sparse pattern, WAV PCM/buffers/IDs, channel gains and mixer policy are unchanged.
There is no fixed-channel attenuation; these are instrument sample levels.

Every generated frame is checked before PCM16 narrowing at compile time and
against the original waveform formula in host tests. Development square extrema
are -2384/+2400 (absolute peak 2400); triangle extrema are -2048/+2036 (absolute
peak 2048). The pre-division int32 product is bounded by 4914600; post-scale
magnitude is at most 2400. Larger intermediate values and sample overflows are
excluded by these bounds. Overlapping voices can still saturate unity-gain
summation; no clipping-free balance or hardware performance is claimed.

Firmware build/link and focused host checks passed without upload or hardware
access. All 8192 frames of each actual ELF array were also verified as exact 4x
legacy PCM and within bounds. Linker headroom remains 322560 bytes for RAM1
locals/stack and 230368 bytes for RAM2 malloc/new; runtime margin is not measured.
ArkOS application sources are unchanged, so its validated package is retained;
new audible balance remains for user validation.

### Transactional sparse working preview

New opt-in firmware advertises `BTPREVIEW1 DELTACAP 1` alongside the existing
`BTPREVIEW1 CAP 1`, NEXT and STATE. BTX sends the following only after discovery;
older firmware continues to receive the existing full-preview exchange. All fields
are unsigned decimal, spaces are single, and each bounded line ends with LF:

```text
BTPREVIEW1 DBEGIN 1 <id> <base_revision> <changed_count>
BTPREVIEW1 DCELL 1 <id> <index> <row> <channel> <note> <instrument> <timing>
BTPREVIEW1 DCOMMIT 1 <id> <changed_count> <checksum>
```

The namespace/version is BTPREVIEW1/version 1 with separately negotiated delta
support. IDs share the existing preview NEXT high-water mark: nonzero uint32,
consumed once staging begins, never reset by HELLO/disconnect; NEXT 0 is exhausted.
`base_revision` is the authoritative accepted pending ID if present, otherwise
applied working ID (boot revision 0 is valid). The base must still match during
CELL/COMMIT; application of that same pending identity does not invalidate it.
An unrelated revision change returns existing ERROR syntax with code 12
(stale base), requiring STATE/readback recovery. Duplicate/reused transactions
reject, including conflicting contents; a submitted duplicate cannot reapply.

`changed_count` is 1..512 and cannot exceed the active coordinate space in a valid
transfer. Index starts at zero and increments exactly once. Coordinates are
zero-based within unchanged active dimensions; cells must be strictly ordered by
row then channel, with no duplicates. Notes retain 24..127, EMPTY and OFF semantics;
instrument values remain raw 0..254/255 no-update. Timing is 0 Arrival or 1
EndOfPosition, independently validated even when not meaningful for a non-OFF cell.
Checksum is FNV-1a32, starting at 2166136261, over id, base_revision and count as
three little-endian uint32 values, then five bytes per ordered cell: row, channel,
note, instrument, timing. DCOMMIT count and checksum must both match.

Staging uses the existing fixed pattern storage, not an additional audio buffer.
Every malformed/order/count/checksum/stale-base failure preserves working playback
and the published baseline. Unsubmitted staging times out after 5000 ms and is
cancelled by HELLO/disconnect. Submitted storage stays frozen until the audio-owner
acknowledgement; timeouts cannot prove cancellation. The existing host 6000 ms
timeout enters authoritative recovery rather than resending COMMIT. Audio validates the complete
reconstructed pattern through ReplaceWorkingPattern before acceptance. Existing
RECEIVING, SUBMITTED, ACCEPTED, APPLIED, SUPERSEDED, REJECTED and ERROR replies retain
their meanings and strict identities. Unsupported/missing instruments remain
explicit rejections with the draft visible; no automatic retry of that revision.

BTX captures complete cell results when edits/Undo/Redo/Restore occur, independent
of cursor movement. Fixed slots retain the latest value for every coordinate;
repeated unsent edits coalesce, while cross-channel edits are retained. One frozen
transfer plus one latest candidate bounds work/storage. Restore can send all 512
changed cells (514 lines); a normal single-cell edit sends only three lines.
Host capture slots/flags and delta indices add approximately 4 KiB of fixed storage;
firmware reuses existing pattern staging and adds only small revision metadata.
Existing 75 ms debounce/250 ms maximum delay and repeat timing are unchanged.
Backpressure delays/coalesces work without dropping cells or blocking navigation.
Attempted asynchronous lines remain immutable, and transport commands take priority
at complete-line boundaries. Discovery reserves 13 of the existing 16 TX slots.

Matched sparse SUBMITTED/ACCEPTED/APPLIED acknowledgements establish the submitted
revision without a whole-pattern GET after every tiny edit. Newer candidates may
supersede pending ones using the accepted candidate as their base. Initial sync,
reconnect, uncertain COMMIT and revision mismatch still use full authoritative
working/pending readback. Published GET/F5 baseline confirmation is unchanged.
No automatic publication, Start, persistence or timing-policy change is introduced.

Deterministic host tests use the real serial coordinator, strict parsers and player.
With injected 2 ms service/completion, a one-cell update is 97 bytes/three lines
for 16, 32 and 64 rows with eight channels: edit to first TX 78 ms, COMMIT completion
84 ms, ACCEPTED observation 86 ms and APPLIED observation 88 ms. The test advances
simulated audio explicitly; these are model timings, not hardware/audible latency.
An independent PCM fixture checks exact next-row application. The user's prior
approximately 1.14-second 256-cell transfer is historical hardware evidence, not a
controlled before/after comparison. New hardware behavior remains pending.

After user builds/uploads the updated firmware and runs updated BTX, check one-cell
and rapid cross-channel edits, Restore, STOP during transfer, stale/invalid drafts
and powered-device reconnect. Confirm ACCEPTED versus APPLIED ordering and capture
host timing separately from listening. An already-consumed row can still wait for
its next occurrence; instantaneous WYSIWYG is not promised.

Implementation validation: Windows host CTest 8/8 and Windows application build
passed; incremental Eoan ARM64 CTest 10/10 and terminal/ALSA-bridge ABI/dependency
checks passed, with the fresh package staged in deploy/arkos. teensy41_dev compiled
and linked: RAM1 linker space for locals 322560 bytes; RAM2 remaining for heap
230368 bytes. These linker figures do not measure worst-case runtime stack/heap
headroom. No upload, hardware access or new audible/performance result in this step.

### Sparse preview second-pass review (2026-10-16)

Adversarial regressions reproduced two high-priority recovery defects. A BEGIN
rejected before staging can leave NEXT unchanged; the host previously rejected
that legitimate discovery and could poll STATUS indefinitely. BTX now accepts the
authoritative value while retaining a separate attempted-ID floor across disconnect.
Later explicit revisions skip attempted identities; confirmed consumed IDs never
regress within the connection, and exhaustion becomes unavailable rather than
wrapping. Existing counter rollback on reconnect clears the local floor only when
it establishes a reset. There is no automatic retry of the uncertain candidate.

STATE could also relabel cached cell data with a different identity while Ready,
Rejected or Sending. All verified applied/pending copies now invalidate on such a
mismatch, forcing authoritative recovery before another sparse base is selected.
A completed but uninstalled recovery result is invalidated too. A real parser/owner
fixture applies an unobserved remote revision, recovers it, reconstructs the selected
latest draft, removes an unexpected remote cell and checks exact mixed PCM.

Restore/Undo to already verified applied content now reports synchronized without
consuming an identity or sending cells. Matching pending content remains explicitly
waiting for application; a later APPLIED for that older revision can settle it even
after a newer rejected attempt. Legacy full transfers still require their existing
exact readback, including when sparse capability arrives late. Absolute dimensions
and captured-cell bounds are guarded before fixed-array access. Explicit editor
context reset clears captured edits as well as history and draft data.

The Windows and ArkOS callers retain the shared coordinator and their existing
capture-before-recovery ordering. Review found no additional firmware frozen-storage
or attempted/partial-line ownership defect. Asynchronous tests hold owned writes,
change revision state, request STOP, complete partial lines and reconnect without
blind COMMIT replay or automatic Start/publication. Paused F5, exact published
readback, subsequent preview and Restore keep the baseline separate.

Fixed host storage remains bounded: 512 five-byte captured cells plus validity flags
use 3072 bytes; 512 delta indices use 1024 bytes. A Windows layout probe measures
LocalPatternEditor at 268596 bytes, primarily its existing 64-transaction history;
Undo/Redo capture adds one 1538-byte pattern backup on the host stack. The review
adds one uint32 attempted-ID counter, with platform-dependent padding. No player or
audio-callback storage/work is added by these corrections. Sparse transfer still
performs complete candidate validation through the owner; worst-case callback
cost/stack usage is not established by these host tests or linker headroom.

The injected 2 ms timing fixture remains 78/84/86/88 ms for first TX, COMMIT,
ACCEPTED and APPLIED observation, respectively, after an edit. No new hardware
latency result or scheduling optimization is claimed. The no-op Restore case sends
zero protocol cells/transactions when authoritative content already matches.
Hardware checks remain open: stressed STOP/backpressure, uncertain recovery,
rapid cross-channel edits and Restore after rejection, paused publication, actual
row-boundary audibility and maximum-capacity realtime cost on both target hosts.

Final review validation passes: 292 shared/core cases, all eight Windows CTest
targets, Windows application compilation, all ten incremental Eoan ARM64 CTest
targets and both ABI/dependency checks. deploy/arkos is refreshed only after those
checks. teensy41_dev compilation/linking passes with unchanged firmware headroom.
No Windows package, upload or hardware access was performed; new hardware behavior
remains pending.

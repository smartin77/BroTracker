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
- Page Up/Down increment/decrement the selected numeric value. An empty or OFF
  Note field initializes to C-0 (raw 24); pitched values cycle through C-0..G-8
  (24..127): incrementing G-8 wraps to C-0, decrementing C-0 wraps to G-8.
  Empty instrument fields initialize to zero; instrument bounds do not wrap.
- O enters NOTE_OFF in the Note field, defaulting to arrival-time `¯`. For OFF,
  the second field selects timing: `¯` stops at the current position's beginning,
  `_` stops at its end, whose duration follows musical timing/BPM. Both PgUp and
  PgDn toggle to the other mode; neither performs numeric instrument editing.
  Delete in this timing field restores `¯`. For pitched/empty notes the second
  field remains an instrument, so instrument-only events remain representable.
  Timing uses explicit local metadata; raw instrument IDs are preserved separately
  and never encoded or reinterpreted as timing. Changing the Note field resets
  timing to arrival mode.
- Delete clears the selected normal field to
  NOTE_EMPTY or instrument 0xFF; instrument-only cells remain representable.
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

NOTE_OFF timing selection is **local only**: the device snapshot/protocol has no
end-of-position timing representation, and Teensy continues its existing immediate
row-start NOTE_OFF playback. Publication, protocol/runtime representation and
end-of-position scheduling remain future work; selecting `_` does not change audio.

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
renders are silent and do not advance. Continue transactionally converts the next
absolute row-start tick to its sample position and restarts a cursor segment there;
it neither scans skipped ticks nor replays events. The next row is processed once
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
consumed position until its first successful render consumes the next row.
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
OFF remains a note-off command; explicit instruments with OFF remain visible.
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

Deferred instrument-numbering decision: prefer first visible instrument `01`
and no-instrument marker `--`; mapping to raw IDs and capacity are undecided.
Instrument IDs and their display are unchanged by this adjustment.

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

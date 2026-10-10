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

These records distinguish reported log/capture evidence from user listening and
visual observations;
exact sample/phase continuity and restart-at-zero hardware timing remain unmeasured.
Full hardware validation remains open, including hardware backpressure/staleness
checks, legacy regression, ArkOS, MQS and realtime performance. Logical telemetry
consistency and visible restart at row 0 / loop 0 do not measure exact physical
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

1. Close serial monitors, disable competing Windows Listen routing, upload the
   opt-in firmware, then launch BTX. Check an IDLE handshake and silence before
   pressing Space, including a cold boot with the board attached.
2. Press Space: check STARTED/playing and two distinct quiet diagnostic bursts.
   The immutable 16-row, two-channel pattern loops at 127.53 BPM, approximately
   every 1.88 seconds. It includes separate triggers, adjacent-row retriggers,
   simultaneous triggers at row 8 and row-start NOTE_OFF commands. In particular,
   channel B's row-14 NOTE_OFF cuts its row-13 burst short. Rows here are zero-based.
3. Press Space while playing: verify restart at the first burst, not continuation.
   Press Enter: check STOPPED/idle and silence. Space starts at row zero again.
4. Check several loops with Windows USB audio routing and, where connected, MQS.
   Mono PCM is routed identically to USB left/right and the existing MQS channel.
   No SD card or legacy WAV sequence is needed or played.
5. Stop, unplug/replug and let BTX reconnect: expect IDLE and silence until a new
   Space press. Reconnect while playing as a separate check: no START is sent by
   HELLO/reconnect; separately powered hardware may continue an already-running
   pattern, while a power-cycled board boots stopped. Inspect the authoritative
   STATUS reply rather than assuming disconnect stopped playback. Previously
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
IDLE, PLAYING or ERROR. STARTED/STOPPED are queued only after the audio owner
applies the request. Main-loop parsing is limited to 128 input bytes and one
complete command per service; USB writes are outside audio processing and limited
to 64 available bytes per service. Optional live position telemetry is described
below; it does not change the ordinary firmware's BTTEST1 exchange.

### BTPATTERN1 live position telemetry

The opt-in firmware emits this explicitly versioned ASCII line (LF terminated):

```text
BTPATTERN1 POS 1 <running> <valid> <tick> <row> <loop> <rows> <channels>
```

Fields are unsigned decimal integers separated by exactly one ASCII space;
no signs, tabs, extra fields or trailing spaces are accepted. The literal `1`
is the message version. `running` and `valid` are 0 or 1. `tick`, `row` and `loop`
support the full uint64 range. Active dimensions must be 1..16 rows and 1..8
channels, the current implementation capacities. A valid position requires
running playback, `row = (tick / 96) % rows` and `loop = (tick / 96) / rows`.
When invalid, tick/row/loop are all zero; running can still be 1 before the first
tick has been rendered. Row and loop indices are zero-based in the protocol;
BTX displays the row as one-based POS and keeps LOOP zero-based.

Position is the **latest musical tick actually consumed within successfully
rendered audio**, including empty rows. It is not the next sample/block position
or a measurement of physical output latency. Rejected/zero-frame renders retain
the prior core position; tick-free blocks retain it too. Configure, Start,
repeated Start and Stop invalidate it until another tick is consumed. The
bring-up fail-stop policy invalidates it on any fault. Transport, position and
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
unknown-cell placeholders: the loaded host preview tune is not the firmware
fixture. Its fixed preview/edit cursor is hidden in this device view, not moved
with playback; standalone/disconnected preview retains the existing layout and
cursor. UI refresh only renders snapshots and never advances playback.

Remaining Windows BTX + Teensy 4.1 checks **are not covered by the validation above**:

- Verify displayed active dimensions and unknown device cells match the fixture;
  moving highlighting and POS/LOOP changes are user-confirmed, but exact physical
  row-boundary timing and restart phase remain unmeasured.
- Check pending START/restart and terminal-error highlight clearing under delayed
  communication; STOP invalidation and highlight removal are confirmed above.
- Pause/slow host service or apply CDC backpressure: stale positions disappear,
  fresh positions recover, acknowledgements remain ordered and audio continues.
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

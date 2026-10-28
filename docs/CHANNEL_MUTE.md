# Session channel mute

Current desktop/handheld bindings and validation scope are maintained in
[BTX controls](BTX_CONTROLS.md); this page describes mute behavior and protocol checks.

Implemented on feature/midi-foundation, following D0036. Prior Windows/ArkOS
version was user-confirmed working. Windows mouse mute, header navigation and
enlarged numbers are now user-confirmed. Keyboard mute, example-config review,
listening and physical handheld checks remain unconfirmed. External MIDI hardware/playback and audible alignment remain pending.
Bridge diagnostics stay paused. No persistence, solo or Mixer screen is added.

## Input and presentation

The editor has a separate header target, never a sentinel pattern row. Up or
previous-beat from row zero enters it. Down/next-beat returns to row zero; Up in
the header stays there and the last pattern row never wraps into it. Header
Left/Right clamps channels; Windows Tab/Shift+Tab retains channel navigation.
The Note/Instrument field is retained for returning to pattern rows. Existing
bounded repeat and modifier/release/focus/controller-removal safeguards apply.
Header navigation changes no pattern data, history or preview cells.

Windows left press in a header selects it and requests one toggle. Actual window
coordinates are mapped through renderer-output dimensions and viewport into the
640x480 framebuffer. Motion/drag/held duplicates do not toggle. Border/outside
clicks have no new action. Handheld A in a selected header toggles mute and is
consumed before Publish; outside headers A keeps Publish. R1+Left/Right still
controls tempo. Existing L1 controls remain; header value edits have no cell target.

The shared production renderer draws confirmed device mask only. Pending, rejected,
uncertain and unsupported states are separate status text, never optimistic marks.
Selected header: orange outline inside existing border. Header digits now use the
existing bitmap at exactly 2x nearest-neighbor scale, centered in unchanged headers.
Muted header: two crossing
one-pixel diagonal segments centered on its number, each with a six-pixel half-length.
Equal X/Y steps form a square with four equal arms at 45, 135, 225 and 315 degrees,
clear of the orange frame and borders, using row_header_bright (76,195,201). At digit-covered pixels the
segment is background (16,16,16), using actual scaled font bitmap coverage rather than
incidental framebuffer RGB. Selection and cross coexist without touching borders.
This artwork is temporary; no new background bands or pattern color changes.

## Engine and voice policy

The eight-bit session mask belongs to Teensy, separate from pattern metadata and
mixer gains. SetMuteMask filters entire pattern row events BEFORE logical channel
state/preparation, so notes, instrument selections and future pattern FX cannot
execute on a muted channel. No catch-up occurs. Unmute triggers nothing itself.
The last actual selection is retained; skipped selections cannot silently update it.

A newly muted channel emits a sample-targeted Release at offset zero of its next
render. Sample release fades linearly to zero over ceil(rate/500) frames (89 at
44100 Hz, about 2 ms); Synth uses its existing smooth 15 ms NOTE OFF release.
MIDI uses the existing tracker/shared-pitch ownership path, including duplicate
owners. Only the last owner sends wire OFF. No broad controller reset is sent.
Obsolete per-channel deferred OFF is cancelled before generating new events;
other channels and future unmuted notes are unaffected. MIDI backend faults remain
isolated from internal playback; core/audio integrity failures remain fatal.

No blanket gain or renderer mute exists. The low-level voice renderer can still
accept a new trigger after release, leaving a separate future manual-input path.
Current monophonic replacement/NOTE OFF policy otherwise remains unchanged; the
separate Sample/Synth-continues-on-MIDI POC is not implemented.

Preview reconstruction normally scans prior rows. To avoid resurrecting suppressed
instrument changes, channels affected by mute retain their actual logical state
through reconstruction, including after unmute. New actual unmuted events still
update selections normally. This history protection clears for unmuted channels
at Stop/restart; muted channels retain their last actual selection. Configure/boot
clears mask/history. Preview/publication cannot retroactively play skipped events.

## Ordering and bounds

Main-context submission replaces one fixed latest mute slot under existing IRQ
exclusion. At the next audio block boundary it applies BEFORE the bounded request
FIFO (transport, tempo requests, preview and publication), then rendering runs.
Start/Stop and Pause/Continue retain the mask; Publish/Restore never include it.
Tempo still applies at the next unconsumed row boundary; remaining deferred OFFs
precede tempo/preview/row work. Mute releases come first at block offset zero;
remaining OFF/row commands retain deterministic sample/channel order. USB timing
never controls the shared engine timeline.

The existing 16-command render/export capacity remains: at a normal positive
block boundary, newly muted releases replace those channels' row/deferred work.
Storage and loops are fixed; no allocation, logging, I/O, locks or blocking USB
operations occur in rendering. Sample fade and Synth release progress by rendered
samples, independent of block partition. Real Teensy audio blocks are 128 frames.

## Bounded protocol and host coordination

```text
BTMUTE1 CAP 1
BTMUTE1 GET 1
BTMUTE1 STATE 1 applied_id next_id mask
BTMUTE1 SET 1 request_id mask
BTMUTE1 SUBMITTED 1 request_id
BTMUTE1 APPLIED 1 request_id
BTMUTE1 REJECTED 1 request_id
```

Mask is decimal 0..255, channel 1 is bit 0. Initial mask/applied ID are zero,
next ID one. IDs strictly increase for this boot; next zero denotes exhaustion.
Malformed/range/identity requests reject without mutation. SUBMITTED means accepted
into the latest slot, not physically applied. A later accepted full-mask request
may supersede one before the boundary; STATE reports the actually applied ID/mask.
All lines fit the existing 96-byte slot. Production HELLO has 21 lines, so firmware
reserves that bound and uses 24 fixed TX slots rather than dropping discovery.

Host keeps one frozen SET and one coalesced desired mask. Independent toggles XOR
the desired mask, including while a prior request waits for acknowledgment/readback.
Only confirmed STATE is rendered. Host serializes requests with existing transport,
tempo, preview/publication work; transport takes precedence at complete-line
boundaries. Possibly delivered SETs are never blindly resent. At five seconds an
unconfirmed request is uncertain and STATE is polled at most once per second.
Rejection is explicit: failed-channel intent is discarded, but later toggles on
independent channels survive and wait for readback before a fresh, never-reused
request ID. Reconnect discards old goals and reads current device state.
It does not replay toggles or start playback. Older firmware lacks CAP, so mute is
unavailable while ordinary controls remain usable. Boot defaults all unmuted.

## Validation and manual checklist

The preview uses the real editor and renderer: empty channel 8, sparse channel 7,
muted headers 1/7, selected-muted header 7 and ordinary unmuted headers.

Before manual Windows/handheld testing:

1. Navigate row 1 -> header with Up/beat-Up, move channels, return with Down/beat-
   Down. Check Note/Instrument retention, bounds and held/modifier-release behavior.
2. Windows: click headers once, hold/drag/outside-click. Handheld: A on header
   toggles without Publish; A in rows still publishes; R1 tempo remains available.
3. While mixed playback runs, mute/unmute each instrument type. Other channels and
   timeline continue; check smooth internal release and no unmute catch-up.
4. Check crossed digits plus orange outline and unchanged borders/beat/readout.
   Rapid independent toggles should settle to the requested mask, with honest
   pending/rejected status; transport and publication retain it. Reconnect reads
   current mask, reboot clears it. Preserve logs for unexpected behavior.

Manual checks do not replace external routing verification or simultaneous
separate-output audio onset/offset/jitter measurement. No audible sync claim.

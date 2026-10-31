# MIDI instrument foundation

Implemented on `feature/midi-foundation`, retaining the unmerged live-tempo and
beat-navigation history. Instrument 02 (raw 1) is now note-driven USB device MIDI.
01 Sample, 03 Synth, 04 retained Sample, test WAVs, gains, output choice,
USB_BRIDGE default and the default pattern are retained. Bridge diagnostics stay
paused. Timing stays 96 ticks/row, four rows/quarter, 384 ticks/quarter.

## Timing contract and inspected path

The objective is audible alignment at the combined hardware audio output. Four
separate properties must be assessed:

1. **Engine generation:** `MusicalTickCursor` supplies absolute sample positions;
   row events retain their exact offsets within each half-open render span. Due
   deferred OFFs precede tempo-segment changes, working-pattern application and
   row triggers. Successful rendering exports that same ordered command batch,
   never tentative validation/reconstruction commands. The MIDI adapter receives
   the absolute block sample position and each command offset. Failure exports
   no tentative work. Sample and Synth render at those offsets; MIDI adds no PCM.
2. **Dispatch/transport:** the output adapter maps the sample offset once to
   `render_callback_entry_us + floor(offset * 1000000 / 44100)`. This is an
   uncompensated **render-phase release**, not a physical-output timestamp.
   Wall-clock measurements do not advance or retime the engine. Callback-entry
   variation, timer service and USB/host scheduling add uncertainty after exact
   engine generation. Targets at the start of a block cannot be submitted until
   rendering/command export completes; they are counted as late by that amount.
3. **Internal audio latency:** `PatternAudioSource::update()` renders 128 frames
   (about 2.9025 ms), then transmits an AudioStream block. The inspected Teensy4
   PT8211/MQS drivers retain first/second block slots and copy 64-frame halves
   into the inactive DMA half while the other half plays. A newly rendered block
   therefore precedes physical output; callback entry is not its DAC onset.
   PT8211's configured oversampling/filter path adds its own response. Bridge
   mode uses MQS as the audio-update owner, also routes to USB Audio, then passes
   through USB packetization, host capture, bridge buffers and the selected host
   audio device. AudioMemory(8) is a pool size, not a measured eight-block delay.
   No exact end-to-end delay is inferred from these source-level observations.
4. **External response/alignment:** USB host routing, CME forwarding, DIN
   serialization where used, synth processing and patch attack determine external
   onset. None has been measured against internal audio in this milestone.

The dependency for physically precise dispatch is an observed mapping from the
engine sample timeline to the chosen audio output's physical phase, together
with measured external response for that topology. The first transport does not
satisfy the final audible-sync goal by itself. No lookahead, audio delay or fixed
compensation has been added.

## Bounded adapter and installed USB core

Inspected installed `framework-arduinoteensy` **1.162.0**, Teensy4:

- `usb_midi.h` note methods ultimately call `usb_midi_write_packed()`.
- `usb_midi.c` uses four 512-byte DMA TX slots. Packet size is 512 bytes at high
  speed or 64 bytes at full speed. The stock writer can yield/wait up to 40 ms,
  return silently on timeout/disconnect, and buffer a partial packet until SOF.
  A full packet or `usb_midi_flush_output()` initiates `usb_transmit()`.
- `usb.c::schedule_transfer()` primes the hardware endpoint. Its descriptor
  tripwire loop has a 2400-cycle bound; Teensy startup enables DWT. It also
  unconditionally enables PRIMASK. Core DMA acceptance still does not establish
  host receipt, external delivery or audible onset.

`tools/teensy_usb_audio/midi_core.py` validates installed source hashes and builds
isolated copies under `.pio/build/*/core-patches`; it never edits the installed
framework. `midi_tx.inc` adds an all-or-none, nonblocking attempt to reserve one
free descriptor and initiate a transfer immediately. At most sixteen USB-MIDI
words (64 bytes, including full-speed support) are copied. Stock MIDI send,
flush, SysEx and automatic partial-packet buffering are not used by BroTracker.
USB reset/SET_CONFIGURATION/suspend hooks and port connection/suspend bits
invalidate stale sessions. Configuration alone is not evidence that a MIDI
consumer is listening.

A static IntervalTimer services output every **100 us**, at priority **208**,
equal to the installed AudioStream software interrupt. Neither owner preempts
the other; main-context access uses the existing short interrupt exclusion.
Rendering performs no USB operation, logging, file access, allocation or lock.
Submission uses short PRIMASK/BASEPRI protection because the underlying PJRC
routine re-enables PRIMASK. Higher priority IRQ execution can defer service;
100 us is the configured cadence, not a certified maximum jitter.

The handoff holds **64 tracker commands**, retaining absolute sample targets,
release deadlines and input order. Each timer invocation handles at most eight
commands and initiates at most one transfer. Equal-target commands preserve
tracker order; replacements release before ON. More than eight coincident tracker
commands span multiple timer services, adding service-cadence spread before USB
transport. This is part of dispatch uncertainty, not sample-accurate wire timing.
Busy submission retains the
whole attempted group and ownership for retry, never partially commits it.
The adapter does not drain accumulated events whenever `loop()` happens to run.

At the timer-service timestamp, a command more than one nominal block (**2903 us**) late, malformed/backward
input, disconnected enqueue, or queue overflow latches a fault, cancels unsent
work and requests per-note cleanup. This is an explicit failure threshold, not
an agreed acceptable audible tolerance. A MIDI sink fault quarantines MIDI only;
Sample/Synth PCM and the shared timeline continue. The engine export is always
drained, including rejected batches, so recovery cannot replay it. Submitted DMA descriptors are also
checked every timer service for errors and for a **40 ms** completion stall,
using the installed core's timeout duration as a failure watchdog. This catches
an undelivered last OFF even with no future tracker events. Neither watchdog
proves that successful USB completion reached an external synth.

Saturating counters report queued submissions, busy attempts, deadline failures,
overflows, lifecycle discontinuities, maximum observed timer-service lateness,
cleanup submissions, transport faults and accepted USB transfers. Lateness is
measured at the service timestamp before packet preparation/endpoint priming;
it is not an actual wire timestamp or a transport-delivery deadline guarantee.
Diagnostics
are copied/formatted only outside rendering. `BTMIDI1 GET 1` returns two lines
(each fits the existing 96-byte serial TX slot):

```text
BTMIDI1 STATE 1 fault cleanup_pending queued submitted busy late overflow
BTMIDI1 TIMING 1 discontinuities max_lateness_us cleanup_submitted transport_faults transfers
```

## MIDI output failure isolation (2026-10-22)

The physical ArkOS report is a successfully installed current BTX package, Teensy
connected directly to the console without external MIDI equipment/router, and a
MIDI 02 note on tracker channel 3 causing playback ERROR. Removing the note did
not recover transport; USB reconnect restored operation until another MIDI note.
BTX remained responsive. Preserve the supplied original log under
`_backyard/midi-error/brotracker-midi-playback-error-20261022-205527.log`
(SHA256 `ce5d44b05f60d173bc4b0d5d0b476a7fd4e3d096e724ae4302653249a96d400f`).
It records CONTINUED at 20:42:17.150 then, 410 ms later, pattern fault=2 code=21
alloc=0. Later STARTs are rejected as midi-not-ready-or-restart-required; a later
preview applies while transport remains ERROR. ALSA reports zero capture/playback
XRUNs and zero discarded frames. This supports the isolation defect, not a proven
initiating MIDI fault: the old log contains no MIDI reason. It does not prove a
40 ms watchdog trigger, absent equipment as a trigger, or crackling's cause.

The previous code explicitly propagated adapter enqueue/fault state through
`g_control.OutputFailure()` to the core Render/ComponentError fault and then
blocked START/Continue on the latched MIDI fault. Those propagation/gating paths
are removed. Core configuration/render/allocation checks and their fatal handling
remain intact. Setup acknowledgment and configured-channel restart requirements
remain intact. USB configuration/attachment, transfer acceptance, USB completion,
application consumption, routing and external synth presence/delivery are separate
facts. Teensy USB MIDI cannot detect an external synth behind its host.

A fault cancels all 64-slot software work, rejects further musical batches for
that session, and requests asynchronous **MIDI TX endpoint only** DMA cancellation.
The timer writes ENDPTFLUSH and checks once per service for flush/prime/status
completion, reissuing the flush if the endpoint is still active. Only then does it
detach the queue head/reset descriptors using the installed core's TX configuration
routine. No wait loop, global USB reset or audio endpoint change is introduced.
If cancellation stalls, MIDI stays quarantined. Data already transferred before
fault observation cannot be recalled; cancelled DMA is never treated as delivered.

The existing note ledger survives cancellation. Best-effort targeted OFF cleanup
uses at most eight pitches/one submission per service. A stalled cleanup transfer
is cancelled and remains uncertain, with no endless accepted-transfer retry loop.
STOP/Pause/restart/reset may explicitly request a new bounded cleanup pass.
No broad controller reset is sent. Cleanup uncertainty clears only after the
cleanup pass's USB transfers complete, never merely because cancellation leaves
an idle endpoint or while disconnected. External release following these fault/
disconnect cleanup paths remains unverified; the manual normal-OFF confirmation
below is a separate result.

Stopped/paused SD operations distinguish active cleanup from idle quarantine.
A stalled cleanup remains reported as pending/uncertain, with its pitch ledger
and fault retained. Once backend cancellation is complete, DMA is idle, and
there are no queued MIDI events or pending abort, that quarantined state does
not block Save/Load/session persistence. Active cleanup, queued events, pending
cancellation and DMA still block storage; the audio/core readiness guards remain.
An explicit STOP/reset re-arms cleanup and temporarily blocks storage again.
Storage eligibility never confirms note release or clears a MIDI fault.

Normal internal playback can START/Continue while MIDI is faulted. **MIDI recovery
requires explicit STOP, completed cancellation and cleanup, then START while the
engine is stopped and USB configured.** Wait for STOPPED and cleanup_pending=0.
If the boundary arrives too early, internal playback starts with MIDI still
quarantined; repeat STOP/START after cleanup becomes possible. Recovery never
occurs halfway through a running/paused song, on reconnection, on SELECTED, or
just because a consumer appears. It emits only newly rendered events. If cleanup
cannot complete, internal playback remains usable and MIDI remains uncertain.
Serial session disconnect retains the existing intentional engine STOP policy;
a MIDI backend fault by itself does not invoke it.

Change-only bounded telemetry (also sent on STATUS/HELLO and explicit MIDI GET):

```text
BTMIDI1 OUTPUT 1 usb_configured fault cleanup_pending initiating_reason
```

Reason values: 0 none, 1 unavailable at enqueue, 2 release deadline, 3 capacity,
4 DMA transfer error, 5 invalid/backward event, 6 USB lifecycle, 7 timer initialization,
8 completion stall. The **first** reason stays latched across cleanup failures and
disconnect until successful explicit recovery. Busy/late/overflow/disconnect and
transport counters remain available in the existing STATE/TIMING replies.
The 2903 us release deadline, 40 ms completion watchdog, sample target timestamps,
ordering, 100 us timer cadence and service/storage bounds are unchanged. BTX logs
and displays MIDI health separately from transport; repeated identical health
states do not generate repeated change notifications. Older firmware without
OUTPUT health retains acknowledged/unsupported setup status.

Native tests use the actual USB extension with mocked DMA that accepts but never
completes transfers, rather than equating USB configuration with a consumer. Mixed
Sample/Synth/MIDI rendering stays bit-identical to a reference and advances the
same timeline across repeated notes/loops; tests cover cancellation, stalled flush,
busy/deadline/capacity faults, ownership/disconnect, uncertain cleanup and explicit
recovery. A native stall is not proof of the initiating
ArkOS hardware trigger. Those checks did not verify external playback or
combined-audio alignment; see the later manual Windows confirmation below.
The separate severe crackling report remains unresolved.

Windows hardware checks on the discovered Teensy 4.1 (serial 17681760, COM3)
observed completion-stall reason 8 when no WinMM application input was opened,
while mixed internal playback remained PLAYING and the timeline advanced. That is
separate evidence for the Windows topology; it does not identify the old ArkOS
trigger. The raw physical logs/captures are retained under ignored build storage.
Successful MIDI capture verifies content/order and host-observed timing only,
not external delivery, PCM quality or audible synchronization.

## Notes, voices and cleanup

Raw pattern note 69 produces wire note 69 with velocity 100. The A-3 display
convention does not transpose it. Each tracker channel owns at most one MIDI
note; selection-only cells never release or trigger. Arrival OFF releases now;
end-of-position OFF follows its musical deadline through live tempo segments.
A pitched replacement/retrigger releases its old MIDI ownership first, including
replacement by a Sample or Synth. Loop boundaries do not reset ownership.

Eight fixed ownership entries share one output channel. A separate 128-bit ledger
retains pitches involved in accepted transfers until the backend observes DMA
completion; a failed final OFF therefore remains recoverable after its logical
owner has gone away. Completion and lifecycle observation are serialized so a
USB reset cannot masquerade as successful completion. This ledger is bounded to
128 possible pitches, with at most eight targeted cleanup OFFs per timer service.
Pitches whose OFF completes are retired, so cleanup does not release historical
notes. USB completion still cannot prove external delivery.

When multiple tracker
channels target the same note, only its first owner emits ON; only its last
owner emits OFF. A sole owner's identical retrigger emits OFF/ON. A shared
identical retrigger holds the existing wire note, so it cannot independently
re-articulate without disturbing its peers. This deliberate limit avoids relying
on synth-specific duplicate-note stacking. It is not eight independent external
parts; the external patch may itself be monophonic or polyphonic.

Stop, Start/restart, Pause and engine reset cancel queued work and request one
OFF for each distinct owned or delivery-uncertain pitch. Continue retains the inherited
current-row replay behavior. Serial-session disconnect also cancels MIDI work
and requests engine Stop; reconnect never queues START. USB lifecycle changes
with queued/submitted ownership quarantine MIDI and retain a separate best-effort
cleanup ledger for reconnection. They do not fault the core/audio engine. While disconnected, no cleanup delivery is
claimed. OFFs retry in bounded services where delivery is possible, and cleanup remains
pending until the backend observes those transfers complete. No broad
CC reset/all-notes-off affects unrelated notes or parts. An abrupt CPU reset,
power loss or upload cannot deliver cleanup afterward; stop first where possible.
Fresh boot cannot know an external synth's retained voices. A STOPPED engine ACK
is not an assertion of completed USB cleanup or external release.

## Shared configuration and negotiation

Add to the existing shared host `config.ini` (keep its required `[btx]` section):

```ini
[midi]
output_channel=12
```

Windows and ArkOS/dArkOS use the same bounded parser. Decimal 1-16 is accepted;
valid explicit configuration takes precedence over firmware fallback 12.
Missing section/key or empty value uses 12. Invalid nonempty values produce one
startup warning and use 12 while retaining the other valid settings. Duplicate
keys/sections and unknown settings retain the parser's normal rejection policy.
Safe config bypasses external settings. The example is updated; user files are
never rewritten. Channel numbering changes only inside USB MIDI packing.

New firmware advertises `BTMIDI1 CAP 1`. BTX sends `BTMIDI1 SET 1 channel` only
when supported and requires matching `BTMIDI1 SELECTED 1 channel` before START
or Continue. Firmware gates these commands on accepted channel setup. A timer or
MIDI backend failure is an isolated output fault, not a transport gate. First valid selection activates the channel for this boot; repeated same
selection, including reconnect, is idempotent and does not start playback.
Another channel returns `BTMIDI1 ERROR 1 restart-required`. Malformed/range input
returns `invalid-channel` without changing selection. Host mismatch/rejection
and missing acknowledgment block playback; there is no blind automatic retry
within that connection. Reconnect repeats the resolved configuration safely.
Older firmware stays usable and exposes an explicit MIDI-unsupported status.
No instrument channel editor or channel persistence on Teensy is added.

## Approved direction: shared MIDI scheduling and selectable backends

Planning only (2026-10-26): USB device MIDI remains the only implemented output.
Keep one common engine-facing event/scheduling layer, preserving sample-domain
positions until a transport adapter needs them. Backend selection must not change
tracker notes, instrument IDs or pattern data.

| Output backend | Intended route and status |
| --- | --- |
| USB device MIDI | Implemented: Teensy device -> PC/handheld host software router (MIDI-OX/ALSA) -> H4MIDI USB device/client -> DIN synth. HxMIDI Tools configures H4MIDI's internal routing; it is distinct from the host software forwarding Teensy events. Handheld H4MIDI playback remains pending. |
| USB host MIDI | Future: independent Teensy host port -> optional hub -> synth, interface or controller. The main USB device connection to BTX remains usable. |
| Physical DIN MIDI | Future: Teensy UART plus appropriate electrical interface. That hardware is not installed and a production backend is not implemented. |
| Network MIDI over Ethernet | Future transport family: Ethernet hardware/link and the MIDI network protocol are separate choices. No network protocol is selected; arbitrary UDP carrying raw MIDI bytes does not establish interoperability. |

**Protocol capability is a separate axis.** MIDI 1.0 messages, UMP containers and
MIDI 2.0 semantics are not interchangeable names. MIDI 2.0/UMP is not a fifth
physical backend. Each transport must explicitly support its chosen encoding and
endpoint negotiation; MIDI-CI and Property Exchange are separate optional features,
not implied by UMP support. Any supported conversion to MIDI 1.0/DIN must expose
unsupported semantics and its conversion policy, rather than promise losslessness.
The MIDI Association's [UMP/MIDI 2.0 specification](https://midi.org/universal-midi-packet-ump-and-midi-2-0-protocol-specification)
and [Network MIDI 2.0 specification](https://midi.org/network-midi-2-0) are primary
references; detailed protocol selection, negotiation and conversion rules remain open.

### Future configuration and routing ownership

Conceptual setting groups only; no new INI keys or commands are defined here:

- Explicit output backend selection.
- Destination identity and port/virtual cable, separate from the MIDI channel.
- Backend-specific USB destination, DIN port or network endpoint/session settings.
- Protocol capabilities and any explicitly supported conversion policy.

A future application setting or shared config may request these without changing
tracker data. Teensy must validate and acknowledge the request and remains
authoritative for active routing and scheduling. Current `[midi] output_channel`
parsing, fallback, acknowledgment and channel-change restart rules are unchanged.
Start with **one explicitly selected output backend**. Simultaneous output/fan-out
is a later feature requiring its own ownership, timing and cleanup rules. No
automatic fallback may silently redirect notes to a different instrument.

Shared implementation requirements:

- Preserve musical event order, explicit OFFs and bounded note ownership.
- Fixed queue capacities and bounded execution; no blocking transport calls,
  allocation or filesystem I/O in audio/timing ISRs. Define overflow, deadlines
  and cleanup uncertainty for each adapter.
- Isolate backend faults from Sample/Synth and timeline progression.
- Change routes only in a safe stopped state with bounded cleanup and
  authoritative status. Define disconnect/reconnect destination identity handling;
  never replay stale NOTE ONs or automatically START.
- Measure latency/jitter and combined-audio alignment per backend/topology.
  Neither equivalent timing nor automatic compensation is assumed.
- Incoming controller MIDI and future HID actions are separate input paths.
  External MIDI volume remains separate from internal PCM master gain.

### Deferred USB host investigation

[PJRC Teensy 4.1 hardware](https://www.pjrc.com/store/teensy41.html) provides a
separate USB host connection. [USBHost_t36](https://github.com/PaulStoffregen/USBHost_t36)
supports MIDI, hubs, keyboards and mice, but the installed version's send path,
interrupt behavior and interaction with audio require review/adaptation against
the realtime constraints before adoption. Keyboard/mouse support also requires
integration with BTX UI/action ownership; library class support alone is not that
integration. No RAM reservation, implementation schedule or latency improvement
is promised. USB host is deferred; successful handheld H4MIDI playback would
remove its immediate urgency, but that physical test remains pending.

## Physical confirmation and future voice experiment (2026-10-22)

The user confirms on ArkOS that adding a note with MIDI instrument 02 no longer
stops internal playback. External MIDI equipment was not connected. This confirms
failure isolation only: it does not establish fault-free MIDI output, external
delivery or audible synchronization, and says nothing about external-device
presence detection. Other unreported recovery cases remain unconfirmed.

The user also exercised different instrument types within one tracker channel.
A separate future POC may investigate letting an already sounding Sample/Synth
voice continue when a MIDI note arrives on that same tracker channel. This is
exploration, not an approved voice-policy change. Current monophonic replacement,
MIDI ownership, instrument selection-only behavior and NOTE OFF semantics remain
unchanged. Any proposed overlap needs separately reviewed lifetime/OFF/cleanup
and retrigger rules before implementation.

The current development/test2.wav/test3.wav fixture now starts at exactly
155.00 BPM (applied and published); firmware fixture initialization, host dummy
tune and preview metadata agree. New projects still default to exactly 125.00 BPM.
This changes no WAV bytes, sample rate, pitch, stretching, MIDI dispatch or voice
policy. Bridge diagnostics remain paused. External hardware/playback was not
checked in that run; the later Windows USB route is manually confirmed below.
Physical combined-audio measurements remain pending.

The updated development firmware was built/uploaded to discovered Teensy
17681760 / COM3. Bounded serial checks verify both initial and final applied /
published fixture tempo 155.00; tempo/publication, Windows MIDI capture and Synth
helpers pass and leave the original fixture restored, playback stopped and handles
closed. Windows 8/8 and Eoan ARM64 20/20 suites plus package checks pass. These
PC checks add no external-delivery or audible-sync claim.

## Actual routing prerequisites

### User-confirmed Windows USB route (2026-10-24)

The user manually confirmed successful playback through **Windows BTX -> Teensy
USB MIDI -> MIDI-OX -> WASP DELUXE USB**. BTX controls Teensy; MIDI-OX routes
Teensy directly to WASP. **MIDI-OX Event Port was disconnected.** This is a
user-reported physical/listening result, separate from automated USB capture
and SD readback checks.

Without an explicit OFF the note stays held. OFF releases it, after which WASP
completes its envelope tail; the user accepts this behavior. The user heard no
objectionable timing difference between external MIDI and internal Samples/Synth
and is satisfied with this topology. Exact offset, jitter and drift remain
unmeasured: this is listening acceptance, not measured synchronization proof.
No compensation is introduced or inferred for another topology.

Windows H4MIDI/DIN playback is separately confirmed below; handheld routing
remains unverified. The newly replaced SD bass
fixture was verified by authoritative readback, not auditioned during provisioning;
this manual confirmation does not relabel that run as a listening test.

### User-confirmed Windows H4MIDI/DIN routes (2026-10-24)

For all four combinations, Windows BTX controls Teensy 4.1. MIDI-OX routes
**"Teensy MIDI/Audio" input directly to the H4MIDI output**. H4MIDI connects to
Windows through its USB-C device/client port; **H4MIDI DIN OUT1 -> Behringer WASP
MIDI DIN IN**. WASP is separately powered and its USB connection is disconnected.
Configured MIDI output and WASP receive channel are **12**. Teensy sends USB MIDI;
H4MIDI provides USB-to-DIN conversion/output, not a Teensy DIN implementation.

| User-tested combination | PC/USB attachment | Hub B external power | H4MIDI separate external power | User-confirmed result |
| --- | --- | --- | --- | --- |
| Direct PC | Teensy and H4MIDI on separate front-panel PC ports; Hub B bypassed | Not in route | No | **WORKS** |
| Powered Hub B | Hub B on front USB panel; Teensy and H4MIDI both on B | Yes | No | **WORKS** |
| Unpowered Hub B, powered H4MIDI | Hub B on front USB panel; Teensy and H4MIDI both on B | No | Yes | **WORKS** |
| Unpowered Hub B and H4MIDI | Hub B on front USB panel; Teensy and H4MIDI both on B | No | No | Earlier failure: playback failed; HxMIDI Tools and MIDI-OX hangs observed. Later tests passed (2026-10-31, below). |

The earlier unpowered shared-hub failure is a historical, intermittent observation,
not a rule that this configuration necessarily fails. Power availability was
suspected, but no electrical measurements or cause were established. The later
successful tests below supersede the warning against using this arrangement.
These are separate user confirmations, not automated or long-term stability tests.
The pure-USB OFF/envelope and subjective timing acceptance above is not transferred
to the DIN routes. Measured offset/jitter/drift remain open; no compensation is
introduced.

![H4MIDI saved USB-to-DIN routing reference](https://github.com/smartin77/m8-headless-on-r36-game-consoles/raw/main/images/H4MIDI-OUT1-2-routing.png)

Saved USB-to-DIN routing reference ([source image](https://github.com/smartin77/m8-headless-on-r36-game-consoles/raw/main/images/H4MIDI-OUT1-2-routing.png)).
The image illustrates OUT1/OUT2 routing; only DIN **OUT1** playback is confirmed.
OUT2 was not tested.

### User-confirmed Windows shared-hub tests (2026-10-31)

Setup: **Windows 11 25H2, PC front USB panel, hub B**, with Teensy 4.1,
H4MIDI-WC and a Wi-Fi dongle attached to B. The route was **Teensy USB MIDI ->
MIDI-OX -> H4MIDI USB-C device port -> configured DIN OUT -> Behringer WASP**.
A standalone PowerShell arpeggio also tested H4MIDI independently of BTX and
MIDI-OX.

| Hub B external power | H4MIDI external power | User-confirmed result |
| --- | --- | --- |
| Connected | Connected | Arpeggio and BTX/WASP worked. |
| Disconnected | Connected | BTX and MIDI kept working. Reconnecting the Wi-Fi dongle during pause and playback caused no visible interruption. |
| Disconnected; entire hub reconnected with all devices attached | Connected | Arpeggio and BTX/MIDI worked. |
| Disconnected | Disconnected | Arpeggio and BTX/MIDI worked; a subsequent reconnect of the entire chain also passed. |

The PC has a **550 W PSU**. These successful unpowered hub/H4MIDI tests apply to
this PC and front USB port; the PSU rating does not establish current available
at that port. No electrical measurements or universal USB power requirement are
inferred.

### Open issues and platform limits (2026-10-31)

- **Windows routing after reconnect:** MIDI-OX lost its Teensy input after a hub
  reconnect. Restarting MIDI-OX restored routing while BTX stayed open.
- **Windows MIDI hangs:** during earlier testing, MIDI-OX and a direct WinMM
  PowerShell test hung, and Windows MIDI Service remained `STOP_PENDING`.
  Forcibly ending that service's own process and starting the service restored
  the arpeggio and later MIDI-OX operation. This is an observed recovery, not a
  guaranteed fix; the underlying cause is unknown.
- **Unstable USB tests:** earlier descriptor failures, hub disconnects and
  SD-unavailable were observed. No shared cause is confirmed. After reconnecting
  Teensy, the previous session returned; this does not explain the failures.
- **Multiple BTX instances:** two simultaneous instances produced a misleading
  waiting-for-device state. Prevention is an [open improvement](TODO_LIST.md#next-development-steps),
  not an explanation for the unrelated MIDI or SD failures.
- **dArkOS:** H4MIDI detection was intermittent, and direct notes sometimes failed
  with USB enumeration/URB errors. Standalone arpeggio also worked directly and
  through hub B in other tests. Full **BTX -> H4MIDI -> WASP** operation remains
  open. These dArkOS observations do not validate ArkOS; ArkOS external routing
  remains separately pending. Windows tests validate neither handheld platform.
- **Handheld power/topology:** the R36H runs on battery. Power connected to its
  separate USB-C charging port did not appear to improve the observed behavior,
  but this was not measured or systematically isolated. Handheld USB power and
  host-controller behavior may differ from the PC. Supply, controller and topology
  remain possible factors to investigate; neither insufficient handheld USB power
  nor a particular faulty hub has been proven.

### Earlier automated capture and other routing prerequisites

The earlier automated topology was **Teensy USB device -> Windows USB host ->
WinMM input capture**. Windows enumerated `Teensy MIDI/Audio`. No CME output was
present in the observed WinMM output list (only Microsoft GS and Teensy).
No external synth or two-channel audio measurement was available to this run.

For the confirmed PC/CME/DIN route above, connect Teensy and the CME's USB
**client/device** port to the PC, directly or through a tested hub arrangement.
Configure a host MIDI routing application (for example a user-chosen MIDI-OX route) from Teensy MIDI input to the appropriate
CME MIDI output, then connect that CME DIN OUT to synth DIN IN. Confirm the
CME's saved routing/filtering, output port, synth receive channel and patch.
Neither Windows USB attachment nor BroTracker config creates that route. No
Windows router is implemented or automatically configured here.

The [CME H4MIDI WC manual](https://www.cme-pro.com/wp-content/uploads/2025/02/H4MIDI-WC-user-manual_English_v05b.pdf)
describes a USB-A **host** port able to host class-compliant USB MIDI devices,
plus a USB-C client port and configurable routes through HxMIDI Tools. If the
user's exact model has that host port, a physical alternative is Teensy device
-> CME USB-A HOST -> CME DIN OUT -> synth. That uses CME as host; it does not add
USB Host MIDI to Teensy. Composite-device compatibility and saved routing need
checking. However the current firmware requires BTX CDC setup/transport control:
the CME does not automatically forward Teensy's CDC interface to a PC. A single
Teensy USB port cannot simultaneously attach to PC and CME as two hosts. Thus
this alternative is not asserted operational with current BTX control; use the
PC routing topology for this milestone. Two USB device ports never route to one
another without a host.

## Physical measurement still required

1. Use a temporary pattern with a known sharp internal Sample/Synth onset and
   MIDI note at the same known scheduler positions. Retain a copy of the tune.
   Choose a fast external patch; document its envelope and any processing.
2. Record internal audio and external synth audio **separately and simultaneously**
   on two channels of the **same** recording device. State whether internal audio
   was PT8211 or USB_BRIDGE, including host device/buffer settings, MIDI router,
   CME ports/routing, DIN/USB path, synth and patch. Avoid monitoring loopback or
   channel-specific effects that introduce unknown delays.
3. Capture many repeated isolated triggers across rows/loops and tempo changes.
   Estimate each channel's onset consistently (inspect waveform/threshold or
   correlation with known transients). Interpret external patch attack separately
   from dispatch/transport delay; a slow attack is not evidence of USB jitter.
4. Report repeated external-minus-internal onset offsets in samples and ms.
   Report the constant offset (mean/median) separately from jitter (spread,
   min/max, standard deviation and outliers). Preserve captures and method.
   Agree an acceptable tolerance with the user; none is chosen here.
5. Repeat for each audio/MIDI topology. One topology's result or compensation
   does not apply automatically to another. Timestamps, successful USB delivery
   and unmeasured listening cannot verify audible alignment.

If evidence requires compensation, review it separately. MIDI lookahead needs a
known scheduling horizon and a policy for edits/tempo changes whose external
notes may already have been sent; live input cannot be sent before it arrives.
Internal audio delay can align a slower MIDI route but increases monitoring,
live-edit and input latency and needs bounded buffering. Choose from measured
constant offset and jitter; do not conceal variable jitter with fixed delay.

## Validation and manual checks

Generated captures, logs and review exports remain ignored; ARM64 binaries and
required package assets are intentional Git delivery. The initial Eoan build
validation did not refresh deploy/arkos. Use the corrected
[build-to-delivery workflow](../tools/arkos/README.md#build-check-and-prepare-git-delivery)
for a console update.

Actual results on 2026-10-22: Windows BTX with `-EmptyWindowTitle` passed all
8 suites; clean GCC 13.3 Release passed all 6 CI suites; Eoan ARM64 application,
all 7 suites and ABI/dependency checks passed. The core suite has 325 Windows cases and 323 cases in the CI configuration.
All 8 affected Teensy configurations built; final development FLASH is 173052
bytes, RAM1 free 348960 bytes and RAM2 used/free 295968/228320 bytes.
Development upload to COM3 / Teensy 17681760 passed. Windows WinMM captured
26 messages in the three-loop check, verified the first 24 against exact expected
content/order, and observed three 960 ms loop intervals at WinMM's millisecond
resolution. Boot-cumulative diagnostics reported zero deadline, overflow,
discontinuity or transport faults and a 187 us maximum observed timer-service lateness.
Cleanup was not pending at readback. Pause/Continue, setup rejection, idempotent
reconnect and restart-required checks passed. The original tune/tempo was restored,
playback stopped, serial closed and WinMM close returned success. These observations
do not measure DAC latency, external synth response or audible synchronization.
Native cases cover config/channel mapping, exact samples/order, ownership,
replacement, cleanup/disconnect, live tempo, deferred OFFs, loops, partition
independence, handoff bounds/deadlines, actual core DMA submission/error/stall
behavior, acknowledgment/rejection/reconnect and restart-required selection.
Existing Sample/Synth, publication, serial, tempo and input suites are retained.

Manual checklist:

- Add/select the MIDI route explicitly; set the synth to the resolved channel.
- Enter instrument 02 with A-3/raw 69; check velocity, retrigger, OFF timing,
  Sample/Synth replacement and documented identical-note sharing limits.
- Exercise live tempo, looping, Pause/Continue, STOP/START and reconnect; confirm
  no automatic START and check `BTMIDI1 GET 1` for faults/pending cleanup.
- Change channel in config, restart BTX, confirm restart-required, then restart
  Teensy and confirm the new acknowledgment before playback.
- Perform the two-channel measurement above before claiming measured alignment
  or choosing compensation; topology-specific listening acceptance is recorded above.
- Keep remaining handheld checks separate from Windows results; neither ArkOS
  nor dArkOS external MIDI routing is established by the PC confirmations.

## USB lifecycle review (2026-10-23)

Timer service captures the USB generation with its lifecycle observation. The
nonblocking submit adapter rechecks that generation inside the existing USB IRQ
exclusion, through DMA submission. This prevents a reset/reconfiguration between
observation and submission from accepting a packet computed for the old session.
Refusal leaves work for the next lifecycle observation to quarantine/discard, with
the uncertainty ledger retained for bounded cleanup. Queue-level regression models
refused submission without queue commit, next-observation quarantine, OFF-only
cleanup and explicit stopped recovery before fresh commands. Production wiring
and hardware interrupt exclusion were inspected against the installed Teensy core;
the model does not execute the interrupt controller or physically inject this race.
Neither model nor PC MIDI capture establishes audible synchronization.

Protocol identities reset only at a detected connection/session boundary. Without
a boot/session identity, an undetected device reboot on a surviving host connection
cannot reliably establish a new protocol session. Explicit disconnect and fresh
negotiation/readback remain required; no protocol change is implemented.

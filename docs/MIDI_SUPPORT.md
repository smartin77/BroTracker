# MIDI Support Specification

## Status

Draft

## Purpose

This document defines the MIDI capabilities currently planned for BroTracker.

The implementation is intentionally conservative and focused on reliable sequencing rather than advanced MIDI processing.

## Supported Interfaces

BroTracker is designed to support MIDI through platform-specific interfaces.

The initial implementation prioritizes USB MIDI.

Direct physical DIN MIDI connections on the Teensy are planned but intentionally deferred.

The MIDI subsystem must remain independent of the physical transport. The
core consumes and produces transport-independent MIDI events, while
platform-specific MIDI backends are responsible for receiving and transmitting
them through the available hardware or operating-system interface.

### Future MIDI over Ethernet

Teensy 4.1 provides Ethernet capability, so Ethernet may become an additional
MIDI transport in a future BroTracker implementation.

No MIDI-over-network protocol or implementation is selected by this document.
The eventual choice must follow research and evaluation of applicable official
or established specifications and protocols. It must also be practical within
Teensy 4.1 CPU, memory, buffering, latency and realtime constraints.

Where practical, Ethernet MIDI should enter and leave BroTracker through the
same transport-independent MIDI boundary as USB, future DIN MIDI and host MIDI
backends. Tracker, scheduler and instrument logic must not become coupled to a
particular network transport. The exact protocol and implementation remain a
future architecture decision.

## MIDI OUT Priority

MIDI OUT is a primary BroTracker feature.

MIDI sequencing is not treated as a secondary or optional extension of the internal audio engine.

Internal audio events and MIDI OUT events should originate from the same realtime scheduling model.

The primary realtime performance goals are:

- low MIDI OUT latency;
- low MIDI jitter;
- deterministic event ordering;
- close timing alignment between internal audio and external MIDI devices.

The exact latency characteristics of each host platform or transport are implementation and measurement concerns.

MIDI timing must remain independent of the audio transport used by the host platform.

In particular, USB Audio and USB MIDI may pass through different buffering and clock domains when a host system is involved.

The Teensy realtime scheduler remains the authoritative timing source for both internal audio events and MIDI events.

## Initial Development Configuration

During initial hardware development, BroTracker will use USB connectivity.

The primary external MIDI routing hardware is the CME H4MIDI.

The initial development path is:

Teensy 4.1
    |
   USB
    |
CME H4MIDI
    |
 DIN MIDI
    |
External MIDI hardware

This configuration avoids requiring direct DIN MIDI circuitry on the Teensy during the initial development phase.

Direct physical DIN MIDI IN/OUT on Teensy hardware is deferred until a later stage.

## MIDI Input

MIDI input is supported through platform-specific MIDI backends.

The shared core should consume a common internal MIDI event representation rather than depending on a specific MIDI device or driver.

On Teensy, the initial MIDI input implementation will use the available USB MIDI path.

Host platforms may support additional MIDI input devices through their native or common MIDI APIs.

Specific host MIDI devices are not part of the core architecture.

## External Instrument State

BroTracker plans to support saving and restoring the state of external MIDI instruments.

This is intentionally designed as a general instrument capability rather than a SysEx-specific feature. Different hardware may provide different ways to capture and restore its state.

### Manufacturer SysEx

BroTracker plans to support both sending and receiving MIDI System Exclusive (SysEx) messages.

For many existing MIDI synthesizers, manufacturer-specific SysEx is the standard way to transfer patches or other device state.

For example, a Novation Bass Station II instrument module could store the SysEx patch data used by a Tune. When the Tune is later loaded with a compatible Bass Station II connected, BroTracker could send the stored patch back to the synthesizer before playback begins.

Device-specific SysEx behaviour should belong to the appropriate instrument or device module. The generic MIDI subsystem should transport SysEx data without containing manufacturer-specific knowledge about its contents.

### MIDI-CI Property Exchange

Modern MIDI devices may provide a more standardized way to discover capabilities and exchange device state through MIDI Capability Inquiry (MIDI-CI) and Property Exchange.

Where supported by the hardware, BroTracker may use MIDI-CI Property Exchange features such as Device State instead of manufacturer-specific SysEx handling.

This provides a future path for compatible devices without removing support for the large amount of existing MIDI hardware that relies on traditional SysEx.

The exact MIDI-CI and Property Exchange implementation will be designed later according to the official MIDI specifications and the capabilities of Teensy 4.1.

### State Restore and Playback

External instrument state should normally be restored when a Tune or instrument is loaded or initialized.

The intended workflow is:

1. Load the Tune and its instruments.
2. Identify the connected compatible MIDI device.
3. Restore the required External Instrument State where available.
4. Begin normal playback.
5. Control the instrument during playback using realtime MIDI messages such as notes, note states and CC messages.

Large SysEx or device-state transfers are therefore not intended to be part of the normal realtime playback path.

Future devices may have valid reasons to use SysEx, Property Exchange or other state-related messages during playback, but such behaviour should be implemented only when specifically required.

### Implementation Limits

SysEx messages and other device-state transfers can vary greatly in size.

Their buffering and transfer must respect Teensy 4.1 memory limits and must not interfere with BroTracker's realtime operation.

The exact buffering strategy, External Instrument State storage format, device-module API and automatic restore workflow remain future design decisions.

## Host MIDI

Windows, macOS and Linux host implementations may provide MIDI input and output through platform-specific backends.

The host MIDI implementation must translate platform-specific MIDI APIs into the common BroTracker MIDI event model.

The shared core must not depend on a specific operating-system MIDI API.

Android MIDI support may be added in a future platform implementation.

## DIN MIDI

Direct Teensy DIN MIDI IN/OUT hardware is planned for a future development stage.

It is intentionally not required for the initial realtime engine implementation.

The MIDI core and scheduler must nevertheless be designed so that a future DIN MIDI backend can be added without changing tracker or sequencing logic.

## Core Principles

BroTracker shall:

- receive MIDI events
- transmit MIDI events
- support external MIDI synchronization
- support internal MIDI sequencing
- provide deterministic real-time MIDI timing

## Scope

BroTracker is not intended to become a full-featured MIDI routing application.

Advanced functionality such as:

- event filtering
- event transformation
- channel remapping
- scripting
- MIDI merging
- complex routing graphs

is intentionally left to dedicated external software or hardware.

## Compatibility

BroTracker should interoperate with existing MIDI routing solutions whenever possible.

Examples include:

- MIDI-OX
- loopMIDI
- Pocket MIDI
- TXL MIDI Router
- CME H4MIDI

These examples are informative only and do not represent required integrations.

## Future Considerations

A simple internal MIDI Patchbay may be considered in a future revision.

The intention would be to provide straightforward routing between supported MIDI interfaces (for example USB and DIN) without implementing a full-featured MIDI router.

This remains a future design consideration and is not part of the current specification.

MIDI timing follows the common realtime scheduling model defined in [SCHEDULER.md](SCHEDULER.md).

Platform-specific MIDI transport characteristics, including latency and buffering, are implementation and measurement concerns and must not change the logical timing model.
The realtime timing model is defined in [SCHEDULER.md](SCHEDULER.md).

MIDI-specific behaviour is defined in [MIDI_SUPPORT.md](MIDI_SUPPORT.md).

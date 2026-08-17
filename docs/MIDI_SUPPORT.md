# MIDI Support Specification

## Status

Draft

## Purpose

This document defines the MIDI capabilities currently planned for BroTracker.

The implementation is intentionally conservative and focused on reliable sequencing rather than advanced MIDI processing.

---

## Supported Interfaces

BroTracker is designed to support MIDI through platform-specific interfaces.

The initial implementation prioritizes USB MIDI.

Direct physical DIN MIDI connections on the Teensy are planned but intentionally deferred.

The MIDI subsystem must remain independent of the physical transport. The core generates MIDI events, while platform-specific MIDI backends are responsible for transmitting them through the available hardware or operating-system interface.

---

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

---

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

---

## MIDI Input

MIDI input is supported through platform-specific MIDI backends.

The shared core should consume a common internal MIDI event representation rather than depending on a specific MIDI device or driver.

On Teensy, the initial MIDI input implementation will use the available USB MIDI path.

Host platforms may support additional MIDI input devices through their native or common MIDI APIs.

Specific host MIDI devices are not part of the core architecture.

---

## Host MIDI

Windows, macOS and Linux host implementations may provide MIDI input and output through platform-specific backends.

The host MIDI implementation must translate platform-specific MIDI APIs into the common BroTracker MIDI event model.

The shared core must not depend on a specific operating-system MIDI API.

Android MIDI support may be added in a future platform implementation.

---

## DIN MIDI

Direct Teensy DIN MIDI IN/OUT hardware is planned for a future development stage.

It is intentionally not required for the initial realtime engine implementation.

The MIDI core and scheduler must nevertheless be designed so that a future DIN MIDI backend can be added without changing tracker or sequencing logic.

---

## Core Principles

BroTracker shall:

- receive MIDI events
- transmit MIDI events
- support external MIDI synchronization
- support internal MIDI sequencing
- provide deterministic real-time MIDI timing

---

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

---

## Compatibility

BroTracker should interoperate with existing MIDI routing solutions whenever possible.

Examples include:

- MIDI-OX
- loopMIDI
- Pocket MIDI
- TXL MIDI Router
- CME H4MIDI

These examples are informative only and do not represent required integrations.

---

## Future Considerations

A simple internal MIDI Patchbay may be considered in a future revision.

The intention would be to provide straightforward routing between supported MIDI interfaces (for example USB and DIN) without implementing a full-featured MIDI router.

This remains a future design consideration and is not part of the current specification.
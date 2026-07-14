# MIDI Support Specification

## Status

Draft

## Purpose

This document defines the MIDI capabilities currently planned for BroTracker.

The implementation is intentionally conservative and focused on reliable sequencing rather than advanced MIDI processing.

---

## Supported Interfaces

BroTracker is expected to support:

- USB MIDI
- DIN MIDI

Additional interfaces may be added in future revisions.

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
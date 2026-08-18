

# Scheduler Specification

## Status

Draft

## Purpose

This document defines the timing architecture of BroTracker.

The scheduler is responsible for deterministic playback timing of all time-critical events.

---

## Design Goals

The scheduler shall provide a single timing reference for the entire playback engine.

All real-time events are scheduled from the same timing source.

This includes:

- sample playback
- MIDI note output
- MIDI clock generation
- transport control
- future synchronized engine events

---

## Core Principle

Audio playback and MIDI output shall originate from the same scheduler.

BroTracker should minimize timing differences between internal sample playback and external MIDI devices by scheduling both from the same timeline.

The scheduler should not treat audio playback and MIDI output as independent systems.

---

## Output Transport Independence

The scheduler is the authoritative timing source for BroTracker realtime playback.

Audio output transports must not become the realtime timing authority.

Native Teensy audio output and USB Audio may have different buffering, latency and clock characteristics. These differences must not change the logical timing of scheduled tracker events.

Host-side audio routing must therefore be treated as an output path, not as the source of the BroTracker realtime clock.

For platform-specific audio transport behaviour, see [TEENSY_AUDIO_ARCHITECTURE.md](TEENSY_AUDIO_ARCHITECTURE.md).

---

## Deterministic Timing

Whenever possible, events occurring on the same tracker row should be dispatched from the same scheduling cycle.

Example:

- trigger kick sample
- send MIDI Note On
- generate MIDI Clock (if required)

These events should remain time-aligned.

---

## Implementation

The exact implementation remains open.

Possible techniques include:

- interrupt-driven scheduler
- timer-based scheduler
- event queue
- buffered scheduling

The implementation may evolve without changing the external timing model.

---

## Performance Goals

The scheduler should prioritize:

- deterministic timing
- low jitter
- predictable latency
- minimal CPU overhead

Implementation details remain subject to future profiling and hardware testing.

For platform-specific audio transport behaviour, see [TEENSY_AUDIO_ARCHITECTURE.md](TEENSY_AUDIO_ARCHITECTURE.md).

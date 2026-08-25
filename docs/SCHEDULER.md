# Scheduler Specification

## Status

Draft

## Purpose

This document defines the timing architecture of BroTracker.

The scheduler is responsible for deterministic playback timing of all time-critical events.

## Design Goals

The scheduler shall provide a single timing reference for the entire playback engine.

All real-time events are scheduled from the same timing source.

This includes:

- sample playback
- MIDI note output
- MIDI clock generation
- transport control
- future synchronized engine events

## Core Principle

Audio playback and MIDI output shall originate from the same scheduler.

BroTracker should minimize timing differences between internal sample playback and external MIDI devices by scheduling both from the same timeline.

The scheduler should not treat audio playback and MIDI output as independent systems.

## Audio-Driven Realtime Timeline

For audio-capable playback, the realtime timeline should be advanced according to the number of audio samples processed by the audio engine.

The scheduler owns the logical playback position. The audio processing boundary provides the realtime execution context in which that timeline advances.

This allows tracker timing to remain independent of CPU frequency and platform-specific wall-clock implementations.

For a sample rate of 44100 Hz, processing 44100 audio samples represents one second of audio time.

Tracker ticks are therefore scheduled against the audio sample timeline rather than against CPU cycles or general-purpose wall-clock timers.

The platform audio layer may process samples in blocks, but tick and event positions must remain sample-accurate within those blocks where required.

The same realtime timeline is used for:

- internal audio events;
- sample playback;
- software synthesis;
- MIDI Note On/Off events;
- MIDI Clock;
- transport events;
- other synchronized realtime events.

The exact platform implementation of the audio processing callback is platform-specific.

## Output Transport Independence

The scheduler is the authoritative timing source for BroTracker realtime playback.

Audio output transports must not become the realtime timing authority.

Native Teensy audio output and USB Audio may have different buffering, latency and clock characteristics. These differences must not change the logical timing of scheduled tracker events.

Host-side audio routing must therefore be treated as an output path, not as the source of the BroTracker realtime clock.

For platform-specific audio transport behaviour, see [TEENSY_AUDIO_ARCHITECTURE.md](TEENSY_AUDIO_ARCHITECTURE.md).

## Deterministic Timing

Whenever possible, events occurring on the same tracker row should be dispatched from the same scheduling cycle.

Example:

- trigger kick sample
- send MIDI Note On
- generate MIDI Clock (if required)

These events should remain time-aligned.

## Implementation

The scheduler implementation must preserve the common logical timing model independently of the underlying platform.

For audio-capable playback, the audio sample timeline provides the realtime representation of playback position.

Audio processing callbacks, interrupts or equivalent platform mechanisms provide execution opportunities for processing audio and scheduled events.

The scheduler must not derive logical playback timing from CPU cycles or CPU frequency.

Platform-specific audio processing mechanisms are defined by the corresponding platform architecture.

## Performance Goals

The scheduler should prioritize:

- deterministic timing
- low jitter
- predictable latency
- minimal CPU overhead

Implementation details remain subject to future profiling and hardware testing.

## CPU Frequency Stability

The realtime playback system assumes a stable CPU clock during active playback.

Dynamic CPU frequency changes must not be used as an emergency mechanism for realtime overload recovery.

Performance scaling, if supported by the platform, must be configured before realtime playback begins and must not change the timing model during active playback.

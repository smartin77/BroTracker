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

## Timing Measurement

Realtime timing performance shall be evaluated separately from the logical playback timeline.

The scheduler's logical playback position is not a measurement clock.

The scheduler represents playback time in the audio sample domain. Measurement instrumentation may use a platform-specific high-resolution timing source to measure execution duration, jitter and processing margin, but such instrumentation must not become part of the playback timing model.

The distinction is:

    Measurement Clock
        |
        +---- measures execution duration
        +---- measures jitter
        +---- measures processing margin

    Scheduler Timeline
        |
        +---- represents logical playback time
        +---- schedules realtime events
        +---- advances according to processed audio samples

Measurement instrumentation must not modify, drive or otherwise become authoritative for the scheduler timeline.

## Realtime Determinism

The realtime system should be evaluated not only by average processing time but also by timing variation.

A processing path with a low average execution time but large timing variation may be unsuitable for reliable synchronization.

Measurements should therefore consider at least:

- minimum execution time;
- average execution time;
- maximum execution time;
- execution-time spread or jitter;
- processing budget;
- number of processing overruns.

The primary objective is predictable realtime behaviour rather than minimum average execution time alone.

## Audio and MIDI Timing Consistency

Audio events and MIDI events must continue to originate from the same scheduler timeline regardless of their physical output path.

Physical output paths may introduce different constant latencies.

A constant latency difference can be measured and compensated for later without changing the logical timing model.

Variable latency or jitter is more problematic because it cannot be corrected reliably using a single fixed compensation value.

The realtime architecture should therefore prioritize:

1. common logical timing;
2. deterministic event scheduling;
3. low timing variation;
4. predictable processing latency;
5. absolute latency measurement and compensation.

The scheduler must not introduce output-specific compensation into the logical playback timeline.

Any future latency compensation should be applied at the appropriate output boundary and should not alter the common logical event position.

## Benchmarking Principle

Initial realtime benchmarks should establish the timing characteristics of the audio processing boundary before implementing the complete audio engine.

The first benchmark should measure deterministic audio-block processing behaviour and scheduler advancement without introducing tracker-specific synthesis, sample streaming or MIDI transport complexity.

The benchmark should establish a baseline for:

- audio block processing time;
- scheduler advancement overhead;
- available processing margin;
- timing variation;
- processing overruns.

The benchmark results should be used to guide later audio-engine and platform-boundary decisions.

Specific audio block sizes, sample rates, measurement mechanisms and hardware configuration remain implementation and hardware-testing concerns.

## CPU Frequency Stability

The realtime playback system assumes a stable CPU clock during active playback.

Dynamic CPU frequency changes must not be used as an emergency mechanism for realtime overload recovery.

Performance scaling, if supported by the platform, must be configured before realtime playback begins and must not change the timing model during active playback.

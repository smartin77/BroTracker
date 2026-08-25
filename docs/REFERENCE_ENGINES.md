# Reference Engines

This document lists tracker engines, runtime frameworks and related projects that BroTracker uses as references for engine architecture, playback, audio processing, platform abstraction and host integration.

These projects are references and sources of inspiration. BroTracker is not intended to reproduce their architecture or feature set.

## Tracker and Engine References

### ProTracker

The original Amiga tracker and an important reference for the classic linear tracker workflow, pattern structure and sample-based playback.

- [ProTracker on GitHub](https://github.com/8bitbubsy/ProTracker)

### FastTracker II

A major reference for the classic tracker workflow, linear song structure, patterns, instruments, samples and effects.

- [FastTracker II on Wikipedia](https://en.wikipedia.org/wiki/FastTracker_2)

### MilkyTracker

An open-source, cross-platform tracker inspired by FastTracker II. Useful as a reference for modern implementations of the classic tracker workflow.

- [MilkyTracker](https://milkytracker.org/)
- [MilkyTracker on GitHub](https://github.com/milkytracker/MilkyTracker)

### OpenMPT

A mature open-source tracker and module editor supporting many module formats. Useful as a reference for module formats, compatibility and modern tracker workflows.

- [OpenMPT](https://openmpt.org/)
- [OpenMPT on GitHub](https://github.com/OpenMPT/openmpt)
- [OpenMPT Effect reference](https://wiki.openmpt.org/Manual:_Effect_Reference)

### Dirtywave M8

An important reference for compact tracker UI, hardware-oriented workflow, realtime interaction and headless operation.

BroTracker is not intended to reproduce the M8 workflow directly. In particular, BroTracker follows a more traditional linear pattern/song structure.

- [Dirtywave M8](https://dirtywave.com/products/m8-tracker)
- [M8 Headless](https://github.com/Dirtywave/M8Headless)

### LSDj

A reference for tracker-based music production on constrained hardware and for designing a powerful workflow around limited resources.

- [LSDj](https://www.littlesounddj.com/lsd/)

### iTracker

A modern browser-based tracker using an XM-style, tick-driven playback model. It is particularly relevant as a reference for tracker playback architecture and modern tracker UI.

- [iTracker](https://tracker.isystem.app/)

## Engine and Runtime References

### Impulse Tracker

The complete original Impulse Tracker source code is available and provides a valuable low-level reference for tracker playback, dispatching, sound drivers and hardware-oriented audio processing.

Of particular interest to BroTracker:

- separation between tracker playback logic and sound drivers;
- dedicated mixing and output buffers;
- hardware interrupt and DMA-oriented audio paths;
- platform-specific sound drivers behind a common playback system;
- separation of playback state, global music data and UI modules.

The source is especially valuable because the actual implementation can be studied rather than inferred from external documentation.

- [Impulse Tracker source code](https://github.com/jthlim/impulse-tracker)

### FT2 Plugin

A FastTracker II implementation adapted to operate as a modern plugin runtime.

Relevant BroTracker concepts include:

- tracker engine separated from the host environment;
- audio and MIDI integration through platform/host interfaces;
- transport and synchronization with an external host;
- reuse of tracker logic outside the original standalone environment.

This is particularly relevant to BroTracker's goal of keeping the tracker core independent from the runtime platform.

- [FT2 Plugin](https://github.com/juho/ft2-plugin)

### JUCE

A mature cross-platform C++ application and audio framework.

JUCE is relevant as a reference for:

- platform-independent audio abstractions;
- MIDI abstractions;
- host/application integration;
- realtime audio callback based processing;
- cross-platform desktop audio applications;
- CMake-based integration.

JUCE is a reference for host/platform architecture rather than a dependency of the BroTracker realtime Teensy core.

The desktop runtime may ultimately use JUCE, SDL2 or another suitable backend. This remains an implementation choice.

- [JUCE](https://github.com/juce-framework/JUCE)

## How These References Relate to BroTracker

The references above are useful at different architectural levels.

Tracker references provide insight into:

- tracker workflow;
- pattern and song structures;
- playback semantics;
- user interaction.

Engine and runtime references provide insight into:

- separation of tracker logic from platform code;
- realtime audio processing;
- sound-driver architecture;
- host integration;
- audio and MIDI abstraction.

BroTracker combines these concerns but keeps them explicitly separated.

The intended direction is:

    Tracker Core
        |
        +--- Scheduler / Playback
        |
        +--- Audio Engine
        |
        +--- MIDI Engine
        |
        +--- Platform Interfaces
                 |
          +------+------+
          |             |
        Teensy        Host
        runtime       runtime

The Teensy 4.1 runtime remains the reference realtime implementation.

## Related BroTracker Documentation

- [Core Architecture](./CORE_ARCHITECTURE.md)
- [Scheduler](./SCHEDULER.md)
- [Teensy Audio Architecture](./TEENSY_AUDIO_ARCHITECTURE.md)
- [Teensy 4.1 Memory Architecture](./TEENSY_MEMORY_ARCHITECTURE.md)
- [MIDI Support](./MIDI_SUPPORT.md)

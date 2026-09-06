# Teensy Runtime

The Teensy 4.1 runtime is the reference realtime implementation of BroTracker.

It is responsible for integrating the shared core with Teensy-specific hardware capabilities, including:

- audio hardware;
- USB;
- MIDI interfaces;
- SD storage;
- hardware timing facilities;
- DMA and other realtime hardware resources where appropriate.

The Teensy runtime must not contain the graphical or text-based UI.

The Teensy runtime must not depend on a UI client being connected.

The realtime core must remain fully operational when no UI client is present.

Detailed Teensy audio architecture is documented in [TEENSY_AUDIO_ARCHITECTURE.md](TEENSY_AUDIO_ARCHITECTURE.md).

Detailed Teensy memory architecture is documented in [TEENSY_MEMORY_ARCHITECTURE.md](TEENSY_MEMORY_ARCHITECTURE.md).

## Subsystem Architecture

The BroTracker runtime is composed of multiple logical subsystems integrated into a single application.

The main architectural boundary is between platform-independent tracker logic and platform-specific runtime functionality.

The intended high-level structure is:

```text
                    BroTracker Application
                            |
             +--------------+--------------+
             |                             |
        Shared Core                    Runtime
             |                             |
     +-------+-------+          +---------+---------+
     |       |       |          |         |         |
  Tracker Scheduler Playback   Audio     MIDI      SD
   Model              Engine
             |
             +----------------------+
                                    |
                              UI Communication
                                    |
                                  UI Client
```

The exact number and boundaries of subsystems may evolve as implementation progresses.

Major subsystems should remain independently replaceable where practical. A subsystem may be rewritten or replaced as long as its externally visible contract remains compatible with the surrounding architecture.

The architecture should prefer clear subsystem boundaries over deep abstraction hierarchies.

The final Teensy firmware remains one integrated application. Subsystems are not intended to run as independent processes or separately deployed programs.

### Subsystem Responsibilities

The shared core contains platform-independent tracker concepts and behaviour.

The realtime scheduler is responsible for deterministic time progression and scheduling of tracker events.

The playback engine translates tracker state and scheduled positions into playback events.

The playback engine must respect the channel mute state when processing pattern playback.

For a muted channel, pattern-generated events must be suppressed while pattern playback and channel position continue normally.

Manual input to a muted channel remains independent of pattern event suppression and may control the channel's selected instrument using the input controls supported by that instrument.

The audio subsystem is responsible for audio generation and playback.

The MIDI subsystem is responsible for MIDI event output and hardware-specific MIDI transport.

The storage subsystem is responsible for accessing persistent tracker and sample data.

The Teensy runtime integrates these subsystems with Teensy-specific hardware resources.

The UI client is responsible for presentation, input and editing. It must not become responsible for realtime playback timing.

### Dependency Direction

Dependencies should generally point from higher-level application orchestration toward subsystem interfaces rather than allowing arbitrary cross-dependencies.

Platform-independent core code must not depend on Teensy-specific runtime code.

Realtime subsystems must not depend on UI rendering performance.

The renderer must consume state intended for presentation rather than directly controlling realtime playback.

Subsystem boundaries should be reviewed whenever a new feature would introduce a dependency between otherwise independent components.

### Clock / Sync Subsystem

Clock generation and external synchronization are handled by a dedicated Clock / Sync subsystem.

The subsystem provides a timing reference to the realtime scheduler without exposing the scheduler to the implementation details of the underlying clock source.

Potential clock sources include:

- internal Teensy hardware timing;
- external MIDI Clock;
- external audio synchronization;
- future external hardware clock sources.

The Clock / Sync subsystem is responsible for timing-source-specific functionality such as:

- clock generation or capture;
- hardware timestamping where available;
- edge detection;
- synchronization measurement;
- filtering;
- phase correction;
- tempo estimation where required.

The realtime scheduler converts the resulting timing information into BroTracker's internal timing domain.

The scheduler uses the internal timing resolution defined by D0029: 96 ticks per tracker row.

One quarter note consists of four tracker rows and therefore corresponds to 384 internal ticks.

MIDI Clock provides 24 clocks per quarter note, resulting in 16 internal scheduler ticks per MIDI Clock.

The MIDI Clock output must be derived from the same scheduler timing model as internal playback.

### Realtime Clock Domains

BroTracker may contain multiple realtime clock domains.

These may include:

- the audio sample clock;
- internal Teensy hardware timing;
- external MIDI Clock;
- future MIDI 2.0 timing mechanisms;
- other external synchronization sources.

These clock domains must not be treated as interchangeable timing sources.

The Clock / Sync subsystem provides the mechanism for relating external and internal timing references to BroTracker's common logical timeline.

The realtime scheduler operates on this common timeline rather than directly depending on a specific hardware timer or transport.

```text
             Realtime Clock Domains
                      |
    +-----------------+-----------------+
    |                 |                 |
    v                 v                 v
Audio clock      Internal timing    MIDI timing
    |                 |                 |
    +-----------------+-----------------+
                      |
                      v
              BroTracker Timeline
                      |
                      v
                Scheduler
                      |
              +-------+-------+
              |               |
              v               v
            Audio           MIDI
```

The audio sample clock may provide the primary execution reference for audio processing, while MIDI and other timing sources may operate in their own transport or hardware domains.

Synchronization between these domains must preserve the common logical playback position.

Physical output latency must not change the logical scheduler position. Output paths may introduce different constant latencies, while variable latency, jitter or clock drift require separate handling by the appropriate synchronization or output layer.

The exact hardware implementation of internal timing, timestamping and interrupt handling remains an implementation and hardware-testing decision.

This section is a working architectural draft and does not define a specific Teensy timer or interrupt implementation.

### Audio Synchronization

When audio is used as a synchronization source, timing-critical edge detection should not depend on the Teensy Audio Library's 128-sample block processing.

At a nominal 44.1 kHz sample rate, one sample represents approximately 22.7 microseconds and one 128-sample audio block approximately 2.9 milliseconds.

Where practical, an external audio synchronization signal should therefore be captured through a suitable digital hardware input and Teensy's hardware timing facilities or interrupts rather than relying on block-level audio callbacks.

The audio synchronization implementation must remain independent of the Audio Library's normal block processing.

The exact signal conditioning, capture and filtering implementation will be defined during implementation and hardware testing.

### Timing Flow

The intended timing relationship is:

```text
External / Internal Clock Source
              |
              v
       Clock / Sync
              |
              v
      Realtime Scheduler
         96 ticks/row
              |
              v
       Playback Engine
          /        \
         v          v
      Audio        MIDI
```

The Clock / Sync subsystem and the Realtime Scheduler should remain independently replaceable.

Changing the clock source or its implementation must not require changes to tracker pattern data, playback semantics or UI rendering.

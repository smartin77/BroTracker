## Teensy Runtime

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

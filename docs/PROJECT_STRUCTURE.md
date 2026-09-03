# Project Structure

BroTracker uses a simple layered repository structure that reflects its headless and timing-first architecture.

## Repository layout

- core/: shared engine logic, timing primitives, data models and reusable components
- firmware/: Teensy 4.1-specific firmware and hardware integration code
- ui/: host-side user interface and editor client
- tools/: scripts, converters and utilities that support development or import/export workflows
- docs/: design documents, goals, roadmap and architecture notes
- assets/: media and project resources

## Architectural boundaries

- The realtime engine remains the central responsibility of the core and firmware layers.
- The UI layer should only handle presentation, input and communication.
- Shared data structures and logic should live in core so they can be reused by both firmware and UI tooling.
- New features should be introduced in small, testable steps that respect the separation of concerns above.

## Platform Separation

The `core/` directory contains platform-independent tracker logic.

The `firmware/` directory contains the Teensy 4.1-specific realtime runtime and hardware integration.

The `ui/` directory contains host-side UI, rendering and interaction logic.

Platform-specific host implementations should be kept separate from the shared core. This includes:

- host audio backends;
- host MIDI backends;
- operating-system integration;
- display/input backends;
- transport implementations used to communicate with external realtime hardware.

The architecture should allow the same core to be built for Teensy and for a host development environment without duplicating tracker logic.

The primary UI target is an ArkOS handheld. Desktop host platforms such as Windows, macOS and Linux are supported development and UI targets. Android may be supported later.

Detailed platform architecture is documented separately in:

- [TEENSY_AUDIO_ARCHITECTURE.md](TEENSY_AUDIO_ARCHITECTURE.md)
- [TEENSY_MEMORY_ARCHITECTURE.md](TEENSY_MEMORY_ARCHITECTURE.md)
- [SCHEDULER.md](SCHEDULER.md)
- [MIDI_SUPPORT.md](MIDI_SUPPORT.md)

## Teensy Reusable Infrastructure

The `firmware/teensy/` directory may contain reusable platform-specific infrastructure that is shared by the Teensy firmware and development or diagnostic tools.

This includes functionality that depends directly on Teensy hardware or the Teensy/Arduino environment but is intended to remain part of the project beyond a single diagnostic tool.

Examples include:

- SD-card logging;
- diagnostic LED signalling;
- Teensy-specific timing or hardware helpers;
- persistent debug and diagnostic functionality;
- other reusable Teensy hardware integration components.

Such components are not considered tool-specific code merely because a diagnostic tool is their current or first consumer.

Diagnostic tools belong under `tools/teensy_diagnostics/`.

A diagnostic tool may consume reusable Teensy infrastructure from `firmware/teensy/`, but must not create a second implementation of an existing reusable component.

For example:

- `firmware/teensy/BroTracker/diagnostics.*` is the single source of truth for Teensy diagnostics;
- `tools/teensy_diagnostics/` contains diagnostic tools that consume this infrastructure.

The same principle applies to shared platform-independent components. A tool must use the existing implementation rather than creating a local replacement.

The distinction is therefore:

- `src/` — shared platform-independent implementation;
- `firmware/teensy/` — Teensy-specific reusable runtime and hardware infrastructure;
- `tools/` — development and diagnostic tools that consume these components.

Arduino IDE integration must not be solved by copying or forking reusable components. A diagnostic sketch must consume the existing source of truth while preserving the repository architecture.

## Architectural vs. Directory Boundaries

Directory structure should reflect major architectural boundaries where practical, but directory separation alone does not define a subsystem.

A subsystem may consist of multiple source files and may span implementation details that are logically part of the same component.

Architectural boundaries are defined primarily by responsibility, dependency direction and interfaces rather than by the number of directories or files.

The project should avoid creating directories or abstractions solely to make the structure appear more modular.

### Arduino IDE Integration

Arduino IDE diagnostic sketches may require access to reusable BroTracker components that are located outside the individual sketch directory.

Repository-relative include paths must not be used as a mechanism for accessing source files outside the Arduino sketch build context. Arduino IDE builds sketches in its own build environment and does not treat the repository root as a general C++ include/build root.

Arduino-specific integration must therefore use a supported Arduino library or sketch integration mechanism to expose the existing source-of-truth files to the sketch.

This integration mechanism must not require:

- copying reusable source files into individual diagnostic tools;
- maintaining duplicate implementations;
- moving platform-specific infrastructure solely to satisfy Arduino IDE;
- introducing PlatformIO, CMake, or another build system.

The repository location of the reusable component remains authoritative.

For example:

- `firmware/teensy/BroTracker/diagnostics.*` remains the source of truth for Teensy diagnostics;
- `src/runtime/scheduler.*` remains the source of truth for the Scheduler;
- an Arduino IDE integration layer may expose these existing components to a diagnostic sketch without creating another implementation.

Arduino IDE packaging is considered an integration concern and must not redefine the architectural ownership of the reusable component.

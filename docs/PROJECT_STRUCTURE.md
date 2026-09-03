# Project Structure

BroTracker uses a simple layered repository structure that reflects its headless and timing-first architecture.

## Repository layout

- core/: shared engine logic, timing primitives and data models
- libraries/: reusable BroTracker components, separated by platform dependency
- firmware/: Teensy 4.1-specific firmware and application integration code
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

## Reusable Libraries

Reusable BroTracker components belong under `libraries/`.

The `libraries/` directory contains components that are intended to be consumed by multiple parts of the project, including firmware, diagnostic tools, host development code or other future applications.

Reusable libraries are separated according to platform dependency.

### Platform-independent libraries

Platform-independent reusable components must not depend on Teensy hardware, Arduino APIs or operating-system-specific functionality.

These components may be consumed by:

- Teensy firmware;
- host development runtimes;
- automated tests;
- diagnostic tools;
- future supported platforms.

Examples include:

- shared runtime components;
- scheduler and timing primitives;
- reusable data structures;
- other platform-independent realtime logic.

### Platform-specific libraries

Platform-specific reusable components may be placed under an appropriate platform-specific library when their implementation depends directly on that platform.

For Teensy, this includes reusable infrastructure such as:

- SD-card logging;
- diagnostic LED signalling;
- Teensy-specific timing or hardware helpers;
- persistent debug and diagnostic functionality;
- other reusable Teensy hardware integration components.

These components are reusable project infrastructure even when their current consumers are firmware or diagnostic tools.

The distinction is therefore:

- `libraries/` — reusable project components;
- `firmware/` — platform-specific application/firmware integration;
- `tools/` — development and diagnostic tools that consume reusable components.

A reusable component must have a single source of truth. Consumers must use the existing library implementation rather than copying, forking or locally reimplementing it.

Debug and diagnostic functionality intended to remain available throughout the project is considered permanent platform infrastructure and belongs in the appropriate reusable library rather than inside an individual diagnostic tool.

## Architectural vs. Directory Boundaries

Directory structure should reflect major architectural boundaries where practical, but directory separation alone does not define a subsystem.

A subsystem may consist of multiple source files and may span implementation details that are logically part of the same component.

Architectural boundaries are defined primarily by responsibility, dependency direction and interfaces rather than by the number of directories or files.

The project should avoid creating directories or abstractions solely to make the structure appear more modular.

### Arduino IDE Integration

Arduino IDE diagnostic sketches may consume reusable BroTracker libraries located under the repository `libraries/` directory.

The repository `libraries/` directory is intentionally compatible with Arduino IDE's standard library discovery mechanism when the BroTracker repository is used as the Arduino Sketchbook.

Arduino-specific packaging metadata such as `library.properties` is an integration mechanism and does not change the architectural ownership of the component.

Reusable components must not be duplicated into individual diagnostic sketches merely to satisfy Arduino IDE.

The preferred repository layout is therefore:

- `libraries/` — reusable components exposed as Arduino-compatible libraries where required;
- `firmware/` — Teensy-specific firmware and application integration;
- `tools/teensy_diagnostics/` — diagnostic sketches and tools consuming the reusable libraries.

Repository-relative include paths must not be used as the primary mechanism for accessing reusable components from an Arduino sketch.

Arduino IDE integration must not require:

- duplicated reusable implementations;
- hard links;
- symlinks;
- moving reusable components solely for Arduino IDE discovery;
- PlatformIO, CMake or another alternative build system.

Where a reusable component cannot be consumed directly by the Arduino IDE because of its build/discovery constraints, a developer-side preparation step may be used as a fallback. Such preparation must operate on generated/local integration files and must not become a second source of truth in the repository.

The physical Arduino IDE / Teensy build remains a manual developer verification step.

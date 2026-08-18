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

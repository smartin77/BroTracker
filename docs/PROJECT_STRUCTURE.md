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

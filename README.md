# BroTracker

**BroTracker** is an open-source hardware-oriented music tracker focused on deterministic timing, low-latency MIDI sequencing and sample playback.

The project targets **Teensy 4.1** as the realtime engine and Linux-based handheld devices as the user interface.

## Project Goals

* Classic tracker workflow inspired by ProTracker and FastTracker
* Deterministic timing for both internal audio and external MIDI devices
* Low-latency sample playback
* Sample Regions (non-destructive slicing)
* MOD/XM import
* Modular architecture with clearly separated and replaceable subsystems
* Simple and community-friendly development model

## Repository Structure

The repository is organized around clear architectural boundaries:

* core for platform-independent tracker logic, shared data structures and realtime concepts
* firmware for Teensy 4.1-specific runtime and hardware integration
* ui for host-side editing and interface logic
* tools for utilities and import/export helpers
* docs and assets for project documentation and resources

Major runtime functionality is implemented as separate logical subsystems within the single BroTracker application. Subsystems are designed to remain independently testable and replaceable where practical.

## Documentation

Complete project documentation is available in the [`docs`](docs/) directory.

Start here:

[Documentation Index](./docs/README.md)

## Project Status

This project is currently in its early proof-of-concept stage.

The primary goal is **not** to build the biggest tracker, but to build a solid and reliable foundation that can grow over time.

## Contributing

BroTracker is developed as an open-source community project. Contributions, ideas and constructive discussions are welcome.

## License

BroTracker is licensed under the GNU General Public License v3.0 (or later).

See the `LICENSE` file for details.

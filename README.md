# BroTracker

**BroTracker** is an open-source hardware-oriented music tracker focused on deterministic timing, low-latency MIDI sequencing and sample playback.

The project targets **Teensy 4.1** as the realtime engine and Linux-based handheld devices as the user interface.

## Project Goals

- Classic tracker workflow inspired by ProTracker and FastTracker
- Deterministic timing for both internal audio and external MIDI devices
- Low-latency sample playback
- Sample Regions (non-destructive slicing)
- MOD/XM support – rather use an external tool to convert to native BroTracker module
- Modular architecture with clearly separated and replaceable subsystems
- Simple and community-friendly development model
- Modular instrument architecture with built-in core instruments and optional loadable instrument modules
- Generic MIDI controller support without requiring device-specific drivers
- Optional loadable hardware controller modules for enhanced device-specific functionality
- Clear and transparent documentation to help future contributors, instrument developers and hardware manufacturers understand, extend and integrate with BroTracker

## Repository Structure

The repository is organized around clear architectural boundaries:

- `src/core/` contains the platform-independent tracker logic and data
  structures referred to as the BroTracker core.
- `firmware/teensy/` contains the Teensy 4.1 firmware and hardware
  integration.
- `src/ui/` and `src/renderer/` contain the current shared UI and framebuffer
  rendering code. The top-level `ui/` directory is reserved for host UI
  clients and platform integration as those are implemented.
- `tools/ui_preview/` contains the current host-side UI preview tool. It
  renders the UI programmatically into the 640 × 480 BroTracker framebuffer
  and can save the result as `assets/ui_main_screen.bmp`.
- `assets/` contains project resources such as fonts and preview input data.
  `assets/ui_main_screen.bmp`, when generated, is a preview artifact rather
  than the source of the UI.
- `tests/` contains host-side tests for platform-independent components.
- `docs/` contains the project architecture, design decisions and other
  contributor documentation.
- `libraries/` contains reusable components shared by firmware, host tools or
  diagnostics where appropriate.

Major runtime functionality is implemented as separate logical subsystems within the single BroTracker application. Subsystems are designed to remain independently testable and replaceable where practical.

Optional instrument and hardware controller functionality may be provided as independently distributed binary modules. This allows third-party developers and hardware manufacturers to provide proprietary extensions without becoming part of the BroTracker core.

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

Disclaimer:
"Try to avoid developing on Windows! I liked Windows 11 until I started developing real apps on this piece of shit!"

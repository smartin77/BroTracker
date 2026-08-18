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

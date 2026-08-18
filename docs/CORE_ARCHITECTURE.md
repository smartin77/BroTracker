# Core Architecture

BroTracker uses a layered architecture built around a platform-independent tracker core and a Teensy 4.1 realtime implementation.

The Teensy 4.1 remains the reference realtime platform.

## Architecture Overview

                         BROTRACKER CORE
        +---------------------------------------------+
        | Tune / Pattern / Instrument / Sample       |
        | Sequencer / Scheduler / Playback           |
        | Tracker Events / State / Commands          |
        +----------------------+----------------------+
                               |
                    Platform interfaces
                               |
              +----------------+----------------+
              |                                 |
              v                                 v
       Teensy Runtime                     Host Runtime
       (reference)                        (development)
              |                                 |
       +------+-------+                 +-------+-------+
       |              |                 |       |       |
     Audio          MIDI              Audio   MIDI     UI
       |              |                 |       |       |
    HW / USB       USB MIDI           Host    Host    SDL/
     / future      / future          audio   MIDI   host API
       |
    Storage

The shared core must not depend on a specific UI, operating system, audio API or MIDI driver.

## Core / UI Separation

BroTracker is headless by design.

The realtime engine does not implement the graphical or text-based presentation of the tracker.

The core is responsible for producing deterministic tracker state and realtime events, including:

- pattern and song state;
- scheduler state;
- playback position;
- instrument state;
- audio state;
- MIDI state;
- synchronization state;
- project and storage state.

A UI client is responsible for presenting this state and converting user interaction into commands.

The communication interface must transport tracker state and commands rather than pre-rendered graphical output.

This separation allows the realtime engine to remain independent of display resolution, font rendering and UI implementation.

## Host Development Runtime

The shared BroTracker core may be executed on a host computer without requiring Teensy hardware.

This host runtime is primarily intended for:

- development;
- automated testing;
- tracker behaviour verification;
- UI development;
- MIDI sequencing tests;
- audio engine development;
- desktop use on larger displays.

The host development runtime may provide platform-specific implementations for audio, MIDI, storage, timing and other services required to execute the core outside the Teensy environment.

Host execution must preserve the same tracker semantics and scheduling model as the Teensy implementation.

Host audio and MIDI backends may differ from the Teensy hardware implementations. Such differences must remain outside the shared core.

A host development runtime does not replace the Teensy 4.1 realtime implementation.

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

Audio generation, realtime scheduling and synchronization remain under the control of the Teensy runtime.

The preferred physical audio output path is a native Teensy audio interface using an appropriate DAC or audio codec.

USB Audio may be provided as an additional host-facing interface, but the host audio subsystem must not become the authoritative realtime clock for BroTracker.

Detailed Teensy audio architecture is documented separately in `TEENSY_AUDIO_ARCHITECTURE.md`.

## Teensy Memory Architecture

The Teensy 4.1 realtime implementation uses the platform's distinct memory regions according to realtime requirements.

The current working model separates:

- ITCM for time-critical instruction paths;
- DTCM for frequently accessed realtime state;
- RAM2 / DMAMEM for DMA-capable buffers and larger working data;
- Flash for non-realtime application code and static resources;
- PSRAM / EXTMEM for large data such as samples and wavetables.

The detailed memory allocation policy is maintained separately in `TEENSY_MEMORY_ARCHITECTURE.md` and remains subject to hardware and toolchain verification.

The realtime kernel must avoid runtime dynamic memory allocation. Memory required by realtime processing should be statically allocated or obtained from explicitly controlled fixed-size pools established before realtime operation begins.

C++ exceptions are not part of the realtime kernel error-handling model.

Memory placement must not be treated as an implementation detail when it can affect realtime determinism, DMA operation, cache coherency or available realtime resources.

## Realtime Scheduling

Internal audio and external MIDI events must originate from the same realtime scheduling model.

The scheduler must therefore be designed around timestamped or otherwise deterministically ordered realtime events rather than treating MIDI as a secondary output path.

The primary performance target is to minimize latency and jitter between:

- internal audio events;
- MIDI OUT events;
- synchronization events.

The exact implementation is defined by the scheduler and realtime engine design.

## UI Rendering

The UI renderer operates independently from the realtime engine.

The canonical logical UI resolution is 640 × 480.

The renderer should produce the same logical framebuffer regardless of the physical display platform.

A host frontend may scale the framebuffer using integer scaling without changing the UI layout.

## Communication Between UI and Teensy

When the UI runs on a separate device, such as an ArkOS handheld, the UI client communicates with the Teensy realtime runtime through a platform transport.

The initial transport is USB.

The communication protocol must carry commands and tracker state.

The protocol must not require the Teensy realtime core to render or transmit graphical output.

The UI client must not be responsible for realtime playback timing.

UI communication must never compromise realtime playback.

The realtime core must continue operating if communication is delayed, interrupted or temporarily unavailable.

The same communication model should be usable by other UI client platforms, including Windows, macOS and Linux.

## Initial Hardware MIDI Routing

During initial development, BroTracker will use USB connectivity.

The primary external MIDI hardware router is the CME H4MIDI.

The intended development path is:

Teensy 4.1
    |
   USB
    |
CME H4MIDI
    |
 DIN MIDI
    |
External MIDI hardware

This configuration provides physical MIDI IN/OUT through the CME H4MIDI without requiring direct DIN MIDI circuitry on the Teensy.

Physical DIN MIDI IN/OUT directly connected to Teensy hardware is intentionally deferred until a later development stage because it requires additional hardware work and soldering.

The initial architecture must therefore avoid depending on direct Teensy DIN connections.

## Realtime Priorities

The primary responsibility of the Teensy core is reliable realtime operation.

The highest-priority areas are:

1. accurate timing;
2. deterministic scheduling;
3. synchronization;
4. audio processing;
5. MIDI processing and timing;
6. pattern/song playback;
7. storage and non-realtime data operations.

UI communication must not compromise realtime timing.

The display application may be delayed, disconnected or restarted without interrupting the realtime engine.

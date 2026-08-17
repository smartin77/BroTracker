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

## Host Runtime

A host runtime may execute the same tracker core on Windows, macOS or Linux without requiring Teensy hardware.

The host runtime is primarily intended for:

- development;
- automated testing;
- tracker behaviour verification;
- UI development;
- MIDI sequencing tests;
- audio engine development;
- desktop use on larger displays.

Android may be supported by a future host implementation.

Host execution must preserve the same tracker semantics and scheduling model as the Teensy implementation.

Host audio and MIDI backends may differ from the Teensy hardware implementations. Such differences must remain outside the shared core.

## Teensy Runtime

The Teensy runtime provides the reference implementation of the realtime platform.

It is responsible for integrating the shared core with Teensy-specific hardware capabilities, including:

- audio hardware;
- USB;
- MIDI interfaces;
- SD storage;
- hardware timing facilities;
- DMA and other realtime hardware resources where appropriate.

The Teensy runtime must not depend on the UI being connected.

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

When the UI runs on a separate device, such as an ArkOS handheld, communication with the Teensy is performed through a platform transport.

The initial transport is USB.

The communication protocol must carry commands and tracker state.

The protocol must not require the Teensy realtime core to render or transmit graphical output.

UI communication must never compromise realtime playback.

The realtime core must continue operating if communication is delayed, interrupted or temporarily unavailable.

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

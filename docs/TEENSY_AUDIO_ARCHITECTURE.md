# Teensy Audio Architecture

This document defines the current working architecture for audio on the Teensy 4.1 realtime platform.

The document is intentionally focused on the Teensy platform and its interaction with external host systems such as ArkOS handhelds.

The shared BroTracker audio engine must remain platform-independent.

## Reference Platform

Teensy 4.1 is the reference realtime audio platform for BroTracker.

The Teensy is responsible for realtime audio generation, processing and scheduling.

A host device such as an ArkOS handheld may provide the UI, project management and communication transport without becoming the authoritative realtime audio clock.

## Audio Architecture

The BroTracker audio architecture separates the realtime audio engine from platform-specific audio output interfaces.

The conceptual architecture is:

    BroTracker Core
        |
    Audio Engine
        |
        +-------------------+
        |                   |
        v                   v
    Native Audio        USB Audio
    Output              Interface
        |                   |
        v                   v
    I2S / DMA             USB
        |                   |
        v                   v
    DAC / Codec        Host system

The audio engine must not depend on a specific DAC, USB implementation or host operating system.

## Realtime Audio Output

The primary realtime audio path should be a native Teensy audio output path.

The preferred architecture is:

    BroTracker Audio Engine
        |
       I2S
        |
       DMA
        |
    DAC / Audio Codec
        |
    Physical audio output

This path keeps audio generation, realtime scheduling and the physical audio clock within the Teensy realtime environment.

A Teensy 4.1 board does not provide a conventional built-in stereo headphone output by itself. A suitable external DAC, codec or audio hardware is therefore required for a native physical audio output.

## USB Audio

Teensy 4.1 may also expose a USB Audio interface to a connected host.

The host may enumerate the device as a USB audio device, for example:

    BroTracker Audio

USB Audio should be treated as a platform interface and host integration feature.

It must not become the authoritative timing source for the BroTracker realtime engine.

A combined USB configuration may also expose MIDI and other USB interfaces alongside USB Audio, subject to the selected Teensy USB configuration and platform implementation.

## USB Audio and Host Audio Output

When BroTracker is connected to an ArkOS or another Linux-based host, the host may enumerate BroTracker as a USB Audio device.

This does not mean that audio automatically appears on the host's physical headphone output.

A possible host-side path is:

    BroTracker
        |
      USB Audio
        |
        v
    ArkOS / Linux
        |
    ALSA / audio routing
        |
        v
    Host DAC
        |
        v
    Headphone output

The host therefore becomes an additional audio processing and buffering layer.

This routing may be useful for monitoring, recording or other host integration, but it is not considered the primary BroTracker realtime audio path.

## Realtime Timing

The Teensy realtime scheduler remains authoritative for BroTracker playback timing.

Audio events and MIDI events must originate from the same realtime scheduling model.

The audio output interface must consume the scheduled audio stream without changing the logical timing of tracker events.

The USB host must not become responsible for BroTracker playback timing.

## Audio Clock Domains

Native Teensy audio output and USB Audio may involve different hardware or host clock domains.

The following distinction is therefore important:

    BroTracker realtime clock
        |
        +---- scheduler
        |
        +---- audio engine
        |
        +---- MIDI timing
        |
        +---- native audio output

USB Audio introduces a host-side transport and audio clock domain:

    BroTracker audio
        |
        v
    USB Audio
        |
        v
    Host USB audio subsystem
        |
        v
    Host audio clock / DAC

The USB Audio path must therefore not be assumed to have identical timing characteristics to the native Teensy audio path.

## Latency and Jitter

USB Audio may introduce:

- transport latency;
- buffering latency;
- host-side audio buffering;
- scheduling jitter;
- host audio routing latency;
- resampling or clock-domain conversion where required.

Constant latency is not itself a synchronization error.

A fixed additional delay may be acceptable for monitoring.

Variable latency, jitter or clock drift are more significant for realtime synchronization.

The BroTracker architecture must therefore avoid relying on the host USB Audio playback path for latency-critical realtime synchronization.

## MIDI and Audio Synchronization

BroTracker must maintain a single authoritative realtime timeline.

For events occurring at the same tracker position, the following should remain logically aligned:

- internal audio events;
- MIDI Note On/Off events;
- MIDI Clock events;
- transport events;
- other realtime synchronized events.

The physical transport may introduce different measured latencies, but the event scheduling model must remain common.

## Host UI and Audio

A host UI may run on a device such as an ArkOS handheld.

The UI may:

- display tracker state;
- send commands;
- control transport;
- configure audio options;
- select available host audio devices.

The UI must not become responsible for generating BroTracker realtime audio timing.

The realtime engine must continue operating if the UI is delayed, disconnected or restarted.

## ArkOS / Handheld Use

For a handheld host such as an R36-series device, the preferred realtime architecture is:

    Handheld
        |
       USB
        |
        v
    Teensy 4.1
        |
        +---- realtime audio
        |
        +---- MIDI
        |
        +---- synchronization

If a native Teensy DAC or audio codec is available, the physical audio output should preferably originate directly from the Teensy.

An alternative configuration may route USB Audio back through the handheld's Linux audio system to its internal DAC.

That configuration is considered a host monitoring path rather than the primary realtime audio path.

## Practical Experience

Experience with M8 Headless on handheld platforms indicates that routing Teensy-generated audio through the host audio subsystem while synchronizing external hardware through MIDI can introduce additional timing complexity.

The relevant path is:

    Teensy audio
        |
      USB
        |
      Host
        |
    audio buffers / routing
        |
    Host DAC

while MIDI timing may follow a separate path:

    Teensy
        |
      MIDI
        |
    external hardware

These paths may have different latency, buffering and clock behaviour.

This does not mean that USB Audio is inherently unsuitable.

It means that host-routed USB Audio must not be treated as equivalent to a direct Teensy audio output when evaluating realtime synchronization.

## Preferred Architecture

The preferred BroTracker architecture is:

    Teensy 4.1
        |
        +---- Scheduler
        |
        +---- Audio Engine
        |
        +---- MIDI Engine
        |
        +---- Native Audio Output
        |
        +---- USB MIDI
        |
        +---- optional USB Audio

The Teensy remains the authoritative realtime system.

USB Audio is an additional interface rather than the foundation of the realtime audio path.

## Future Possibilities

The architecture should allow BroTracker to operate as a USB audio device for:

- host monitoring;
- audio recording;
- DAW integration;
- audio capture;
- USB effects processing;
- future host-based workflows.

These uses must not compromise the deterministic realtime engine.

## Open Questions

The following remain to be verified before implementation:

1. Exact Teensy USB Audio capabilities and supported configurations.
2. USB Audio sample rate and format supported by the selected Teensy implementation.
3. USB Audio endpoint buffering and latency.
4. USB Audio feedback and clock-domain behaviour.
5. I2S and Audio Library DMA requirements.
6. DAC / codec options for native Teensy output.
7. DMA buffer placement and cache requirements.
8. Interaction between USB Audio and USB MIDI in the selected Teensy USB configuration.
9. Behaviour of ArkOS / Linux when BroTracker Audio is connected.
10. Whether host-side audio routing is required, optional or disabled by default.
11. Practical latency and jitter measurements for USB Audio versus native audio output.

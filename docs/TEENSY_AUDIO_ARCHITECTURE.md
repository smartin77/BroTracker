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

## Platform Audio Boundary

The shared audio engine must expose a platform-independent sample processing interface.

The platform layer provides:

- audio callback or equivalent realtime execution;
- output buffer;
- sample count;
- sample rate;
- platform-specific synchronization and hardware handling.

The shared engine provides:

- sample generation;
- tracker timing;
- event scheduling;
- software synthesis;
- sample playback;
- synchronized MIDI events.

This boundary allows the same audio engine architecture to run on Teensy and on host platforms using different audio backends.

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

## Audio Processing Blocks

The Teensy audio implementation is expected to process audio in fixed-size blocks determined by the selected audio platform and driver.

The BroTracker audio engine must therefore support block-based processing while retaining a sample-based internal timeline.

A block boundary must not become a tracker timing boundary.

If a tracker event occurs part-way through an audio block, the engine must preserve the event's position within that block.

This allows tracker timing to remain independent of the selected audio block size.

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

## Audio-Driven Timing

On the Teensy realtime platform, audio processing provides the primary execution boundary for the audio-capable playback timeline.

The audio engine processes samples in platform-defined audio blocks. The scheduler tracks the logical playback position within that sample timeline.

For example, at a 44100 Hz sample rate:

    44100 samples = 1 second of audio time

A tracker tick is therefore represented by a position on the audio sample timeline rather than by a CPU-cycle count.

When a tick or other scheduled event falls within an audio block, the audio engine must be able to identify its sample position and dispatch the corresponding realtime events at the appropriate point in the block.

This allows:

- software synthesis;
- sample playback;
- internal audio events;
- MIDI events;
- MIDI clock;
- transport events

to originate from the same logical timeline.

The audio callback or interrupt provides the execution context.

It does not replace the scheduler as the owner of logical playback time.

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

## Audio Timeline and Clock Synchronization

The Teensy audio sample clock provides a stable reference for the audio processing timeline.

The scheduler may be advanced from the audio processing boundary as audio samples are processed. This establishes a deterministic relationship between processed audio samples and the BroTracker logical playback timeline.

This does not make the physical audio output latency part of the scheduler timeline.

```text
             Audio sample processing
                      |
                      v
               Audio block boundary
                      |
              +-------+-------+
              |               |
              v               v
        process samples   advance logical
                            timeline
                                |
                                v
                            Scheduler
                                |
                         +------+------+
                         |             |
                         v             v
                       Audio          MIDI
```

The scheduler therefore represents logical playback time, while the physical audio output path may introduce additional latency after the audio samples have been generated.

Other realtime timing sources may operate in separate clock domains.

These may include:

- internal Teensy hardware timing;
- MIDI 1.0 Clock;
- future MIDI 2.0 timing;
- external synchronization sources.

Such sources must be synchronized to the BroTracker logical timeline rather than directly replacing the audio processing timeline.

MIDI timing must therefore be treated as a realtime synchronization domain rather than as a direct audio processing clock.

The architecture should allow MIDI 1.0 and future MIDI 2.0 implementations to share the same logical timing model while using different transport and timestamp mechanisms.

The exact implementation of internal hardware timers, interrupts, timestamping and MIDI timing remains subject to Teensy hardware verification.

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

## Realtime Audio Benchmark

The first Teensy audio benchmark shall establish the realtime processing characteristics of the audio platform before implementing the complete BroTracker audio engine.

The benchmark is intended to measure the behaviour of the platform audio processing boundary and the minimal BroTracker timing path.

It is not intended to implement the audio engine.

The initial benchmark should measure:

- audio processing block duration;
- scheduler advancement overhead;
- minimum processing time;
- average processing time;
- maximum processing time;
- processing-time variation;
- available processing margin;
- processing overruns.

The benchmark should use a deterministic audio workload so that timing results primarily represent the processing path rather than unpredictable application behaviour.

The initial benchmark should not depend on:

- tracker pattern playback;
- software synthesis;
- sample streaming from SD;
- MIDI transport;
- USB Audio host routing;
- host-side audio processing.

These systems will be benchmarked separately when their corresponding realtime paths exist.

### Measurement Clock

The benchmark must use a measurement mechanism separate from the BroTracker scheduler timeline.

The scheduler represents logical playback time in audio samples.

The measurement mechanism measures real execution duration.

These are different concepts and must remain architecturally separate.

Conceptually:

    Audio Processing Boundary
            |
            +---- Scheduler
            |       |
            |       +---- logical sample timeline
            |
            +---- Measurement
                    |
                    +---- execution duration
                    +---- jitter
                    +---- processing margin

The measurement mechanism must not become the source of playback timing.

The exact Teensy measurement mechanism remains an implementation and hardware-testing decision.

### Processing Budget

For an audio block containing N samples at a sample rate of R samples per second, the available audio processing interval is:

    block duration = N / R seconds

The benchmark should compare measured processing time against this available interval.

The resulting processing margin should be recorded.

A realtime processing overrun occurs when the processing time exceeds the available interval.

The benchmark should report overruns explicitly rather than hiding them inside an average measurement.

### Timing Stability

Average processing time alone is insufficient to characterize realtime suitability.

The benchmark should therefore distinguish between:

- average execution time;
- worst observed execution time;
- execution-time variation;
- processing overruns.

A system with a predictable constant processing time is preferable to a system with a similar average but large unpredictable timing variation.

This distinction is particularly important for synchronization with external MIDI hardware.

### Synchronization Relevance

The benchmark does not initially measure end-to-end audio-to-MIDI latency.

Instead, it establishes whether the realtime audio processing path behaves predictably enough to support the common scheduler timeline.

Later measurements may compare:

    Scheduler event position
            |
            +---- audio output path
            |
            +---- MIDI output path

The physical paths may have different constant latencies.

A stable constant latency difference may be compensated at an appropriate output boundary.

Variable latency, jitter or clock drift cannot be reliably corrected using a single fixed compensation value and must therefore be minimized.

The common scheduler timeline must remain unchanged by such output compensation.

### Initial Benchmark Scope

The initial Teensy benchmark should remain deliberately small.

It should establish the timing baseline before adding:

- software synthesis;
- sample playback;
- mixing;
- SD streaming;
- MIDI transmission;
- USB Audio;
- host audio routing.

Each later subsystem can then be benchmarked against the established realtime processing budget.

Benchmark results should be used to guide audio block size, processing architecture and realtime resource budgeting rather than being treated as fixed architectural constants.

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
'
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

## CPU Frequency

The audio engine assumes a stable CPU frequency during active realtime playback.

Any Teensy CPU overclocking or performance configuration must be established before realtime playback begins.

Dynamic CPU frequency changes during active audio processing are not part of the BroTracker realtime architecture.

Realtime overload must instead be handled through CPU budgeting, voice management, DSP quality policies or other deterministic software mechanisms.

Realtime playback timing must not depend on CPU frequency, whether the Teensy is running at the reference frequency or at a validated higher performance configuration.

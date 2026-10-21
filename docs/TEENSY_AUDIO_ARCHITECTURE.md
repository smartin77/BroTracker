# Teensy Audio Architecture

This document defines the current working architecture for audio on the Teensy 4.1 realtime platform.

The document is intentionally focused on the Teensy platform and its interaction with external host systems such as ArkOS handhelds.

The shared BroTracker audio engine must remain platform-independent.

## Current Sample/Synth development renderer

`teensy41_dev` and `teensy41_pattern_variable_rate` now use the
[minimal sine Synth foundation](SYNTH_FOUNDATION.md) instead of the development
beeper instruments. Existing output selection, USB_BRIDGE default, SD preparation,
channel gains/mixing and paused bridge diagnostics remain unchanged. Historical
native-rate pattern environments retain their diagnostic PCM fixtures as regression
workloads; they are not the current project instrument list.

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

## Current manual production output selection

BTX reads the existing user `config.ini` once at host startup. The authoritative
path is logged, resolved with `SDL_GetPrefPath("BroTracker", "BroTracker Terminal")`:
Windows normally `%APPDATA%\BroTracker\BroTracker Terminal\config.ini`; Linux/ArkOS
`$XDG_DATA_HOME/BroTracker/BroTracker Terminal/config.ini`, otherwise
`$HOME/.local/share/BroTracker/BroTracker Terminal/config.ini`.
`config/config.ini.example` is the distributed template; retain its required
`[btx]` settings. Add:

```ini
[audio]
output=bridge
```

Supported explicit values are `bridge` (Teensy USB Audio -> existing host bridge)
and `pt8211` (Teensy analog DAC). Sections, keys and values are case-sensitive;
ASCII whitespace is trimmed, matching the existing parser. This changes Teensy's
production route, not the host playback-device selection. The template explicitly
selects bridge. Absent/empty output, missing file or `--safe-config` supplies no
override: retain an already active route, otherwise use firmware fallback
**USB_BRIDGE**. Invalid nonempty output logs a clear warning, ignores only the
audio override and retains other valid settings. Other structural INI errors
keep the parser's existing transactional failure/default behavior.

Precedence is explicit INI preference, then existing firmware selection logic.
`firmware/teensy/BroTracker/audio_output_config.h` defines the fallback:

```cpp
#define BROTRACKER_AUDIO_OUTPUT BROTRACKER_OUTPUT_USB_BRIDGE
```

Normal INI changes do not require firmware rebuild/upload, but require restarting
BTX to reload the file. Once Teensy has activated a route, selecting a different
route requires restarting Teensy too. No hot switch, probing or automatic fallback
is implemented. Changing the firmware fallback itself requires rebuild/upload.

### Startup and reconnect protocol

Firmware advertises `BTAUDIO1 CAP 1` during the existing serial handshake. BTX
sends exactly one `BTAUDIO1 SET 1 bridge` or `pt8211` per connection for an explicit
preference, or `BTAUDIO1 SET 1 default` on capable firmware without an override.
`default` is a wire operation, not a supported explicit INI value. Teensy replies
`BTAUDIO1 SELECTED 1 bridge` or `pt8211` only after activating that route. BTX
requires a matching acknowledgment before allowing START for a requested route;
mismatch, unsupported old firmware, rejection or timeout is visible in status/logs.
There is no silent acknowledgment of a different output.

Audio hardware and AudioMemory activation are deferred until selection, or the
first pattern transport command (START/STOP/PAUSE/CONTINUE), or legacy WAV
START, using the fallback.
An explicit first selection locks the route for this Teensy boot. Same-route
requests and no-override reconnects are idempotent, without resetting audio.
A different explicit request returns `BTAUDIO1 ERROR 1 restart-required active=...`;
the existing route/playback is retained, and no host START is replayed. Reconnect
never starts playback automatically. Older firmware without audio negotiation
continues normal controls when no override is requested; an explicit unsupported
request fails visibly instead of pretending it applied.

### Driver ownership and routing

A fixed aligned static buffer holds exactly one constructed hardware output and
fixed storage holds its connections; outputs are never destroyed/reconstructed.
The hardware constructor claims update responsibility before `AudioMemory(8)`;
an independent BASEPRI threshold of 16 blocks the installed DMA (priority 128)
and software Audio (208) IRQs throughout construction, patching and pool setup.
PJRC DMA allocation, AudioConnection::connect and AudioMemory internally enable
PRIMASK, so an outer PRIMASK guard alone is insufficient. BASEPRI_MAX never weakens
a stricter caller priority mask; both original PRIMASK and BASEPRI are restored,
with barriers. This does not claim continuous global IRQ masking: priority-zero
IRQs and faults are outside BASEPRI protection and do not run the audio handlers.
Do not assign priority zero to these audio IRQs; installed-source guards check
the priority assumptions. The original harness omitted internal interrupt enables
and did not establish this property.
This avoids AudioMemory's heap-allocated fallback timer. USB never owns the graph
clock. Source initialization and pending control processing still occur on the
first audio callback; SD preparation remains boot-only. No general configuration
framework or realtime allocation was added.

PT8211 uses SAI1/DMA, DIN=7, WS=20, BCK=21, 3.3 V and common GND. The mono source/mix
feeds both DAC channels; no production PCM connections feed USB Audio, so the
retained USB interface sends silence. Bridge uses the existing MQS SAI3/DMA timing
path on pins 10/12 and mono PCM to both USB channels. USB enumeration, serial,
MIDI, telemetry and existing core fixes remain enabled. Disconnect all power for
hardware wiring changes. Selection indicates a driver, never attachment or DAC
health. The isolated diagnostics, experimental host-only classifier and historical
evidence are unchanged. Future automatic selection follows the mutually exclusive
module model below, with explicit configuration precedence and USB bridge fallback.
Audio Shield diagnostics and production detection integration await physical hardware.

### Offline validation

Run `python -B tools/teensy_audio/check_selection.py --build build/audio-selection`.
This compiles the actual activation method with instrumented driver/connection/pool
stubs and modeled ARM interrupt masks, including internal interrupt enables and
pending DMA/audio-update interrupts during activation. All 36 activation cases
passed across PT8211/bridge paths and pattern/legacy graphs, checking deferred
construction, one owner, mono routing, idempotence, rejection and mask restoration.
The two PRIMASK-only negative controls reproduce the unsafe activation failure.
These checks do not validate DMA clocks or physical audio. Existing host config/controls tests
cover precedence, case/whitespace, acknowledgments, rejection, timeout and reconnect.

### Windows serial hardware validation (2026-10-19)

The corrected teensy41_dev firmware was uploaded to Teensy serial 17681760 on COM3.
Bridge selection/default retention, different-route rejection, invalid-route
rejection, START/STOP, live version-2 telemetry, idle reconnect without START and
playing reconnect without resetting the timeline passed. Absolute ticks advanced
from 1099 to 2956 across the playing reconnect. Playback was left stopped.
Raw serial and reports are in
`build/activation-review/serial-20261019T191518Z-33f86245/` (untracked review artifacts).
Two failed test-script attempts are preserved: one expected old version-1 telemetry,
the other incorrectly required no partial line at a serial-session boundary.
Those automated serial checks did not establish listening results.

### User-confirmed INI output selection (2026-10-19)

With PT8211 attached, the user confirmed that production INI selection works
correctly for both `[audio] output=pt8211` and `output=bridge` on Windows.
In bridge mode, Windows USB bridge audio plays while PT8211 remains silent.
This confirmation is separate from the earlier isolated 440 Hz left / 880 Hz
right smoke test. USB_BRIDGE remains the development default. Physical ArkOS
and dArkOS validation remains pending; neither is established by Windows results.

### Additional hardware and host regression checks

The basic Windows INI output selection is user-confirmed; the following checklist
retains the detailed lifecycle and playback checks for further validation.

1. For repeat validation with PT8211 attached, stop BTX; merge/edit the
   user INI (retain required `[btx]`) to `output=pt8211`. With all power disconnected,
   verify module wiring, then reconnect Teensy and launch the rebuilt BTX.
2. Confirm the logged config path/value, `BTAUDIO1 SET 1 pt8211` and matching
   `BTAUDIO1 SELECTED 1 pt8211`. No playback should start without START. Start,
   stop and restart; verify equal mono on DAC L/R, silent USB PCM, SD samples and
   telemetry. Check pause/continue where supported.
3. Reconnect serial/BTX using the same configuration, both idle and during playback.
   Confirm one request/ack per connection, no audio reset or automatic START.
4. While Teensy remains powered, restart BTX with `output=bridge`. Expect explicit
   `restart-required active=pt8211`, no applied bridge acknowledgment and no route
   replacement. STOP must remain usable. Stop BTX, disconnect all Teensy power,
   reconnect and launch BTX: now expect SELECTED bridge and normal USB PCM playback.
5. Repeat startup with output missing, empty and invalid (e.g. `PT8211`): on a fresh
   Teensy boot the fallback is bridge; invalid logs a warning. On a locked PT8211
   route a missing/empty/invalid override retains PT8211. Test `--safe-config` too.

## Realtime Audio Output

The primary realtime audio path is a native Teensy 4.1 audio output path.

The initial preferred hardware path is:

    BroTracker Audio Engine
        |
       I2S
        |
    PT8211
        |
    Physical audio output

PT8211 is the initial reference DAC for native Teensy audio output.

The base BroTracker system must nevertheless remain functional without PT8211 or another external DAC.

Native audio output hardware is therefore an optional hardware capability, not a prerequisite for the BroTracker core or basic tracker operation.

The platform audio boundary must allow additional native audio hardware without requiring changes to the shared BroTracker audio engine.

Potential future native audio backends include:

- other I2S DACs;
- audio codecs;
- headphone/line-out hardware;
- other Teensy-compatible audio output hardware.

### USB Audio

USB Audio is a mandatory supported host capability.

The intended path is:

    BroTracker Audio Engine
        |
    USB Audio
        |
       Host
        |
    Host audio system / DAC

USB Audio must remain an output interface and transport mechanism.

It must not become the authoritative BroTracker realtime clock.

The Teensy scheduler and audio timeline remain authoritative.

USB Audio must coexist architecturally with USB MIDI and other supported USB functionality.

### User-Configurable Audio Processing

BroTracker may eventually support user-configurable audio processing paths.

A future implementation could allow users to construct audio paths from reusable processing components, for example:

    Source
      |
    Mixer
      |
    Filter
      |
    Effect
      |
    Output

The exact model is intentionally not defined yet.

Any such system must remain subordinate to the BroTracker architecture.

In particular:

- the BroTracker scheduler remains authoritative;
- the BroTracker sample timeline remains authoritative;
- realtime execution constraints remain mandatory;
- platform-specific audio components remain behind the audio boundary;
- user-configurable audio graphs must not introduce uncontrolled realtime behaviour.

### PJRC Audio System Design Tool / Audio Library

The PJRC Audio System Design Tool and associated Teensy Audio Library shall be investigated as a potential source of reusable audio processing components and architecture ideas.

This is a future investigation, not an adopted implementation decision.

The investigation should determine whether selected PJRC audio components can be integrated or wrapped as BroTracker audio backends or processing nodes while preserving the BroTracker architecture.

The investigation must specifically evaluate:

- compatibility with the BroTracker audio engine;
- realtime processing behaviour on Teensy 4.1;
- memory and CPU requirements;
- DMA and buffer requirements;
- interaction with the BroTracker scheduler and sample timeline;
- suitability for user-configurable audio paths;
- whether components can be isolated behind a clean BroTracker audio abstraction.

PJRC Audio Library components must not become mandatory dependencies of the BroTracker core merely to provide optional audio functionality.

The goal is to use proven Teensy audio building blocks where they provide clear benefit, while keeping BroTracker's architecture and realtime control authoritative.

## Audio Processing Blocks

The Teensy audio implementation is expected to process audio in fixed-size blocks determined by the selected audio platform and driver.

The BroTracker audio engine must therefore support block-based processing while retaining a sample-based internal timeline.

A block boundary must not become a tracker timing boundary.

If a tracker event occurs part-way through an audio block, the engine must preserve the event's position within that block.

This allows tracker timing to remain independent of the selected audio block size.

## USB Audio Integration

Teensy 4.1 may also expose a USB Audio interface to a connected host.

The host may enumerate the device as a USB audio device, for example:

    BroTracker Audio

USB Audio should be treated as a platform interface and host integration feature.

It must not become the authoritative timing source for the BroTracker realtime engine.

A combined USB configuration may also expose MIDI and other USB interfaces alongside USB Audio, subject to the selected Teensy USB configuration and platform implementation.

### Host-Side USB Audio Routing

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

## Audio Output Layer

The Audio Engine is separated from concrete audio output hardware through an abstract audio output layer.
The Audio Engine determines what audio should be produced. The audio output layer determines where the resulting audio is delivered.
The Audio Engine must not depend directly on a specific output device or hardware implementation.

The conceptual architecture is:

    Tracker / Scheduler
           ↓
       AudioEngine
           ↓
        AudioBuffer
           ↓
      AudioOutput
           ↓
    +------+---------+
    |                |
    PT8211        USB Audio
    backend        backend
    |                |
    Teensy I2S     USB Audio
    |                |
    physical DAC      Host

Concrete output backends are replaceable modules behind the AudioOutput boundary.

The initial output backends are:

- Teensy PT8211 Audio Kit
- USB Audio

Additional output backends may be added later without requiring changes to the Audio Engine or scheduler.

The Audio Engine must not contain PT8211-specific or USB Audio-specific implementation details.

The architectural principle is:

    Core determines what is played.
    Backend determines where the audio goes.

### Audio Output Modularity

Audio functionality should be divided into clearly defined modules with explicit boundaries.

The intended structure is conceptually:

    Audio
    ├── AudioEngine
    ├── AudioOutput
    │   ├── AudioOutputPT8211
    │   ├── AudioOutputUSB
    │   └── future output backends
    ├── Mixer
    ├── SamplePlayer
    ├── Voice
    ├── Effects
    └── other audio modules

Each significant audio subsystem should have its own file or set of files where practical.

Individual modules should be replaceable without requiring unrelated parts of the Audio Engine to be rewritten.

Examples:

- changing DSP processing must not require changing an output backend;
- changing the sampler must not require changing the DAC implementation;
- replacing the PT8211 backend must not require rewriting the Audio Engine;
- adding USB Audio must not require changing the scheduler;
- adding a future DAC should require adding a new backend rather than modifying the Audio Engine;
- changing output selection should be handled by audio output configuration rather than by the Audio Engine itself.

This modularity is an architectural requirement, not merely an implementation preference.

### Audio Output Selection

Current production selection uses the existing `config.ini` values documented
above: `bridge` and `pt8211`, with USB_BRIDGE as the development default. This
section describes the future model; it adds no INI values or implemented UI modes.

At most one hardware audio module may be attached to Teensy 4.1: either Audio
Shield or PT8211. There is no priority between these mutually exclusive modules.

An explicit user configuration takes precedence: select the installed hardware
output or USB bridge, including USB bridge while a hardware module is attached.
Without an explicit override, future boot-time selection uses a reliably
recognized supported module. Absent, uncertain or conflicting evidence falls
back to USB bridge; it must not select a hardware output by guessing.

PT8211_LIKE remains experimental host-only loading evidence, not approved module
detection, chip identification or DAC-health validation. Audio Shield recognition
requires diagnostics validated on physical hardware. Automatic selection policy
and production detection integration are deferred until that hardware is available.
Current manual INI selection and the USB_BRIDGE default remain unchanged.

Future selection must preserve static allocation, bounded realtime execution and
one intended hardware owner of audio clocks and updates. User configuration may
be exposed through a future UI, but host controls do not implement audio processing
or alter Teensy scheduler/timeline authority. No live switching is implied.

## Instrument Architecture

BroTracker audio playback shall support replaceable instrument modules behind a common instrument architecture.

The initial instrument foundation consists of:

- Sample Instrument;
- MIDI Instrument;
- Native Synth Instrument.

The conceptual architecture is:

Tracker / Pattern
       |
   Scheduler
       |
   Instrument
   /        \
  /          \
 v            v

Audio path    MIDI path
|             |
v             v
AudioEngine    MIDI Engine

### Sample Instrument

The Sample Instrument plays PCM sample data through the BroTracker audio engine.

Sample storage and streaming remain subject to the BroTracker sample architecture, including bounded RAM buffering and SD-backed streaming where required.

The Sample Instrument must not contain platform-specific audio output logic.

### MIDI Instrument

The MIDI Instrument represents an external hardware or software instrument controlled through MIDI.

It does not generate local audio.

MIDI events generated by a MIDI Instrument must use the same realtime scheduler and logical timing model as internal audio events.

The MIDI Instrument must therefore not introduce an independent sequencing or timing system.

### Native Synth Instrument

BroTracker shall include a built-in Native Synth Instrument as part of the initial instrument foundation.

The initial Native Synth architecture is a simple three-oscillator synthesizer intended as a general-purpose built-in sound source.

The initial implementation may provide:

- multiple basic oscillator waveforms;
- oscillator pitch and detune;
- filtering;
- amplitude envelope;
- basic modulation and sound-shaping controls.

The exact synthesis feature set is an implementation decision and may be expanded incrementally.

The Native Synth is not intended to emulate a specific commercial synthesizer.

### Optional Instrument Modules

Additional specialized synthesis engines may be implemented as optional Instrument Modules.

Potential examples include:

- AY / YM-style synthesis;
- SID-style synthesis;
- TB-303-style synthesis;
- FM synthesis;
- wavetable synthesis;
- other specialized synthesis engines.

These modules must remain behind the instrument architecture and must not require changes to the scheduler or unrelated audio modules.

The initial Teensy implementation does not require a dynamic runtime plugin loader.

Instrument Modules may be compiled into the firmware while remaining independently replaceable logical components.

The architecture should therefore allow a new instrument implementation to be added without rewriting the Audio Engine.

The architectural principle is:

Instrument defines how a musical event is realized.
Scheduler defines when it happens.

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

### SD-Backed Sample Streaming

Sample storage is not part of the realtime audio path.

WAV/sample data that cannot be kept fully in realtime RAM shall be streamed from SD using bounded prefetch buffers.

The architecture separates SD data production from audio consumption:

    KernelRun() / non-realtime context
        |
        | SD reads / prefetch
        v
   sample buffer
        |
        v
AudioStream::update()
        |
        | buffered sample data only
        v
   audio output

AudioStream::update() must not perform SD I/O or block waiting for storage.

SD latency must never affect scheduler advancement or tracker timing. Buffer underruns are treated as an audio-storage fault, not a timing fault.

The initial implementation will validate this model with a single streaming sample before introducing multi-voice streaming or more complex buffering policies.

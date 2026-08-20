# BroTracker Roadmap

## Phase 0

- Repository structure and documentation cleanup
- Define architecture boundaries between core, firmware, UI and tools

## Phase 1

- Timing engine foundation
- Deterministic playback and scheduling primitives

## Phase 2

- Basic sample player
- Simple instrument and sample loading workflow

## Phase 3

- Pattern editor
- Basic pattern editing and transport controls

## Phase 4

- MIDI support
- External clock and device integration

## Phase 5

- MOD/XM import support
- Additional playback engines

## Draft / Consideration - Internal Sound Engines

Future BroTracker development may include a small set of native sound engines designed specifically for the tracker rather than exact emulations of existing commercial instruments or hardware.

The exact engines, sound architecture and implementation priority remain subject to evaluation during development.

### Consideration 1 - Chiptune Engines

Potential native chiptune-oriented engines:

- AY-style chip synth
- SID-style chip synth
- Amiga-style period/sample waveform synth

These engines should be inspired by the characteristic sound and sequencing behaviour of their respective platforms without requiring cycle-accurate hardware emulation.

### Consideration 2 - Acid-style Synth

A simple native acid/bass synth inspired by the workflow and sound characteristics of classic 303-style instruments.

Potential features may include:

- oscillator and waveform selection;
- filter and resonance;
- envelope;
- accent;
- slide;
- drive/distortion.

The goal would be a compact tracker-oriented instrument rather than an exact hardware emulation.

### Consideration 3 - Sample-based Drum Kits

A basic native sample-based drum instrument or drum kit system.

Initial consideration may include common drum voices such as:

- kick;
- snare;
- closed and open hi-hat;
- clap;
- toms;
- rim;
- crash.

User-provided samples should remain supported, allowing native drum instruments to coexist with custom sample-based instruments.

### Development Approach

These engines are considered future additions and are not part of the initial core implementation.

A possible development order is:

1. Sample and MIDI instruments
2. AY-style, SID-style and Amiga-style chiptune engines
3. Acid-style synth
4. Sample-based drum kits
5. Additional engines as required

The actual order may change based on development experience, hardware performance, memory usage and the usefulness of each engine within the BroTracker workflow.

All native engines should integrate through the common Instrument abstraction and remain compatible with the core scheduler without unnecessarily complicating the core engine.

Internal synth engines will be strongly evaluated, estimated and tested to ensure that they do not negatively affect the performance of the reference hardware. Playback accuracy and accurate synchronization with MIDI OUT hardware come first.

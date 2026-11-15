# Sine Synth Design

Status: Proposed. This document describes the intended instrument and a staged implementation.

## Purpose

Extend the existing BroTracker Sine Synth instrument with sine operator
modulation and layering. Expose its settings directly in the Sine Synth
Instrument Screen. Keep a plain sine as the default sound.

The initial musical targets are a clean sub-bass, a pitched kick, a bass whose
timbre evolves over time, and tonal or metallic percussion. A hi-hat or snare
is not an acceptance requirement. All oscillator waveforms are sine waves.
Noise generation, sample layers, filters, and distortion are outside this design.

## Operator model

The full design has three audio-rate sine operators (OP1, OP2, OP3) and one
sine LFO. An operator is a carrier when it feeds the instrument output and a
modulator when it feeds another operator's phase input. BroTracker may call
this FM in the editor; the proposed engine technique is phase modulation (PM).

Each operator can have:

| Setting | Meaning |
| --- | --- |
| Frequency mode | `Ratio` follows the played note; `Fixed` uses a frequency in Hz. |
| Ratio | Multiplier of the note frequency. Fractional ratios allow inharmonic sounds. |
| Fixed Frequency | Base frequency in Hz, independent of the played note. |
| Detune | Fine adjustment in cents around the selected base frequency. |
| Level / Mod Depth | Carrier output level or the strength of phase modulation, according to the operator's role. |
| Amp Envelope | Independent ADSR. It shapes volume on a carrier and timbre on a modulator. |
| Phase Reset | Restarts the operator at a defined phase on note trigger. |
| Start Phase | Phase used when Phase Reset is enabled. |

The first version can use Ratio mode and reset to a fixed zero phase.
Fixed Frequency, Detune, and adjustable phase controls can follow. A start
phase away from zero with an instant attack can deliberately introduce a click;
it should remain a patch choice.

Add one instrument-level pitch envelope with a signed amount in semitones
and a decay time. For the first version, start at the offset on note trigger
and decay to the base pitch. This is enough to audition kick pitch sweeps
before adding an attack stage or independent pitch envelopes per operator.

## Routing

Use named algorithms so the editor can show which operators are audible and
which are modulators. Arrows denote phase modulation; `+` at the output denotes
summing audible carriers.

| Algorithm | Routing | Musical use |
| --- | --- | --- |
| Two Operator | OP2 modulates OP1; OP1 is audible | Bass, bells, metallic percussion |
| PM + Sine | OP2 modulates OP1; OP1 and OP3 are audible | Evolving bass with a separate clean sub |
| Parallel | OP1, OP2, and OP3 are audible | Additive tones and organ-like patches |
| Serial | OP3 modulates OP2, which modulates OP1 | More complex evolving tones |
| Dual Modulator | OP2 and OP3 both modulate OP1 | Independent attack and sustain timbres |
| Shared Modulator | OP3 modulates both OP1 and OP2; both are audible | Two carrier layers with a shared modulation source |

Start with Two Operator. With OP2's Mod Depth at zero, OP1 produces a plain
sine. When OP3 is added, prioritize PM + Sine and Parallel. Treat the remaining
algorithms as candidates to validate through patch design.

## Sine LFO

Add one sine LFO after the core operator controls are usable. Start with Rate
in Hz, Depth, and one selectable Destination: Pitch, Volume, or Mod Depth
for a selected modulator. Tempo sync, onset delay, and multiple destinations
can follow if the simpler controls prove useful.

## Instrument Screen

Selecting the Sine Synth instrument type exposes its settings on the existing
Instrument Screen. Use these groups:

- **Synth:** Algorithm and the instrument pitch envelope.
- **Operator:** OP1 / OP2 / OP3 selector, with the selected operator's settings
  and envelope. Show its role in the selected algorithm and label its main
  control `Level` or `Mod Depth` accordingly.
- **LFO:** Rate, Depth, and Destination once implemented.

The Operator selector edits one operator at a time so the same controls fit
on the handheld. Show only controls supported by the implemented algorithm
and frequency mode. The exact layout and input bindings can be designed
with the rest of the Instrument Screen.

## First implementation and validation

1. Preserve the existing plain-sine sound and behavior for existing
   instruments. Add OP2 as an optional phase modulator, independent operator
   envelopes, Ratio, and the pitch decay envelope.
2. Use three reference patches: Sub, Kick, and Metallic Percussion. Confirm
   that modulation depth changes the timbre without changing the sequencer's
   note timing and that repeated kick triggers have consistent attacks.
3. Measure worst-case audio callback time on Teensy with eight active tracker
   channels, including mixed synth and SD-streamed sample workloads and rapid
   retriggers. Check output headroom and audible aliasing at high ratios and
   modulation depths. Establish the CPU budget before adding operator count
   or routing complexity.
4. Add OP3 and one sine LFO in subsequent steps. Consider feedback, AM/RM,
   velocity response, key tracking, and stereo panning after the core
   instrument is validated.

Teensy owns synthesis and playback timing. The terminal exposes instrument
settings. Keep audio rendering bounded and free of dynamic allocation;
the basic eight-channel tracker must remain usable without PSRAM.

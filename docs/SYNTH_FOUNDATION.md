# Minimal note-driven Synth foundation

The Synth milestone below records its original MIDI placeholder behavior.
Instrument 02 now has the [USB MIDI foundation](MIDI_FOUNDATION.md); it remains
silent locally but emits note events. Synth behavior and its default pattern
are retained.


Implemented on `feature/sine-synth-foundation`, 2026-10-21. This note describes
`teensy41_dev` and `teensy41_pattern_variable_rate`; historical native-rate/beeper
fixtures remain regression workloads. Bridge diagnostics and their evidence are
paused/preserved; no holdback or output-routing change is part of this work.

## Identity and default pattern

Preserve the existing one-based instrument display, as confirmed by the user:

| Displayed instance ID | Raw pattern/wire byte | Category | Resource/behavior |
| --- | --- | --- | --- |
| 01 | 0 | Sample | Existing `Samples/test2.wav`, reference note 60 |
| 02 | 1 | MIDI | Reserved unsupported placeholder; no audio or MIDI events |
| 03 | 2 | Synth | New realtime sine, equal-tempered tuning |
| 04 | 3 | Sample | Retained independently loaded `Samples/test3.wav`, reference 60 |

IDs are instance identities, not type numbers. The binding stores `InstrumentType`
separately; another project can bind Synth, Sample or MIDI at different IDs.
The existing boot-time SD preparation and inspection are unchanged. Missing or
invalid WAV views omit only that Sample binding; they never substitute another
WAV or Synth. `Samples/test.wav` remains inspected/deferred, as before; it has not
been discarded or silently rebound. The default 32-row/eight-channel development
pattern keeps the existing sparse note sequences on CH1 and CH2, both selecting
Synth 03 explicitly. CH3?CH8 start empty; every channel can select Sample or Synth.
16/32/64-row fixture sizing and the 32-row UI display remain unchanged.

MIDI selection is recognized as `UnsupportedMIDI`, explicitly clearing that
channel on a pitched note and emitting silence. It never falls back to Sample or
Synth and no MIDI transport is called. Instrument-only updates retain the existing
selection-only semantics; they do not trigger or replace a playing voice. Unknown
IDs still fail validation. This is not implemented MIDI playback.

## Tuning and DSP contract

The pitched pattern domain is raw **24?127**. Preserve its MIDI-compatible values:
`frequency = 440 * 2^((note - 69)/12)`. Raw 69 is 440 Hz and displays **A-3**;
raw 60 displays C-3; extremes are C-0 (24, 32.70319566 Hz) and G-8
(127, 12543.85395 Hz). The Yamaha display convention subtracts two from the MIDI
quotient; no note transposition/renumbering is introduced. Invalid notes are
rejected before changing voice/output state. Synth accepts output rates 32000?192000
Hz, keeping the entire supported note range below Nyquist; current firmware uses
44100 Hz. Existing Sample-only renderer rates remain supported.

`SineOscillator` owns phase/tuning, `SynthEnvelope` owns amplitude and
`SineSynthVoice` combines them. The existing block renderer owns eight independent
monophonic channels and replacement transitions. A fresh note starts at sine
phase zero. No generic framework, dynamic voice pool or instrument editor exists.

The one-shot envelope uses cubic smoothstep `s(x)=x*x*(3-2*x)`:

- Attack: 0 to 1 over round(rate * 0.015) samples, **662 at 44100 Hz**.
- Decay: smoothly from 1 to exact 0 at sample `rate-1`. The complete envelope
  occupies `rate` samples (one second), including attack; there is no sustain.
- NOTE OFF: continue oscillator phase and release from the last envelope level
  to exact zero over the same 662-sample/15.011 ms interval. Repeated OFF during
  release does not extend it. Existing arrival/end-of-position scheduling applies.
- Nominal peak: **2048 PCM16**, about ?24.1 dBFS per voice. Eight aligned Synth
  voices total at most 16384 before existing gains; overlapping loud Samples can
  still saturate the existing mixer. This does not redefine channel gain policy.

A trigger/replacement involving Synth, or an already active transition, captures
the channel's last emitted PCM value and crossfades from that held value to the
new voice with smoothstep over round(rate * 0.002) samples: **88 samples / 1.995 ms**
at 44100 Hz. The first transition sample equals that captured value; the final
sample equals the new voice output. This is a short held-value transition, not a
second running voice or queued old oscillator. Rapid retriggers capture the current
mixed value again, with no tail accumulation. Fixed storage per channel bounds
work and memory. The new note/envelope begins at the exact command offset; only
its audible onset is softened by this transition. A NOTE OFF during a transition
releases the new envelope while the existing at-most-2 ms transition completes.
Sample-to-sample replacement keeps its established immediate semantics after any
Synth transition has finished. Unsupported MIDI clears audio immediately rather
than adding an audible tail to an unsupported instrument.

Explicit Stop, Start/restart, Reset, Pause and successful reconfiguration retain
the existing immediate voice/transition clearing behavior. They do not acquire
musical NOTE OFF tails. Continue/current-row replay remains owned by the existing
player. Gains, sample interpolation, mixing, sample-accurate event offsets, ordered
equal-offset commands and transactional validation remain in the existing path.
Rendering is fixed-storage and bounded by eight channels, 16 commands and the
caller-bounded frame count (128 in firmware); no allocation, file access, logging,
locks or host-dependent time is used.

## Validation

See the actual ignored [validation report](../build/synth-foundation/validation.md),
[reference WAV](../build/synth-foundation/reference.wav),
[reference measurements](../build/synth-foundation/reference.json) and
[complete diff including new files](../build/synth-foundation/complete.diff).
Generated artifacts remain under the existing ignored `build/` directory.

Native coverage checks note tuning/extremes/invalid inputs, one-second envelope
endpoints, monotonic attack/decay, release endpoints, exact command offsets,
rapid retriggers, Sample/Synth replacement, eight independent channels, explicit
MIDI silence, arbitrary instance identities, missing WAV rejection, transport
reset/restart/pause and block-partition independence through both the voice
renderer and pattern player. Existing playback, sample, gain, SD, publication,
preview, host input and output-selection regressions are run with this change.
The final run passed all five native suites (304 core cases, 30 variable-rate
control/protocol cases, nine SD cases, plus standalone clock/control checks), the
36-case output-selection/IRQ regression and `git diff --check`. All six pattern
firmware environments built. Development firmware uses 163836 flash bytes,
172832 RAM1 bytes (351456 free) and 295968 RAM2 bytes (228320 free).

Development firmware was uploaded to the discovered Teensy 17681760 on COM3.
The bounded serial check passed initial IDLE, default USB_BRIDGE negotiation,
START, 62 advancing position reports (tick 2 to 2485), STOP and final IDLE with
zeroed/invalid position. Both test2.wav and test3.wav loaded 68284 frames at 44100 Hz;
test.wav inspection remained present. The serial handle was closed and playback
left stopped. Default-fixture PERF reported no over-budget callbacks, maximum
142603 of 1741496 available cycles; this short two-sequence run is not an
eight-voice worst-case timing proof. These are automated checks only; no listening,
UX approval, physical waveform capture or console-pop resolution is claimed.

The 5.5-second reference contains C-0, C-3, A-3, G-8 (each one second plus 100 ms
silence), then a retrigger/OFF sequence. It is rendered through the actual
`VariableRateVoiceBlockRenderer`, not a duplicate host oscillator. The host tool
checks frequency from interpolated positive crossings, PCM peak, zero endpoints,
retrigger continuity and OFF completion. This validates generated PCM, not the
physical output route, listening quality or UX.

## Manual checks and later work

User listening/UX approval is pending:

- Listen to Synth 03 at low volume: clean sine tuning, gentle onset, one-second
  decay and silence afterward; compare A-3 against a 440 Hz reference.
- Try repeated notes, pitch changes and OFF during attack/decay/rapid retriggers.
- Select Sample 01 and Synth 03 separately and simultaneously on different
  channels; confirm Sample 04 remains available when its WAV loads and MIDI 02
  remains silent. Check existing gains and START/STOP/Pause/Continue UX.
- Note any console/output pops separately; automated checks do not close the
  paused bridge-pop investigation or establish a solution.

Later Synth work adds selectable primitive waveforms and their combinations,
then agreed controls. The planned three-oscillator/envelope/filter editor in
[UI_SCREEN_MAP.md](UI_SCREEN_MAP.md) remains future work; no new screen is built.
A later modular **Synth2** is separate work and is not implemented here. MIDI
backend/playback, instrument registry redesign, plugin loading and persistence
changes remain out of scope. No handheld deployment or SSH is required/performed.

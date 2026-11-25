# Sample Format Specification

## Status

Production streaming accepts unsigned PCM8 and signed PCM16 in standard RIFF
WAV, mono or interleaved stereo, at 7,040, 8,000, 8,363, 10,000, 11,025, 14,019,
16,000, 17,500, 20,000, 22,050, 32,000 and 44,100 Hz. The seven additional
rates came from the Wav-LQ library inventory. Mono and Synth are centered.
IEEE float, extensible WAV, other rates/depths, compressed WAV and loops
remain outside the production streaming subset.

## Purpose

This document defines the minimum sample data that BroTracker must support for playback and editing. The exact on-disk container is intentionally left open for future implementation changes. BroTracker may use standard PCM WAV or a custom internal format, as long as the runtime sample object exposes the same logical fields.

## Required sample properties

The agreed sample model must support the following properties. This is design
scope, not a claim that every property is currently implemented:

* PCM audio
* 8-bit or 16-bit depth
* mono or stereo
* 44.1 kHz sample rate
* additional production rates listed above
* future extension to 48 kHz
* loop start
* loop end
* loop type

  * None
  * Forward
  * Ping-Pong
  * Backward
* reference note (root-note playback anchor)
* signed fine-tuning playback correction
* optional original BPM and musical length in quarter-note beats

## Notes

* Sample slices are not stored in the sample itself. Slices belong to the instrument layer.
* The sample container format is not fixed yet. Existing industry formats are preferred when possible.
* The implementation should prioritize simplicity, fast loading, and deterministic playback on Teensy 4.1.
* The parser must ignore or skip unsupported metadata safely.

## Current format direction

The preferred starting point is support for existing PCM WAV samples, with the option to introduce an internal BroTracker sample cache format later if profiling shows a clear benefit.

## Sample Loading

BroTracker is designed to load sample data from storage on demand whenever practical, instead of requiring all samples to be loaded into RAM. Buffering, caching, and other optimization strategies are implementation details that may evolve as the project and target hardware requirements become better understood.

## Reference pitch, initialization and rhythmic tempo

See D0055 in [Architecture Decisions](ARCHITECTURE_DECISIONS.md), alongside
D00010's baseline, D0018's Yamaha numbering and D0029's quarter-note tempo model.
Source sample rate, reference note, fine tuning and loop tempo are independent.
BPM must not automatically determine reference pitch. Preserve valid explicit
reference-note/tuning metadata; otherwise default to internal 60 (C-3) and zero
correction. That note reproduces original speed, pitch and duration. It is a
playback anchor, not evidence that the recording is tuned to C.

The Sample Instrument editor stores Root Note and a Fine Tune default of 00–99
cents. Fine Tune 00 leaves the interval unchanged and 99 raises it by one cent
less than a semitone. Future analysis may suggest values while retaining explicit
settings. Broader signed tuning remains design scope.

```text
source-frame step = (source_sample_rate / output_sample_rate)
                  * 2^((target_note - reference_note) / 12)
                  * 2^(fine_tune_cents / 1200)
```

Zero correction gives a tuning ratio of one. Lower-rate sources must retain
original speed at the reference note through correct source/output conversion.
Rate and bit depth are independent; 8000/22050 Hz does not imply 8-bit PCM.
Production retains source PCM. Any future prepared representation must preserve
pitch, duration and consistent effective-rate metadata.

Original BPM and musical length in quarter-note beats are optional independent
metadata. Given known beat count, BPM = 60 * beats / exact duration in seconds.
Do not infer meter/bar count from duration alone. Tempo detection is future
suggestion work with manual correction. Explicitly enabled loop-speed matching
uses target_BPM / original_BPM, changing pitch and duration. It is not automatic
for every sample and is distinct from future pitch-preserving time stretching.
When requested, describe speed transposition using the nearest integer semitone
plus signed fractional residual; this does not identify the musical root note.
Combining tempo and note adjustments requires explicit semantics, without applying
the same rate adjustment twice.

## Implemented subset and next foundations

The legacy full-RAM loader accepts signed PCM16 mono at 44,100 Hz. Production
Sample bindings stream native frames through fixed-memory prefixes and rings.
PCM8 stays packed and is widened to the signed PCM16 domain as it is rendered.
Stored bytes 0, 128 and 255 map to -32768, 0 and 32512 respectively.
Trigger preparation applies Fine Tune to the Q32.32 note ratio, then composes
the source/output rate ratio once. Products round to the nearest Q32.32 step
with ties upward. The
output stays at 44,100 Hz. At Fine Tune 00, a Root Note trigger preserves source
pitch and duration to Q32.32 and output-frame precision. Sample Browser audition
uses the WAV's original pitch and speed regardless of Instrument tuning. The renderer
continues to use linear interpolation. See [production streaming](SD_SAMPLE_STREAMING.md).

A future Pattern finetune effect will replace the persisted Instrument Fine Tune
default for an individual trigger. An explicit zero override means zero cents.
The Instrument definition and Root Note retain their values. Effect execution,
operand range and MOD/XM `E5x` conversion are deferred to the FX task.

Future interpolation and anti-alias controls, additional rates and depths,
tuning metadata integration, tempo matching/detection, time stretching and
sample-loop playback remain pending.

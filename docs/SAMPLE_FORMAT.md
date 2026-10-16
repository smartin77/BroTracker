# Sample Format Specification

## Status

Draft

## Purpose

This document defines the minimum sample data that BroTracker must support for playback and editing. The exact on-disk container is intentionally left open for future implementation changes. BroTracker may use standard PCM WAV or a custom internal format, as long as the runtime sample object exposes the same logical fields.

## Required sample properties

The agreed sample model must support the following properties. This is design
scope, not a claim that every property is currently implemented:

* PCM audio
* 16-bit depth
* mono
* 44.1 kHz sample rate
* future lower-rate support (including 8 kHz and 22.05 kHz) and extension to 48 kHz
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

Users can specify known/measured pitch and signed fine tuning; positive correction
raises pitch/rate. Future analysis may suggest settings without silently replacing
explicit values. Fine-tuning storage units/precision remain undecided.

```text
source-frame step = (source_sample_rate / output_sample_rate)
                  * 2^((target_note - reference_note) / 12)
                  * fine_tuning_ratio
```

Zero correction means fine_tuning_ratio = 1. Lower-rate sources must retain
original speed at the reference note through correct source/output conversion.
Rate and bit depth are independent; 8000/22050 Hz does not imply 8-bit PCM.
Retaining source PCM or converting internal PCM remains a future storage decision;
both approaches must preserve pitch/duration and consistent effective-rate metadata.

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

Current portable loading supports signed PCM16 mono 44100 Hz. Native-rate playback
and VariableRateSampleVoice both require matching source/output sample rates;
the variable-rate voice accepts explicit Q32.32 steps. Reference-note interval
mapping is next. Additional rates, general source/output conversion, tuning metadata
integration, tempo matching/detection and time stretching remain pending. The
C-3/D-3 beeper bindings are trigger anchors, not measured physical tuning. Loop
metadata above also does not imply sample-loop playback is implemented.

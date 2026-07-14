# Sample Format Specification

## Status

Draft

## Purpose

This document defines the minimum sample data that BroTracker must support for playback and editing. The exact on-disk container is intentionally left open for future implementation changes. BroTracker may use standard PCM WAV or a custom internal format, as long as the runtime sample object exposes the same logical fields.

## Required sample properties

BroTracker samples must support:

* PCM audio
* 16-bit depth
* mono
* 44.1 kHz sample rate
* future extension to 48 kHz
* loop start
* loop end
* loop type

  * None
  * Forward
  * Ping-Pong
  * Backward
* root note
* fine tune

## Notes

* Sample slices are not stored in the sample itself. Slices belong to the instrument layer.
* The sample container format is not fixed yet. Existing industry formats are preferred when possible.
* The implementation should prioritize simplicity, fast loading, and deterministic playback on Teensy 4.1.
* The parser must ignore or skip unsupported metadata safely.

## Current format direction

The preferred starting point is support for existing PCM WAV samples, with the option to introduce an internal BroTracker sample cache format later if profiling shows a clear benefit.

## Sample Loading

BroTracker is designed to load sample data from storage on demand whenever practical, instead of requiring all samples to be loaded into RAM. Buffering, caching, and other optimization strategies are implementation details that may evolve as the project and target hardware requirements become better understood.

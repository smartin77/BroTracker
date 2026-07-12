# BroTracker Core Design Principles

This document defines the fundamental design principles of BroTracker.

These principles should remain stable throughout the lifetime of the project and serve as the foundation for all future development.

## 1. Teensy 4.1 is the Reference Platform

BroTracker is designed around Teensy 4.1.

Every core feature must be practical, maintainable and performant on the reference hardware.

If a feature cannot reasonably run on Teensy 4.1, it does not belong in the core project.

## 2. Playback First

Deterministic playback always has higher priority than user interface updates.

Audio, MIDI and clock timing must never depend on rendering performance.

## 3. Headless Architecture

The user interface is a client.

The realtime engine runs on Teensy 4.1.

Linux-based handheld devices are responsible only for display, user input and communication with the realtime engine.

The repository should reflect this separation by keeping engine logic in core and firmware, while UI-specific concerns remain isolated in ui.

## 4. Timing Over Features

Timing accuracy is more important than feature count.

A smaller tracker with excellent timing is preferred over a larger tracker with inconsistent timing.

## 5. Keep It Simple

Every new feature must justify its complexity.

Proof of concept comes before feature completeness.

BroTracker should evolve through small, reliable and testable steps.

## 6. Community Driven

BroTracker is an open-source project.

The architecture should encourage contributions without compromising the project's core philosophy.

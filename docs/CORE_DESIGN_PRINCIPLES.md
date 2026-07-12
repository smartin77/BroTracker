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

## 7. Modern Simplicity

BroTracker is inspired by the simplicity and efficiency of classic trackers such as ProTracker and FastTracker.

The project intentionally avoids unnecessary complexity where proven classic concepts still provide efficient solutions.

Pattern editing and song structure should remain primarily linear rather than relying on phrase-based composition.

Modern features should only be introduced when they provide clear practical advantages without compromising simplicity, deterministic playback or realtime performance.

The availability of modern storage media should be used to simplify memory management rather than increase runtime complexity.

Whenever possible, BroTracker should prefer streaming and efficient storage over keeping large amounts of data in RAM.

The limitations of the reference platform should guide architectural decisions, while modern hardware capabilities such as SD storage should be used where they provide real benefits.


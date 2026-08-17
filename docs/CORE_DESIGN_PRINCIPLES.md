# BroTracker Core Design Principles

This document defines the fundamental design principles of BroTracker.

These principles should remain stable throughout the lifetime of the project and serve as the foundation for all future development.

## 1. Teensy 4.1 is the Reference Platform

BroTracker is designed around Teensy 4.1.

Every core feature must be practical, maintainable and performant on the reference hardware.

If a feature cannot reasonably run on Teensy 4.1, it does not belong in the realtime core.

Host platforms are development and UI targets. They must not become the reference for realtime behaviour or hardware requirements.

The Teensy implementation remains the authoritative realtime target for:

- audio processing;
- MIDI processing and output timing;
- synchronization;
- deterministic sequencing;
- storage and realtime resource management.

## 2. Playback First

Deterministic playback always has higher priority than user interface updates.

Audio, MIDI and clock timing must never depend on rendering performance.

Internal audio events and external MIDI events should originate from the same realtime scheduling model so that their timing remains as closely aligned as practical.

MIDI OUT latency and jitter are core performance concerns.

## 3. Headless Architecture

BroTracker is headless by design.

The realtime engine is independent of graphical and text-based presentation.

The primary realtime implementation runs on Teensy 4.1.

The user interface is a client responsible for:

- presentation;
- user input;
- editing;
- communication with the realtime core.

The UI communicates with the core using commands and tracker state rather than graphical output.

The realtime core must remain functional if the UI is delayed, disconnected or restarted.

## 4. Platform Separation

The shared BroTracker core must not depend directly on:

- a specific display system;
- an operating system;
- a desktop audio API;
- a platform-specific MIDI driver;
- a specific UI framework.

Platform-specific functionality must be isolated behind interfaces or adapters.

The core should be testable on a host computer without requiring Teensy hardware.

## 5. One Core, Multiple Runtimes

BroTracker tracker logic should be implemented once and reused across the Teensy realtime implementation and optional host development runtimes.

The same core concepts and tracker semantics must be preserved across runtimes.

The Teensy 4.1 implementation remains the reference realtime runtime.

A host runtime may execute the same core on a computer or other supported platform for development, testing and convenient standalone use.

A host runtime is not a replacement for the Teensy realtime platform.

## 6. UI Client Platforms

The primary UI client platform is an ArkOS-based handheld gaming console, including devices such as the R36S/H and related RGV-family devices.

Additional UI client platforms are intended to include:

- Windows;
- macOS;
- Linux.

Android may be supported in the future.

UI client platforms are responsible for:

- presentation;
- user input;
- editing;
- communication with the realtime core.

A UI client must not become responsible for realtime playback timing.

The same logical BroTracker UI design should be used across supported UI client platforms rather than introducing a separate desktop-oriented tracker interface.

## 7. Fixed Logical UI Resolution

The canonical BroTracker UI resolution is 640 × 480.

The UI renderer should operate on this logical resolution independently of the physical display.

ArkOS handhelds may display the UI at native resolution.

Desktop UI clients may display the same framebuffer at an integer scale, such as 2×, to provide a larger working area without changing the logical UI layout.

Higher physical display resolution must not require a different tracker interface.

## 8. Timing Over Features

Timing accuracy is more important than feature count.

A smaller tracker with excellent timing is preferred over a larger tracker with inconsistent timing.

In particular, the timing relationship between internal audio and external MIDI output must remain a primary performance target.

## 9. Keep It Simple

Every new feature must justify its complexity.

Proof of concept comes before feature completeness.

BroTracker should evolve through small, reliable and testable steps.

## 10. Community Driven

BroTracker is an open-source project.

The architecture should encourage contributions without compromising the project's core philosophy.

## 11. Modern Simplicity

BroTracker is inspired by the simplicity and efficiency of classic trackers such as ProTracker and FastTracker.

The project intentionally avoids unnecessary complexity where proven classic concepts still provide efficient solutions.

Pattern editing and song structure should remain primarily linear rather than relying on phrase-based composition.

Modern features should only be introduced when they provide clear practical advantages without compromising simplicity, deterministic playback or realtime performance.

The availability of modern storage media should be used to simplify memory management rather than increase runtime complexity.

Whenever possible, BroTracker should prefer streaming and efficient storage over keeping large amounts of data in RAM.

The limitations of the reference platform should guide architectural decisions, while modern hardware capabilities such as SD storage should be used where they provide real benefits.
# BroTracker Design Decisions

This document records major architectural and project decisions.

The purpose is to preserve the reasoning behind important choices so they remain understandable as the project evolves.

---

## D0001 — Reference Platform

Reference platform: **Teensy 4.1**

Reason:

- Excellent realtime performance.
- Mature Arduino ecosystem.
- USB Host support.
- Enough CPU power and RAM for deterministic playback.

---

## D0002 — Headless Architecture

BroTracker follows a headless architecture.

The realtime playback engine runs on Teensy 4.1.

The user interface is a client responsible only for presentation, user interaction and communication.

---

## D0003 — Playback First

Realtime playback always has higher priority than user interface updates.

Timing accuracy must never depend on rendering performance.

---

## D0004 — First Playback Engine

The first playback engine will be a sample-based engine.

Reasons:

- Immediately usable.
- Validates the engine architecture.
- Provides a reference implementation for future playback engines.

Future engines may include FM synthesis, wavetable synthesis or other technologies.

---

## D0005 — Incremental Development

Development should proceed in small, self-contained milestones.

Every completed milestone should leave the repository in a usable and buildable state.
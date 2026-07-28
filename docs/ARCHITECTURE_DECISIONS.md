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


---

## D0006 — Native Import Philosophy

BroTracker prioritizes its own internal architecture over external file format compatibility.

Importers should convert supported external formats into BroTracker's internal representation whenever practical.

The playback engine should operate only on BroTracker's internal data structures and should not contain format-specific logic.

The importer should be permissive, while the playback engine should remain deterministic, simple and predictable.

---

## D0007 — WAV Import

WAV is considered a first-class import format.

BroTracker should support importing the most common PCM WAV variants used in music production, within the practical limits of the reference platform.

When possible, incompatible but convertible WAV formats should be automatically converted during import into BroTracker's internal sample representation.

Engine limitations should be defined by the capabilities of the reference hardware, not by the WAV container itself.

---

## D0008 — Project and Sample Storage

BroTracker projects must never depend on external file locations.

All samples required by a project must remain part of the project itself, either as individual BroTracker sample files or as part of a future project container.

The exact storage format is intentionally left open for future evaluation.

The storage system must be designed with the limitations of the reference platform in mind.

Particular attention should be given to:

* Fast sequential access from SD storage.
* Efficient streaming with limited RAM.
* Predictable realtime performance.
* Independence from the original source file locations.

The internal sample representation may differ from the imported file format if it provides advantages for playback performance or storage efficiency.

---

## D0009 — Project Memory

The GitHub repository is the primary source of truth for the BroTracker project.

Important architectural decisions, design principles and project ideas must be recorded in the project documentation instead of remaining only in discussions.

Documentation is expected to evolve together with the project.

Recording a decision does not make it permanent; decisions may be revised or replaced as better solutions are discovered through implementation and testing.

The purpose of the documentation is to preserve project knowledge, provide continuity and allow both humans and AI collaborators to understand the project's evolution.

---

## D00010 - Sample Format Baseline

BroTracker currently standardizes on the following minimum sample capabilities:

- PCM
- 16-bit
- Mono
- 44.1 kHz (48 kHz may be added later)
- Loop start/end
- Loop types: None, Forward, Ping-Pong, Backward
- Root note
- Fine tune

The storage format (standard WAV vs. internal format) remains an implementation decision. The detailed specification is maintained in `docs/SAMPLE_FORMAT.md`.

---

## D00011 - MIDI Routing Philosophy

BroTracker is primarily a tracker and sequencer.

The project shall provide reliable MIDI input and output across all supported MIDI interfaces.

A simple internal MIDI port routing mechanism between supported interfaces is considered part of the core system. Its purpose is to connect available MIDI inputs and outputs in a straightforward and predictable manner.

BroTracker is not intended to become a general-purpose MIDI processing application.

Advanced functionality such as:

- event filtering
- event transformation
- channel remapping
- scripting
- complex routing graphs

is outside the current project scope and should be handled by dedicated external software or hardware when required.

This decision keeps the MIDI subsystem focused on reliable sequencing while still allowing practical MIDI connectivity between supported interfaces.

---

## D0012 — External Inspiration

BroTracker may draw inspiration from existing open-source projects and proven software architectures and audio libraries (juce.com).

However, external projects should serve only as references for design ideas, workflows and architectural concepts.

BroTracker must remain an independent implementation built around its own goals and the constraints of the reference platform.

In particular:

- The realtime engine must remain optimized for Teensy 4.1.
- External frameworks should not become mandatory dependencies for the core engine.
- Ideas may be adopted, but implementations should remain native to BroTracker whenever practical.
- Proven concepts are preferred over unnecessary reinvention, provided they fit the project's architecture and philosophy.

The goal is to learn from successful projects without becoming constrained by them.

---

# D0013 - UI Base Resolution

The canonical UI resolution is **640×480**.

Future versions may support higher resolutions (e.g. **800×600**) while remaining fully backward compatible with the 640×480 layout.

Higher resolutions may display additional content (primarily more tracker channels), but all functionality must remain accessible at 640×480 through scrolling or page navigation.

If content extends beyond the visible 640×480 area, the UI must provide a clear visual indicator that additional content is available.

---

# D0014 - UI Bitmap Font

The primary BroTracker UI font is a fixed-width bitmap font.

Character bitmap size is **5×7 pixels**.

Default spacing:
- Top spacing: **1 px**
- Right spacing: **1 px**

This results in a default character cell size of **6×8 pixels**.

Word spacing and other text layout rules may be adjusted separately while preserving the character cell alignment.

The bitmap font is considered part of the BroTracker visual identity and should remain consistent across all supported platforms.

---

# D0015 - UI Character Set

The primary UI character set is ASCII.

Optional extended character support may be added for localized text.

To preserve the retro appearance and minimize font size, accented characters are provided only as lowercase glyphs. Uppercase accented letters reuse the corresponding lowercase accented glyph.

Example:

Á → á

Č → č

Š → š

This behavior applies only to accented characters. Standard ASCII uppercase and lowercase letters remain unchanged.

---

# D0016 - Pattern Row Numbering

The first column of the Pattern View displays row numbers.

The numbering format is configurable:

- H0 - hexadecimal, starting from 0
- H1 - hexadecimal, starting from 1
- D0 - decimal, starting from 0
- D1 - decimal, starting from 1

Internally, row numbering always starts at 0. The selected mode affects only the displayed values and user interaction. All internal processing remains zero-based.

Pattern View may optionally highlight bar start rows using a brighter color. The bar interval is configurable (for example every 4, 8 or 16 rows).

---

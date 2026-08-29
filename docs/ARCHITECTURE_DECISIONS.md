# BroTracker Design Decisions

This document records major architectural and project decisions.

The purpose is to preserve the reasoning behind important choices so they remain understandable as the project evolves.

## D0001 — Reference Platform

Reference platform: **Teensy 4.1**

Reason:

- Excellent realtime performance.
- Mature Arduino ecosystem.
- USB Host support.
- Enough CPU power and RAM for deterministic playback.

## D0002 — Headless Architecture

BroTracker follows a headless architecture.

The realtime playback engine runs on Teensy 4.1.

The user interface is a client responsible only for presentation, user interaction and communication.

## D0003 — Playback First

Realtime playback always has higher priority than user interface updates.

Timing accuracy must never depend on rendering performance.

## D0004 — First Playback Engine

The first playback engine will be a sample-based engine.

Reasons:

- Immediately usable.
- Validates the engine architecture.
- Provides a reference implementation for future playback engines.

Future engines may include FM synthesis, wavetable synthesis or other technologies.

## D0005 — Incremental Development

Development should proceed in small, self-contained milestones.

Every completed milestone should leave the repository in a usable and buildable state.

## D0006 — Native Import Philosophy

BroTracker prioritizes its own internal architecture over external file format compatibility.

Importers should convert supported external formats into BroTracker's internal representation whenever practical.

The playback engine should operate only on BroTracker's internal data structures and should not contain format-specific logic.

The importer should be permissive, while the playback engine should remain deterministic, simple and predictable.

## D0007 — WAV Import

WAV is considered a first-class import format.

BroTracker should support importing the most common PCM WAV variants used in music production, within the practical limits of the reference platform.

When possible, incompatible but convertible WAV formats should be automatically converted during import into BroTracker's internal sample representation.

Engine limitations should be defined by the capabilities of the reference hardware, not by the WAV container itself.

## D0008 — Project and Sample Storage

BroTracker projects must never depend on external file locations.

All samples required by a project must remain part of the project itself, either as individual BroTracker sample files or as part of a future project container.

The exact storage format is intentionally left open for future evaluation.

The storage system must be designed with the limitations of the reference platform in mind.

Particular attention should be given to:

- Fast sequential access from SD storage.
- Efficient streaming with limited RAM.
- Predictable realtime performance.
- Independence from the original source file locations.

The internal sample representation may differ from the imported file format if it provides advantages for playback performance or storage efficiency.

## D0009 — Project Memory

The GitHub repository is the primary source of truth for the BroTracker project.

Important architectural decisions, design principles and project ideas must be recorded in the project documentation instead of remaining only in discussions.

Documentation is expected to evolve together with the project.

Recording a decision does not make it permanent; decisions may be revised or replaced as better solutions are discovered through implementation and testing.

The purpose of the documentation is to preserve project knowledge, provide continuity and allow both humans and AI collaborators to understand the project's evolution.

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

## D0013 - UI Base Resolution

The canonical UI resolution is **640×480**.

Future versions may support higher resolutions (e.g. **800×600**) while remaining fully backward compatible with the 640×480 layout.

Higher resolutions may display additional content (primarily more tracker channels), but all functionality must remain accessible at 640×480 through scrolling or page navigation.

If content extends beyond the visible 640×480 area, the UI must provide a clear visual indicator that additional content is available.

## D0014 - UI Bitmap Font

The primary BroTracker UI font is a fixed-width bitmap font.

Character bitmap size is **5×7 pixels**.

Default spacing:

- Top spacing: **1 px**
- Right spacing: **1 px**

This results in a default character cell size of **6×8 pixels**.

Word spacing and other text layout rules may be adjusted separately while preserving the character cell alignment.

The bitmap font is considered part of the BroTracker visual identity and should remain consistent across all supported platforms.

## D0015 - UI Character Set

The primary UI character set is ASCII.

Optional extended character support may be added for localized text.

To preserve the retro appearance and minimize font size, accented characters are provided only as lowercase glyphs. Uppercase accented letters reuse the corresponding lowercase accented glyph.

Example:

Á → á
Č → č
Š → š

This behavior applies only to accented characters. Standard ASCII uppercase and lowercase letters remain unchanged.

## D0016 - Pattern Row Numbering

The first column of the Pattern View displays row numbers.

The numbering format is configurable:

- H0 - hexadecimal, starting from 0
- H1 - hexadecimal, starting from 1
- D0 - decimal, starting from 0
- D1 - decimal, starting from 1

Internally, row numbering always starts at 0. The selected mode affects only the displayed values and user interaction. All internal processing remains zero-based.

Pattern View may optionally highlight bar start rows using a brighter color. The bar interval is configurable (for example every 4, 8 or 16 rows).

## D0017 – BroTracker Font Format (BTF)

BroTracker uses a custom 5×7 bitmap font with fixed spacing.

The original source format for the font is the export generated by BitFontMaker 2:

[BitFontMaker2](https://www.pentacom.jp/pentacom/bitfontmaker2/)

BitFontMaker was selected because it provides several advantages:

- pixel-perfect editing;
- simple text-based representation;
- easy version control with Git;
- convenient change tracking;
- independence from operating-system-specific font rendering;
- full control over glyph appearance.

BroTracker does not rely on TrueType (TTF), OpenType (OTF) or other scalable font formats at runtime.

## D0018 - Note Nnumbering and Octave_Cconvention

Internal:
0–127 MIDI note number

Display:
Yamaha convention
C3 = MIDI 60

Transport:
MIDI value unchanged

Compatibility:
Alternative display conventions may be supported

## D0019 - Note-Off Events

BroTracker supports an explicit `NOTE_OFF` event in pattern data.

`NOTE_OFF` is distinct from an empty pattern position. It represents an explicit note termination event at the current pattern position.

For tracker UI representation, the instrument field of a `NOTE_OFF` event uses one of two dedicated glyphs:

- `¯` — upper dash: note is terminated immediately at the beginning of the current pattern row.
- `_` — lower underscore: note remains active for the current pattern row and is terminated at the end of the row, immediately before the next pattern position is processed.

The exact runtime event representation for distinguishing these two note-off timings is intentionally deferred until the playback/scheduler implementation.

The glyphs are UI representation only and are not stored as part of the musical event data.

## D0020 - Future export

Most likely, we won't support exporting our module to other module formats. For the audio part, we’ll only support a mixdown audio file based on the internal MIX system parameters. As for MIDI, it will be a standalone MIDI file, regardless of how the user manages to connect or use it meaningfully with third-party tools. Since these involve different platforms and hardware synths controlled via MIDI, users will need their own hardware mixer or separate processing for the final audio.

## D0021 - One Core, Multiple Runtimes

BroTracker shall use a shared platform-independent tracker core.

The same core logic should be reusable by:

- the Teensy 4.1 realtime runtime;
- optional host development runtimes.

The Teensy 4.1 runtime remains the reference realtime implementation.

A host runtime is an optional environment for executing the shared core outside the Teensy hardware, primarily for development, testing and convenient standalone use.

The core must not depend directly on operating-system, display, audio or MIDI APIs.

## D0022 - Supported UI Client Platforms

BroTracker shall support multiple UI client platforms while maintaining a single logical UI design.

The primary UI client platform is an ArkOS-based gaming handheld, including R36S/H and related RGV-family devices.

Additional UI client targets are:

- Windows;
- macOS;
- Linux.

Android may be supported in a future implementation.

UI client platform support must not compromise the Teensy 4.1 realtime architecture.

## D0023 - Host Development Runtime

The shared BroTracker core shall be testable on a host computer without requiring Teensy hardware.

A host development runtime may provide platform-specific implementations for:

- audio output;
- MIDI input/output;
- display;
- keyboard;
- mouse;
- gamepad;
- file storage;
- timing.

The host development runtime must preserve the same tracker semantics and scheduling model as the Teensy implementation.

Host runtime implementations are development and convenience environments. They do not replace Teensy 4.1 as the reference realtime platform.

## D0024 - Logical UI Resolution

The canonical BroTracker UI resolution is 640 × 480.

The UI renderer shall use this logical resolution independently of the physical display platform.

ArkOS handhelds may display the framebuffer at native resolution.

Desktop frontends may display the same framebuffer using integer scaling, such as 2×, without changing the logical UI layout.

This allows desktop systems to provide a larger physical display while maintaining a single canonical BroTracker interface.

## D0025 - Shared Realtime Event Scheduling

Internal audio events and external MIDI events shall originate from the same realtime scheduling model.

MIDI OUT timing is a primary realtime performance requirement.

The scheduler architecture must therefore avoid treating MIDI output as an asynchronous secondary process that can introduce unnecessary timing differences relative to internal audio.

The actual latency and jitter characteristics of each hardware and host transport must be measured during implementation.

## D0026 - Initial USB MIDI Hardware Configuration

Initial hardware development shall use USB connectivity.

The CME H4MIDI is the initial external MIDI routing device for physical DIN MIDI connectivity.

The initial development path is:

Teensy 4.1
    |
   USB
    |
CME H4MIDI
    |
 DIN MIDI
    |
External MIDI hardware

Direct physical DIN MIDI IN/OUT connections on the Teensy are intentionally deferred because they require additional hardware work.

The core MIDI architecture must remain independent of this temporary hardware configuration.

## D0027 - Physical Teensy MIDI Connections Deferred

Direct physical DIN MIDI IN/OUT hardware on Teensy is deferred until a later development stage.

The initial implementation shall prioritize USB MIDI and reliable MIDI timing through the common MIDI event architecture.

Adding physical DIN MIDI hardware later must not require changes to the tracker, sequencer or core event model.

## D0028 - Candidate Ideas Under Consideration (External Inspiration)

Per the D0012 external inspiration policy, the following ideas are under consideration as potential future benefits for BroTracker. None of these are committed yet; each must be revisited and evaluated concretely during its practical implementation phase.

- **Module loader registry pattern** — a loader interface (identify + load) combined with a registry/manager that iterates registered loaders. Candidate pattern for a BroTracker MOD/XM importer that converts external formats into the internal representation (consistent with D0006).
- **Tick-based effect semantics reference** — documented `.MOD`/`.XM` tick-based effect behavior (arpeggio, portamento, vibrato, volume slide, pattern break, pattern loop, note delay/cut, retrigger) as a possible reference when interpreting imported MOD/XM pattern data into BroTracker's own event model. BroTracker's own effect/event set would not need to match legacy numbering or reproduce legacy quirks.
- **Interpolation formulas** — compact, fixed-point-friendly linear/cubic Lagrange/cubic spline resampling formulas, as a possible reference for Teensy-side sample playback interpolation given the hardware's constraints (D0003).
- **Volume ramping to avoid clicks** — ramping at sample start/end/cut as a possible technique for BroTracker's own sample engine, regardless of import format.
- **Padded sample buffers** — allocating small guard padding around sample memory to avoid per-sample bounds checks during interpolation, as a possible low-level technique for a resource-constrained realtime engine.

These remain open considerations, not adopted decisions. BroTracker continues to prefer independent, native implementations per D0012.

## D0029 - Timing Model and Microtiming

BroTracker will use **BPM (Beats Per Minute) as the primary tempo representation**.

BPM represents the number of quarter-note beats occurring per minute.

The user-facing BPM value shall be displayed with one decimal place:

`NNN.N BPM`

Examples:

- `120.0 BPM`
- `127.5 BPM`
- `130.0 BPM`
- `140.0 BPM`

The UI shall therefore provide a tempo resolution of **0.1 BPM**.

### Sequencer Timing Relationship

BroTracker uses a conventional sequencer timing relationship in which:

- one BPM beat represents one quarter note;
- one tracker row represents one sixteenth note;
- four tracker rows therefore represent one quarter-note beat;
- sixteen tracker rows represent one four-beat sequencer bar.

This relationship is independent of the internal scheduler resolution.

For example, at `130.0 BPM`:

- one quarter-note beat = approximately `461.538 ms`;
- one tracker row = approximately `115.385 ms`;
- sixteen tracker rows = approximately `1.846 s`.

A conventional 16-step sequencer pattern containing events on steps `1, 5, 9, 13` therefore maps directly to BroTracker rows `1, 5, 9, 13`.

This establishes a predictable relationship between BroTracker pattern rows and conventional drum-machine/sequencer timing.

### Internal Timing Resolution

BroTracker will use an internal timing resolution of **96 ticks per tracker row**.

These ticks are an internal scheduler resolution and are not exposed as PPQN or as a user-facing musical unit.

The resolution is intentionally finer than the visible tracker row.

Therefore:

- 1 tracker row = 96 internal ticks;
- 1 quarter-note beat = 4 tracker rows = 384 internal ticks;
- 16 tracker rows = 1536 internal ticks.

The internal tick resolution is an implementation detail of the realtime scheduler. It must not be treated as a fixed limitation of the musical model.

The scheduler architecture should allow the internal resolution to be increased in the future without changing the logical pattern row structure or the user-facing timing model.

### Timing Subdivisions

The 96-tick row resolution provides exact integer timing positions for common rhythmic subdivisions.

Examples within one tracker row:

- halves: 48 ticks;
- triplets: 32 ticks;
- quarters: 24 ticks;
- eighths: 12 ticks;
- sixteenths: 6 ticks.

This allows common rhythmic subdivisions to be represented without fractional tick positions or rounding.

The initial implementation does not need to expose all available subdivisions to the user.

The internal scheduler should nevertheless preserve the full timing resolution so that additional rhythmic divisions, microtiming and more advanced Table functionality can be introduced later without redesigning the core timing engine.

### Tables

Tables are the user-facing mechanism for rhythmic subdivisions, microtiming and other event timing details within the tracker row.

Tables are separate from BPM. They do not change the meaning of BPM and do not represent beats per minute.

Tables may define:

- straight subdivisions;
- triplets;
- other multiplets;
- timing offsets;
- future rhythmic divisions;
- other event timing relationships required by the tracker.

The initial Table implementation may expose only a subset of these capabilities.

The Table system must be designed around the internal timing resolution rather than defining a separate incompatible timing clock.

### Microtiming

The visible tracker row remains the primary editing and display unit.

Events may nevertheless occur at different internal timing positions within the row.

This allows multiple events associated with the same tracker row to have different timing positions without introducing additional visible pattern rows.

For example, multiple events may occupy a single tracker row at internal positions such as:

`0`, `32`, and `64`.

The exact user-facing representation of these positions is an implementation decision.

### Scheduler Timing

The user-facing BPM and Table values do not directly define the implementation of the realtime scheduler.

The scheduler shall operate using the internal 96-tick timing resolution.

The relationship is therefore:

`BPM → tracker row duration → internal tick duration`

At a given BPM, the duration of one internal tick is derived from the duration of one tracker row.

For example, at `130.0 BPM`:

- one quarter-note beat ≈ `461.538 ms`;
- one tracker row ≈ `115.385 ms`;
- one internal tick ≈ `1.202 ms`.

The scheduler may use a higher-resolution hardware timer or clock source internally. The 96-tick model defines the musical timing resolution, not necessarily the physical timer interrupt frequency.

### Future Resolution

The initial implementation uses 96 internal ticks per tracker row.

The timing architecture must not assume that 96 is the permanent maximum resolution.

Future implementations may use higher internal resolutions such as 192 or 384 ticks per tracker row if required for more precise microtiming, MIDI synchronization, audio scheduling or other realtime requirements.

Such an increase should not require changes to:

- the logical pattern row structure;
- the BPM model;
- the user-facing tracker grid;
- the basic Table concept;
- the musical relationship between sequencer beats and tracker rows.

The initial implementation should therefore treat 96 ticks as the **current internal timing resolution**, not as a permanent architectural limit.

## D0030 - Loading a New Tune During Playback

If a tune is currently playing and the user interface allows another tune/project to be opened during playback, opening the new tune must not require the user to manually restart playback.

The behaviour is controlled by a configuration directive:

- **Default:** the newly opened tune starts playing immediately after loading.
- **`NEW_FILE_QUEUE`:** the newly opened tune is placed into the playback queue instead.

The default behaviour is considered a **must-have core workflow**.

The queue behaviour will be evaluated and implemented later. The initial implementation shall support only the default behaviour of immediately starting playback of the newly loaded tune. The configuration directive and queue mechanism therefore do not need to be functional in the initial implementation.

The important distinction is that opening a new tune during playback must never leave the newly loaded tune stopped and require the user to exit the file browser and manually start playback.

## D0031 - External Format Import

External tracker format compatibility is **not a priority of the BroTracker realtime core**.

The BroTracker playback engine shall operate exclusively on BroTracker's internal data representation and shall not contain format-specific loading logic for external tracker formats.

External formats such as MOD and XM shall be handled by dedicated importers located outside the realtime core.

The intended workflow is:

External module file
→ GUI importer
→ format-specific parser
→ conversion to BroTracker internal representation
→ BTM (BroTracker Module / BroTracker Tune)
→ BroTracker core

The importer is responsible for:

- reading the original file format;
- interpreting its format-specific structures and semantics;
- converting compatible data into BroTracker's internal representation;
- handling unsupported or incompatible features during conversion;
- producing a valid BroTracker module/tune.

The resulting BTM data must be fully self-contained and suitable for normal BroTracker playback.

MOD is an initial candidate for future import support. XM may be added later. Other tracker formats may be considered in the future, but external format support is not a core development priority.

The importer is primarily a **GUI-side functionality** intended for the target handheld and other UI clients. The realtime engine must not depend on the presence of an external-format importer.

This separation keeps the core engine small, deterministic and focused on realtime playback, while allowing external format support to evolve independently.

## D0032 - BroTracker File and Container Formats

BroTracker distinguishes between three related file/container formats with different purposes:

- **BTT — BroTracker Tune**
- **BTP — BroTracker Project**
- **BTM — BroTracker Module**

These formats are not three different playback representations. The BroTracker realtime core operates on its internal data structures. The formats define how tunes and projects are stored, referenced and transported.

### BTT — BroTracker Tune

BTT is a lightweight tune/module representation.

Sample instruments in a BTT may reference samples by their external storage locations rather than containing the sample data itself.

This makes BTT suitable when the samples are intentionally kept outside the tune container.

If a referenced sample cannot be found when loading a BTT, the tune and instrument definition must remain available. The missing sample shall be reported as **not located**, and the user should be able to locate the sample manually.

Instrument parameters must not be discarded merely because the referenced sample is unavailable.

### BTP — BroTracker Project

BTP is the native BroTracker project format and is the initial project format.

A BTP project is stored together with its samples in a dedicated project directory on the attached storage device.

The project directory uses the tune/project name. Renaming the tune/project should also rename the associated project directory automatically.

All samples used by the project are copied into the project directory so that the project does not depend on their original external locations.

The BTP format shall be designed as an **addressable format**.

Important runtime-relevant data shall be stored so that it can be loaded first and efficiently into memory when required. Optional or non-essential information shall be placed later in the file and may be loaded independently from the storage device.

Examples of information suitable for separate, on-demand loading include:

- tune descriptions;
- instrument descriptions;
- additional descriptive metadata;
- information that is not required for realtime playback.

This allows descriptive information to have practical or effectively unrestricted text length without unnecessarily increasing the memory requirements of the realtime portion of the project.

Instrument descriptions are particularly useful for MIDI instruments. A creator may document the external hardware used, MIDI channel, bank/program information, or short notes describing the external instrument configuration.

The exact binary structure and field sizes of BTP are intentionally left open for a future file format specification.

### BTM — BroTracker Module

BTM is a transport and distribution container for a complete BTP project.

A BTM file is a ZIP container containing the complete BTP project directory, including the project data and all required samples.

BTM therefore does not define a separate musical or playback representation. It packages an existing BTP project into a single portable file.

The intended relationship is:

BTT = lightweight tune with external sample references

BTP = complete native BroTracker project

BTM = ZIP container containing a complete BTP project

The separation allows the native project format to remain optimized for BroTracker's storage and runtime requirements while BTM provides a convenient single-file format for transfer, backup and distribution.

## D0033 - Development Automation and CI

BroTracker shall use GitHub Actions as its primary continuous integration platform.

The purpose of CI is to automatically verify that changes remain buildable and testable, particularly as the project grows and community contributions are introduced.

The initial CI system should remain small and focused.

Initial CI responsibilities:

- build the shared BroTracker core;
- build the host development runtime;
- run available automated tests;
- report the result for pushes and pull requests.

The CI system should support the project's development principle that functional commits should leave the repository in a buildable state.

As the project matures, CI may be extended to include:

- Teensy 4.1 firmware builds;
- additional host platforms;
- static analysis;
- CodeQL or similar code scanning;
- additional automated tests;
- release artifact generation.

GitHub Actions is preferred because it is integrated directly into the project's GitHub repository and does not require separate CI infrastructure.

BroTracker shall avoid unnecessary DevOps infrastructure. Container orchestration, self-hosted CI infrastructure and other complex deployment systems are outside the current project requirements.

Dependency maintenance and security updates may use GitHub Dependabot where appropriate.

Release automation is considered a future extension. The intended release workflow is to build and verify release artifacts through CI and publish them through GitHub Releases.

The CI system must support the project rather than become a development burden. Automation should be introduced incrementally when it provides a clear practical benefit.

## D0034 - Modular Subsystem Architecture

BroTracker shall be implemented as a single integrated application composed of clearly separated logical subsystems rather than as a monolithic implementation.

The final Teensy 4.1 firmware remains a single application and executable. Modularity refers to the internal architecture of the application, not to independently running programs.

Major subsystems should have clear responsibilities, explicit interfaces and limited dependencies on other subsystems.

The primary goal is replaceability: a major subsystem should be replaceable or substantially rewritten without requiring unrelated subsystems to be redesigned.

Subsystem boundaries should therefore be established before implementing large amounts of functionality.

Major logical subsystems may include, but are not limited to:

- tracker core and data model;
- realtime scheduler;
- playback engine;
- audio engine;
- MIDI engine;
- storage and file loading;
- Teensy runtime and hardware adapters;
- UI communication/state;
- renderer;
- user interface and editing.

Subsystems should expose only the interfaces required by other subsystems. Implementation details should remain internal to the subsystem wherever practical.

The architecture should avoid unnecessary coupling between subsystems. In particular:

- the realtime scheduler must not depend on UI rendering;
- the core tracker model must not depend on Teensy hardware;
- UI code must not own realtime playback timing;
- platform-specific hardware functionality must remain behind runtime or adapter boundaries;
- storage implementation must not become part of the tracker data model;
- audio and MIDI output should receive events from the same realtime playback model rather than implementing independent sequencing logic.

Modules should be coarse-grained logical components rather than an excessive number of small classes or files. Modularity must not introduce unnecessary abstraction, runtime overhead or complexity on Teensy 4.1.

Where practical, each major subsystem should be testable independently on a host platform using test doubles or platform-independent implementations.

Development should proceed incrementally: a subsystem interface should be established, implemented, tested and integrated before building substantial functionality on top of it.

The architecture should permit future replacement of individual implementations, such as a scheduler, audio engine, MIDI backend or storage implementation, without changing the overall tracker architecture.

This decision extends the existing principles of platform separation, headless operation, one core/multiple runtimes and timing-first development.

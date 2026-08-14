# Effect Event Architecture

## Status

Draft

## Purpose

This document defines the architecture and visual behavior of effect events in BroTracker.

BroTracker supports effect events as independent events associated with a pattern position.

An effect event does not require a new note or instrument to be present at the same position.

Effect events may modify the playback state while the currently active note continues to play, unless the note has been explicitly terminated by a `NOTE_OFF` event.

The exact list of supported effect events will be defined and expanded as tracker formats and existing implementations are researched.

## Pattern Position Events

A pattern position may contain a note, an instrument, and zero or more effect events.

For example:

    C-5  01

represents a note `C-5` played using instrument `01`.

Effect events may also exist at the same position, but their values are not displayed directly inside the Pattern View.

The Pattern View only indicates the presence of additional events through its visual state.

## Event State Continuation

Pattern positions are processed as a continuous sequence of events.

A missing note, instrument, or effect event does not clear the corresponding previous state.

Each event type updates only the part of the playback state that it represents.

Conceptually:

    Pattern Position
          │
          ├── Note
          │     └── updates current note
          │
          ├── Instrument
          │     └── updates current instrument
          │
          └── Effect
                └── updates effect state

An empty field leaves the corresponding state unchanged.

For example:

    Row    Note    Instrument    Effect
    -------------------------------------
    00     C-4     01             ---
    01     ---     ---            ---
    02     ---     02             ---
    03     ---     ---            E01
    04     ---     ---            ---
    05     OFF     ---            ---

The resulting playback state can be understood as:

    Row 00
        note = C-4
        instrument = 01

    Row 01
        note = C-4
        instrument = 01

    Row 02
        note = C-4
        instrument = 02

    Row 03
        note = C-4
        instrument = 02
        effect = E01

    Row 04
        note = C-4
        instrument = 02
        effect = E01

    Row 05
        note = OFF
        active note terminated

A note event therefore does not require an instrument event, and an instrument event does not require a note event.

If an instrument is specified without a new note, the previous active note is retained and is associated with the newly specified instrument according to the playback semantics of the instrument.

Similarly, an effect event may exist without a note or instrument event. In that case, the effect modifies the currently active playback state rather than creating a new note.

An explicit `NOTE_OFF` event terminates the active note. Empty note fields do not terminate it.

This state-continuation model is fundamental to the BroTracker event architecture and is intended to support tracker-style pattern authoring and compatibility with traditional tracker formats.

## Independent Effect Events

An effect event may exist without a note or instrument at the same pattern position.

For example:

    --- --

may represent an otherwise empty pattern position.

If effect events are present at that position, the same glyphs are displayed using the `bright` visual mode:

    --- --

The bright representation indicates that the position contains additional event data.

The actual effect types and parameter values are displayed separately in the effect detail area.

## Pattern View Visual State

The Pattern View does not display effect event types or parameter values directly.

The visual state of a pattern position provides only a simple indication of whether additional event data is present.

### Note and Instrument

A normal note and instrument without effect events may be displayed using the normal visual mode.

For example:

    C-5  01

This case is expected to be relatively uncommon because effect events may be associated with the same position.

### Empty Position

A completely empty position is displayed using the normal visual mode:

    --- --

### Effect-Only Position

If a position contains effect events but no note or instrument, the same empty glyphs are displayed using the `bright` visual mode:

    --- --

The bright state indicates that the position contains effect data.

### Position with Effects

If a position contains a note and/or instrument together with effect events, the position is displayed using the `bright` visual mode.

For example:

    C-5  01

The Pattern View does not indicate which effects are present. The bright state only indicates that additional event data exists at that position.

## Effect Detail Area

The detailed information about effect events is displayed in a dedicated area on the right-hand side of the Pattern View.

Only the effect events belonging to the currently selected or active pattern position are displayed.

Conceptually:

    Pattern View                         Effect Details
    ---------------------------------------------------
    C-5  01                              ...
    D-5  01                              ...
    --- --                              ...
    E-5  01                              ...

The exact layout and formatting of the effect details will be defined by the UI specification.

The effect detail area is context-sensitive and changes when the current pattern position changes.

## Playback

During playback, the current pattern position changes continuously.

The effect detail area therefore follows the playback position and displays the effect events belonging to the currently playing position.

This allows the Pattern View to remain compact while still providing detailed information about the currently active position.

The effect detail area is considered part of the Pattern View interaction model and is not a separate effect editor.

## Effect Layers

BroTracker will initially support three effect layers per pattern position.

The layered structure is inspired by the approach used by the Dirtywave M8.

Conceptually:

    Layer 1
    Layer 2
    Layer 3

Each layer provides one independent effect event slot.

The exact set of supported effects and the rules governing their use will be defined separately.

## Layer 1 and Compatibility

Layer 1 has a special compatibility role for imported module formats.

For imported Amiga and other module formats, Layer 1 is reserved for volume-related events where required by the source format.

This provides a predictable mapping for formats in which volume commands occupy a dedicated volume field or equivalent structure.

This restriction applies to the import mapping convention, not to the native BroTracker effect system.

Native BroTracker pattern data may use Layer 1 for any supported effect event.

In other words:

    Native BroTracker data
              │
              └── Layer 1 → any supported effect

    Imported module data
              │
              └── Layer 1 → mapped volume event(s), where required

## Common Effect Semantics and Engine Mapping

Effect events should be defined by their semantic meaning rather than by the playback technology used to realize them.

The same BroTracker effect event may therefore be handled differently by different playback engines while retaining the same logical meaning.

Conceptually:

                    Effect Event
                         │
             ┌───────────┴───────────┐
             │                       │
       Sample Engine             MIDI Engine
             │                       │
       native mechanism        MIDI equivalent

For example:

    Set Volume
         │
         ├── Sample Engine → set the volume of the sample voice
         │
         └── MIDI Engine   → use the appropriate MIDI volume control

Another example may be:

    Pitch Slide
         │
         ├── Sample Engine → change playback pitch over time
         │
         └── MIDI Engine   → use MIDI Pitch Bend or another appropriate mechanism

An effect that has no meaningful MIDI equivalent may be implemented by the Sample Engine while being ignored or otherwise treated as unsupported by the MIDI Engine.

    Sample Reverse
         │
         ├── Sample Engine → reverse playback
         │
         └── MIDI Engine   → no direct equivalent

This model avoids maintaining completely separate sets of sample effects and MIDI effects. Each playback engine instead maps common BroTracker effect semantics to the mechanisms available to it.

The exact mapping for individual effects will be defined as each effect is researched and specified.

**Please keep in mind that the MIDI part with a separate HW layer in particular might have different representations—especially for volume and potentially some events for the HW section—considering that it is a logically and audibly completely separate representation.**

## Event Representation

Effect events are logical playback events and must not depend on their visual representation.

The internal event representation must allow effect events to exist independently from note and instrument data at the same pattern position.

The visual `bright` state is only a UI indication that additional event data exists.

The exact internal data structure and encoding will be defined during implementation of the pattern and playback engine.

## Supported Effects

The supported effect event types will be added to this document as they are researched and agreed upon.

Each effect type should eventually define:

- event name
- purpose
- parameter range
- parameter representation
- playback behavior
- interaction with an active note
- interaction with other effect layers
- mapping to supported playback engines, where applicable
- import mapping, where applicable

### Volume Events

The initial Volume event group is expected to include both absolute and relative volume operations:

- **Set Volume** — sets the current volume to an explicit value.
- **Volume Slide Up** — increases the current volume by a specified amount.
- **Volume Slide Down** — decreases the current volume by a specified amount.

These entries are currently a working draft and may be renamed, changed, expanded, or removed as the supported tracker formats and BroTracker playback model are researched.

For imported module formats, volume-related events are mapped to Effect Layer 1 where applicable.

The exact parameter ranges and the interaction between sample/instrument volume, event volume, channel volume, and MIDI volume control remain subject to further research.

This section will evolve as the BroTracker effect system is defined.

## Candidate Effect Set

BroTracker will consider support for the effect semantics found in ProTracker and compatible Amiga tracker formats.

The ProTracker command set is used as a reference for:

- module import compatibility
- identifying commonly used tracker effect semantics
- defining the minimum capabilities expected from the BroTracker effect system

The original ProTracker command letters and numbers are not currently part of the BroTracker event format.

BroTracker may use different event names, codes, or visual representations.

For example:

    ProTracker command
        B01
            │
            ▼
    BroTracker event
        HOP 01

or:

    ProTracker command
        ED3
            │
            ▼
    BroTracker event
        DEL 03

The internal event model should preserve the semantic meaning of the source command while allowing BroTracker to use terminology and notation that are more suitable for its own user interface.

## ProTracker Reference Events

The following commands are currently considered candidates for MOD import support and for comparison when defining native BroTracker events.

### Standard Commands

| ProTracker | Meaning | Candidate BroTracker semantic |
|------------|---------|--------------------------------|
| `0xy` | Arpeggio | Arpeggio |
| `1xx` | Portamento Up | Pitch Slide Up |
| `2xx` | Portamento Down | Pitch Slide Down |
| `3xx` | Tone Portamento | Tone Portamento |
| `4xy` | Vibrato | Vibrato |
| `5xy` | Tone Portamento + Volume Slide | Tone Portamento + Volume Slide |
| `6xy` | Vibrato + Volume Slide | Vibrato + Volume Slide |
| `7xy` | Tremolo | Tremolo |
| `9xx` | Sample Offset | Sample Offset |
| `Axy` | Volume Slide | Volume Slide |
| `Bxx` | Position Jump | Pattern/Position Jump |
| `Cxx` | Set Volume | Set Volume |
| `Dxx` | Pattern Break | Pattern Break |
| `Fxx` | Set Speed / BPM | Speed / Tempo |

### Extended Commands (`E`)

| ProTracker | Meaning | Candidate BroTracker semantic |
|------------|---------|--------------------------------|
| `E0x` | Set Filter | Filter |
| `E1x` | Fine Slide Up | Fine Pitch Slide Up |
| `E2x` | Fine Slide Down | Fine Pitch Slide Down |
| `E3x` | Glissando Control | Glissando |
| `E4x` | Vibrato Waveform | Vibrato Waveform |
| `E5x` | Set Fine Tune | Fine Tune |
| `E6x` | Pattern Loop | Pattern Loop |
| `E7x` | Tremolo Waveform | Tremolo Waveform |
| `E9x` | Retrigger Note | Note Retrigger |
| `EAx` | Fine Volume Slide Up | Fine Volume Slide Up |
| `EBx` | Fine Volume Slide Down | Fine Volume Slide Down |
| `ECx` | Note Cut | Note Cut |
| `EDx` | Note Delay | Note Delay |
| `EEx` | Pattern Delay | Pattern Delay |
| `EFx` | Invert Loop | Loop Invert |

This list is a reference and is not yet a commitment to support every command.

## MOD Import Compatibility

When importing MOD and related Amiga tracker formats, BroTracker will consider mapping the source effects represented by the ProTracker command set into its own effect model.

The importer should preserve the musical intent of a supported command whenever practical.

The source command code does not need to be preserved as the BroTracker representation.

Conceptually:

    External module
          │
          ▼
    ProTracker-style command
          │
          ▼
    BroTracker semantic event
          │
       ┌──┴──┐
       ▼     ▼
    Sample  MIDI
     Engine Engine

This allows BroTracker to remain compatible with important tracker concepts without forcing its native event notation to reproduce the historical MOD command layout.

## Native Event Naming

BroTracker event names may use descriptive names instead of traditional tracker command letters.

Examples:

    DEL 03     → Note Delay
    HOP 01     → Position Jump
    VOL 20     → Set Volume
    PUP 04     → Pitch Slide Up

The exact names, abbreviations, parameter formats, and display notation will be defined during UX and event-system research.

The naming should prioritize readability and intuitive editing while preserving compatibility with the semantics of established tracker effects.

## MIDI Compatibility

Each supported BroTracker event should be evaluated for compatibility with the MIDI playback engine.

Where a meaningful MIDI equivalent exists, the MIDI engine should implement the same logical event semantics using the appropriate MIDI mechanism.

Where no meaningful equivalent exists, the event may remain available to the Sample Engine without requiring an artificial MIDI implementation.

Examples:

    Set Volume
         │
         ├── Sample Engine → voice volume
         └── MIDI Engine   → MIDI volume control

    Pitch Slide
         │
         ├── Sample Engine → playback pitch change
         └── MIDI Engine   → Pitch Bend

    Sample Offset
         │
         ├── Sample Engine → start playback at offset
         └── MIDI Engine   → no direct equivalent

The exact MIDI mapping is part of the individual event specification and should not be assumed from the source tracker command alone.

## Future Refinement

This section is intentionally a working list.

Individual events may be:

- added
- removed
- renamed
- merged
- split into multiple semantic events
- assigned to different effect layers
- given different native BroTracker parameters

The ProTracker command set is therefore a compatibility and research reference, not a fixed BroTracker command specification.

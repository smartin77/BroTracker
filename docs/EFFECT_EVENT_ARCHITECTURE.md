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

- Native BroTracker data: Layer 1 may contain any supported effect event.
- Imported module data: Layer 1 is used for mapped volume events where required by the source format.

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
- import mapping, where applicable

This section will evolve as the BroTracker effect system is defined.
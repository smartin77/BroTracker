# BroTracker UI/UX Architecture

## Display Resolution

BroTracker's primary UI target is a **640 × 480 pixel** display.

The UI is designed as a raster-based interface with a fixed pixel grid. Rendering must remain pixel-accurate and must not rely on anti-aliasing or sub-pixel positioning.

The physical display bezel provides the outer visual boundary. BroTracker therefore does **not** draw a full-screen outer border and uses the complete 640 × 480 pixel area.

All UI geometry should use configurable layout parameters rather than hard-coded coordinates wherever practical. This allows the layout to be tuned without redesigning the renderer.

## Hardware / Display Architecture

BroTracker is designed as a headless tracker from the beginning.

The core/brain target is the **Teensy 4.1**. It is responsible for the realtime tracker engine, sequencing, audio processing, MIDI processing, synchronization, storage and other core functions.

The graphical/text-based UI is **not part of the Teensy core engine**.

The display and user-control device is an **ArkOS handheld gaming console**, such as an **RGV or R36S/H**, with a minimum target resolution of 640 × 480 pixels.

A dedicated ArkOS application will be responsible for presenting the BroTracker UI and handling display-side interaction. Its architecture is expected to be similar in principle to M8C, with communication between the console application and the Teensy through an appropriate transport protocol.

The separation between the realtime core and the display application is intentional. The Teensy core must remain independent of the graphical representation of the tracker state.

The highest priorities of the Teensy core are:

- accurate timing;
- reliable synchronization;
- audio processing;
- MIDI processing and timing;
- deterministic sequencing;
- storage and data handling.

The UI application is responsible for translating the core state into the BroTracker visual interface.

BroTracker does not depend on a GPU, HDMI output, or a desktop graphical environment.

## Visual Language

BroTracker is primarily a **text-based user interface**.

The visual appearance should be based on the BroTracker bitmap font and its fixed pixel grid. The current target font uses an **8 × 6 pixel character cell**.

The apparent graphical elements of the interface are intentionally limited.

They consist primarily of:

- text;
- frames and borders;
- separator lines;
- special bitmap glyphs;
- dedicated markers;
- margins and padding between sections;
- spacing between text groups and functional areas.

The interface should not evolve into a conventional graphical UI built from large graphical widgets, icons, gradients or decorative elements.

The basic visual structure remains text-based.

Frames, special glyphs and spacing are used to guide the user's eye, establish hierarchy and separate functional areas while keeping the tracker visually compact and raster-efficient.

This principle should be considered when extending the UI in the future: **new UI elements should primarily be expressed through text, pixel-accurate lines, glyphs, margins and padding rather than conventional graphical widgets.**

## UI Rendering Philosophy

BroTracker uses a pseudo-text-mode visual language:

- text is rendered using the BroTracker bitmap font;
- lines and frames are rasterised directly onto the pixel grid;
- no anti-aliasing is used;
- repeated pattern content should remain static whenever possible;
- during playback, only the elements that actually change need to be redrawn;
- static frames and other unchanged UI elements should not be unnecessarily regenerated.

The BroTracker bitmap font is part of the project assets and is rendered at native pixel resolution.

## Main Pattern Screen

The main pattern screen is the primary working screen of BroTracker.

It should provide as much useful information as possible without requiring the user to enter several nested screens for common operations.

The main pattern area contains:

- row numbers;
- the current row-index display mode (`d0`, `d1`, `h0`, `h1`);
- channel numbers;
- up to eight visible channels;
- note and instrument information;
- channel separator lines;
- a current row/playhead marker;
- pattern and tune information;
- quick access to important tracker functions.

The right side of the main screen is reserved for contextual information and controls. This area may show information related to the currently selected channel/event, including note, instrument, volume, panning and up to three effects.

The exact right-side layout will continue to evolve during UI prototyping.

## Row Numbering

The default row numbering mode is **d1**.

Supported display modes are:

- `d0` — decimal, starting at 00;
- `d1` — decimal, starting at 01;
- `h0` — hexadecimal, starting at 00;
- `h1` — hexadecimal, starting at 01.

The numbering mode is a display option and does not change the internal row indexing.

The current numbering mode should be identifiable directly from the main pattern screen.

The default `d1` display uses two screens for a 64-row pattern:

```text
01 ... 32
33 ... 64
```

Pattern lengths of 16, 32 and 64 rows, and the exact relationship between pattern length and screen/page behaviour, remain subject to further investigation and development.

## Pattern Row Geometry

The current target is **32 visible rows** on the main pattern screen.

The initial row height target is **13 pixels**.

The renderer should use named layout parameters for values such as:

- first row Y position;
- row height;
- row-number column width;
- channel width;
- channel separator spacing;
- header height;
- top/bottom pattern padding;
- left/right pattern padding;
- marker padding.

The top and bottom pattern padding should be kept as small as practical so that 32 rows fit while preserving readable glyph spacing.

The current geometry should be tested with 32 rows before reducing the row height.

## d0 / Row-Mode Marker

The row-mode label (`d0`, `d1`, `h0`, `h1`) occupies the left side of the pattern header.

Its left padding is aligned with the row-number column below it. It is vertically aligned with the row-number column rather than with an individual channel cell.

The horizontal separator below the channel header is intentionally shortened by one channel-cell width at both ends. It begins approximately one pixel to the left of channel 1 and ends approximately one pixel to the right of channel 8.

The row-mode area has its own turquoise frame.

The frame is vertically aligned with the beginning of channel 1 and its bottom edge aligns with the lower edge of the channel-header frame.

The exact frame colour remains configurable.

## Pattern and Section Frames

The main pattern header is framed.

The left section of the pattern area is also framed, including the row-number area and the row-mode area.

The outer physical display boundary is not drawn by BroTracker.

A nominal **3-pixel spacing** is maintained between adjacent frame elements where the layout calls for separated lines.

Channel separator lines are intentionally lightweight and must not visually dominate the pattern data.

## Channel Layout

The initial main pattern screen displays **eight channels**.

Each channel contains at minimum:

```text
NOTE INSTRUMENT
```

For example:

```text
C-4 01
D#5 01
--- --
```

The channel columns are separated by vertical divider lines.

The current design uses fixed-width character cells so that columns remain aligned even though the bitmap glyphs themselves may have different visual widths.

## Note Notation

BroTracker supports both International and German note naming conventions.

International notation uses:

```text
C D E F G A B
```

German notation uses:

```text
C D E F G A H
```

Sharp and flat display of semitones is supported.

Sharp notation uses:

```text
C#5
```

Flat notation uses the dedicated BroTracker bitmap glyph represented in source/UI text as `þ`:

```text
Db5
```

The actual rendered glyph is supplied by the BroTracker font.

The program's normal UI text uses basic Latin uppercase and lowercase characters.

## Note Numbering and Octave Convention

BroTracker uses a MIDI-compatible internal note number from 0 to 127.

The internal note number is independent of the textual octave naming convention.

The BroTracker default display convention follows the Yamaha-style octave numbering:

```text
MIDI 0    = C-2
MIDI 12   = C-1
MIDI 24   = C0
MIDI 36   = C1
MIDI 48   = C2
MIDI 60   = C3
MIDI 72   = C4
MIDI 84   = C5
MIDI 96   = C6
MIDI 108  = C7
MIDI 120  = C8
```

Therefore:
C3 = MIDI note 60

This convention is a BroTracker display convention and does not alter the MIDI note value transmitted to external devices.

MIDI note 60 remains MIDI note 60 regardless of how an external device or application labels the octave.

The purpose of this convention is to provide a consistent internal and user-facing representation without depending on the differing octave conventions used by individual DAWs, trackers or hardware manufacturers.

Future compatibility options may provide alternative display conventions such as Roland or FL Studio numbering without changing the underlying note number.

## Empty Pattern Events

A completely empty event is displayed as:

```text
--- --
```

An empty note/instrument position that contains at least one effect is visually distinguished from a completely empty event.

The intended visual distinction is:

```text
--- --   normal empty event
--- --   bright empty event containing an effect
```

The final effect data model is not yet implemented. UI dummy data may therefore be used to demonstrate the intended appearance before the effect data model is implemented.

## LPB and Beat Highlighting

**LPB (Lines Per Beat)** defines how many pattern rows correspond to one musical beat.

For example, with:

```text
LPB = 4
```

one beat occupies four pattern rows:

```text
01  C-4 01   <- beat start
02  --- --
03  --- --
04  --- --
05  C-4 01   <- next beat start
```

Beat-start rows are displayed using the **bright** colour variant.

Therefore, with `LPB = 4` and `d1`, the bright beat positions are:

```text
01, 05, 09, 13, 17, 21, 25, 29...
```

This visual convention makes the musical grid immediately visible and supports conventional sequencer-style editing.

The bright row treatment is a visual aid only and does not alter the underlying pattern data.

## Playback and Edit Modes

BroTracker distinguishes between **playback mode** and **edit mode**.

### Playback Mode

During playback:

- the pattern data remains visually static;
- the playback marker moves through the visible rows;
- only changing UI elements need to be redrawn;
- the right-side contextual information follows the playback position and selected channel;
- an oscilloscope area may be displayed;
- the oscilloscope is a playback-only visual element.

The playback marker may be rendered as a horizontal frame/highlight spanning the visible channels.

### Edit Mode

When playback is not active, the main screen automatically behaves as the pattern editing screen.

The edit position is kept at the centre of the visible pattern area whenever practical.

Instead of moving the cursor through the screen, the pattern scrolls by complete rows as the edit position moves.

This creates a stable visual focus:

```text
pattern moves
cursor remains centred
```

The exact navigation and input behaviour will be defined separately.

## Current Position Highlighting

The current playback row may use a horizontal frame/highlight across all visible channels.

In edit mode, the currently edited channel/position may use a stronger visual treatment, potentially including inverse rendering.

The design intentionally avoids excessive simultaneous frames. A full-width row marker and a stronger local edit highlight should remain visually distinguishable.

The filled position marker glyph `¦` is reserved for future navigation/channel-position use and is not currently defined as the LPB beat indicator.

The glyph `¨` is reserved for a possible future channel navigation indicator.

## Oscilloscope

The oscilloscope is part of the playback visualisation and is not displayed during normal pattern editing.

The available pattern area may therefore be divided between:

- pattern rows;
- oscilloscope information.

The exact number of visible pattern rows versus per-channel oscilloscope views remains under investigation.

Possible layouts include keeping most rows visible and using a smaller oscilloscope region, or showing fewer rows and providing per-channel oscilloscope information.

No final oscilloscope layout is defined here yet.

## Contextual Right-Side Panel

The right side of the main screen is intended to reduce screen diving by providing contextual information for the selected or active event.

The panel may include:

- note;
- instrument;
- volume;
- panning;
- effect 1;
- effect 2;
- effect 3.

In edit mode, the panel follows the currently edited position.

During playback, it may follow the current playback row while allowing the user to change the selected channel.

The exact controls and presentation remain subject to UI prototyping.

## Quick Access

The main screen should provide direct access to frequently used areas without unnecessary navigation depth.

The primary quick-access menu is ordered as:

1. **OPT** — Options
2. **INS** — Instrument Editor
3. **MIX** — Mixer

This order reflects the intended workflow of the tracker.

### OPT — Options

Options are divided into two logical areas:

- **Program / Interface Options**
- **Project Options**

Program / Interface Options contain tracker-wide settings such as:

- row numbering mode (`d0`, `d1`, `h0`, `h1`);
- note notation and display conventions;
- interface settings;
- other tracker-wide configuration.

Project Options contain project-specific operations and settings, including:

- Create;
- Save;
- Load;
- project main settings;
- project description.

### INS — Instrument Editor

Instrument editing is intentionally a first-class function.

The user should be able to create or select an instrument before composing a pattern rather than being forced through a chain/phrase hierarchy first.

The Instrument Editor will eventually support different instrument types, including:

- sample-based instruments;
- MIDI instruments/devices;
- internal synthesizers.

Further instrument-specific editing may be nested below the Instrument Editor.

### MIX — Mixer

The Mixer area is intended to provide master/channel mixing controls, including at minimum channel levels and related master mix information.

Export to Windows PCM WAV primarily, later maybe to other other audio (compression formats).

The exact mixer feature set remains under development.

## CPU Load

CPU load should be available as a compact percentage indicator on the main UI where space permits.

This is intended as useful development and performance feedback without becoming a dominant UI element.

## Position and Navigation Glyphs

The BroTracker bitmap font already contains dedicated glyphs for position and navigation.

Current glyph assignments are:

- `¦` — filled position marker;
- `¨` — channel navigation marker.

The glyphs are already part of the BroTracker font and are available to the UI renderer.

Their exact interaction semantics and navigation behaviour may still evolve during UI development.

## Colour System

The UI uses a limited colour palette inspired by classic constrained computer graphics and the Sinclair ZX Spectrum approach of using normal and bright colour levels.

The initial BroTracker colour scheme uses:

- turquoise;
- purple;
- white.

Each colour has a normal and bright variant.

The current visual prototype treats full white as the bright white level.

The existing purple is used as the normal purple level. If required, a lighter purple variant will be used for bright purple.

The final colour values remain configurable during visual prototyping.

A second colour scheme is planned using:

- teal;
- magenta;
- white.

It will also provide normal and bright variants.

Normal and bright colours must remain clearly distinguishable when used for:

- normal pattern information;
- beat positions;
- empty events;
- effect-bearing empty events;
- highlights and inverse selections.

## Performance and Redrawing

The UI is designed around the assumption that the Teensy 4.1 is the tracker core and that the display is a remote/headless console display.

The design therefore favours:

- static layout elements;
- minimal redraw regions;
- text-only updates where possible;
- redraw of only the moving playback marker during playback;
- redraw of only the affected rows during edit scrolling;
- no unnecessary full-screen redraws.

The UI must remain practical for the available hardware and communication path even though the Teensy 4.1 provides substantially more CPU performance than many historical tracker platforms.

Performance should be preserved where it can be preserved without compromising usability.

## Layout Configuration

UI geometry must be defined through a small set of central layout parameters.

The renderer should not require individual drawing functions to contain duplicated absolute coordinates for the same layout dimensions.

Typical configurable values include:

```text
SCREEN_WIDTH
SCREEN_HEIGHT

PATTERN_HEADER_HEIGHT
PATTERN_TOP_PADDING
PATTERN_BOTTOM_PADDING

ROW_HEIGHT
VISIBLE_ROWS

ROW_NUMBER_WIDTH
ROW_NUMBER_PADDING

CHANNEL_WIDTH
CHANNEL_GAP
CHANNEL_COUNT

MARKER_PADDING
SECTION_PADDING
FRAME_GAP
```

Changing one of these values should allow the visual layout to be retuned without rewriting the rendering logic.

## Design Principle

BroTracker is intended to be a professional-feeling tracker that remains free and community-driven.

The UI is therefore treated as a core part of the instrument rather than as a secondary presentation layer.

The guiding principle is:

> The interface should make the user's musical task easier, not make the user adapt to the interface.

UI/UX decisions should favour direct access, stable visual focus, predictable navigation and high information density without sacrificing readability.

The primary visual language should remain text-based, using the BroTracker 8 × 6 bitmap font as its foundation. Apparent graphical complexity should be achieved through carefully designed frames, special glyphs, margins, padding and spacing rather than through conventional graphical widgets.

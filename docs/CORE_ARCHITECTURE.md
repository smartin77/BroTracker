# Core Architecture

                    +----------------------+
                    |     ArkOS UI         |
                    |----------------------|
                    | Pattern Editor       |
                    | Tune / Song Editor   |
                    | Instrument Editor    |
                    | Project Manager      |
                    | Mixer                |
                    | Options              |
                    +----------+-----------+
                              |
                    USB / Serial Protocol
                              |
===============================================================
              Teensy 4.1 Realtime Core
===============================================================

## Core / UI Separation

BroTracker is headless by design.

The **Teensy 4.1 Realtime Core** does not implement the graphical or text-based presentation of the tracker.

The core is responsible for producing deterministic tracker state and realtime events, including:

- pattern and song state;
- scheduler state;
- playback position;
- instrument state;
- audio state;
- MIDI state;
- synchronization state;
- project and storage state.

The **ArkOS UI application** is responsible for presenting this state to the user.

The UI application handles:

- text rendering;
- frames and separators;
- bitmap glyphs;
- layout;
- colours;
- pattern highlighting;
- playback markers;
- editing cursor representation;
- oscilloscope visualization;
- user interaction.

The communication protocol between the ArkOS application and the Teensy core must therefore transport tracker state and commands rather than pre-rendered graphical output.

This separation allows the realtime engine to remain independent of display resolution, font rendering and UI implementation.

## Realtime Priorities

The primary responsibility of the Teensy core is reliable realtime operation.

The highest-priority areas are:

1. accurate timing;
2. deterministic scheduling;
3. synchronization;
4. audio processing;
5. MIDI processing and timing;
6. pattern/song playback;
7. storage and non-realtime data operations.

UI communication must not compromise realtime timing.

The display application may be delayed, disconnected or restarted without interrupting the realtime engine.

The core must therefore remain functional independently of the UI application.


                 +--------------------------+
                 |      Project Manager     |
                 +------------+-------------+
                              |
                loads project / samples
                              |
                 +------------v-------------+
                 |      Song Engine         |
                 +------------+-------------+
                              |
                     current pattern
                              |
                 +------------v-------------+
                 |        Scheduler         |
                 +------------+-------------+
                              |
          +-------------------+--------------------+
          |                   |                    |
          |                   |                    |
 +--------v-------+  +--------v-------+  +---------v--------+
 | Sample Engine  |  |   MIDI Engine  |  | Clock Generator  |
 +--------+-------+  +--------+-------+  +---------+--------+
          |                   |                    |
          |                   |                    |
     Audio Output        MIDI OUT / USB      MIDI Clock / Sync

===============================================================

               Storage Layer (SD Card)

     Projects
     Samples
     Future cache
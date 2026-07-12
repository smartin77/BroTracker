                        +----------------------+
                        |      Linux UI        |
                        |----------------------|
                        | Pattern Editor       |
                        | Song Editor          |
                        | Instrument Editor    |
                        | Region Editor        |
                        | Project Manager      |
                        +----------+-----------+
                                   |
                          USB / Serial Protocol
                                   |
===============================================================
                 Teensy 4.1 Realtime Core
===============================================================

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
/*
 * BroTracker
 *
 * Description: Dummy tune data for development and UI testing.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 * 
 * This file can be removed!!!
 * 
 */

#include "dummy_data.h"

Tune CreateDummyTune()
{
    Tune tune;

    tune.title = "My epic tune";
    tune.tempo = 140;

    Pattern pattern;
    pattern.number = 5;
    pattern.length = 64;

    // Channel 1 - Kick, every fourth row.
    Channel kick;
    kick.rows.resize(64);

    kick.rows[0].note = 60;
    kick.rows[0].instrument = 1;

    kick.rows[4].note = 60;
    kick.rows[4].instrument = 1;

    kick.rows[8].note = 60;
    kick.rows[8].instrument = 1;

    kick.rows[12].note = 60;
    kick.rows[12].instrument = 1;

    // Channel 2 - Closed hat, every second row.
    Channel closed_hat;
    closed_hat.rows.resize(64);

    for (int row = 0; row < 16; row += 2)
    {
        closed_hat.rows[row].note = 60;
        closed_hat.rows[row].instrument = 2;
    }

    // Channel 3 - Snare, every eighth row.
    Channel snare;
    snare.rows.resize(16);

    snare.rows[4].note = 60;
    snare.rows[4].instrument = 3;

    snare.rows[12].note = 60;
    snare.rows[12].instrument = 3;

    // Channel 4 - Bass, every fourth row with offset 2.
    Channel bass;
    bass.rows.resize(16);

    bass.rows[2].note = 51;
    bass.rows[2].instrument = 4;

    bass.rows[6].note = 51;
    bass.rows[6].instrument = 4;

    bass.rows[10].note = 51;
    bass.rows[10].instrument = 4;

    bass.rows[14].note = 51;
    bass.rows[14].instrument = 4;

    // Channel 5 - Piano melody.
    Channel piano;
    piano.rows.resize(16);

    piano.rows[0].note = 72;
    piano.rows[0].instrument = 5;

    piano.rows[2].note = 74;
    piano.rows[2].instrument = 5;

    piano.rows[4].note = 76;
    piano.rows[4].instrument = 5;

    piano.rows[6].note = 79;
    piano.rows[6].instrument = 5;

    piano.rows[8].note = 76;
    piano.rows[8].instrument = 5;

    piano.rows[10].note = 74;
    piano.rows[10].instrument = 5;

    piano.rows[12].note = 72;
    piano.rows[12].instrument = 5;

    piano.rows[14].note = 67;
    piano.rows[14].instrument = 5;

    pattern.channels.push_back(kick);
    pattern.channels.push_back(closed_hat);
    pattern.channels.push_back(snare);
    pattern.channels.push_back(bass);
    pattern.channels.push_back(piano);

    tune.patterns.push_back(pattern);

    return tune;
}
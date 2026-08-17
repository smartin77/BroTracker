/*
 * BroTracker
 *
 * Description: Unit tests for the JSON tune loader.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include "test_framework.h"

#include <exception>

#include "core/note.h"
#include "core/tune_loader.h"

TEST_CASE(LoadTuneFromJson_ParsesDummyTune)
{
    // Assumes ctest's WORKING_DIRECTORY is the repository root.
    const Tune tune = LoadTuneFromJson("assets/dummy_my_tune.json");

    CHECK_EQ(tune.title, std::string("my_tune"));
    CHECK_EQ(tune.tempo, 140);
    CHECK(!tune.patterns.empty());

    const Pattern& pattern = tune.patterns.front();

    CHECK_EQ(pattern.number, 5);
    CHECK_EQ(pattern.length, 64);
    CHECK(!pattern.channels.empty());

    const Channel& kick = pattern.channels.front();

    CHECK(kick.rows.size() >= 2);

    // "C-5" parses to MIDI 84 under the Yamaha convention (D0018).
    CHECK_EQ(kick.rows[0].note, 84);
    CHECK_EQ(kick.rows[0].instrument, 1);
    CHECK_EQ(kick.rows[1].note, NOTE_EMPTY);
}

TEST_CASE(LoadTuneFromJson_ThrowsOnMissingFile)
{
    bool threw = false;

    try
    {
        LoadTuneFromJson("assets/does_not_exist.json");
    }
    catch (const std::exception&)
    {
        threw = true;
    }

    CHECK(threw);
}

/*
 * BroTracker
 *
 * Description: Unit tests for the JSON tune loader.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include "test_framework.h"
#include "temporary_directory.h"

#include <exception>
#include <fstream>
#include <filesystem>
#include <utility>

#include "core/note.h"
#include "core/tune_loader.h"

TEST_CASE(LoadTuneFromJson_ParsesDummyTune)
{
    // Assumes ctest's WORKING_DIRECTORY is the repository root.
    const Tune tune = LoadTuneFromJson("assets/dummy_my_tune.json");

    CHECK_EQ(tune.title, std::string("my_tune"));
    CHECK_EQ(tune.tempo, 140.5);
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
TEST_CASE(LoadTuneFromJson_RejectsUnsupportedPatternTextWithoutRelabeling)
{
    TemporaryTestDirectory directory;
    const auto path = directory.Path() / "pattern.json";
    const auto load = [&](const char* note) {
        {
            std::ofstream file(path);
            if(!file) throw std::runtime_error("Unable to create temporary tune fixture");
            file << "{\"tune\":{\"pattern\":[{\"channel\":[{\"row\":[{\"note\":\""
                << note << "\"}]}]}]}}";
        }
        return LoadTuneFromJson(path.string());
    };
    for (const auto& entry : {std::pair<const char*, Note>{"C-0", 24}, {"C-3", 60}, {"G-8", 127},
        {"OFF", NOTE_OFF}, {"", NOTE_EMPTY}}) {
        CHECK_EQ(load(entry.first).patterns[0].channels[0].rows[0].note, entry.second);
    }
    for (const auto* text : {"C--1", "C--2", "G#8", "C-9", "C?0"}) {
        bool rejected = false;
        try { (void)load(text); } catch (const std::exception&) { rejected = true; }
        CHECK(rejected);
    }
    std::filesystem::remove(path);
}

TEST_CASE(TemporaryFixturesAreDistinctAndCleanedDuringException) {
    std::filesystem::path removed;
    try {
        TemporaryTestDirectory first,second;
        CHECK(first.Path()!=second.Path()); removed=first.Path();
        std::ofstream file(first.Path()/"partial.json"); file<<"partial"; file.close();
        throw std::runtime_error("fixture cleanup probe");
    } catch(const std::runtime_error&) {}
    CHECK(!removed.empty()); CHECK(!std::filesystem::exists(removed));
}

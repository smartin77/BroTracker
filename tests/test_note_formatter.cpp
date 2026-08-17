/*
 * BroTracker
 *
 * Description: Unit tests for FormatNote.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include "test_framework.h"

#include "core/note_formatter.h"

TEST_CASE(FormatNote_EmptyReturnsDashes)
{
    CHECK_EQ(
        FormatNote(NOTE_EMPTY, AccidentalMode::Sharp),
        std::string("---"));
}

TEST_CASE(FormatNote_OffReturnsOff)
{
    CHECK_EQ(
        FormatNote(NOTE_OFF, AccidentalMode::Sharp),
        std::string("OFF"));
}

TEST_CASE(FormatNote_UsesYamahaOctaveConvention)
{
    // Yamaha convention (D0018): MIDI 60 = C3.
    CHECK_EQ(FormatNote(60, AccidentalMode::Sharp), std::string("C-3"));
    CHECK_EQ(FormatNote(0, AccidentalMode::Sharp), std::string("C--2"));
    CHECK_EQ(FormatNote(120, AccidentalMode::Sharp), std::string("C-8"));
}

TEST_CASE(FormatNote_SharpVersusFlatNaming)
{
    CHECK_EQ(FormatNote(61, AccidentalMode::Sharp), std::string("C#3"));
    CHECK_EQ(FormatNote(61, AccidentalMode::Flat), std::string("Db3"));
}

TEST_CASE(FormatNote_RoundTripsWithDummyTuneJsonNoteText)
{
    // "C-5" in assets/dummy_my_tune.json parses to MIDI 84; see
    // LoadTuneFromJson_ParsesDummyTune for the matching parser assertion.
    CHECK_EQ(FormatNote(84, AccidentalMode::Sharp), std::string("C-5"));
}

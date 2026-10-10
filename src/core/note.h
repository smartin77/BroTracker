/*
 * BroTracker
 *
 * Description: Note definitions.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#pragma once

#include <cstdint>

using Note = std::uint8_t;

constexpr Note NOTE_EMPTY = 0xFF;
constexpr Note NOTE_OFF = 0xFE;

// D0018: pitched pattern notes use Yamaha C-0..G-8 without transposition.
// These checks are for pattern consumers, not generic MIDI transport.
constexpr Note kPatternNoteMinimum = 24;
constexpr Note kPatternNoteMaximum = 127;
constexpr bool IsPitchedPatternNote(unsigned note) noexcept
{ return note >= kPatternNoteMinimum && note <= kPatternNoteMaximum; }
constexpr bool IsPatternNote(unsigned note) noexcept
{ return IsPitchedPatternNote(note) || note == NOTE_EMPTY || note == NOTE_OFF; }

enum class AccidentalMode : std::uint8_t
{
    Sharp,
    Flat
};

enum class NoteNamingMode : std::uint8_t
{
    International,
    German
};

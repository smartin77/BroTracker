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
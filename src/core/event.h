/*
 * BroTracker
 *
 * Description: Pattern event.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#pragma once

#include <cstdint>

#include "note.h"

struct Event
{
    Note note = NOTE_EMPTY;

    std::uint8_t instrument = 0xFF;
};
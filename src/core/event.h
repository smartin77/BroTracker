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
#include <string>

struct Event
{
    std::string note = "---";

    std::uint8_t instrument = 0;
};
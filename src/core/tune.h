/*
 * BroTracker
 *
 * Description: Tune data.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#pragma once

#include <string>
#include <vector>

#include "pattern.h"

struct Tune
{
    std::string title = "UNTITLED";

    std::uint16_t tempo = 140;

    std::uint8_t channel_count = 8;

    std::vector<Pattern> patterns;
};
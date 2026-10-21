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
#include "tempo.h"

#include "pattern.h"

struct Tune
{
    std::uint32_t tempo_hundredths = BroTracker::kDefaultTempo;
    std::string title = "UNTITLED";
    std::uint8_t channel_count = 8;
    std::vector<Pattern> patterns;
};
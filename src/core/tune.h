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
    double tempo = 140.0;
    std::string title = "UNTITLED";
    std::uint8_t channel_count = 8;
    std::vector<Pattern> patterns;
};
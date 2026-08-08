/*
 * BroTracker
 *
 * Description: Pattern data.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#pragma once

#include <cstdint>
#include <vector>

#include "channel.h"

struct Pattern
{
    std::uint16_t number = 0;

    std::uint8_t length = 16;

    std::vector<Channel> channels;
};
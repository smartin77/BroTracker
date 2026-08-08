/*
 * BroTracker
 *
 * Description: Channel data.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#pragma once

#include <vector>

#include "event.h"

struct Channel
{
    std::vector<Event> rows;
};
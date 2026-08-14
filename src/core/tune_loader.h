/*
 * BroTracker
 *
 * Description: JSON tune loader for development and UI testing.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#pragma once

#include <string>

#include "tune.h"

Tune LoadTuneFromJson(
    const std::string& path);
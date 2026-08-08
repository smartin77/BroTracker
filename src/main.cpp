/*
 * BroTracker
 *
 * Description: Application entry point.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include <iostream>

#include "core/logger.h"

int main()
{
    LogInfo("BroTracker starting.");

    LogDebug("Debug mode is enabled.");

    LogInfo("Initialization complete.");

    return 0;
}
/*
 * BroTracker
 *
 * Description: Basic application logger implementation.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include "logger.h"

#include <iostream>

bool debug_mode = true;

void LogInfo(const char* message)
{
    std::cout << "[INFO] " << message << '\n';
}

void LogDebug(const char* message)
{
    if (!debug_mode)
    {
        return;
    }

    std::cout << "[DEBUG] " << message << '\n';
}

void LogWarning(const char* message)
{
    std::cout << "[WARNING] " << message << '\n';
}

void LogError(const char* message)
{
    std::cerr << "[ERROR] " << message << '\n';
}
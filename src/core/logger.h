/*
 * BroTracker
 *
 * Description: Basic application logger.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#pragma once

extern bool debug_mode;

void LogInfo(const char* message);
void LogDebug(const char* message);
void LogWarning(const char* message);
void LogError(const char* message);
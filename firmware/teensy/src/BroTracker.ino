/*
 * BroTracker
 *
 * Teensy 4.1 firmware entry point.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include "platform.h"

void setup()
{
    BroTracker::PlatformInit();
    BroTracker::KernelInit();
}

void loop()
{
    BroTracker::KernelRun();
}
#pragma once

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace BroTracker
{
    inline void IndicateInitializationComplete()
    {
#if defined(ARDUINO)
        constexpr unsigned long kFlashDurationMs = 750;
        constexpr unsigned long kPauseDurationMs = 750;

        pinMode(LED_BUILTIN, OUTPUT);
        digitalWrite(LED_BUILTIN, LOW);

        for (int flash = 0; flash < 3; ++flash)
        {
            digitalWrite(LED_BUILTIN, HIGH);
            delay(kFlashDurationMs);
            digitalWrite(LED_BUILTIN, LOW);
            delay(kPauseDurationMs);
        }
#endif
    }
}
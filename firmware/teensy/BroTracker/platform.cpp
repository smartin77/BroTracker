#include "platform.h"

#include "diagnostics.h"

#include <Arduino.h>

namespace BroTracker
{
    void PlatformInit()
    {
        Serial.begin(115200);

        while (!Serial && millis() < 3000)
        {
        }

        pinMode(LED_BUILTIN, OUTPUT);
        digitalWrite(LED_BUILTIN, LOW);
        DiagnosticsInitialize();
    }

    void KernelInit()
    {
        if (!DiagnosticLog("BroTracker initialized"))
            Serial.println("Diagnostic log write: FAIL");

        DiagnosticBlink(3);
    }

    void KernelRun()
    {
        // Kernel main loop
    }
}
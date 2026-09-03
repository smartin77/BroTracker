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
        DiagnosticLog("BroTracker starting");
        DiagnosticLog("Core initialized");
        DiagnosticLog("Scheduler initialized");
        DiagnosticLog("Playback engine initialized");
        DiagnosticLog("Storage initialized");
        DiagnosticLog("MIDI initialized");
        DiagnosticLog("BroTracker ready");

        DiagnosticBlink(3);
    }

    void KernelRun()
    {
        // Kernel main loop
    }
}
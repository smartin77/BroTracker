#pragma once

namespace BroTracker
{
    bool DiagnosticsInitialize();
    bool DiagnosticLog(const char* message);
    void DiagnosticBlink(unsigned int count);
}
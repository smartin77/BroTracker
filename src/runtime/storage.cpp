#include "storage.h"

namespace BroTracker
{
    bool Storage::Initialize()
    {
        // Storage initialization.
        // Teensy firmware logs initialization via DiagnosticsInitialize() and DiagnosticLog().
        // Host builds do not require SD logging here.
        return true;
    }
}
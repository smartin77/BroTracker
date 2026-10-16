#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "ui/host_logging.h"
// Host observation timestamps and filtering; never device physical event time.
inline void WindowsDiagnostic(FILE* log,const char* source,const char* message) {
    BroTracker::HostLog(log,BroTracker::ClassifyHostDiagnostic(source,message),source,message);
}

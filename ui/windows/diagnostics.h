#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
// UTC correlates user actions; monotonic milliseconds measure stage durations.
inline void WindowsDiagnostic(FILE* log, const char* source, const char* message) {
    if (!log) return;
    SYSTEMTIME utc{}; GetSystemTime(&utc);
    std::fprintf(log, "[%04u-%02u-%02uT%02u:%02u:%02u.%03uZ tick=%llu] %s: %s\n",
        utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond,
        utc.wMilliseconds, static_cast<unsigned long long>(GetTickCount64()), source, message);
    std::fflush(log);
}

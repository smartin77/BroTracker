#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <optional>
#include <stdexcept>

struct WindowsAudioFailure : std::runtime_error {
    HRESULT code;
    WindowsAudioFailure(const char* what, HRESULT hr) : std::runtime_error(what), code(hr) {}
};

// An unverifiable capture candidate is never a match, but must not prevent
// discovery of other endpoints. Render identity remains a strict requirement.
template<class Read, class Skipped>
auto TryCaptureContainer(Read read, Skipped skipped) -> std::optional<decltype(read())> {
    try { return read(); }
    catch (const WindowsAudioFailure& error) {
        skipped(error);
        return std::nullopt;
    }
}

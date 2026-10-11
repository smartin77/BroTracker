#pragma once
#include <algorithm>

// The worker owns one bounded write at a time. Accept a prefix rather than
// rejecting a longer protocol line; the shared scheduler retains its suffix.
constexpr unsigned kWindowsSerialWriteCapacity = 32;
inline unsigned WindowsSerialWritePrefix(unsigned count) noexcept {
    return std::min(count, kWindowsSerialWriteCapacity);
}

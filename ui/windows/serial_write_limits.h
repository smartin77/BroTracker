#pragma once
#include <algorithm>
#include "bringup/pattern_snapshot.h"

// The worker owns one bounded write at a time. Accept a prefix rather than
// rejecting a longer protocol line; the shared scheduler retains its suffix.
// A complete bounded protocol line fits one worker request; longer callers
// still retain suffix ownership through the existing partial-write contract.
constexpr unsigned kWindowsSerialWriteCapacity = BroTracker::kPatternSnapshotLineCapacity;
inline unsigned WindowsSerialWritePrefix(unsigned count) noexcept {
    return std::min(count, kWindowsSerialWriteCapacity);
}

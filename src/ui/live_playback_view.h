#pragma once
#include "bringup/pattern_telemetry.h"
#include <optional>

// UI-only snapshot. Freshness never advances or extrapolates musical position.
// Connected legacy devices have no telemetry; stale/pending/error snapshots have
// no valid highlight. Dimensions describe the device, not the host preview tune.
struct LivePlaybackView
{
    bool telemetry_available = false;
    bool fresh = false;
    BroTracker::PatternTelemetry position;
};
constexpr std::uint32_t kLivePositionStaleMs = 500;

struct PatternDisplayState
{
    bool device_mode = false;
    bool playback_highlight = false;
    std::uint32_t row = 0, rows = 0, channels = 0;
};
inline PatternDisplayState ResolvePatternDisplay(
    const std::optional<LivePlaybackView>& live) noexcept
{
    if (!live) return {};
    const auto& p = live->position;
    const bool dimensions = live->telemetry_available && p.rows > 0 &&
        p.rows <= BroTracker::kRealtimePatternRowCapacity && p.channels > 0 &&
        p.channels <= BroTracker::kRealtimePatternChannelCapacity;
    const bool highlight = dimensions && live->fresh &&
        BroTracker::ValidPatternTelemetry(p) && p.running && p.valid;
    return {true, highlight, highlight ? static_cast<std::uint32_t>(p.row) : 0,
        dimensions ? p.rows : 0, dimensions ? p.channels : 0};
}

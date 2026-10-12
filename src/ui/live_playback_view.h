#pragma once
#include "bringup/pattern_telemetry.h"
#include "bringup/pattern_snapshot.h"
#include <optional>
#include "bringup/host_channel_gains.h"
#include <string>

// UI-only snapshot. Freshness never advances or extrapolates musical position.
// Connected legacy devices have no telemetry; stale/pending/error snapshots have
// no valid highlight. Dimensions describe the device, not the host preview tune.
struct LivePlaybackView
{
    bool telemetry_available = false;
    bool fresh = false;
    BroTracker::PatternTelemetry position;
    std::optional<BroTracker::DevicePatternSnapshot> device_pattern = std::nullopt;
    bool pattern_loading = false, pattern_failed = false;
    bool preview_supported = false;
    bool gains_supported = false;
    std::optional<BroTracker::HostGainState> channel_gains;
    std::optional<BroTracker::Pcm16ChannelGains> desired_gains;
    std::string gain_status;
};
constexpr std::uint32_t kLivePositionStaleMs = 500;

struct PatternDisplayState
{
    // device_pattern borrows from the optional view; retain that view while drawing.
    bool device_mode = false;
    bool playback_highlight = false;
    std::uint32_t row = 0, rows = 0, channels = 0;
    const BroTracker::DevicePatternSnapshot* device_pattern = nullptr;
};
inline PatternDisplayState ResolvePatternDisplay(
    const std::optional<LivePlaybackView>& live, bool preview = false) noexcept
{
    if (!live) { PatternDisplayState unavailable; unavailable.device_mode=!preview; return unavailable; }
    const auto& p = live->position;
    const bool dimensions = live->telemetry_available && p.rows > 0 &&
        p.rows <= BroTracker::kRealtimePatternRowCapacity && p.channels > 0 &&
        p.channels <= BroTracker::kRealtimePatternChannelCapacity;
    const bool highlight = dimensions && live->fresh &&
        BroTracker::ValidPatternTelemetry(p) && (p.running || p.paused) && p.valid;
    const auto* snapshot = live->device_pattern ? &*live->device_pattern : nullptr;
    if (!dimensions || !snapshot || snapshot->pattern.active_rows != p.rows ||
        snapshot->pattern.active_channels != p.channels) snapshot = nullptr;
    return {true, highlight, highlight ? static_cast<std::uint32_t>(p.row) : 0,
        dimensions ? p.rows : 0, dimensions ? p.channels : 0, snapshot};
}

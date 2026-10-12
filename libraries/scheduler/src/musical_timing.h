#pragma once

#include <cstdint>

namespace BroTracker
{
    constexpr std::uint32_t kRowsPerQuarterNote = 4;
    constexpr std::uint32_t kTicksPerRow = 96;
    constexpr std::uint32_t kTicksPerQuarterNote = kRowsPerQuarterNote * kTicksPerRow;

    enum class TickToSampleStatus
    {
        Success,
        InvalidConfiguration,
        Overflow
    };

    // Convert an absolute musical tick to an absolute audio sample position:
    // floor(tick_index * sample_rate_hz * 6000 /
    //       (tempo_hundredths * kTicksPerQuarterNote)). Tick zero is sample zero.
    // Internal tempo resolution is 0.01 BPM (12000 = 120.00 BPM); the UI's
    // one-decimal display precision does not restrict internal timing precision.
    // Exact rational arithmetic rounds down only the absolute result, so the
    // rounding error stays below one sample rather than accumulating per tick.
    // Configuration is fixed for a playback segment; phase continuity across
    // tempo changes is outside this API. Queries have no state or ordering effect.
    // Zero tempo_hundredths or sample_rate_hz returns InvalidConfiguration.
    // A result exceeding uint64_t returns Overflow. On either failure,
    // sample_position is unchanged and must not be used as a new playback position.
    // Bounded to 64 iterations, with no heap allocation or platform dependencies.
    [[nodiscard]] TickToSampleStatus TickToSamplePosition(
        std::uint64_t tick_index,
        std::uint32_t tempo_hundredths,
        std::uint32_t sample_rate_hz,
        std::uint64_t& sample_position) noexcept;
}

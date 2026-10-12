#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include "core/playback/row_events.h"

namespace BroTracker
{
    struct Pcm16MixInputs
    {
        const std::int16_t* channels[kRealtimePatternChannelCapacity]{};
    };
    enum class Pcm16MixStatus { Success, InvalidInput, InvalidDestination, RangeOverflow };

    // Unity-gain saturating mono summation, not a final channel/master volume policy.
    // All eight inputs have frame_count PCM16 frames (silence uses zero buffers).
    // Sum in int32_t, clamp only the final sum, preserving cancellation independent
    // of channel order. No averaging, gain, dither or floating-point arithmetic.
    // Zero frames succeeds with null pointers. Validate metadata before any write;
    // rejection leaves destination unchanged. Inputs are never modified and may
    // share read-only storage. Caller guarantees actual allocation extents/lifetimes
    // and destination non-overlap with every input and request metadata. Metadata
    // remains immutable during the call. No pointer comparisons infer aliasing.
    // Stateless O(8*frames), no allocation, temporary PCM, I/O, callbacks or clock.
    // No measured Teensy budget is claimed. Counts are checked before size_t conversion.
    [[nodiscard]] inline Pcm16MixStatus MixPcm16Mono(const Pcm16MixInputs& inputs,
        std::int16_t* destination, std::uint64_t frame_count) noexcept
    {
        if (frame_count == 0) return Pcm16MixStatus::Success;
        constexpr auto maximum_frames = std::numeric_limits<std::size_t>::max() / sizeof(std::int16_t);
        if (frame_count > maximum_frames) return Pcm16MixStatus::RangeOverflow;
        if (destination == nullptr) return Pcm16MixStatus::InvalidDestination;
        for (auto* input : inputs.channels)
            if (input == nullptr) return Pcm16MixStatus::InvalidInput;
        constexpr auto minimum = std::numeric_limits<std::int16_t>::min();
        constexpr auto maximum = std::numeric_limits<std::int16_t>::max();
        // Eight PCM16 samples sum to [-262144,262136], safely within int32_t.
        static_assert(kRealtimePatternChannelCapacity == 8, "Recheck int32_t sum bounds if capacity changes");
        const auto count = static_cast<std::size_t>(frame_count);
        for (std::size_t frame = 0; frame < count; ++frame)
        {
            std::int32_t sum = 0;
            for (auto* input : inputs.channels) sum += input[frame];
            if (sum < minimum) sum = minimum;
            else if (sum > maximum) sum = maximum;
            destination[frame] = static_cast<std::int16_t>(sum);
        }
        return Pcm16MixStatus::Success;
    }
}

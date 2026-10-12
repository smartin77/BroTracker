#pragma once
#include "core/audio/pcm16_mixer.h"

namespace BroTracker
{
    // Unsigned Q16 linear gain: 0=silence, 65536=exact unity; no amplification.
    constexpr std::uint32_t kPcm16UnityGain = 65536;
    struct Pcm16ChannelGains
    {
        std::uint32_t channels[kRealtimePatternChannelCapacity]{
            kPcm16UnityGain, kPcm16UnityGain, kPcm16UnityGain, kPcm16UnityGain,
            kPcm16UnityGain, kPcm16UnityGain, kPcm16UnityGain, kPcm16UnityGain};
    };
    enum class Pcm16GainMixStatus { Success, InvalidGain, InvalidInput, InvalidDestination, RangeOverflow };

    // Separate stateless gain primitive; not a final instrument/channel/master
    // volume or headroom policy. Zero gain affects audio contribution only: it
    // does not suppress tracker events or define channel mute/header styling.
    // Accumulate all exact signed PCM*Q16 products in int64_t. Round the sum once
    // to nearest integer, ties away from zero, using signed division (C++17
    // truncation toward zero), never negative shifts. Saturate only after rounding.
    // Exact unity gains are bit-identical to MixPcm16Mono.
    // Eight products lie in [-17179869184,17179344896]; adding/subtracting the
    // rounding half-unit (32768) also fits int64_t. No intermediate saturation.
    // All gain values must be <=unity, even for zero frames. After gain validation,
    // zero frames succeeds without pointers. Positive spans require every input
    // (including zero-gain channels) and destination; byte counts are checked
    // before size_t conversion. All metadata validation precedes any write;
    // rejection preserves destination, and inputs are never modified.
    // Caller guarantees actual memory extents/lifetimes and destination non-overlap
    // with inputs AND inputs/gain metadata. Metadata is immutable during the call;
    // inputs may share read-only storage. No alias/extent inference via pointers.
    // O(8*frames), fixed storage, noexcept; no allocation, temporary PCM, I/O,
    // callbacks, timeline or clock ownership. Teensy cost is unmeasured.
    [[nodiscard]] inline Pcm16GainMixStatus MixPcm16MonoWithGains(
        const Pcm16MixInputs& inputs, const Pcm16ChannelGains& gains,
        std::int16_t* destination, std::uint64_t frame_count) noexcept
    {
        static_assert(kRealtimePatternChannelCapacity == 8, "Recheck gain defaults and sum bounds");
        static_assert(std::int64_t{32768} * kPcm16UnityGain * kRealtimePatternChannelCapacity +
            kPcm16UnityGain / 2 <= std::numeric_limits<std::int64_t>::max(), "Accumulator must include rounding headroom");
        for (const auto gain : gains.channels)
            if (gain > kPcm16UnityGain) return Pcm16GainMixStatus::InvalidGain;
        if (frame_count == 0) return Pcm16GainMixStatus::Success;
        constexpr auto maximum_frames = std::numeric_limits<std::size_t>::max() / sizeof(std::int16_t);
        if (frame_count > maximum_frames) return Pcm16GainMixStatus::RangeOverflow;
        if (!destination) return Pcm16GainMixStatus::InvalidDestination;
        for (const auto* input : inputs.channels)
            if (!input) return Pcm16GainMixStatus::InvalidInput;
        const auto count = static_cast<std::size_t>(frame_count);
        for (std::size_t frame = 0; frame < count; ++frame)
        {
            std::int64_t sum = 0;
            for (unsigned channel = 0; channel < kRealtimePatternChannelCapacity; ++channel)
                sum += static_cast<std::int64_t>(inputs.channels[channel][frame]) * gains.channels[channel];
            auto rounded = (sum >= 0 ? sum + kPcm16UnityGain / 2 : sum - kPcm16UnityGain / 2) /
                kPcm16UnityGain;
            if (rounded < std::numeric_limits<std::int16_t>::min()) rounded = std::numeric_limits<std::int16_t>::min();
            else if (rounded > std::numeric_limits<std::int16_t>::max()) rounded = std::numeric_limits<std::int16_t>::max();
            destination[frame] = static_cast<std::int16_t>(rounded);
        }
        return Pcm16GainMixStatus::Success;
    }
}

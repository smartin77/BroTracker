#pragma once

#include <cstdint>

namespace BroTracker
{
    // Sample formats supported by the loader/player pipeline.
    // Only Pcm16Mono44100 is implemented for now; the remaining values
    // exist so the interface does not need to change when they are added.
    enum class SampleFormat : std::uint8_t
    {
        Pcm16Mono44100,
        Pcm16Stereo44100,
        Pcm8Mono44100,
        Pcm16Mono48000,
    };

    enum class LoopType : std::uint8_t
    {
        None,
        Forward,
        PingPong,
        Backward,
    };

    // In-RAM representation of a loaded sample, per docs/SAMPLE_FORMAT.md.
    // Playback fields (sample_rate_hz, channel_count, bits_per_sample) are
    // kept explicit rather than derived from `format` so future formats can
    // be added without changing how a player reads sample data.
    struct Sample
    {
        SampleFormat format = SampleFormat::Pcm16Mono44100;
        std::uint32_t sample_rate_hz = 0;
        std::uint8_t channel_count = 0;
        std::uint8_t bits_per_sample = 0;

        // Owned buffer of signed 16-bit frames. For the currently supported
        // format this is mono, so frame_count == sample count.
        std::int16_t* data = nullptr;
        std::uint32_t frame_count = 0;

        std::uint32_t loop_start = 0;
        std::uint32_t loop_end = 0;
        LoopType loop_type = LoopType::None;

        bool IsValid() const
        {
            return data != nullptr && frame_count > 0;
        }
    };

    // Releases the sample's owned data buffer, if any.
    void FreeSample(Sample& sample);
}

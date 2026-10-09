#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace BroTracker
{
    // Non-owning immutable PCM16 mono view. Caller retains the data unchanged
    // and valid until Stop/Reset/Configure, replacement or natural completion.
    struct Pcm16MonoView
    {
        const std::int16_t* data = nullptr;
        std::size_t frame_count = 0;
        std::uint32_t sample_rate_hz = 0;
    };

    enum class RamVoiceStatus
    {
        Success, InvalidSampleRate, NotConfigured, InvalidSample,
        SampleRateMismatch, InvalidDestination, RangeOverflow
    };

    // Native-rate one-shot primitive, no clock/scheduler position or ownership.
    // Single-owner use. No allocation, freeing, I/O, callbacks, gain or resampling.
    class RamSampleVoice
    {
    public:
        // Counts are size_t frames; reject counts whose PCM byte size overflows.
        static constexpr std::size_t kMaximumFrameCount =
            std::numeric_limits<std::size_t>::max() / sizeof(std::int16_t);

        // Zero rate fails unchanged. Valid configuration releases playback,
        // including configuring the same rate. Explicit output rate, no default.
        [[nodiscard]] RamVoiceStatus Configure(std::uint32_t output_sample_rate_hz) noexcept
        {
            if (output_sample_rate_hz == 0) return RamVoiceStatus::InvalidSampleRate;
            output_sample_rate_hz_ = output_sample_rate_hz;
            Stop();
            return RamVoiceStatus::Success;
        }

        // Reset retains configuration. Both operations release the view and
        // reset the relative next-frame cursor to zero, even if already silent.
        void Reset() noexcept { Stop(); }
        void Stop() noexcept { sample_ = {}; position_ = 0; }

        // Validate before mutation. Any failure leaves the current voice intact.
        // Every valid trigger replaces playback and starts at frame zero.
        [[nodiscard]] RamVoiceStatus Trigger(Pcm16MonoView sample) noexcept
        {
            if (output_sample_rate_hz_ == 0) return RamVoiceStatus::NotConfigured;
            if (sample.data == nullptr || sample.frame_count == 0 || sample.sample_rate_hz == 0)
                return RamVoiceStatus::InvalidSample;
            if (sample.frame_count > kMaximumFrameCount) return RamVoiceStatus::RangeOverflow;
            if (sample.sample_rate_hz != output_sample_rate_hz_)
                return RamVoiceStatus::SampleRateMismatch;
            sample_ = sample;
            position_ = 0;
            return RamVoiceStatus::Success;
        }

        // Zero count is a no-op, permitting nullptr. Otherwise destination must
        // point to count writable frames and must not overlap the retained sample.
        // Caller guarantees actual source/destination bounds and valid lifetimes;
        // pointer/length metadata cannot establish the allocation's true extent.
        // Invalid arguments leave state and all destination bytes unchanged.
        // Silent voices render zeros even before configuration. At end, release
        // the view and zero-pad this span; future spans stay silent. Work is O(count),
        // never O(sample length). Caller controls span size and processing budget.
        [[nodiscard]] RamVoiceStatus Render(std::int16_t* destination, std::size_t count) noexcept
        {
            if (count == 0) return RamVoiceStatus::Success;
            if (destination == nullptr) return RamVoiceStatus::InvalidDestination;
            if (count > kMaximumFrameCount) return RamVoiceStatus::RangeOverflow;
            std::size_t copied = 0;
            if (IsActive())
            {
                const std::size_t remaining = sample_.frame_count - position_;
                copied = count < remaining ? count : remaining;
                for (std::size_t i = 0; i < copied; ++i)
                    destination[i] = sample_.data[position_ + i];
                // copied <= frame_count - position_: addition cannot wrap.
                position_ += copied;
                if (position_ == sample_.frame_count) Stop();
            }
            for (std::size_t i = copied; i < count; ++i) destination[i] = 0;
            return RamVoiceStatus::Success;
        }

        bool IsActive() const noexcept { return sample_.data != nullptr; }
        std::size_t GetFramePosition() const noexcept { return position_; }
        std::uint32_t GetOutputSampleRate() const noexcept { return output_sample_rate_hz_; }

    private:
        Pcm16MonoView sample_;
        std::size_t position_ = 0;
        std::uint32_t output_sample_rate_hz_ = 0;
    };
}

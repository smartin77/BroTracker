#pragma once

#include "ram_sample_voice.h"
#include "core/playback/row_events.h"

namespace BroTracker
{
    // Initial command implementation capacity, not a permanent pattern-format limit.
    constexpr std::size_t kRamVoiceCommandCapacity = 16;
    enum class RamVoiceAction { Trigger, Stop };
    struct RamVoiceCommand
    {
        std::uint32_t channel = 0;
        std::uint64_t sample_offset = 0;
        RamVoiceAction action = RamVoiceAction::Stop;
        Pcm16MonoView sample;
    };
    struct RamVoiceCommandBatch
    {
        std::size_t count = 0;
        RamVoiceCommand commands[kRamVoiceCommandCapacity]{};
    };
    struct RamVoiceOutputs
    {
        std::int16_t* channels[kRealtimePatternChannelCapacity]{};
    };
    enum class RamBlockStatus
    {
        Success, NotConfigured, InvalidSampleRate, InvalidCommandCount,
        InvalidChannel, InvalidAction, InvalidOffset, UnorderedOffsets,
        InvalidDestination, RangeOverflow, InvalidSample, SampleRateMismatch
    };

    // Single-owner audio consumer of prepared positions, no note interpretation,
    // clock selection, synchronization, tempo correction or independent timeline.
    // MIDI scheduling is separate and need not wait for audio block boundaries.
    class RamVoiceBlockRenderer
    {
    public:
        // Valid configuration clears all playback; zero fails without mutation.
        [[nodiscard]] RamBlockStatus Configure(std::uint32_t sample_rate_hz) noexcept
        {
            if (sample_rate_hz == 0) return RamBlockStatus::InvalidSampleRate;
            for (auto& voice : voices_) (void)voice.Configure(sample_rate_hz);
            sample_rate_hz_ = sample_rate_hz;
            return RamBlockStatus::Success;
        }
        void Reset() noexcept { for (auto& voice : voices_) voice.Reset(); }
        // Read-only snapshot; nullptr for out-of-range channels. No mutable access.
        const RamSampleVoice* GetVoice(std::uint32_t channel) const noexcept
        {
            return channel < kRealtimePatternChannelCapacity ? &voices_[channel] : nullptr;
        }

        // Preflight the whole request before touching voices/output. Offsets are
        // uint64_t, checked before size_t conversion, ordered nondecreasing and
        // strictly below frame_count. Equal-offset commands retain input order.
        // An end-boundary command belongs to the next block at offset zero.
        // Configured zero-frame blocks accept only empty batches and permit null
        // outputs. Stop ignores its sample field. Voices persist between blocks.
        // Caller guarantees eight writable, mutually non-overlapping output spans,
        // each of frame_count frames, disjoint from ALL retained/triggered immutable
        // sample data, this object and request metadata. Sample extents/lifetimes
        // are caller-owned through replacement, stop, reset, configure or completion.
        // Metadata must remain unchanged during the call. Extents/aliasing cannot
        // be inferred from pointers. No sample data copies for validation/ownership.
        // All rejection paths preserve every voice and every destination byte.
        // Fixed storage, no allocation/I/O/callbacks/exceptions. Work O(8*frames +
        // 8*16): at most 16 commands and 17 render intervals. No measured Teensy
        // processing budget is claimed; caller bounds frames and command preparation.
        [[nodiscard]] RamBlockStatus Render(const RamVoiceOutputs& output,
            std::uint64_t frame_count, const RamVoiceCommandBatch& batch) noexcept
        {
            if (sample_rate_hz_ == 0) return RamBlockStatus::NotConfigured;
            if (batch.count > kRamVoiceCommandCapacity) return RamBlockStatus::InvalidCommandCount;
            if (frame_count > RamSampleVoice::kMaximumFrameCount) return RamBlockStatus::RangeOverflow;
            if (frame_count != 0)
                for (auto* destination : output.channels)
                    if (destination == nullptr) return RamBlockStatus::InvalidDestination;

            RamSampleVoice probe = voices_[0];
            for (std::size_t i = 0; i < batch.count; ++i)
            {
                const auto& command = batch.commands[i];
                if (command.channel >= kRealtimePatternChannelCapacity) return RamBlockStatus::InvalidChannel;
                if (command.action != RamVoiceAction::Trigger && command.action != RamVoiceAction::Stop)
                    return RamBlockStatus::InvalidAction;
                if (command.sample_offset >= frame_count) return RamBlockStatus::InvalidOffset;
                if (i != 0 && command.sample_offset < batch.commands[i - 1].sample_offset)
                    return RamBlockStatus::UnorderedOffsets;
                if (command.action == RamVoiceAction::Trigger)
                {
                    const auto status = probe.Trigger(command.sample);
                    if (status == RamVoiceStatus::InvalidSample) return RamBlockStatus::InvalidSample;
                    if (status == RamVoiceStatus::RangeOverflow) return RamBlockStatus::RangeOverflow;
                    if (status == RamVoiceStatus::SampleRateMismatch) return RamBlockStatus::SampleRateMismatch;
                }
            }
            if (frame_count == 0) return RamBlockStatus::Success;
            std::size_t position = 0;
            for (std::size_t i = 0; i < batch.count; ++i)
            {
                const auto& command = batch.commands[i];
                const auto offset = static_cast<std::size_t>(command.sample_offset);
                RenderSpan(output, position, offset - position);
                auto& voice = voices_[command.channel];
                if (command.action == RamVoiceAction::Trigger) (void)voice.Trigger(command.sample);
                else voice.Stop();
                position = offset;
            }
            RenderSpan(output, position, static_cast<std::size_t>(frame_count) - position);
            return RamBlockStatus::Success;
        }
    private:
        void RenderSpan(const RamVoiceOutputs& output, std::size_t start, std::size_t count) noexcept
        {
            if (count == 0) return;
            // All arguments and triggers were validated; these calls cannot fail.
            for (std::size_t channel = 0; channel < kRealtimePatternChannelCapacity; ++channel)
                (void)voices_[channel].Render(output.channels[channel] + start, count);
        }
        RamSampleVoice voices_[kRealtimePatternChannelCapacity]{};
        std::uint32_t sample_rate_hz_ = 0;
    };
}

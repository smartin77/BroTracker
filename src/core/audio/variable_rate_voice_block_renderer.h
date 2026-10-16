#pragma once

#include "variable_rate_sample_voice.h"
#include "core/playback/row_events.h"

namespace BroTracker
{
    // Initial command implementation capacity, not a permanent pattern-format limit.
    constexpr std::size_t kVariableRateVoiceCommandCapacity = 16;
    enum class VariableRateVoiceAction { Trigger, Stop };
    struct VariableRateVoiceCommand
    {
        std::uint32_t channel = 0;
        std::uint64_t sample_offset = 0;
        VariableRateVoiceAction action = VariableRateVoiceAction::Stop;
        Pcm16MonoView sample;
        std::uint64_t step_q32_32 = kSourceFrameStepUnity;
    };
    struct VariableRateVoiceCommandBatch
    {
        std::size_t count = 0;
        VariableRateVoiceCommand commands[kVariableRateVoiceCommandCapacity]{};
    };
    struct VariableRateVoiceOutputs
    {
        std::int16_t* channels[kRealtimePatternChannelCapacity]{};
    };
    enum class VariableRateBlockStatus
    {
        Success, NotConfigured, InvalidSampleRate, InvalidCommandCount,
        InvalidChannel, InvalidAction, InvalidOffset, UnorderedOffsets,
        InvalidDestination, RangeOverflow, InvalidSample, SampleRateMismatch, InvalidStep
    };

    // Single-owner audio consumer of prepared positions, no note interpretation,
    // clock selection, synchronization, tempo correction or independent timeline.
    // Explicit positive Q32.32 steps only; matching source/output rates are required.
    // Linear interpolation may alias and changes pitch AND duration; no note mapping.
    // MIDI scheduling is separate and need not wait for audio block boundaries.
    class VariableRateVoiceBlockRenderer
    {
    public:
        // Valid configuration clears all playback; zero fails without mutation.
        [[nodiscard]] VariableRateBlockStatus Configure(std::uint32_t sample_rate_hz) noexcept
        {
            if (sample_rate_hz == 0) return VariableRateBlockStatus::InvalidSampleRate;
            for (auto& voice : voices_) (void)voice.Configure(sample_rate_hz);
            sample_rate_hz_ = sample_rate_hz;
            return VariableRateBlockStatus::Success;
        }
        void Reset() noexcept { for (auto& voice : voices_) voice.Reset(); }
        // Read-only snapshot; nullptr for out-of-range channels. No mutable access.
        const VariableRateSampleVoice* GetVoice(std::uint32_t channel) const noexcept
        {
            return channel < kRealtimePatternChannelCapacity ? &voices_[channel] : nullptr;
        }

        // Preflight the whole request before touching voices/output. Offsets are
        // uint64_t, checked before size_t conversion, ordered nondecreasing and
        // strictly below frame_count. Equal-offset commands retain input order.
        // An end-boundary command belongs to the next block at offset zero.
        // Configured zero-frame blocks accept only empty batches and permit null
        // outputs. Stop ignores its sample and step fields. Voices persist between blocks.
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
        [[nodiscard]] VariableRateBlockStatus Render(const VariableRateVoiceOutputs& output,
            std::uint64_t frame_count, const VariableRateVoiceCommandBatch& batch) noexcept
        {
            if (sample_rate_hz_ == 0) return VariableRateBlockStatus::NotConfigured;
            if (batch.count > kVariableRateVoiceCommandCapacity) return VariableRateBlockStatus::InvalidCommandCount;
            if (frame_count > VariableRateSampleVoice::kMaximumFrameCount) return VariableRateBlockStatus::RangeOverflow;
            if (frame_count != 0)
                for (auto* destination : output.channels)
                    if (destination == nullptr) return VariableRateBlockStatus::InvalidDestination;

            VariableRateSampleVoice probe = voices_[0];
            for (std::size_t i = 0; i < batch.count; ++i)
            {
                const auto& command = batch.commands[i];
                if (command.channel >= kRealtimePatternChannelCapacity) return VariableRateBlockStatus::InvalidChannel;
                if (command.action != VariableRateVoiceAction::Trigger && command.action != VariableRateVoiceAction::Stop)
                    return VariableRateBlockStatus::InvalidAction;
                if (command.sample_offset >= frame_count) return VariableRateBlockStatus::InvalidOffset;
                if (i != 0 && command.sample_offset < batch.commands[i - 1].sample_offset)
                    return VariableRateBlockStatus::UnorderedOffsets;
                if (command.action == VariableRateVoiceAction::Trigger)
                {
                    const auto status = probe.Trigger(command.sample, command.step_q32_32);
                    if (status == VariableVoiceStatus::InvalidStep) return VariableRateBlockStatus::InvalidStep;
                    if (status == VariableVoiceStatus::InvalidSample) return VariableRateBlockStatus::InvalidSample;
                    if (status == VariableVoiceStatus::RangeOverflow) return VariableRateBlockStatus::RangeOverflow;
                    if (status == VariableVoiceStatus::SampleRateMismatch) return VariableRateBlockStatus::SampleRateMismatch;
                }
            }
            if (frame_count == 0) return VariableRateBlockStatus::Success;
            std::size_t position = 0;
            for (std::size_t i = 0; i < batch.count; ++i)
            {
                const auto& command = batch.commands[i];
                const auto offset = static_cast<std::size_t>(command.sample_offset);
                RenderSpan(output, position, offset - position);
                auto& voice = voices_[command.channel];
                if (command.action == VariableRateVoiceAction::Trigger) (void)voice.Trigger(command.sample, command.step_q32_32);
                else voice.Stop();
                position = offset;
            }
            RenderSpan(output, position, static_cast<std::size_t>(frame_count) - position);
            return VariableRateBlockStatus::Success;
        }
    private:
        void RenderSpan(const VariableRateVoiceOutputs& output, std::size_t start, std::size_t count) noexcept
        {
            if (count == 0) return;
            // All arguments and triggers were validated; these calls cannot fail.
            for (std::size_t channel = 0; channel < kRealtimePatternChannelCapacity; ++channel)
                (void)voices_[channel].Render(output.channels[channel] + start, count);
        }
        VariableRateSampleVoice voices_[kRealtimePatternChannelCapacity]{};
        std::uint32_t sample_rate_hz_ = 0;
    };
}

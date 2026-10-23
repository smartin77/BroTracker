#pragma once

#include "variable_rate_sample_voice.h"
#include <algorithm>
#include "sine_synth_voice.h"
#include "core/playback/row_events.h"

namespace BroTracker
{
    // Initial command implementation capacity, not a permanent pattern-format limit.
    constexpr std::size_t kVariableRateVoiceCommandCapacity = 16;
    enum class VariableRateVoiceAction { Trigger, Stop, TriggerSynth, TriggerMidi, Release };
    struct VariableRateVoiceCommand
    {
        std::uint32_t channel = 0;
        std::uint64_t sample_offset = 0;
        VariableRateVoiceAction action = VariableRateVoiceAction::Stop;
        Pcm16MonoView sample;
        std::uint64_t step_q32_32 = kSourceFrameStepUnity;
        Note synth_note = 69; // Raw Synth/MIDI pitch; never the display octave convention.
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
        InvalidDestination, RangeOverflow, InvalidSample, SampleRateMismatch, InvalidStep, InvalidNote
    };

    // Single-owner audio consumer of prepared sample/Synth commands; no scheduling,
    // clock selection, synchronization, tempo correction or independent timeline.
    // Sample: explicit positive Q32.32 steps; matching source/output rates required.
    // Synth: MIDI-compatible note tuning, independent one-shot envelope per channel.
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
            for (auto& synth : synths_) (void)synth.Configure(sample_rate_hz);
            Reset();
            sample_rate_hz_ = sample_rate_hz;
            return VariableRateBlockStatus::Success;
        }
        void Reset() noexcept {
            for (auto& voice : voices_) voice.Reset();
            for (auto& synth : synths_) synth.Reset();
            for (auto& channel : channels_) channel = {};
        }
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
        // outputs. Stop ignores its sample/step fields: Sample stops immediately; Synth
        // releases over 15 ms. Reset clears all voices and transitions immediately.
        // TriggerMidi explicitly clears this channel and emits no MIDI events.
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
                if (command.action != VariableRateVoiceAction::Trigger && command.action != VariableRateVoiceAction::Stop &&
                    command.action != VariableRateVoiceAction::TriggerSynth && command.action != VariableRateVoiceAction::TriggerMidi &&
                    command.action != VariableRateVoiceAction::Release)
                    return VariableRateBlockStatus::InvalidAction;
                if (command.sample_offset >= frame_count) return VariableRateBlockStatus::InvalidOffset;
                if (i != 0 && command.sample_offset < batch.commands[i - 1].sample_offset)
                    return VariableRateBlockStatus::UnorderedOffsets;
                if (command.action == VariableRateVoiceAction::TriggerSynth) {
                    if(sample_rate_hz_<32000 || sample_rate_hz_>192000) return VariableRateBlockStatus::InvalidSampleRate;
                    if(!IsPitchedPatternNote(command.synth_note)) return VariableRateBlockStatus::InvalidNote;
                }
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
                auto& channel = channels_[command.channel];
                auto& synth = synths_[command.channel];
                if(command.action == VariableRateVoiceAction::Release) {
                    if(channel.synth) synth.Release();
                    else { channel.release_length=std::max<std::uint32_t>(2,(sample_rate_hz_+499)/500); channel.release=channel.release_length; }
                } else if(command.action == VariableRateVoiceAction::TriggerMidi) {
                    voice.Reset(); synth.Reset(); channel={}; // MIDI contributes no local PCM.
                } else if(command.action == VariableRateVoiceAction::Stop) {
                    if(channel.synth) synth.Release(); else voice.Stop();
                } else {
                    channel.release=channel.release_length=0;
                    // Fixed 2 ms smoothstep crossfade from the last emitted value.
                    // A repeated trigger captures the current mixed value; no tail stack.
                    // Sample-to-sample replacement keeps its established semantics.
                    if(channel.synth || channel.transition<channel.length || command.action == VariableRateVoiceAction::TriggerSynth) {
                        channel.from=channel.last; channel.transition=0;
                        channel.length=static_cast<std::uint32_t>((std::uint64_t(sample_rate_hz_)*2+500)/1000);
                        if(channel.length<2) channel.length=2;
                    }
                    channel.synth=command.action == VariableRateVoiceAction::TriggerSynth;
                    if(channel.synth) { voice.Reset(); (void)synth.Trigger(command.synth_note); }
                    else { synth.Reset(); (void)voice.Trigger(command.sample,command.step_q32_32); }
                }
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
            {
                auto& state=channels_[channel];
                auto* destination=output.channels[channel]+start;
                if(!state.synth) {
                    (void)voices_[channel].Render(destination,count);
                    if(state.transition>=state.length && !state.release) { state.last=destination[count-1]; continue; }
                }
                for(std::size_t i=0;i<count;++i) {
                    float value=state.synth ? synths_[channel].Next() : float(destination[i]);
                    if(state.transition<state.length) {
                        const float weight=SmoothEndpoint(float(state.transition++)/float(state.length-1));
                        value=state.from*(1.0f-weight)+value*weight;
                    }
                    if(state.release) {
                        value*=float(state.release-1)/float(state.release_length-1);
                        if(!--state.release) voices_[channel].Stop();
                    } else if(!state.synth && state.release_length) value=0;
                    destination[i]=static_cast<std::int16_t>(std::lround(value));
                    state.last=destination[i];
                }
            }
        }
        struct ChannelTransition {
            bool synth=false;
            std::uint32_t transition=0,length=0,release=0,release_length=0;
            float from=0,last=0;
        } channels_[kRealtimePatternChannelCapacity]{};
        SineSynthVoice synths_[kRealtimePatternChannelCapacity]{};
        VariableRateSampleVoice voices_[kRealtimePatternChannelCapacity]{};
        std::uint32_t sample_rate_hz_ = 0;
    };
}

#pragma once

#include "channel_state.h"
#include "core/audio/ram_voice_block_renderer.h"

namespace BroTracker
{
    // Initial adapter capacity, not a permanent instrument/format limit.
    constexpr std::size_t kNativeRateSampleBindingCapacity = 16;
    struct NativeRateSampleBinding
    {
        std::uint8_t instrument = kNoInstrumentUpdate;
        Note native_rate_note = 0;
        Pcm16MonoView sample;
    };
    struct NativeRateSampleBindings
    {
        std::size_t count = 0;
        NativeRateSampleBinding bindings[kNativeRateSampleBindingCapacity]{};
    };
    enum class SampleCommandStatus
    {
        Success, NotConfigured, InvalidSampleRate, InvalidBindingCount,
        ReservedInstrument, DuplicateInstrument, InvalidNativeRateNote,
        InvalidSample, SampleRateMismatch, RangeOverflow, InvalidEventCount,
        InvalidChannel, InvalidNote, UnorderedOffsets, MissingInstrument,
        UnknownInstrument, UnsupportedPitch
    };

    // Narrow native-rate sample adapter, not an instrument/plugin ABI. Single
    // owner, ordered consumption: apply each supplied row command exactly once.
    // Fixed storage, no allocation, rendering, I/O, callbacks or exceptions.
    // No clock, timeline, physical synchronization or MIDI timing ownership.
    // Caller keeps PCM immutable and its actual memory extents/lifetime valid
    // while configured AND while any renderer retains emitted views, even after
    // this adapter resets/reconfigures/is destroyed. Caller coordinates renderer
    // release/replacement separately; Configure/Reset do not stop that renderer.
    // Input/output metadata must not overlap this object and must remain valid
    // for the call. No PCM ownership/copying or pointer extent inference.
    class NativeRateSampleCommandPreparer
    {
    public:
        // Validate count and every active binding before mutation (inactive slots
        // are not consumed). IDs 0..254 are unique;
        // native pattern notes are 24..127, samples must match the explicit output rate.
        // Zero bindings is valid (NOTE_OFF still works). Success copies metadata
        // and clears logical state; failure preserves bindings, rate and state.
        // Validation reuses RamSampleVoice::Trigger, without reading sample data.
        // At most 16 sample validations and 120 duplicate comparisons.
        [[nodiscard]] SampleCommandStatus Configure(std::uint32_t sample_rate_hz,
            const NativeRateSampleBindings& bindings) noexcept
        {
            if (sample_rate_hz == 0) return SampleCommandStatus::InvalidSampleRate;
            if (bindings.count > kNativeRateSampleBindingCapacity)
                return SampleCommandStatus::InvalidBindingCount;
            RamSampleVoice probe;
            (void)probe.Configure(sample_rate_hz);
            for (std::size_t i = 0; i < bindings.count; ++i)
            {
                const auto& binding = bindings.bindings[i];
                if (binding.instrument == kNoInstrumentUpdate)
                    return SampleCommandStatus::ReservedInstrument;
                if (!IsPitchedPatternNote(binding.native_rate_note))
                    return SampleCommandStatus::InvalidNativeRateNote;
                for (std::size_t j = 0; j < i; ++j)
                    if (bindings.bindings[j].instrument == binding.instrument)
                        return SampleCommandStatus::DuplicateInstrument;
                const auto status = probe.Trigger(binding.sample);
                if (status == RamVoiceStatus::InvalidSample) return SampleCommandStatus::InvalidSample;
                if (status == RamVoiceStatus::SampleRateMismatch) return SampleCommandStatus::SampleRateMismatch;
                if (status == RamVoiceStatus::RangeOverflow) return SampleCommandStatus::RangeOverflow;
            }
            bindings_ = bindings;
            sample_rate_hz_ = sample_rate_hz;
            state_.Reset();
            return SampleCommandStatus::Success;
        }

        // Explicit reset retains configuration/bindings. No reset at loop/row zero.
        void Reset() noexcept { state_.Reset(); }
        // Read-only snapshot; invalid channel leaves output unchanged.
        [[nodiscard]] ChannelStateStatus GetChannel(std::uint32_t channel,
            LogicalChannelState& output) const noexcept { return state_.GetChannel(channel, output); }

        // Transactional: tentative logical state and fixed output commit together.
        // Failure preserves ALL channels and the entire caller output batch;
        // success replaces output, including an empty batch. Instrument-only IDs
        // remain raw selections; lookup is required only for explicit normal notes.
        // Same-event instrument updates take effect before resolving the note.
        // Explicit repeated notes retrigger. NOTE_OFF emits Stop without lookup,
        // implementing only current row-start termination, not deferred row-end
        // note-off. Empty fields continue state and never imply new triggers.
        // Preserve channel, uint64_t offset and input order (including ties).
        // Reject decreasing offsets; renderer validates block bounds/ownership.
        // Work bounded by eight events, eight channel-state entries and 8*16 lookups.
        // No silent truncation; no measured Teensy execution budget is claimed.
        [[nodiscard]] SampleCommandStatus Prepare(const RowEventBatch& events,
            RamVoiceCommandBatch& output) noexcept
        {
            static_assert(kRamVoiceCommandCapacity >= kRealtimePatternChannelCapacity,
                "Every row event must fit the prepared command batch");
            if (sample_rate_hz_ == 0) return SampleCommandStatus::NotConfigured;
            if (events.count > kRealtimePatternChannelCapacity)
                return SampleCommandStatus::InvalidEventCount;
            auto tentative = state_;
            RamVoiceCommandBatch result{};
            for (std::size_t i = 0; i < events.count; ++i)
            {
                const auto& event = events.events[i];
                if (i != 0 && event.sample_offset < events.events[i - 1].sample_offset)
                    return SampleCommandStatus::UnorderedOffsets;
                RowEventApplication applied;
                const auto status = tentative.Apply(event, applied);
                if (status == ChannelStateStatus::InvalidChannel) return SampleCommandStatus::InvalidChannel;
                if (status == ChannelStateStatus::InvalidNote) return SampleCommandStatus::InvalidNote;
                if (event.note == NOTE_EMPTY) continue;
                RamVoiceCommand command{event.channel, event.sample_offset, RamVoiceAction::Stop, {}};
                if (event.note != NOTE_OFF)
                {
                    if (applied.after.instrument == kNoInstrumentUpdate)
                        return SampleCommandStatus::MissingInstrument;
                    const NativeRateSampleBinding* selected = nullptr;
                    for (std::size_t j = 0; j < bindings_.count; ++j)
                        if (bindings_.bindings[j].instrument == applied.after.instrument)
                        {
                            selected = &bindings_.bindings[j];
                            break;
                        }
                    if (selected == nullptr) return SampleCommandStatus::UnknownInstrument;
                    if (event.note != selected->native_rate_note) return SampleCommandStatus::UnsupportedPitch;
                    command.action = RamVoiceAction::Trigger;
                    command.sample = selected->sample;
                }
                result.commands[result.count++] = command;
            }
            state_ = tentative;
            output = result;
            return SampleCommandStatus::Success;
        }
    private:
        NativeRateSampleBindings bindings_{};
        ChannelPlaybackState state_;
        std::uint32_t sample_rate_hz_ = 0;
    };
}

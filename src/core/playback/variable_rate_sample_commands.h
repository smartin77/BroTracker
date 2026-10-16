#pragma once

#include "channel_state.h"
#include "core/audio/variable_rate_voice_block_renderer.h"
#include "core/audio/note_playback_step.h"

namespace BroTracker
{
    // Initial adapter capacity, not a permanent instrument/format limit.
    constexpr std::size_t kVariableRateSampleBindingCapacity = 16;
    struct VariableRateSampleBinding
    {
        std::uint8_t instrument = kNoInstrumentUpdate;
        Note reference_note = 0;
        Pcm16MonoView sample;
    };
    struct VariableRateSampleBindings
    {
        std::size_t count = 0;
        VariableRateSampleBinding bindings[kVariableRateSampleBindingCapacity]{};
    };
    enum class VariableSampleCommandStatus
    {
        Success, NotConfigured, InvalidSampleRate, InvalidBindingCount,
        ReservedInstrument, DuplicateInstrument, InvalidReferenceNote,
        InvalidSample, SampleRateMismatch, RangeOverflow, InvalidEventCount,
        InvalidChannel, InvalidNote, UnorderedOffsets, MissingInstrument,
        UnknownInstrument, InvalidNoteOffTiming
    };

    // Narrow variable-rate sample adapter, not an instrument/plugin ABI. Single
    // owner, ordered consumption: apply each supplied row command exactly once.
    // Fixed storage, no allocation, rendering, I/O, callbacks or exceptions.
    // Equal-tempered transposition uses explicit reference notes, never BPM/duration.
    // No source-rate conversion, tuning correction, tempo matching or defaults.
    // No clock, timeline, physical synchronization or MIDI timing ownership.
    // Caller keeps PCM immutable and its actual memory extents/lifetime valid
    // while configured AND while any renderer retains emitted views, even after
    // this adapter resets/reconfigures/is destroyed. Caller coordinates renderer
    // release/replacement separately; Configure/Reset do not stop that renderer.
    // Input/output metadata must not overlap this object and must remain valid
    // for the call. No PCM ownership/copying or pointer extent inference.
    class VariableRateSampleCommandPreparer
    {
    public:
        // Validate count and every active binding before mutation (inactive slots
        // are not consumed). IDs 0..254 are unique;
        // reference pattern notes are 24..127, samples must match the explicit output rate.
        // Zero bindings is valid (NOTE_OFF still works). Success copies metadata
        // and clears logical state; failure preserves bindings, rate and state.
        // Validation reuses VariableRateSampleVoice::Trigger at unity, without reading sample data.
        // At most 16 sample validations and 120 duplicate comparisons.
        [[nodiscard]] VariableSampleCommandStatus Configure(std::uint32_t sample_rate_hz,
            const VariableRateSampleBindings& bindings) noexcept
        {
            if (sample_rate_hz == 0) return VariableSampleCommandStatus::InvalidSampleRate;
            if (bindings.count > kVariableRateSampleBindingCapacity)
                return VariableSampleCommandStatus::InvalidBindingCount;
            VariableRateSampleVoice probe;
            (void)probe.Configure(sample_rate_hz);
            for (std::size_t i = 0; i < bindings.count; ++i)
            {
                const auto& binding = bindings.bindings[i];
                if (binding.instrument == kNoInstrumentUpdate)
                    return VariableSampleCommandStatus::ReservedInstrument;
                if (!IsPitchedPatternNote(binding.reference_note))
                    return VariableSampleCommandStatus::InvalidReferenceNote;
                for (std::size_t j = 0; j < i; ++j)
                    if (bindings.bindings[j].instrument == binding.instrument)
                        return VariableSampleCommandStatus::DuplicateInstrument;
                const auto status = probe.Trigger(binding.sample, kSourceFrameStepUnity);
                if (status == VariableVoiceStatus::InvalidSample) return VariableSampleCommandStatus::InvalidSample;
                if (status == VariableVoiceStatus::SampleRateMismatch) return VariableSampleCommandStatus::SampleRateMismatch;
                if (status == VariableVoiceStatus::RangeOverflow) return VariableSampleCommandStatus::RangeOverflow;
            }
            bindings_ = bindings;
            sample_rate_hz_ = sample_rate_hz;
            state_.Reset();
            return VariableSampleCommandStatus::Success;
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
        // for Arrival. EndOfPosition updates logical state but emits no immediate
        // command; the pattern player owns the deferred absolute-boundary Stop. Empty fields continue state and never imply new triggers.
        // Preserve channel, uint64_t offset and input order (including ties).
        // Reject decreasing offsets; renderer validates block bounds/ownership.
        // Work bounded by eight events, eight channel-state entries and 8*16 lookups.
        // No silent truncation; no measured Teensy execution budget is claimed.
        [[nodiscard]] VariableSampleCommandStatus Prepare(const RowEventBatch& events,
            VariableRateVoiceCommandBatch& output) noexcept
        {
            static_assert(kVariableRateVoiceCommandCapacity >= kRealtimePatternChannelCapacity,
                "Every row event must fit the prepared command batch");
            if (sample_rate_hz_ == 0) return VariableSampleCommandStatus::NotConfigured;
            if (events.count > kRealtimePatternChannelCapacity)
                return VariableSampleCommandStatus::InvalidEventCount;
            auto tentative = state_;
            VariableRateVoiceCommandBatch result{};
            for (std::size_t i = 0; i < events.count; ++i)
            {
                const auto& event = events.events[i];
                if (i != 0 && event.sample_offset < events.events[i - 1].sample_offset)
                    return VariableSampleCommandStatus::UnorderedOffsets;
                RowEventApplication applied;
                const auto status = tentative.Apply(event, applied);
                if (status == ChannelStateStatus::InvalidChannel) return VariableSampleCommandStatus::InvalidChannel;
                if (status == ChannelStateStatus::InvalidNote) return VariableSampleCommandStatus::InvalidNote;
                if (status == ChannelStateStatus::InvalidNoteOffTiming) return VariableSampleCommandStatus::InvalidNoteOffTiming;
                if (event.note == NOTE_EMPTY || (event.note == NOTE_OFF &&
                    event.note_off_timing == NoteOffTiming::EndOfPosition)) continue;
                VariableRateVoiceCommand command{event.channel, event.sample_offset, VariableRateVoiceAction::Stop, {}};
                if (event.note != NOTE_OFF)
                {
                    if (applied.after.instrument == kNoInstrumentUpdate)
                        return VariableSampleCommandStatus::MissingInstrument;
                    const VariableRateSampleBinding* selected = nullptr;
                    for (std::size_t j = 0; j < bindings_.count; ++j)
                        if (bindings_.bindings[j].instrument == applied.after.instrument)
                        {
                            selected = &bindings_.bindings[j];
                            break;
                        }
                    if (selected == nullptr) return VariableSampleCommandStatus::UnknownInstrument;
                    const auto mapped = NoteToPlaybackStep(event.note, selected->reference_note,
                        command.step_q32_32);
                    if (mapped == NotePlaybackStepStatus::InvalidTargetNote)
                        return VariableSampleCommandStatus::InvalidNote;
                    if (mapped == NotePlaybackStepStatus::InvalidReferenceNote)
                        return VariableSampleCommandStatus::InvalidReferenceNote;
                    command.action = VariableRateVoiceAction::Trigger;
                    command.sample = selected->sample;
                }
                result.commands[result.count++] = command;
            }
            state_ = tentative;
            output = result;
            return VariableSampleCommandStatus::Success;
        }
    private:
        VariableRateSampleBindings bindings_{};
        ChannelPlaybackState state_;
        std::uint32_t sample_rate_hz_ = 0;
    };
}

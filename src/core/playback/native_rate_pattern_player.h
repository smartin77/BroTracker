#pragma once

#include "native_rate_sample_commands.h"
#include "core/audio/pcm16_mixer.h"
#include <limits>

namespace BroTracker
{
    // Initial processing capacities, not permanent pattern or instrument limits.
    constexpr std::size_t kNativeRatePatternFrameCapacity = 128;
    constexpr std::size_t kNativeRatePatternTickBudget = 512;

    struct ConsumedPatternPosition
    {
        bool valid = false;
        std::uint64_t absolute_tick = 0;
        std::uint64_t pattern_row = 0;
        std::uint64_t loop_index = 0;
    };

    enum class PatternPlayerStatus
    {
        Success, NotConfigured, InvalidTempo, InvalidSampleRate,
        InvalidDimensions, InvalidNote, InvalidBindingCount, ReservedInstrument,
        DuplicateInstrument, InvalidNativeRateNote, InvalidSample,
        SampleRateMismatch, RangeOverflow, InvalidDestination, InvalidFrameCount,
        MissingInstrument, UnknownInstrument, UnsupportedPitch,
        ArithmeticExhausted, TickBudgetExceeded, CommandCapacityExceeded,
        ComponentError, InvalidTransportState
    };
    enum class PatternTransportState { Stopped, Playing, Paused };

    // Single-owner native-rate, looping-pattern audio consumer. Reuses logical
    // scheduling positions; no wall/CPU clock, USB authority, physical clock
    // selection, synchronization, tempo correction or MIDI scheduling policy.
    // Pattern/binding metadata is copied. Caller keeps all bound PCM immutable,
    // with valid actual extents/lifetimes until successful reconfiguration or
    // destruction releases the bindings (Stop still retains configuration).
    // Destination must hold frame_count writable frames and be disjoint from
    // ALL bound PCM, this object and request metadata. Configure inputs must
    // not overlap this object; metadata remains valid/unchanged during each call.
    // No pointer inspection establishes allocation extents or non-overlap.
    // No heap allocation, exceptions, I/O, callbacks or concurrent/live edits.
    class NativeRatePatternPlayer
    {
    public:
        // Validate every active cell, dimensions, tempo/rate and active sample
        // bindings before committing. Inactive cells are never consumed. Binding
        // validation reuses the preparer (including byte-size/rate checks).
        // Instrument selection/pitch resolution remains a render-time operation:
        // syntactically valid patterns may contain unsupported/missing bindings.
        // Failure preserves prior configuration AND playback. Success copies fixed
        // metadata, stops voices, clears logical state and starts position at zero.
        [[nodiscard]] PatternPlayerStatus Configure(std::uint32_t tempo_hundredths,
            std::uint32_t sample_rate_hz, const RealtimePattern& pattern,
            const NativeRateSampleBindings& bindings) noexcept
        {
            if (tempo_hundredths == 0) return PatternPlayerStatus::InvalidTempo;
            if (sample_rate_hz == 0) return PatternPlayerStatus::InvalidSampleRate;
            if (pattern.active_rows == 0 || pattern.active_rows > kRealtimePatternRowCapacity ||
                pattern.active_channels == 0 || pattern.active_channels > kRealtimePatternChannelCapacity)
                return PatternPlayerStatus::InvalidDimensions;
            for (std::size_t row = 0; row < pattern.active_rows; ++row)
                for (std::size_t channel = 0; channel < pattern.active_channels; ++channel)
                {
                    const Note note = pattern.cells[row][channel].note;
                    if (!IsPatternNote(note))
                        return PatternPlayerStatus::InvalidNote;
                }
            MusicalTickCursor cursor;
            (void)cursor.Configure(tempo_hundredths, sample_rate_hz);
            NativeRateSampleCommandPreparer preparer;
            const auto status = preparer.Configure(sample_rate_hz, bindings);
            if (status != SampleCommandStatus::Success) return SampleStatus(status);
            RamVoiceBlockRenderer renderer;
            (void)renderer.Configure(sample_rate_hz);
            pattern_ = pattern;
            cursor_ = cursor;
            preparer_ = preparer;
            renderer_ = renderer;
            configured_ = true;
            transport_ = PatternTransportState::Stopped;
            next_sample_ = 0;
            position_ = {};
            return PatternPlayerStatus::Success;
        }

        // Always restart at tick/sample zero, including repeated Start.
        [[nodiscard]] PatternPlayerStatus Start() noexcept
        {
            if (!configured_) return PatternPlayerStatus::NotConfigured;
            Stop();
            transport_ = PatternTransportState::Playing;
            return PatternPlayerStatus::Success;
        }
        // Idempotent, also valid before configuration. Retain pattern/bindings.
        void Stop() noexcept
        {
            renderer_.Reset();
            preparer_.Reset();
            if (configured_) (void)cursor_.Reset();
            transport_ = PatternTransportState::Stopped;
            next_sample_ = 0;
            position_ = {};
        }
        bool IsRunning() const noexcept { return transport_ == PatternTransportState::Playing; }
        PatternTransportState GetTransportState() const noexcept {
            return transport_;
        }
        // Silence voices, retain logical note/instrument continuation and consumed
        // position. No timeline advancement while paused; audio owner only.
        [[nodiscard]] PatternPlayerStatus Pause() noexcept {
            if (!configured_) return PatternPlayerStatus::NotConfigured;
            if (transport_ == PatternTransportState::Paused) return PatternPlayerStatus::Success;
            if (transport_ != PatternTransportState::Playing) return PatternPlayerStatus::InvalidTransportState;
            renderer_.Reset(); transport_ = PatternTransportState::Paused;
            return PatternPlayerStatus::Success;
        }
        // Restart a segment at the next row start, including next-loop row zero.
        // No scan of skipped ticks/events. Before the first consumed tick, row zero
        // is still pending and is the next row. Errors preserve ALL player state.
        [[nodiscard]] PatternPlayerStatus Continue() noexcept {
            if (!configured_) return PatternPlayerStatus::NotConfigured;
            if (transport_ != PatternTransportState::Paused) return PatternPlayerStatus::InvalidTransportState;
            std::uint64_t tick = 0;
            if (position_.valid) {
                const auto row = position_.absolute_tick / kTicksPerRow;
                if (row >= UINT64_MAX / kTicksPerRow) return PatternPlayerStatus::RangeOverflow;
                tick = (row + 1) * kTicksPerRow;
            }
            auto cursor = cursor_;
            std::uint64_t sample = 0;
            const auto status = cursor.ResetAtTick(tick, sample);
            if (status != TickCursorStatus::Success) return PatternPlayerStatus::RangeOverflow;
            cursor_ = cursor; next_sample_ = sample;
            transport_ = PatternTransportState::Playing;
            return PatternPlayerStatus::Success;
        }
        std::uint64_t GetNextSamplePosition() const noexcept { return next_sample_; }
        // Latest tick consumed in a successfully rendered half-open audio span,
        // including empty rows/non-row-start ticks. Not the next block position,
        // a physical output timestamp or a host-extrapolated position. Zero-frame
        // and rejected renders preserve it; tick-free blocks retain it. Configure,
        // Start and Stop clear validity until a tick is successfully consumed.
        ConsumedPatternPosition GetPlaybackPosition() const noexcept { return position_; }
        std::uint32_t GetActiveRows() const noexcept { return configured_ ? pattern_.active_rows : 0; }
        std::uint32_t GetActiveChannels() const noexcept { return configured_ ? pattern_.active_channels : 0; }

        // Zero frames always succeeds as a no-op (nullptr allowed). Positive
        // spans require configuration, <=128 frames and a destination. Stopped
        // rendering writes silence and never advances logical playback.
        // Running blocks are consecutive [next_sample, next_sample+frame_count).
        // Use tentative cursor/preparer/renderer copies and internal scratch:
        // ALL errors preserve running/position, channel/voice state and caller
        // output. Scratch need not be preserved. Success commits all state/output.
        // At most 512 emitted ticks plus one completion probe; excess work and
        // >16 prepared commands explicitly reject the WHOLE block, never truncate.
        // Event order, coincident ticks, half-open boundaries, loop continuation
        // and 0.01 BPM precision come from the existing components.
        // Bound: 513 cursor pulls (each <=64 conversion iterations), <=512*8*16
        // binding comparisons, <=16 audio commands, 8*128 channel frames + mixing
        // and output copying. Fixed 8x128 channel and 128 mono scratch storage.
        // Callers MUST handle rejection; no firmware underrun policy or measured
        // Teensy processing budget is established. A smaller retry may succeed;
        // intrinsically unsupported events require reconfiguration or Stop.
        [[nodiscard]] PatternPlayerStatus Render(std::int16_t* destination,
            std::uint64_t frame_count) noexcept
        {
            if (frame_count == 0) return PatternPlayerStatus::Success;
            if (!configured_) return PatternPlayerStatus::NotConfigured;
            if (frame_count > kNativeRatePatternFrameCapacity) return PatternPlayerStatus::InvalidFrameCount;
            if (destination == nullptr) return PatternPlayerStatus::InvalidDestination;
            const auto count = static_cast<std::size_t>(frame_count);
            if (transport_ != PatternTransportState::Playing)
            {
                for (std::size_t i = 0; i < count; ++i) destination[i] = 0;
                return PatternPlayerStatus::Success;
            }
            auto cursor = cursor_;
            auto preparer = preparer_;
            auto renderer = renderer_;
            auto position = position_;
            bool consumed_tick = false;
            if (frame_count > std::numeric_limits<std::uint64_t>::max() - next_sample_)
                return PatternPlayerStatus::RangeOverflow;
            const auto begin = cursor.BeginBlock(next_sample_, frame_count);
            if (begin == TickCursorStatus::RangeOverflow) return PatternPlayerStatus::RangeOverflow;
            if (begin == TickCursorStatus::ArithmeticExhausted) return PatternPlayerStatus::ArithmeticExhausted;
            if (begin != TickCursorStatus::Success) return PatternPlayerStatus::ComponentError;
            RamVoiceCommandBatch commands{};
            for (std::size_t emitted = 0; ; ++emitted)
            {
                MusicalTick tick;
                const auto status = cursor.Pull(tick);
                if (status == TickCursorStatus::BlockComplete) break;
                if (status == TickCursorStatus::ArithmeticExhausted) return PatternPlayerStatus::ArithmeticExhausted;
                if (status != TickCursorStatus::Tick) return PatternPlayerStatus::ComponentError;
                if (emitted == kNativeRatePatternTickBudget) return PatternPlayerStatus::TickBudgetExceeded;
                position.absolute_tick = tick.tick_index;
                consumed_tick = true;
                RowEventBatch rows{};
                if (GenerateRowEvents(tick, pattern_, rows) != RowEventStatus::Success)
                    return PatternPlayerStatus::ComponentError;
                RamVoiceCommandBatch prepared{};
                const auto preparation = preparer.Prepare(rows, prepared);
                if (preparation != SampleCommandStatus::Success) return SampleStatus(preparation);
                if (prepared.count > kRamVoiceCommandCapacity - commands.count)
                    return PatternPlayerStatus::CommandCapacityExceeded;
                for (std::size_t i = 0; i < prepared.count; ++i)
                    commands.commands[commands.count++] = prepared.commands[i];
            }
            if (consumed_tick)
            {
                PatternPosition mapped;
                if (TickToPatternPosition(position.absolute_tick, pattern_.active_rows, mapped) !=
                    PatternPositionStatus::Success) return PatternPlayerStatus::ComponentError;
                position = {true, position.absolute_tick, mapped.pattern_row, mapped.loop_index};
            }
            RamVoiceOutputs outputs;
            Pcm16MixInputs inputs;
            for (std::size_t channel = 0; channel < kRealtimePatternChannelCapacity; ++channel)
            {
                outputs.channels[channel] = channel_scratch_[channel];
                inputs.channels[channel] = channel_scratch_[channel];
            }
            if (renderer.Render(outputs, frame_count, commands) != RamBlockStatus::Success)
                return PatternPlayerStatus::ComponentError;
            if (MixPcm16Mono(inputs, mono_scratch_, frame_count) != Pcm16MixStatus::Success)
                return PatternPlayerStatus::ComponentError;
            cursor_ = cursor;
            preparer_ = preparer;
            renderer_ = renderer;
            position_ = position;
            // BeginBlock checked the absolute block end for uint64_t overflow.
            next_sample_ += frame_count;
            for (std::size_t i = 0; i < count; ++i) destination[i] = mono_scratch_[i];
            return PatternPlayerStatus::Success;
        }
    private:
        static PatternPlayerStatus SampleStatus(SampleCommandStatus status) noexcept
        {
            switch (status)
            {
            case SampleCommandStatus::InvalidBindingCount: return PatternPlayerStatus::InvalidBindingCount;
            case SampleCommandStatus::ReservedInstrument: return PatternPlayerStatus::ReservedInstrument;
            case SampleCommandStatus::DuplicateInstrument: return PatternPlayerStatus::DuplicateInstrument;
            case SampleCommandStatus::InvalidNativeRateNote: return PatternPlayerStatus::InvalidNativeRateNote;
            case SampleCommandStatus::InvalidSample: return PatternPlayerStatus::InvalidSample;
            case SampleCommandStatus::SampleRateMismatch: return PatternPlayerStatus::SampleRateMismatch;
            case SampleCommandStatus::RangeOverflow: return PatternPlayerStatus::RangeOverflow;
            case SampleCommandStatus::MissingInstrument: return PatternPlayerStatus::MissingInstrument;
            case SampleCommandStatus::UnknownInstrument: return PatternPlayerStatus::UnknownInstrument;
            case SampleCommandStatus::UnsupportedPitch: return PatternPlayerStatus::UnsupportedPitch;
            default: return PatternPlayerStatus::ComponentError;
            }
        }
        // Test-only access to otherwise impractical uint64_t exhaustion boundaries.
        friend struct NativeRatePatternPlayerTestAccess;
        RealtimePattern pattern_;
        MusicalTickCursor cursor_;
        NativeRateSampleCommandPreparer preparer_;
        RamVoiceBlockRenderer renderer_;
        bool configured_ = false;
        PatternTransportState transport_ = PatternTransportState::Stopped;
        std::uint64_t next_sample_ = 0;
        ConsumedPatternPosition position_;
        std::int16_t channel_scratch_[kRealtimePatternChannelCapacity][kNativeRatePatternFrameCapacity]{};
        std::int16_t mono_scratch_[kNativeRatePatternFrameCapacity]{};
    };
}

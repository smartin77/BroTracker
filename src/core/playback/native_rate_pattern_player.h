#pragma once

#include "native_rate_sample_commands.h"
#include "core/audio/pcm16_mixer.h"
#include <limits>

namespace BroTracker
{
    // Initial processing capacities, not permanent pattern or instrument limits.
    constexpr std::size_t kNativeRatePatternFrameCapacity = 128;
    constexpr std::size_t kNativeRatePatternTickBudget = 512;

    enum class PatternPlayerStatus
    {
        Success, NotConfigured, InvalidTempo, InvalidSampleRate,
        InvalidDimensions, InvalidNote, InvalidBindingCount, ReservedInstrument,
        DuplicateInstrument, InvalidNativeRateNote, InvalidSample,
        SampleRateMismatch, RangeOverflow, InvalidDestination, InvalidFrameCount,
        MissingInstrument, UnknownInstrument, UnsupportedPitch,
        ArithmeticExhausted, TickBudgetExceeded, CommandCapacityExceeded,
        ComponentError
    };

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
                    if (note > 127 && note != NOTE_EMPTY && note != NOTE_OFF)
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
            running_ = false;
            next_sample_ = 0;
            return PatternPlayerStatus::Success;
        }

        // Always restart at tick/sample zero, including repeated Start. No resume.
        [[nodiscard]] PatternPlayerStatus Start() noexcept
        {
            if (!configured_) return PatternPlayerStatus::NotConfigured;
            Stop();
            running_ = true;
            return PatternPlayerStatus::Success;
        }
        // Idempotent, also valid before configuration. Retain pattern/bindings.
        void Stop() noexcept
        {
            renderer_.Reset();
            preparer_.Reset();
            if (configured_) (void)cursor_.Reset();
            running_ = false;
            next_sample_ = 0;
        }
        bool IsRunning() const noexcept { return running_; }
        std::uint64_t GetNextSamplePosition() const noexcept { return next_sample_; }

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
            if (!running_)
            {
                for (std::size_t i = 0; i < count; ++i) destination[i] = 0;
                return PatternPlayerStatus::Success;
            }
            auto cursor = cursor_;
            auto preparer = preparer_;
            auto renderer = renderer_;
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
        bool running_ = false;
        std::uint64_t next_sample_ = 0;
        std::int16_t channel_scratch_[kRealtimePatternChannelCapacity][kNativeRatePatternFrameCapacity]{};
        std::int16_t mono_scratch_[kNativeRatePatternFrameCapacity]{};
    };
}

#pragma once

#include "variable_rate_sample_commands.h"
#include "core/audio/pcm16_gain_mixer.h"
#include <limits>
#include "core/tempo.h"

namespace BroTracker
{
    // Initial processing capacities, not permanent pattern or instrument limits.
    constexpr std::size_t kVariableRatePatternFrameCapacity = 128;
    constexpr std::size_t kVariableRatePatternTickBudget = 512;

    struct VariableRateConsumedPatternPosition
    {
        bool valid = false;
        std::uint64_t absolute_tick = 0;
        std::uint64_t pattern_row = 0;
        std::uint64_t loop_index = 0;
    };

    enum class VariableRatePatternPlayerStatus
    {
        Success, NotConfigured, InvalidTempo, InvalidSampleRate,
        InvalidDimensions, InvalidNote, InvalidBindingCount, ReservedInstrument,
        DuplicateInstrument, InvalidReferenceNote, InvalidSample,
        SampleRateMismatch, RangeOverflow, InvalidDestination, InvalidFrameCount,
        MissingInstrument, UnknownInstrument,
        ArithmeticExhausted, TickBudgetExceeded, CommandCapacityExceeded,
        ComponentError, InvalidTransportState, InvalidNoteOffTiming, InvalidGain
    };
    enum class VariableRatePatternTransportState { Stopped, Playing, Paused };

    // Single-owner Sample/Synth, looping-pattern audio consumer. Reuses logical
    // scheduling positions; no wall/CPU clock, USB authority, physical clock
    // selection, synchronization, tempo correction or MIDI scheduling policy.
    // Explicit reference-note mapping, matching source/output rates only. No finetune,
    // source-rate conversion or tempo matching; interpolation changes pitch/duration
    // together and may alias. Existing native-rate orchestration remains separate.
    // Pattern/binding metadata is copied. Caller keeps all bound PCM immutable,
    // with valid actual extents/lifetimes until successful reconfiguration or
    // destruction releases the bindings (Stop still retains configuration).
    // Destination must hold frame_count writable frames and be disjoint from
    // ALL bound PCM, this object and request metadata. Configure inputs must
    // not overlap this object; metadata remains valid/unchanged during each call.
    // No pointer inspection establishes allocation extents or non-overlap.
    // No heap allocation, exceptions, I/O, callbacks or concurrent mutation.
    class VariableRatePatternPlayer
    {
    public:
        // Validate every active cell and timing enum (including non-OFF cells),
        // dimensions, tempo/rate and active sample
        // bindings before committing. Inactive cells are never consumed. Binding
        // validation reuses the preparer (including byte-size/rate checks).
        // Instrument selection/pitch resolution remains a render-time operation:
        // syntactically valid patterns may contain unknown/missing bindings.
        // Failure preserves prior configuration AND playback. Success copies fixed
        // metadata, stops voices, clears logical state and starts position at zero.
        [[nodiscard]] VariableRatePatternPlayerStatus Configure(std::uint32_t tempo_hundredths,
            std::uint32_t sample_rate_hz, const RealtimePattern& pattern,
            const VariableRateSampleBindings& bindings) noexcept
        {
            if (tempo_hundredths == 0) return VariableRatePatternPlayerStatus::InvalidTempo;
            if (sample_rate_hz == 0) return VariableRatePatternPlayerStatus::InvalidSampleRate;
            const auto pattern_status = ValidatePattern(pattern);
            if (pattern_status != VariableRatePatternPlayerStatus::Success) return pattern_status;
            MusicalTickCursor cursor;
            (void)cursor.Configure(tempo_hundredths, sample_rate_hz);
            VariableRateSampleCommandPreparer preparer;
            const auto status = preparer.Configure(sample_rate_hz, bindings);
            if (status != VariableSampleCommandStatus::Success) return SampleStatus(status);
            VariableRateVoiceBlockRenderer renderer;
            (void)renderer.Configure(sample_rate_hz);
            tempo_hundredths_ = tempo_hundredths; pending_tempo_=0;
            sample_rate_hz_ = sample_rate_hz;
            pending_ = {};
            preview_pending_ = false;
            pattern_ = pattern;
            cursor_ = cursor;
            preparer_ = preparer;
            renderer_ = renderer;
            gains_ = {}; // Successful configuration restores exact unity.
            configured_ = true;
            transport_ = VariableRatePatternTransportState::Stopped;
            next_sample_ = 0;
            position_ = {};
            return VariableRatePatternPlayerStatus::Success;
        }

        std::uint32_t GetTempo() const noexcept { return tempo_hundredths_; }
        bool HasPendingTempo() const noexcept { return pending_tempo_!=0; }
        [[nodiscard]] VariableRatePatternPlayerStatus RequestTempo(std::uint32_t value) noexcept {
            if(!configured_) return VariableRatePatternPlayerStatus::NotConfigured;
            if(!ValidInteractiveTempo(value)) return VariableRatePatternPlayerStatus::InvalidTempo;
            if(value==tempo_hundredths_) {pending_tempo_=0; return VariableRatePatternPlayerStatus::Success;}
            if(IsRunning()) pending_tempo_=value;
            else {
                tempo_hundredths_=value; pending_tempo_=0;
                if(transport_==VariableRatePatternTransportState::Stopped) (void)cursor_.Configure(value,sample_rate_hz_);
            }
            return VariableRatePatternPlayerStatus::Success;
        }
        void CancelTempo() noexcept { pending_tempo_=0; }

        // Single-owner audio contribution only, not tracker mute. Validate all
        // eight Q16 gains before committing. Valid updates take effect on the next
        // Render without altering voices, transport, position, deferred Stops or
        // staged previews. May be set before Configure; successful Configure resets
        // unity, failed Configure preserves them. Other transport/replacement calls
        // retain gains. Abrupt changes may click; smoothing is separate future work.
        [[nodiscard]] VariableRatePatternPlayerStatus SetChannelGains(const Pcm16ChannelGains& gains) noexcept {
            for (const auto gain : gains.channels)
                if (gain > kPcm16UnityGain) return VariableRatePatternPlayerStatus::InvalidGain;
            gains_ = gains;
            return VariableRatePatternPlayerStatus::Success;
        }
        Pcm16ChannelGains GetChannelGains() const noexcept { return gains_; }

        // Stopped-only metadata replacement, not live editing or publication.
        // Retains validated sample bindings, tempo and output rate. Caller-owned
        // immutable PCM/lifetime guarantees from Configure continue to apply.
        // Input must remain immutable during the call and not overlap this object.
        // Preflight two complete logical passes from cleared channel state: the
        // first establishes selections; the second checks loop continuation.
        // Instrument-only updates remain selections (no sample lookup until a note).
        // Bound: <=32 rows, <=256 events, <=4096 binding comparisons, fixed copies.
        // No rendering or cursor advancement. Existing block/tick/command limits
        // and arithmetic exhaustion remain Render-time checks, not acceptance guarantees.
        // Rejection preserves ALL state. Success copies even inactive metadata,
        // remains Stopped at zero, and clears voices, continuation and pending Stops.
        [[nodiscard]] VariableRatePatternPlayerStatus ReplacePattern(const RealtimePattern& pattern) noexcept
        {
            if (!configured_) return VariableRatePatternPlayerStatus::NotConfigured;
            if (transport_ != VariableRatePatternTransportState::Stopped) return VariableRatePatternPlayerStatus::InvalidTransportState;
            const auto validation = ValidatePattern(pattern);
            if (validation != VariableRatePatternPlayerStatus::Success) return validation;
            auto tentative = preparer_;
            tentative.Reset();
            const auto compatibility = ApplyLogicalRows(pattern, tentative, 2 * pattern.active_rows);
            if (compatibility != VariableRatePatternPlayerStatus::Success) return compatibility;
            preview_pending_ = false;
            pattern_ = pattern;
            Stop();
            return VariableRatePatternPlayerStatus::Success;
        }

        // Paused-only replacement; dimensions, tempo/rate/bindings are retained.
        // Validate two logical passes from cleared state, then rebuild NEW-pattern
        // continuation immediately before the reported row. Loop zero uses only
        // its prefix; later loops use one full pass plus that prefix, never elapsed
        // loops. No consumed tick means cleared state. Prepared audio commands are
        // discarded: no rendering, cursor movement or deferred scheduling here.
        // <=63 logical rows total (validation + reconstruction), fixed storage.
        // Success remains Paused/silent, preserving position and next sample;
        // failure preserves ALL state. PCM ownership is unchanged from Configure.
        [[nodiscard]] VariableRatePatternPlayerStatus ReplacePausedPattern(const RealtimePattern& pattern) noexcept {
            if (!configured_) return VariableRatePatternPlayerStatus::NotConfigured;
            if (transport_ != VariableRatePatternTransportState::Paused) return VariableRatePatternPlayerStatus::InvalidTransportState;
            if (pattern.active_rows != pattern_.active_rows || pattern.active_channels != pattern_.active_channels)
                return VariableRatePatternPlayerStatus::InvalidDimensions;
            const auto validation = ValidatePattern(pattern);
            if (validation != VariableRatePatternPlayerStatus::Success) return validation;
            auto tentative = preparer_;
            tentative.Reset();
            const auto compatibility = ApplyLogicalRows(pattern, tentative, 2 * pattern.active_rows);
            if (compatibility != VariableRatePatternPlayerStatus::Success) return compatibility;
            tentative.Reset();
            if (position_.valid) {
                const auto absolute_row = position_.absolute_tick / kTicksPerRow;
                const auto prefix = absolute_row % pattern.active_rows;
                const auto rows = (absolute_row / pattern.active_rows ? pattern.active_rows : 0) + prefix;
                const auto reconstruction = ApplyLogicalRows(pattern, tentative, rows);
                if (reconstruction != VariableRatePatternPlayerStatus::Success) return reconstruction;
            }
            preview_pending_ = false;
            pattern_ = pattern;
            preparer_ = tentative;
            renderer_.Reset();
            pending_ = {};
            return VariableRatePatternPlayerStatus::Success;
        }

        // Volatile working-pattern update, separate from publication. Caller keeps
        // input immutable/disjoint during this call. Same active dimensions only.
        // Complete two-pass validation from cleared state before acceptance.
        // Stopped/Paused use their existing replacement semantics. Playing stages
        // one fixed candidate; newer accepted candidates replace it. Apply at the
        // next unconsumed row-start tick (including tick zero), never mid-row.
        // Reconstruct NEW logical state before that absolute row: loop zero prefix,
        // later loops one complete pass + prefix. Do not synthesize past audio.
        // Old due end-of-row Stops execute BEFORE new row commands; cancel any
        // remaining old deferred work at application. Sounding voices otherwise
        // continue, including deletion of a note (deletion is not implicit OFF).
        // Pause retains staged work for Continue's replay boundary; Stop/Start
        // adopt it immediately while clearing playback. Configure/publication
        // supersede it. No baseline/persistence is owned by this player.
        // <=32 validation rows here, <=31 reconstruction rows once per Render.
        [[nodiscard]] VariableRatePatternPlayerStatus ReplaceWorkingPattern(const RealtimePattern& pattern) noexcept {
            if (!configured_) return VariableRatePatternPlayerStatus::NotConfigured;
            if (pattern.active_rows != pattern_.active_rows || pattern.active_channels != pattern_.active_channels)
                return VariableRatePatternPlayerStatus::InvalidDimensions;
            if (transport_ == VariableRatePatternTransportState::Stopped) return ReplacePattern(pattern);
            if (transport_ == VariableRatePatternTransportState::Paused) return ReplacePausedPattern(pattern);
            const auto validation = ValidatePattern(pattern);
            if (validation != VariableRatePatternPlayerStatus::Success) return validation;
            auto tentative = preparer_;
            tentative.Reset();
            const auto compatibility = ApplyLogicalRows(pattern, tentative, 2 * pattern.active_rows);
            if (compatibility != VariableRatePatternPlayerStatus::Success) return compatibility;
            preview_pattern_ = pattern;
            preview_pending_ = true;
            return VariableRatePatternPlayerStatus::Success;
        }
        bool HasPendingWorkingPattern() const noexcept { return preview_pending_; }

        // Always restart at tick/sample zero, including repeated Start.
        [[nodiscard]] VariableRatePatternPlayerStatus Start() noexcept
        {
            if (!configured_) return VariableRatePatternPlayerStatus::NotConfigured;
            Stop();
            transport_ = VariableRatePatternTransportState::Playing;
            return VariableRatePatternPlayerStatus::Success;
        }
        // Idempotent, also valid before configuration. Retain pattern/bindings.
        void Stop() noexcept
        {
            if (preview_pending_) pattern_ = preview_pattern_;
            preview_pending_ = false;
            pending_ = {};
            renderer_.Reset();
            preparer_.Reset();
            pending_tempo_=0;
            if (configured_) (void)cursor_.Configure(tempo_hundredths_,sample_rate_hz_);
            transport_ = VariableRatePatternTransportState::Stopped;
            next_sample_ = 0;
            position_ = {};
        }
        bool IsRunning() const noexcept { return transport_ == VariableRatePatternTransportState::Playing; }
        VariableRatePatternTransportState GetTransportState() const noexcept {
            return transport_;
        }
        // Silence voices, retain logical note/instrument continuation and consumed
        // position. No timeline advancement while paused; audio owner only.
        [[nodiscard]] VariableRatePatternPlayerStatus Pause() noexcept {
            if (!configured_) return VariableRatePatternPlayerStatus::NotConfigured;
            if (transport_ == VariableRatePatternTransportState::Paused) return VariableRatePatternPlayerStatus::Success;
            if (transport_ != VariableRatePatternTransportState::Playing) return VariableRatePatternPlayerStatus::InvalidTransportState;
            if(pending_tempo_) { tempo_hundredths_=pending_tempo_; pending_tempo_=0; }
            pending_ = {};
            renderer_.Reset(); transport_ = VariableRatePatternTransportState::Paused;
            return VariableRatePatternPlayerStatus::Success;
        }
        // Replay the reported absolute row from its start, retaining its loop.
        // Before any consumed tick, replay row zero. Bounded cursor positioning
        // consumes that row-start exactly once on Render, without catch-up work.
        // Retain logical continuation; errors preserve ALL player state.
        [[nodiscard]] VariableRatePatternPlayerStatus Continue() noexcept {
            if (!configured_) return VariableRatePatternPlayerStatus::NotConfigured;
            if (transport_ != VariableRatePatternTransportState::Paused) return VariableRatePatternPlayerStatus::InvalidTransportState;
            std::uint64_t tick = 0;
            if (position_.valid) {
                const auto row = position_.absolute_tick / kTicksPerRow;
                // Division first guarantees this product cannot exceed the input.
                tick = row * kTicksPerRow;
            }
            auto cursor = cursor_;
            std::uint64_t sample = 0;
            const auto status = cursor.ResetAtTick(tick, sample);
            if (status != TickCursorStatus::Success) return VariableRatePatternPlayerStatus::RangeOverflow;
            if(cursor.GetTempo()!=tempo_hundredths_) (void)cursor.BeginSegment(tempo_hundredths_,sample_rate_hz_,tick,sample);
            cursor_ = cursor; next_sample_ = sample;
            transport_ = VariableRatePatternTransportState::Playing;
            return VariableRatePatternPlayerStatus::Success;
        }
        std::uint64_t GetNextSamplePosition() const noexcept { return next_sample_; }
        // Latest tick consumed in a successfully rendered half-open audio span,
        // including empty rows/non-row-start ticks. Not the next block position,
        // a physical output timestamp or a host-extrapolated position. Zero-frame
        // and rejected renders preserve it; tick-free blocks retain it. Configure,
        // Start and Stop clear validity until a tick is successfully consumed.
        VariableRateConsumedPatternPosition GetPlaybackPosition() const noexcept { return position_; }
        std::uint32_t GetActiveRows() const noexcept { return configured_ ? pattern_.active_rows : 0; }
        std::uint32_t GetActiveChannels() const noexcept { return configured_ ? pattern_.active_channels : 0; }

        // Zero frames always succeeds as a no-op (nullptr allowed). Positive
        // spans require configuration, <=128 frames and a destination. Stopped
        // rendering writes silence and never advances logical playback.
        // Running blocks are consecutive [next_sample, next_sample+frame_count).
        // Use tentative cursor/preparer/renderer copies and internal scratch:
        // ALL errors preserve running/position, channel/voice state, deferred Stops and caller
        // output. Scratch need not be preserved. Success commits all state/output.
        // At most 512 emitted ticks plus one completion probe; excess work and
        // >16 prepared commands explicitly reject the WHOLE block, never truncate.
        // Event order, coincident ticks, half-open boundaries, loop continuation
        // and 0.01 BPM precision come from the existing components.
        // Bound: 513 cursor pulls (each <=64 conversion iterations), <=512*8*16
        // binding comparisons, <=16 audio commands, 8*128 channel frames + mixing
        // and output copying, plus <=513*64 pending-entry checks and <=512*8
        // absolute OFF-boundary conversions. A preview adds <=31 logical row preparations
        // once per block and one fixed pattern copy. Fixed 8x128 channel and 128 mono scratch storage.
        // Callers MUST handle rejection; no firmware underrun policy or measured
        // Teensy processing budget is established. A smaller retry may succeed;
        // unresolved bindings require reconfiguration or Stop.
        [[nodiscard]] VariableRatePatternPlayerStatus Render(std::int16_t* destination,
            std::uint64_t frame_count) noexcept
        {
            if (frame_count == 0) return VariableRatePatternPlayerStatus::Success;
            if (!configured_) return VariableRatePatternPlayerStatus::NotConfigured;
            if (frame_count > kVariableRatePatternFrameCapacity) return VariableRatePatternPlayerStatus::InvalidFrameCount;
            if (destination == nullptr) return VariableRatePatternPlayerStatus::InvalidDestination;
            const auto count = static_cast<std::size_t>(frame_count);
            if (transport_ != VariableRatePatternTransportState::Playing)
            {
                for (std::size_t i = 0; i < count; ++i) destination[i] = 0;
                return VariableRatePatternPlayerStatus::Success;
            }
            auto cursor = cursor_;
            auto tempo=tempo_hundredths_, next_tempo=pending_tempo_;
            auto preparer = preparer_;
            auto renderer = renderer_;
            auto position = position_;
            auto pending = pending_;
            auto working_pattern = pattern_;
            bool preview_pending = preview_pending_;
            bool consumed_tick = false;
            if (frame_count > std::numeric_limits<std::uint64_t>::max() - next_sample_)
                return VariableRatePatternPlayerStatus::RangeOverflow;
            const auto begin = cursor.BeginBlock(next_sample_, frame_count);
            if (begin == TickCursorStatus::RangeOverflow) return VariableRatePatternPlayerStatus::RangeOverflow;
            if (begin == TickCursorStatus::ArithmeticExhausted) return VariableRatePatternPlayerStatus::ArithmeticExhausted;
            if (begin != TickCursorStatus::Success) return VariableRatePatternPlayerStatus::ComponentError;
            VariableRateVoiceCommandBatch commands{};
            // At most eight pending entries. Emit by absolute sample, then channel;
            // consume before row commands at a tie. No backlog scan or allocation.
            const auto flush = [&](std::uint64_t limit, bool inclusive) {
                for (std::size_t n = 0; n < kRealtimePatternChannelCapacity; ++n) {
                    std::size_t selected = kRealtimePatternChannelCapacity;
                    for (std::size_t c = 0; c < kRealtimePatternChannelCapacity; ++c)
                        if (pending.entries[c].active &&
                            (pending.entries[c].sample < limit || (inclusive && pending.entries[c].sample == limit)) &&
                            (selected == kRealtimePatternChannelCapacity ||
                             pending.entries[c].sample < pending.entries[selected].sample)) selected = c;
                    if (selected == kRealtimePatternChannelCapacity) break;
                    if (pending.entries[selected].sample < next_sample_) return VariableRatePatternPlayerStatus::ComponentError;
                    if (commands.count == kVariableRateVoiceCommandCapacity) return VariableRatePatternPlayerStatus::CommandCapacityExceeded;
                    commands.commands[commands.count++] = {static_cast<std::uint8_t>(selected),
                        pending.entries[selected].sample - next_sample_, VariableRateVoiceAction::Stop, {}};
                    pending.entries[selected].active = false;
                }
                return VariableRatePatternPlayerStatus::Success;
            };
            for (std::size_t emitted = 0; ; ++emitted)
            {
                MusicalTick tick;
                const auto status = cursor.Pull(tick);
                if (status == TickCursorStatus::BlockComplete) break;
                if (status == TickCursorStatus::ArithmeticExhausted) return VariableRatePatternPlayerStatus::ArithmeticExhausted;
                if (status != TickCursorStatus::Tick) return VariableRatePatternPlayerStatus::ComponentError;
                if (emitted == kVariableRatePatternTickBudget) return VariableRatePatternPlayerStatus::TickBudgetExceeded;
                const auto flushed = flush(tick.sample_position, true);
                if (flushed != VariableRatePatternPlayerStatus::Success) return flushed;
                if(next_tempo && tick.tick_index%kTicksPerRow==0) {
                    if(cursor.SetTempoAtTick(tick.tick_index,next_tempo)!=TickCursorStatus::Success)
                        return VariableRatePatternPlayerStatus::ComponentError;
                    tempo=next_tempo; next_tempo=0;
                    for(auto& stop:pending.entries) if(stop.active &&
                        cursor.SampleAtTick(stop.tick,stop.sample)!=TickCursorStatus::Success)
                        return VariableRatePatternPlayerStatus::RangeOverflow;
                }
                if (preview_pending && tick.tick_index % kTicksPerRow == 0) {
                    auto rebuilt = preparer;
                    rebuilt.Reset();
                    const auto absolute_row = tick.tick_index / kTicksPerRow;
                    const auto prefix = absolute_row % preview_pattern_.active_rows;
                    const auto rows = (absolute_row / preview_pattern_.active_rows ? preview_pattern_.active_rows : 0) + prefix;
                    const auto reconstruction = ApplyLogicalRows(preview_pattern_, rebuilt, rows);
                    if (reconstruction != VariableRatePatternPlayerStatus::Success) return reconstruction;
                    working_pattern = preview_pattern_;
                    preparer = rebuilt;
                    pending = {}; // Due old Stops already precede new row triggers.
                    preview_pending = false;
                }
                position.absolute_tick = tick.tick_index;
                consumed_tick = true;
                RowEventBatch rows{};
                if (GenerateRowEvents(tick, working_pattern, rows) != RowEventStatus::Success)
                    return VariableRatePatternPlayerStatus::ComponentError;
                for (std::size_t i = 0; i < rows.count; ++i) {
                    const auto& event = rows.events[i];
                    if (event.note != NOTE_OFF || event.note_off_timing != NoteOffTiming::EndOfPosition) continue;
                    const auto absolute_row = event.tick_index / kTicksPerRow;
                    if (absolute_row >= UINT64_MAX / kTicksPerRow) return VariableRatePatternPlayerStatus::RangeOverflow;
                    std::uint64_t sample = 0;
                    if (cursor.SampleAtTick((absolute_row + 1) * kTicksPerRow, sample) != TickCursorStatus::Success) return VariableRatePatternPlayerStatus::RangeOverflow;
                    // A channel cannot have two row-end Stops outstanding: flush
                    // its previous boundary before consuming this row.
                    if (pending.entries[event.channel].active) return VariableRatePatternPlayerStatus::ComponentError;
                    pending.entries[event.channel] = {true, sample, (absolute_row+1)*kTicksPerRow};
                }
                VariableRateVoiceCommandBatch prepared{};
                const auto preparation = preparer.Prepare(rows, prepared);
                if (preparation != VariableSampleCommandStatus::Success) return SampleStatus(preparation);
                if (prepared.count > kVariableRateVoiceCommandCapacity - commands.count)
                    return VariableRatePatternPlayerStatus::CommandCapacityExceeded;
                for (std::size_t i = 0; i < prepared.count; ++i)
                    commands.commands[commands.count++] = prepared.commands[i];
            }
            const auto flushed = flush(next_sample_ + frame_count, false);
            if (flushed != VariableRatePatternPlayerStatus::Success) return flushed;
            if (consumed_tick)
            {
                PatternPosition mapped;
                if (TickToPatternPosition(position.absolute_tick, pattern_.active_rows, mapped) !=
                    PatternPositionStatus::Success) return VariableRatePatternPlayerStatus::ComponentError;
                position = {true, position.absolute_tick, mapped.pattern_row, mapped.loop_index};
            }
            VariableRateVoiceOutputs outputs;
            Pcm16MixInputs inputs;
            for (std::size_t channel = 0; channel < kRealtimePatternChannelCapacity; ++channel)
            {
                outputs.channels[channel] = channel_scratch_[channel];
                inputs.channels[channel] = channel_scratch_[channel];
            }
            if (renderer.Render(outputs, frame_count, commands) != VariableRateBlockStatus::Success)
                return VariableRatePatternPlayerStatus::ComponentError;
            if (MixPcm16MonoWithGains(inputs, gains_, mono_scratch_, frame_count) != Pcm16GainMixStatus::Success)
                return VariableRatePatternPlayerStatus::ComponentError;
            pattern_ = working_pattern;
            preview_pending_ = preview_pending;
            pending_ = pending;
            cursor_ = cursor;
            tempo_hundredths_=tempo; pending_tempo_=next_tempo;
            preparer_ = preparer;
            renderer_ = renderer;
            position_ = position;
            // BeginBlock checked the absolute block end for uint64_t overflow.
            next_sample_ += frame_count;
            for (std::size_t i = 0; i < count; ++i) destination[i] = mono_scratch_[i];
            return VariableRatePatternPlayerStatus::Success;
        }
    private:
        static VariableRatePatternPlayerStatus ApplyLogicalRows(const RealtimePattern& pattern,
            VariableRateSampleCommandPreparer& preparer, std::uint64_t row_count) noexcept {
            for (std::uint64_t row=0; row<row_count; ++row) {
                RowEventBatch events{};
                const MusicalTick tick{row * kTicksPerRow, 0, 0};
                if (GenerateRowEvents(tick,pattern,events) != RowEventStatus::Success)
                    return VariableRatePatternPlayerStatus::ComponentError;
                VariableRateVoiceCommandBatch ignored{};
                const auto result = preparer.Prepare(events,ignored);
                if (result != VariableSampleCommandStatus::Success) return SampleStatus(result);
            }
            return VariableRatePatternPlayerStatus::Success;
        }

        static VariableRatePatternPlayerStatus ValidatePattern(const RealtimePattern& pattern) noexcept
        {
            if (pattern.active_rows == 0 || pattern.active_rows > kRealtimePatternRowCapacity ||
                pattern.active_channels == 0 || pattern.active_channels > kRealtimePatternChannelCapacity)
                return VariableRatePatternPlayerStatus::InvalidDimensions;
            for (std::size_t row = 0; row < pattern.active_rows; ++row)
                for (std::size_t channel = 0; channel < pattern.active_channels; ++channel)
                {
                    const Note note = pattern.cells[row][channel].note;
                    if (!ValidNoteOffTiming(pattern.note_off_timing[row][channel]))
                        return VariableRatePatternPlayerStatus::InvalidNoteOffTiming;
                    if (!IsPatternNote(note))
                        return VariableRatePatternPlayerStatus::InvalidNote;
                }
            return VariableRatePatternPlayerStatus::Success;
        }
        static VariableRatePatternPlayerStatus SampleStatus(VariableSampleCommandStatus status) noexcept
        {
            switch (status)
            {
            case VariableSampleCommandStatus::InvalidSampleRate: return VariableRatePatternPlayerStatus::InvalidSampleRate;
            case VariableSampleCommandStatus::InvalidBindingCount: return VariableRatePatternPlayerStatus::InvalidBindingCount;
            case VariableSampleCommandStatus::ReservedInstrument: return VariableRatePatternPlayerStatus::ReservedInstrument;
            case VariableSampleCommandStatus::DuplicateInstrument: return VariableRatePatternPlayerStatus::DuplicateInstrument;
            case VariableSampleCommandStatus::InvalidReferenceNote: return VariableRatePatternPlayerStatus::InvalidReferenceNote;
            case VariableSampleCommandStatus::InvalidSample: return VariableRatePatternPlayerStatus::InvalidSample;
            case VariableSampleCommandStatus::SampleRateMismatch: return VariableRatePatternPlayerStatus::SampleRateMismatch;
            case VariableSampleCommandStatus::RangeOverflow: return VariableRatePatternPlayerStatus::RangeOverflow;
            case VariableSampleCommandStatus::MissingInstrument: return VariableRatePatternPlayerStatus::MissingInstrument;
            case VariableSampleCommandStatus::UnknownInstrument: return VariableRatePatternPlayerStatus::UnknownInstrument;
            case VariableSampleCommandStatus::InvalidNoteOffTiming: return VariableRatePatternPlayerStatus::InvalidNoteOffTiming;
            default: return VariableRatePatternPlayerStatus::ComponentError;
            }
        }
        // Test-only access to otherwise impractical uint64_t exhaustion boundaries.
        friend struct VariableRatePatternPlayerTestAccess;
        struct DeferredStop { bool active = false; std::uint64_t sample = 0, tick = 0; };
        struct PendingStops { DeferredStop entries[kRealtimePatternChannelCapacity]{}; } pending_;
        std::uint32_t tempo_hundredths_ = 0, sample_rate_hz_ = 0, pending_tempo_=0;
        Pcm16ChannelGains gains_;
        RealtimePattern pattern_;
        RealtimePattern preview_pattern_; // Single bounded pending volatile candidate.
        bool preview_pending_ = false;
        MusicalTickCursor cursor_;
        VariableRateSampleCommandPreparer preparer_;
        VariableRateVoiceBlockRenderer renderer_;
        bool configured_ = false;
        VariableRatePatternTransportState transport_ = VariableRatePatternTransportState::Stopped;
        std::uint64_t next_sample_ = 0;
        VariableRateConsumedPatternPosition position_;
        std::int16_t channel_scratch_[kRealtimePatternChannelCapacity][kVariableRatePatternFrameCapacity]{};
        std::int16_t mono_scratch_[kVariableRatePatternFrameCapacity]{};
    };
}

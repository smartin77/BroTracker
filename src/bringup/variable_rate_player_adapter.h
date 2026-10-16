#pragma once
#include "core/playback/native_rate_pattern_player.h" // Existing protocol-facing types, no native instance.
#include "core/playback/variable_rate_pattern_player.h"
namespace BroTracker {
// Compile-time facade for the shared control/protocol. No virtual dispatch or heap.
// Owns exactly one variable-rate player. Protocol statuses remain compatible;
// InvalidReferenceNote maps to the existing binding-note validation status.
class VariableRateBringUpPlayer {
public:
    PatternPlayerStatus Configure(std::uint32_t tempo_hundredths, std::uint32_t rate,
        const RealtimePattern& pattern, const VariableRateSampleBindings& bindings) noexcept {
        return Map(player_.Configure(tempo_hundredths,rate,pattern,bindings));
    }
    PatternPlayerStatus Start() noexcept { return Map(player_.Start()); }
    void Stop() noexcept { player_.Stop(); }
    PatternPlayerStatus Pause() noexcept { return Map(player_.Pause()); }
    PatternPlayerStatus Continue() noexcept { return Map(player_.Continue()); }
    PatternPlayerStatus ReplacePattern(const RealtimePattern& pattern) noexcept { return Map(player_.ReplacePattern(pattern)); }
    PatternPlayerStatus ReplacePausedPattern(const RealtimePattern& pattern) noexcept { return Map(player_.ReplacePausedPattern(pattern)); }
    PatternPlayerStatus ReplaceWorkingPattern(const RealtimePattern& pattern) noexcept { return Map(player_.ReplaceWorkingPattern(pattern)); }
    PatternPlayerStatus SetChannelGains(const Pcm16ChannelGains& gains) noexcept { return Map(player_.SetChannelGains(gains)); }
    Pcm16ChannelGains GetChannelGains() const noexcept { return player_.GetChannelGains(); }
    PatternPlayerStatus Render(std::int16_t* output,std::uint64_t frames) noexcept { return Map(player_.Render(output,frames)); }
    bool IsRunning() const noexcept { return player_.IsRunning(); }
    bool HasPendingWorkingPattern() const noexcept { return player_.HasPendingWorkingPattern(); }
    PatternTransportState GetTransportState() const noexcept {
        switch(player_.GetTransportState()) {
        case VariableRatePatternTransportState::Playing: return PatternTransportState::Playing;
        case VariableRatePatternTransportState::Paused: return PatternTransportState::Paused;
        default: return PatternTransportState::Stopped;
        }
    }
    ConsumedPatternPosition GetPlaybackPosition() const noexcept {
        const auto p=player_.GetPlaybackPosition();
        return {p.valid,p.absolute_tick,p.pattern_row,p.loop_index};
    }
    std::uint64_t GetNextSamplePosition() const noexcept { return player_.GetNextSamplePosition(); }
    std::uint32_t GetActiveRows() const noexcept { return player_.GetActiveRows(); }
    std::uint32_t GetActiveChannels() const noexcept { return player_.GetActiveChannels(); }
private:
    static PatternPlayerStatus Map(VariableRatePatternPlayerStatus status) noexcept {
        switch(status) {
        case VariableRatePatternPlayerStatus::Success: return PatternPlayerStatus::Success;
        case VariableRatePatternPlayerStatus::NotConfigured: return PatternPlayerStatus::NotConfigured;
        case VariableRatePatternPlayerStatus::InvalidTempo: return PatternPlayerStatus::InvalidTempo;
        case VariableRatePatternPlayerStatus::InvalidSampleRate: return PatternPlayerStatus::InvalidSampleRate;
        case VariableRatePatternPlayerStatus::InvalidDimensions: return PatternPlayerStatus::InvalidDimensions;
        case VariableRatePatternPlayerStatus::InvalidNote: return PatternPlayerStatus::InvalidNote;
        case VariableRatePatternPlayerStatus::InvalidBindingCount: return PatternPlayerStatus::InvalidBindingCount;
        case VariableRatePatternPlayerStatus::ReservedInstrument: return PatternPlayerStatus::ReservedInstrument;
        case VariableRatePatternPlayerStatus::DuplicateInstrument: return PatternPlayerStatus::DuplicateInstrument;
        case VariableRatePatternPlayerStatus::InvalidReferenceNote: return PatternPlayerStatus::InvalidNativeRateNote;
        case VariableRatePatternPlayerStatus::InvalidSample: return PatternPlayerStatus::InvalidSample;
        case VariableRatePatternPlayerStatus::SampleRateMismatch: return PatternPlayerStatus::SampleRateMismatch;
        case VariableRatePatternPlayerStatus::RangeOverflow: return PatternPlayerStatus::RangeOverflow;
        case VariableRatePatternPlayerStatus::InvalidDestination: return PatternPlayerStatus::InvalidDestination;
        case VariableRatePatternPlayerStatus::InvalidFrameCount: return PatternPlayerStatus::InvalidFrameCount;
        case VariableRatePatternPlayerStatus::MissingInstrument: return PatternPlayerStatus::MissingInstrument;
        case VariableRatePatternPlayerStatus::UnknownInstrument: return PatternPlayerStatus::UnknownInstrument;
        case VariableRatePatternPlayerStatus::ArithmeticExhausted: return PatternPlayerStatus::ArithmeticExhausted;
        case VariableRatePatternPlayerStatus::TickBudgetExceeded: return PatternPlayerStatus::TickBudgetExceeded;
        case VariableRatePatternPlayerStatus::CommandCapacityExceeded: return PatternPlayerStatus::CommandCapacityExceeded;
        case VariableRatePatternPlayerStatus::ComponentError: return PatternPlayerStatus::ComponentError;
        case VariableRatePatternPlayerStatus::InvalidTransportState: return PatternPlayerStatus::InvalidTransportState;
        case VariableRatePatternPlayerStatus::InvalidNoteOffTiming: return PatternPlayerStatus::InvalidNoteOffTiming;
        case VariableRatePatternPlayerStatus::InvalidGain: return PatternPlayerStatus::InvalidGain;
        }
        return PatternPlayerStatus::ComponentError;
    }
    VariableRatePatternPlayer player_;
};
}

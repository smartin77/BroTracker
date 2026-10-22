#pragma once
#include "core/playback/native_rate_sample_commands.h"
#include "core/playback/variable_rate_sample_commands.h"

#ifndef BROTRACKER_PATTERN_FIXTURE_ROWS
#define BROTRACKER_PATTERN_FIXTURE_ROWS 32
#endif
namespace BroTracker {
// Development/test2.wav/test3.wav workload only; new projects retain kDefaultTempo (12500).
constexpr std::uint32_t kDevelopmentFixtureTempo = 15500;
constexpr std::uint8_t kPatternBringUpFixtureRows = BROTRACKER_PATTERN_FIXTURE_ROWS;
static_assert(BROTRACKER_PATTERN_FIXTURE_ROWS == 16 || BROTRACKER_PATTERN_FIXTURE_ROWS == 32 ||
    BROTRACKER_PATTERN_FIXTURE_ROWS == 64, "Fixture rows must be 16, 32 or 64");
// Boot-time fixed metadata selection. PCM remains immutable and caller-owned.
// No loading, clock ownership or dependency on RAM placement.
inline RealtimePattern MakeBeeperPattern(std::uint8_t rows = kPatternBringUpFixtureRows) noexcept {
    RealtimePattern pattern;
    pattern.active_rows = rows; pattern.active_channels = 2;
    pattern.cells[0][0] = {60,0};
    pattern.cells[2][1] = {62,1};
    pattern.cells[4][0] = {60,kNoInstrumentUpdate};
    pattern.cells[5][0] = {60,kNoInstrumentUpdate};
    pattern.cells[6][1] = {NOTE_OFF,kNoInstrumentUpdate};
    pattern.cells[8][0] = {60,0}; pattern.cells[8][1] = {62,1};
    pattern.cells[10][0] = {NOTE_OFF,kNoInstrumentUpdate};
    pattern.cells[12][1] = {62,kNoInstrumentUpdate};
    pattern.cells[13][1] = {62,kNoInstrumentUpdate};
    pattern.cells[14][0] = {NOTE_OFF,kNoInstrumentUpdate};
    pattern.cells[14][1] = {NOTE_OFF,kNoInstrumentUpdate};
    return pattern;
}
// Development sequence: explicit cell instruments, never channel-based routing.
inline RealtimePattern MakeDevelopmentPattern(std::uint8_t rows=kPatternBringUpFixtureRows) noexcept {
    RealtimePattern pattern; pattern.active_rows=rows; pattern.active_channels=8;
    struct Entry { std::uint8_t row,note; };
    constexpr Entry first[]={{0,60},{4,64},{7,67},{10,64},{16,65},{20,69},{23,72},{28,67}};
    // B natural (German H) is raw 59, not B-flat/raw 58.
    constexpr Entry second[]={{0,48},{8,55},{12,59},{16,53},{24,60},{28,55}};
    for(unsigned base=0;base<rows && base<kRealtimePatternRowCapacity;base+=32) {
        for(const auto entry:first) if(base+entry.row<rows && base+entry.row<kRealtimePatternRowCapacity)
            pattern.cells[base+entry.row][0]={entry.note,2};
        for(const auto entry:second) if(base+entry.row<rows && base+entry.row<kRealtimePatternRowCapacity)
            pattern.cells[base+entry.row][1]={entry.note,2};
    }
    return pattern;
}

template<class Bindings>
struct BasicPatternBringUpFixture {
    RealtimePattern pattern;
    Bindings bindings;
};
using PatternBringUpFixture = BasicPatternBringUpFixture<NativeRateSampleBindings>;
// Input is the boot fixture's two validated beeper bindings (IDs 0/1).
// Invalid/unavailable prepared views retain the original two-channel fixture.
template<class Bindings>
inline BasicPatternBringUpFixture<Bindings> SelectPatternBringUpFixture(
    const Bindings& beepers, Pcm16MonoView prepared, bool enable_sd) noexcept {
    BasicPatternBringUpFixture<Bindings> fixture{MakeBeeperPattern(), beepers};
    RamSampleVoice validator;
    (void)validator.Configure(44100);
    if (enable_sd && validator.Trigger(prepared) == RamVoiceStatus::Success) {
    fixture.pattern.active_channels = 3;
    fixture.pattern.cells[0][2] = {60, 2}; // C-3 is a trigger reference, not measured pitch.
    fixture.bindings.count = 3;
    fixture.bindings.bindings[2] = {2, 60, prepared};
    }
    return fixture;
}
// UI IDs are one-based; raw bytes remain unchanged: 0=Sample 01,
// 1=MIDI 02, 2=Synth 03, 3=Sample 04 (test3.wav).
// Sample 01 uses the existing test2.wav preparation; test.wav remains inspected/deferred.
inline BasicPatternBringUpFixture<VariableRateSampleBindings> SelectDevelopmentFixture(
    Pcm16MonoView test2, Pcm16MonoView test3={}) noexcept {
    BasicPatternBringUpFixture<VariableRateSampleBindings> result{MakeDevelopmentPattern(),{}};
    auto& bindings=result.bindings;
    bindings.bindings[bindings.count++]={1,0,{},InstrumentType::MIDI};
    bindings.bindings[bindings.count++]={2,0,{},InstrumentType::Synth};
    RamSampleVoice validator; (void)validator.Configure(44100);
    if(validator.Trigger(test2)==RamVoiceStatus::Success)
        bindings.bindings[bindings.count++]={0,60,test2,InstrumentType::Sample};
    if(validator.Trigger(test3)==RamVoiceStatus::Success)
        bindings.bindings[bindings.count++]={3,60,test3,InstrumentType::Sample};
    return result;
}

}

#pragma once
#include "core/playback/native_rate_sample_commands.h"

#ifndef BROTRACKER_PATTERN_FIXTURE_ROWS
#define BROTRACKER_PATTERN_FIXTURE_ROWS 32
#endif
namespace BroTracker {
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
    constexpr std::uint8_t first[]={60,64,67,72,67,64,62,65};
    constexpr std::uint8_t second[]={62,65,69,74,69,65,64,67};
    for(unsigned row=0;row<rows && row<kRealtimePatternRowCapacity;++row) {
        pattern.cells[row][0]={first[row%8],0};
        pattern.cells[row][1]={second[row%8],1};
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
// Independent optional test2/test3 views. ID 2/test.wav remains unbound.
template<class Bindings>
inline BasicPatternBringUpFixture<Bindings> SelectDevelopmentFixture(
    const Bindings& beepers,Pcm16MonoView test2,Pcm16MonoView test3={}) noexcept {
    BasicPatternBringUpFixture<Bindings> result{MakeDevelopmentPattern(),beepers};
    RamSampleVoice validator; (void)validator.Configure(44100);
    if(validator.Trigger(test2)==RamVoiceStatus::Success) {
        result.bindings.count=3; result.bindings.bindings[2]={3,60,test2};
    }
    if(validator.Trigger(test3)==RamVoiceStatus::Success) {
        result.bindings.bindings[result.bindings.count++]={4,60,test3};
    }
    return result;
}

}

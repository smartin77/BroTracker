#pragma once
#include "core/playback/native_rate_sample_commands.h"

namespace BroTracker {
// Boot-time fixed metadata selection. PCM remains immutable and caller-owned.
// No loading, clock ownership or dependency on RAM placement.
inline RealtimePattern MakeBeeperPattern() noexcept {
    RealtimePattern pattern;
    pattern.active_rows = 16; pattern.active_channels = 2;
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
}

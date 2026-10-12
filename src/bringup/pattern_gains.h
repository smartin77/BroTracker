#pragma once
#include "pattern_snapshot.h"
#include "core/audio/pcm16_gain_mixer.h"
namespace BroTracker {
constexpr std::size_t kPatternGainLineCapacity = 96;
static_assert(kPatternGainLineCapacity >= sizeof("BTGAIN1 STATE 1 ") + 2*11 + 8*6,
    "Gain state fits maximum identities, unity values, LF and NUL");
struct PatternGainMessage { std::uint32_t id=0; Pcm16ChannelGains gains; };
inline bool ParsePatternGains(const char* line,std::size_t length,PatternGainMessage& output) noexcept {
    std::uint32_t fields[9]{};
    if(!SnapshotFields(line,length,"BTGAIN1 SET 1 ",fields,9) || !fields[0]) return false;
    PatternGainMessage parsed; parsed.id=fields[0];
    for(unsigned c=0;c<kRealtimePatternChannelCapacity;++c) {
        if(fields[c+1]>kPcm16UnityGain) return false;
        parsed.gains.channels[c]=fields[c+1];
    }
    output=parsed; return true;
}
// Main-loop formatting only. Applied ID zero means configured defaults; NEXT zero
// explicitly means identity exhaustion. Coherent gain/ID snapshot comes from owner.
inline bool FormatPatternGainState(const Pcm16ChannelGains& gains,std::uint32_t applied_id,
    std::uint32_t next_id,char* line,std::size_t capacity) noexcept {
    if(!line || !capacity) return false;
    const int n=std::snprintf(line,capacity,"BTGAIN1 STATE 1 %" PRIu32 " %" PRIu32
        " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 "\n",
        applied_id,next_id,gains.channels[0],gains.channels[1],gains.channels[2],gains.channels[3],
        gains.channels[4],gains.channels[5],gains.channels[6],gains.channels[7]);
    return n>=0 && static_cast<std::size_t>(n)<capacity;
}
}

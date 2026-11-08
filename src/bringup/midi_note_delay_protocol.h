#pragma once
#include "pattern_snapshot.h"
namespace BroTracker {
constexpr std::uint32_t kMaximumMidiNoteDelayMs=160;
struct MidiDelayRequest {std::uint32_t id=0,ms=0;bool set=false;};
inline bool ParseMidiDelayRequest(const char* line,std::size_t size,MidiDelayRequest& out) {
    std::uint32_t f[2]{};MidiDelayRequest next;
    if(SnapshotFields(line,size,"BTMIDIDELAY1 GET 1 ",f,1))next={f[0],0,false};
    else if(SnapshotFields(line,size,"BTMIDIDELAY1 SET 1 ",f,2))next={f[0],f[1],true};
    else return false;
    if(!next.id)return false;
    out=next;return true;
}
}

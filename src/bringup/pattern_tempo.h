#pragma once
#include "pattern_snapshot.h"
#include "core/tempo.h"
namespace BroTracker {
struct TempoState {
    std::uint32_t settled=0,applied_id=0,next_id=1,pending_id=0,tempo=kDefaultTempo,published=kDefaultTempo;
};
inline bool ParseTempoState(const char* line,std::size_t size,TempoState& state) noexcept {
    std::uint32_t f[7]{};
    if(!SnapshotFields(line,size,"BTTEMPO1 STATE ",f,7) || f[0]!=1 || !ValidInteractiveTempo(f[5]) ||
        !ValidInteractiveTempo(f[6]) || f[2]>f[1] || (f[3] && f[3]<=f[1]) || (f[4] && f[4]<=f[1])) return false;
    state={f[1],f[2],f[3],f[4],f[5],f[6]}; return true;
}
inline bool FormatTempoState(const TempoState& state,char* line,std::size_t size) noexcept {
    const int n=std::snprintf(line,size,"BTTEMPO1 STATE 1 %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 "\n",
        state.settled,state.applied_id,state.next_id,state.pending_id,state.tempo,state.published);
    return n>=0 && static_cast<std::size_t>(n)<size;
}
}

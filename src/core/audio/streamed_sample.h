#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
namespace BroTracker {
// Immutable prepared source. File ownership belongs to the foreground adapter.
// Ring spans are published only while the audio consumer is excluded.
constexpr std::uint32_t kStreamRingFrames=4096, kStreamStartFrames=2048, kStreamReadFrames=2048;
struct StreamedSample;
struct StreamedVoiceBuffer {
    std::int16_t* ring=nullptr;
    std::uint64_t generation=0;
    const StreamedSample* sample=nullptr;
    std::uint32_t begin=0,end=0;
};
struct StreamedSample {
    const std::int16_t* start=nullptr;
    std::uint32_t frames=0;
    StreamedVoiceBuffer* voices=nullptr; // eight independent rings, shared start
};
struct StreamedVoiceSnapshot {
    const StreamedSample* sample=nullptr;
    std::uint64_t generation=0,step=0;
    std::uint32_t position=0,phase=0;
    std::uint32_t underruns=0,eofs=0,min_ready=UINT32_MAX,hash=2166136261u;
};
// Source frames determine ring capacity; output frames determine urgency.
// Higher pitch may make a larger ready span more urgent than a smaller one.
inline std::uint64_t StreamRefillUrgency(const StreamedVoiceSnapshot& state,
    const StreamedVoiceBuffer& ready) noexcept {
    if(!state.sample || !state.step || ready.end>=state.sample->frames) return UINT64_MAX;
    const auto consumed=std::max(state.position,kStreamStartFrames);
    const auto used=ready.end>consumed ? ready.end-consumed : 0;
    const auto start=state.position<kStreamStartFrames ? std::min(kStreamStartFrames,state.sample->frames)-state.position : 0;
    const auto available=used+start;
    const auto until=(std::uint64_t(available)<<32)/state.step;
    if(used>kStreamRingFrames || kStreamRingFrames-used<kStreamReadFrames ||
        (available>kStreamRingFrames/2 && until>kStreamRingFrames/2)) return UINT64_MAX;
    return until;
}
// Caller excludes the audio consumer during publication; a stale completion
// can never expose its bytes to a new note or replacement source.
inline bool PublishStreamRead(StreamedVoiceBuffer& ready,const StreamedVoiceSnapshot& request,
    std::uint32_t at,std::uint32_t count) noexcept {
    if(ready.generation!=request.generation || ready.sample!=request.sample) return false;
    if(at>ready.end) ready.begin=at;
    ready.end=at+count;
    // Contiguous refills overwrite old slots; a forward gap still starts at at.
    const auto oldest=ready.end>kStreamRingFrames ? ready.end-kStreamRingFrames : 0;
    ready.begin=std::max(ready.begin,oldest);
    return true;
}
}

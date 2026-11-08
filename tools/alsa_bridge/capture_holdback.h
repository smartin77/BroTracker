#pragma once
#include <algorithm>
#include <cstdint>

namespace capture_holdback {
// Legacy direct diagnostic opt-in remains 45 frames. Numeric selection is
// experimental except the owner's physically tested 45-frame dArkOS reserve.
constexpr uint64_t kFrames=45;
constexpr unsigned kMaxFrames=128;
struct Selection {unsigned frames=0;bool invalid=false;};
// Presence of numeric input is authoritative, even empty/invalid/zero: never
// fall back to an inherited legacy enable after explicit numeric selection.
inline Selection Select(const char* numeric,const char* legacy) noexcept {
    if(!numeric)return {legacy && legacy[0]=='1' && legacy[1]=='\0'?unsigned(kFrames):0,false};
    unsigned n=0;
    if(!*numeric)return {0,true};
    for(const char* p=numeric;*p;++p) {
        if(*p<'0' || *p>'9')return {0,true};
        n=n*10+unsigned(*p-'0');
        if(n>kMaxFrames)return {0,true}; // Reject before an unsigned overflow is possible.
    }
    return {n,false};
}
inline uint64_t Request(int64_t available,uint64_t limit,uint64_t frames=kFrames) noexcept {
    if(!frames)return limit; // Disabled: preserve the existing non-holdback read path.
    return available>int64_t(frames) ? std::min(limit,uint64_t(available)-frames) : 0;
}
// No retained cursor/quota: short reads, EAGAIN and recovery always resample
// availability. The caller retains the existing ALSA error/recovery policy.
struct Metrics {
    uint64_t samples=0,gated=0,max_available=0;
    void Observe(int64_t available,uint64_t frames=kFrames) noexcept {
        ++samples;
        if(available>=0) {
            max_available=std::max(max_available,uint64_t(available));
            if(available<=int64_t(frames)) ++gated;
        }
    }
};
}

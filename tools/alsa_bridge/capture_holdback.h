#pragma once
#include <algorithm>
#include <cstdint>

namespace capture_holdback {
// Experimental guard for the verified stereo PCM16/44100, <=180-byte packet
// profile. Withhold the newest maximum-size packet at the avail snapshot.
constexpr uint64_t kFrames=45;
inline uint64_t Request(int64_t available,uint64_t limit) noexcept {
    return available>int64_t(kFrames) ? std::min(limit,uint64_t(available)-kFrames) : 0;
}
// No retained cursor/quota: short reads, EAGAIN and recovery always resample
// availability. The caller retains the existing ALSA error/recovery policy.
struct Metrics {
    uint64_t samples=0,gated=0,max_available=0;
    void Observe(int64_t available) noexcept {
        ++samples;
        if(available>=0) {
            max_available=std::max(max_available,uint64_t(available));
            if(available<=int64_t(kFrames)) ++gated;
        }
    }
};
}

#pragma once
#include "sd_wav_buffer_loader.h"
#include "sd_wav_inspection.h"
#include <cinttypes>
#include <cstdio>
#include <cstring>
namespace BroTracker {
struct SdWavPreparation {
    bool attempted=false, sd_ready=false;
    SdWavBufferResult result{SdWavBufferStatus::OpenFailed,WavBufferStatus::IoError};
    Pcm16MonoView view;
    bool checksum_valid=false;
    std::uint32_t pcm_checksum=0;
    std::uint64_t capacity=0;
    std::uintptr_t address=0;
    // Boot/main owner only, once, before audio-ready publication. SD.begin is external.
    // Storage may be RAM2 or future PSRAM; parser/voice APIs do not depend on placement.
    void Prepare(bool initialized,const char* path,std::int16_t* pcm,std::uint64_t frames) noexcept {
        if(attempted) return;
        attempted=true; sd_ready=initialized; capacity=frames; address=reinterpret_cast<std::uintptr_t>(pcm);
        if(!initialized) return;
        Pcm16MonoView candidate;
        result=LoadWavBufferFromSd(path,pcm,frames,candidate);
        if(result.status==SdWavBufferStatus::Success) {
            // Boot-only FNV-1a32 over decoded PCM, signed samples encoded low byte
            // then high byte, independent of host byte order. Bounded by frames.
            std::uint32_t hash=2166136261u;
            for(std::size_t i=0;i<candidate.frame_count;++i) {
                const auto word=static_cast<std::uint16_t>(candidate.data[i]);
                hash=(hash ^ (word & 255u))*16777619u;
                hash=(hash ^ (word >> 8))*16777619u;
            }
            view=candidate; pcm_checksum=hash; checksum_valid=true;
        }
    }
};
// Exact measured capacities; storage ownership/placement remains with the caller.
constexpr std::size_t kDevelopmentManualWavFrameCapacity=68284;
static_assert(kDevelopmentManualWavFrameCapacity*sizeof(std::int16_t)==136568,"Measured PCM16 bytes");
inline void PrepareDevelopmentWavs(bool initialized,
    SdWavPreparation& test2,std::int16_t* pcm2,std::uint64_t frames2,
    SdWavPreparation& test3,std::int16_t* pcm3,std::uint64_t frames3) noexcept {
    test2.Prepare(initialized,kDevelopmentWavPaths[1],pcm2,frames2);
    test3.Prepare(initialized,kDevelopmentWavPaths[2],pcm3,frames3);
}
// Retained status plus optional PCM checksum, with a complete-line priority boundary.
// Fixed partial-line state, no diagnostic FIFO.
// Formatting is main-loop only. Reset cancels connection TX, not preparation evidence.
class SdPreparationReport {
public:
    void Reset() noexcept { length_=offset_=first_length_=0; }
    bool Offer(const SdWavPreparation& s,const char* path=nullptr) noexcept {
        if(HasData()) return false;
        const int n=path ?
            std::snprintf(line_,sizeof(line_),"BTPATTERN1 WAVPREP 2 %s %u %u %u %u %zu %" PRIu32 " %" PRIu64 " %" PRIuPTR "\n",
                path,unsigned(s.attempted),unsigned(s.sd_ready),unsigned(s.result.status),unsigned(s.result.load_status),
                s.view.frame_count,s.view.sample_rate_hz,s.capacity,s.address) :
            std::snprintf(line_,sizeof(line_),"BTPATTERN1 WAVPREP 1 %u %u %u %u %zu %" PRIu32 " %" PRIu64 " %" PRIuPTR "\n",
            unsigned(s.attempted),unsigned(s.sd_ready),unsigned(s.result.status),unsigned(s.result.load_status),
            s.view.frame_count,s.view.sample_rate_hz,s.capacity,s.address);
        if(n<0 || static_cast<std::size_t>(n)>=sizeof(line_)) return false;
        first_length_=static_cast<std::size_t>(n);
        length_=first_length_; offset_=0;
        if(path) {
            const int hash_length=std::snprintf(line_+length_,sizeof(line_)-length_,
                "BTPATTERN1 WAVHASH 1 %s %u %zu %" PRIu32 "\n",path,
                unsigned(s.checksum_valid),s.view.frame_count,s.pcm_checksum);
            if(hash_length<0 || static_cast<std::size_t>(hash_length)>=sizeof(line_)-length_) { Reset(); return false; }
            length_+=static_cast<std::size_t>(hash_length);
        }
        return true;
    }
    bool HasData() const noexcept { return offset_<length_; }
    bool Partial() const noexcept { return offset_!=0 && offset_!=first_length_ && HasData(); }
    const char* Data(std::size_t& n) const noexcept { n=(offset_<first_length_ ? first_length_ : length_)-offset_; return line_+offset_; }
    void Consume(std::size_t n) noexcept { std::size_t available=0; (void)Data(available); if(n<=available) offset_+=n; }
private:
    char line_[256]{}; std::size_t length_=0,offset_=0,first_length_=0;
};
}

#pragma once
#include "sd_wav_buffer_loader.h"
#include <cinttypes>
#include <cstdio>
#include <cstring>
namespace BroTracker {
struct SdWavPreparation {
    bool attempted=false, sd_ready=false;
    SdWavBufferResult result{SdWavBufferStatus::OpenFailed,WavBufferStatus::IoError};
    Pcm16MonoView view;
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
        if(result.status==SdWavBufferStatus::Success) view=candidate;
    }
};
// One retained report per connection; fixed partial-line state, no diagnostic FIFO.
// Formatting is main-loop only. Reset cancels connection TX, not preparation evidence.
class SdPreparationReport {
public:
    void Reset() noexcept { length_=offset_=0; }
    bool Offer(const SdWavPreparation& s) noexcept {
        if(HasData()) return false;
        const int n=std::snprintf(line_,sizeof(line_),"BTPATTERN1 WAVPREP 1 %u %u %u %u %zu %" PRIu32 " %" PRIu64 " %" PRIuPTR "\n",
            unsigned(s.attempted),unsigned(s.sd_ready),unsigned(s.result.status),unsigned(s.result.load_status),
            s.view.frame_count,s.view.sample_rate_hz,s.capacity,s.address);
        if(n<0 || static_cast<std::size_t>(n)>=sizeof(line_)) return false;
        length_=static_cast<std::size_t>(n); offset_=0; return true;
    }
    bool HasData() const noexcept { return offset_<length_; }
    bool Partial() const noexcept { return offset_!=0 && HasData(); }
    const char* Data(std::size_t& n) const noexcept { n=length_-offset_; return line_+offset_; }
    void Consume(std::size_t n) noexcept { if(n<=length_-offset_) offset_+=n; }
private:
    char line_[160]{}; std::size_t length_=0,offset_=0;
};
}

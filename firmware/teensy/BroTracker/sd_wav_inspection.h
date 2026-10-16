#pragma once
#include "sd_wav_buffer_loader.h"
#include <cstdio>
#include <cinttypes>
namespace BroTracker {
// Established legacy paths; no enumeration, substitution or general file protocol.
constexpr const char* kDevelopmentWavPaths[]={"Samples/test.wav","Samples/test2.wav","Samples/test3.wav"};
struct SdWavInspection {
    SdWavBufferStatus open_status=SdWavBufferStatus::OpenFailed;
    WavBufferStatus validation=WavBufferStatus::IoError;
    WavMetadata metadata;
};
struct DevelopmentWavInspection {
    bool attempted=false,sd_ready=false;
    SdWavInspection files[3];
    void InspectOnce(bool initialized) noexcept {
        if(attempted) return;
        attempted=true; sd_ready=initialized;
        if(!initialized) return;
        for(unsigned i=0;i<3;++i) {
            SdWavBufferReader reader; // Shared read lock spans validation; RAII closes every exit.
            files[i].open_status=reader.Open(kDevelopmentWavPaths[i]);
            if(files[i].open_status==SdWavBufferStatus::Success)
                files[i].validation=InspectWavMetadata(reader,files[i].metadata);
        }
    }
    bool Total(std::uint64_t& bytes) const noexcept {
        if(!attempted || !sd_ready) return false;
        std::uint64_t total=0;
        for(const auto& f:files) {
            if(f.open_status!=SdWavBufferStatus::Success || f.validation!=WavBufferStatus::Success ||
                f.metadata.required_pcm_bytes>UINT64_MAX-total) return false;
            total+=f.metadata.required_pcm_bytes;
        }
        bytes=total; return true;
    }
};
// Main-owned immutable boot results. One line of fixed storage, four lines/session.
// Finish partial lines; otherwise ACK/telemetry/snapshot work has priority.
class WavInspectionReport {
public:
    void Reset() noexcept { source_=nullptr; index_=4; length_=offset_=0; }
    void Offer(const DevelopmentWavInspection& source) noexcept { if(HasData()) return; Reset(); source_=&source; index_=0; }
    bool HasData() const noexcept { return source_ && index_<4; }
    bool Partial() const noexcept { return offset_ && offset_<length_; }
    const char* Data(std::size_t& count) noexcept {
        if(HasData() && !length_) {
            int n=0;
            if(index_<3) {
                const auto& f=source_->files[index_]; const auto& m=f.metadata;
                const unsigned flags=unsigned(m.file_known) | (unsigned(m.format_known)<<1) |
                    (unsigned(m.data_known)<<2) | (unsigned(m.duration_known)<<3);
                n=std::snprintf(line_,sizeof(line_),"BTPATTERN1 WAVMETA 1 %s %u %u %u %u %" PRIu64 " %u %u %" PRIu32 " %u %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
                    kDevelopmentWavPaths[index_],unsigned(source_->sd_ready),unsigned(f.open_status),unsigned(f.validation),flags,
                    m.file_bytes,unsigned(m.format),unsigned(m.channels),m.sample_rate,unsigned(m.bits),m.data_bytes,m.frames,m.duration_us,m.required_pcm_bytes);
            } else {
                std::uint64_t total=0; const bool valid=source_->Total(total);
                n=std::snprintf(line_,sizeof(line_),"BTPATTERN1 WAVTOTAL 1 %u %" PRIu64 "\n",unsigned(valid),total);
            }
            if(n<0 || static_cast<std::size_t>(n)>=sizeof(line_)) { Reset(); count=0; return line_; }
            length_=static_cast<std::size_t>(n);
        }
        count=length_-offset_; return line_+offset_;
    }
    void Consume(std::size_t count) noexcept {
        if(count>length_-offset_) return;
        offset_+=count;
        if(length_ && offset_==length_) { ++index_; length_=offset_=0; }
    }
private:
    const DevelopmentWavInspection* source_=nullptr;
    unsigned index_=4;
    char line_[256]{};
    std::size_t length_=0,offset_=0;
};
}

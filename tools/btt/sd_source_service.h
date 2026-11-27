#pragma once
// Foreground bridge shared by the developer stopped-load coordinator and diagnostic.
#include "resource_admission.h"
#include "firmware/teensy/BroTracker/sd_sample_streaming.h"

namespace btt::prototype {
class SdSourceService final:public SourceService {
    using Producer=BroTracker::SdSampleStreaming;
    Producer& producer_;
    const void* requester_=nullptr;
    BroTracker::ResourcePathRef path_;
    uint32_t ticket_=0;
    unsigned leases_=0;
    bool cancelling_=false;
    static SourceStatus Map(Producer::PreparationStatus status) noexcept;
    SourceStatus Pin(uint32_t generation,SourceLease& output) noexcept;
    Producer::CandidatePin Token(SourceToken t) const noexcept {return {&producer_,t.generation,t.slot};}
public:
    explicit SdSourceService(Producer& p) noexcept:producer_(p){}
    SdSourceService(const SdSourceService&)=delete;
    SdSourceService& operator=(const SdSourceService&)=delete;
    // Caller must drain cancellation and destroy all leases before the service.
    bool CanDestroy() const noexcept {return !ticket_ && !leases_;}
    SourceStatus Acquire(const char* path,SourceLease& output) noexcept override {return AcquireFor(this,path,output);}
    SourceStatus AcquireFor(const void* requester,const char* path,SourceLease& output) noexcept override;
    SourceStatus CancelFor(const void* requester) noexcept override;
    bool Retain(SourceToken token) noexcept override {
        if(!producer_.RetainCandidatePin(Token(token)))return false;
        ++leases_;return true;
    }
    void Release(SourceToken token) noexcept override {if(leases_ && producer_.ReleaseCandidatePin(Token(token)))--leases_;}
    bool Access(SourceToken token,BroTracker::Pcm16MixedSampleView& view,const char*& path) const noexcept {
        return producer_.AccessCandidatePin(Token(token),view,path);
    }
    // Sole job driver. Call once per foreground turn after producer.Service()
    // and all higher-priority work. Acquire/Step/Cancel never advance SD work.
    SourceStatus Service() noexcept;
};
}

#include "sd_source_service.h"
#include <cstring>

namespace btt::prototype {
SourceStatus SdSourceService::Map(Producer::PreparationStatus s) noexcept {
    using S=Producer::PreparationStatus;
    switch(s) {
    case S::Pending:return SourceStatus::Pending;
    case S::Ready:return SourceStatus::Ready;
    case S::Missing:return SourceStatus::Missing;
    case S::Capacity:return SourceStatus::Capacity;
    case S::PathCapacity:return SourceStatus::PathCapacity;
    case S::Unsupported:return SourceStatus::Unsupported;
    case S::Cancelled:return SourceStatus::Released;
    case S::Quarantined:return SourceStatus::Quarantined;
    case S::Stale:return SourceStatus::Stale;
    case S::GenerationExhausted:return SourceStatus::GenerationExhausted;
    case S::StorageUnavailable:return SourceStatus::StorageUnavailable;
    default:return SourceStatus::Io;
    }
}
SourceStatus SdSourceService::Pin(uint32_t generation,SourceLease& output) noexcept {
    Producer::CandidatePin pin;
    const auto result=producer_.AcquireCandidatePin(generation,pin);
    if(result!=Producer::PinResult::Ready)return result==Producer::PinResult::Capacity?SourceStatus::Capacity:SourceStatus::Stale;
    ++leases_;output=SourceLease::Adopt(*this,{pin.generation,pin.slot});return SourceStatus::Ready;
}
SourceStatus SdSourceService::AcquireFor(const void* requester,const char* path,SourceLease& output) noexcept {
    if(!requester || output || !BroTracker::ValidSamplePath(path?path:""))return SourceStatus::Unsupported;
    if(ticket_) {
        if(requester!=requester_)return SourceStatus::Pending;
        if(cancelling_ || std::strcmp(path_.Get(),path))return SourceStatus::Stale;
        const auto status=Map(producer_.OwnedPreparationStatus(ticket_));
        if(status==SourceStatus::Pending || status==SourceStatus::Quarantined)return status;
        Producer::CandidatePin pin;
        if(!producer_.TakeOwnedPreparation(ticket_,pin))return SourceStatus::Stale;
        ticket_=0;requester_=nullptr;path_.Reset();
        if(status==SourceStatus::Ready){++leases_;output=SourceLease::Adopt(*this,{pin.generation,pin.slot});}
        return status;
    }
    if(const auto generation=producer_.PreparedGeneration(path))return Pin(generation,output);
    if(producer_.PreparationTicketsExhausted())return SourceStatus::GenerationExhausted;
    // Own the request path while waiting; failed foreign contention drops only
    // this temporary ref. No job result belonging to another caller is touched.
    if(!path_.Assign(path))return SourceStatus::PathCapacity;
    if(!producer_.BeginOwnedPreparation(path,ticket_)){path_.Reset();return SourceStatus::Pending;}
    requester_=requester;cancelling_=false;
    return SourceStatus::Pending;
}
SourceStatus SdSourceService::CancelFor(const void* requester) noexcept {
    if(!ticket_ || requester!=requester_)return SourceStatus::Released;
    if(!cancelling_){if(!producer_.CancelOwnedPreparation(ticket_))return SourceStatus::Stale;cancelling_=true;}
    const auto status=Map(producer_.OwnedPreparationStatus(ticket_));
    if(status==SourceStatus::Pending || status==SourceStatus::Quarantined)return status;
    Producer::CandidatePin pin;
    if(!producer_.TakeOwnedPreparation(ticket_,pin))return SourceStatus::Stale;
    ticket_=0;requester_=nullptr;path_.Reset();cancelling_=false;return SourceStatus::Released;
}
SourceStatus SdSourceService::Service() noexcept {
    if(!ticket_)return SourceStatus::Released;
    // Cleanup also yields to refills; each turn performs at most one job action.
    if(!producer_.RefillNeeded() && producer_.BackgroundHeadroom())producer_.StepOwnedPreparation(ticket_);
    return Map(producer_.OwnedPreparationStatus(ticket_));
}
}

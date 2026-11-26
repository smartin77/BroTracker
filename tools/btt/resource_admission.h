#pragma once
// Isolated foreground resource admission. No production publication or SD calls.
#include "foundation.h"
#include "arena.h"
#include "bringup/resource_path.h"

namespace btt::prototype {
struct SourceToken {
    uint32_t generation=0;
    unsigned slot=0;
    bool operator==(SourceToken b) const noexcept {return generation==b.generation && slot==b.slot;}
};
enum class SourceStatus:uint8_t { NotRequired, NotPrepared, Pending, Ready, Missing, Capacity, PathCapacity, Unsupported, Io, Released };
class SourceService;
class SourceLease {
    SourceService* service_=nullptr;
    SourceToken token_{};
public:
    SourceLease() noexcept = default;
    SourceLease(const SourceLease&)=delete;
    SourceLease& operator=(const SourceLease&)=delete;
    SourceLease(SourceLease&& p) noexcept {swap(p);}
    SourceLease& operator=(SourceLease&& p) noexcept {if(this!=&p){Reset();swap(p);}return *this;}
    ~SourceLease(){Reset();}
    void swap(SourceLease& p) noexcept {std::swap(service_,p.service_);std::swap(token_,p.token_);}
    void Reset() noexcept;
    bool Clone(SourceLease& candidate) const noexcept;
    SourceToken token() const noexcept {return token_;}
    explicit operator bool() const noexcept {return service_!=nullptr;}
    // Service must already own one successful reservation. No ownership on failure.
    static SourceLease Adopt(SourceService& service,SourceToken token) noexcept {
        SourceLease p;p.service_=&service;p.token_=token;return p;
    }
};
class SourceService {
public:
    virtual ~SourceService() = default;
    // Compatibility includes asset identity/format, never Root/Fine Tune. The
    // service pins immutable PCM/source metadata and independently owns its path.
    // It must not reclaim any active, rollback, voice or candidate pin.
    virtual SourceStatus Acquire(const char* path,SourceLease& candidate) noexcept = 0;
    virtual bool Retain(SourceToken) noexcept = 0;
    virtual void Release(SourceToken) noexcept = 0;
};
inline void SourceLease::Reset() noexcept {if(service_)service_->Release(token_);service_=nullptr;token_={};}
inline bool SourceLease::Clone(SourceLease& candidate) const noexcept {
    if(!service_ || !service_->Retain(token_))return false;
    candidate=Adopt(*service_,token_);return true;
}

enum class PathStatus:uint8_t { Absent, Valid, Invalid, TooLong, UnavailableBase, UnsupportedEncoding, Owned, Capacity };
enum class SettingStatus:uint8_t { Supported, UnsupportedType, UnsupportedValue, UnsupportedEncoding, Inappropriate };
enum class Admission { Empty, Scanned, Progress, Ready, Blocked, Io, Cancelled };
struct Definition {
    SavedInstrument saved;
    BroTracker::ResourcePathRef path;
    SourceLease source;
    uint16_t effective_root=0,effective_fine=0,effective_midi=0;
    PathStatus path_status=PathStatus::Absent;
    SettingStatus settings=SettingStatus::Supported;
    SourceStatus source_status=SourceStatus::NotRequired;
    bool required=false;
};
struct AdmissionScratch {char path[BroTracker::kSamplePathMaximum+1]{};};
struct InitialRequirements {
    uint32_t pattern_id=0;
    std::array<uint32_t,8> incoming_instruments{}; // Explicit carry-in; zero is unknown/unbound.
};
class StoppedExchange;
class Candidate {
    friend class StoppedExchange;
    ReaderOwner backing_;
    std::array<Definition,99> definitions_{};
    uint32_t initial_pattern_=0;
    unsigned count_=0,cursor_=0;
    Admission state_=Admission::Empty;
    bool linked_=false,unresolved_=false;
    void ReleaseRuntime() noexcept;
    Admission Block(Admission result) noexcept;
    Result Path(Definition&,AdmissionScratch&) noexcept;
public:
    Candidate() noexcept = default;
    Candidate(const Candidate&)=delete;
    Candidate& operator=(const Candidate&)=delete;
    ~Candidate(){ReleaseRuntime();}
    bool CanDestroy() const noexcept {return !linked_;}
    // Caller supplies arena-owned scratch/Pattern. No complete Document copy.
    Result Begin(const Document&,InitialRequirements,Workspace&,Pattern&,AdmissionScratch&) noexcept;
    // At most one path or source acquisition per call, making cancellation
    // possible after each acquisition. Capacity/I/O failures roll back only self.
    Admission Step(SourceService&,AdmissionScratch&) noexcept;
    Code Cancel() noexcept;
    Code Discard() noexcept;
    unsigned count() const noexcept {return count_;}
    uint32_t initial_pattern() const noexcept {return initial_pattern_;}
    const Definition* At(unsigned i) const noexcept {return i<count_?&definitions_[i]:nullptr;}
    const Definition* Find(uint32_t id) const noexcept;
    const ReaderOwner& backing() const noexcept {return backing_;}
    Admission state() const noexcept {return state_;}
    bool initial_resources_ready() const noexcept {return state_==Admission::Ready;}
    bool unresolved_requirements() const noexcept {return unresolved_;}
};
// Simulates ownership handoff only. `stopped` is a test precondition, not a
// device acknowledgement. Neither player binding nor production API is touched.
class StoppedExchange {
    Candidate* current_=nullptr;
    Candidate* previous_=nullptr;
public:
    Code Commit(Candidate&,bool stopped) noexcept;
    Code Rollback(bool stopped) noexcept;
    Code ReleasePrevious(bool stopped) noexcept;
    Code Detach(bool stopped) noexcept;
    const Candidate* current() const noexcept {return current_;}
    const Candidate* previous() const noexcept {return previous_;}
};
}

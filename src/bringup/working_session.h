#pragma once
#include "pattern_file_store.h"
namespace BroTracker {
// Two independently validated BTSS records: published baseline and working intent.
// No transport/request identity is persisted. Fixed maximum: 3144 bytes.
constexpr std::size_t kWorkingSessionCapacity=24+2*kPatternFileCapacity;
struct WorkingSession {
    RealtimePattern published,working;
    std::uint32_t published_tempo=12500,working_tempo=12500;
};
inline PatternFileResult DecodeWorkingSession(const std::uint8_t* b,std::size_t n,WorkingSession& out) noexcept {
    if(!b || n<24 || n>kWorkingSessionCapacity)return PatternFileResult::Invalid;
    if(std::memcmp(b,"BTWS",4) || b[4]!=1 || b[5] || b[6]!=24 || b[7])return PatternFileResult::Unsupported;
    for(unsigned i=16;i<20;++i)if(b[i])return PatternFileResult::Invalid;
    const auto a=PatternFileU32(b+8),c=PatternFileU32(b+12);
    if(a>kPatternFileCapacity || c>kPatternFileCapacity || n!=24+a+c || PatternFileU32(b+20)!=PatternFileCrc(b,n))return PatternFileResult::Invalid;
    if(a<24 || c<24 || b[32]!=b[24+a+8] || b[33]!=b[24+a+9])return PatternFileResult::Invalid;
    // Validate both before touching caller state.
    auto r=ValidatePatternFile(PatternFileKind::Session,b+24,a);if(r!=PatternFileResult::Ok)return r;
    r=ValidatePatternFile(PatternFileKind::Session,b+24+a,c);if(r!=PatternFileResult::Ok)return r;
    DecodePatternFile(PatternFileKind::Session,b+24,a,out.published,out.published_tempo);
    DecodePatternFile(PatternFileKind::Session,b+24+a,c,out.working,out.working_tempo);
    return PatternFileResult::Ok;
}
template<class Backend> class WorkingSessionStore {
public:
    PatternFileResult Save(Backend& b,const WorkingSession& s) noexcept {
        if(s.published.active_rows!=s.working.active_rows || s.published.active_channels!=s.working.active_channels)return PatternFileResult::Invalid;
        std::size_t a=0,c=0;
        auto r=EncodePatternFile(PatternFileKind::Session,s.published,s.published_tempo,bytes_+24,kPatternFileCapacity,a);
        if(r!=PatternFileResult::Ok)return r;
        r=EncodePatternFile(PatternFileKind::Session,s.working,s.working_tempo,bytes_+24+a,kPatternFileCapacity,c);
        if(r!=PatternFileResult::Ok)return r;
        std::memset(bytes_,0,24);std::memcpy(bytes_,"BTWS",4);bytes_[4]=1;bytes_[6]=24;
        PatternFilePut32(bytes_+8,a);PatternFilePut32(bytes_+12,c);
        const auto n=24+a+c;PatternFilePut32(bytes_+20,PatternFileCrc(bytes_,n));
        Lease lease(b,true);if(!lease.held)return PatternFileResult::Busy;
        if(b.Exists(kSessionCandidatePath) || b.Exists(kSessionPreviousPath))return PatternFileResult::RecoveryRequired;
        if(b.Exists(kSessionPath) && !b.RegularFile(kSessionPath))return PatternFileResult::Io;
        if(!b.Directory("/BroTracker"))return PatternFileResult::Io;
        if(!b.WriteNew(kSessionCandidatePath,bytes_,n) || !b.Verify(kSessionCandidatePath,bytes_,n)) {
            if(b.Exists(kSessionCandidatePath) && !b.Remove(kSessionCandidatePath))return PatternFileResult::RecoveryRequired;
            return PatternFileResult::Io;
        }
        const bool old=b.Exists(kSessionPath);
        if(old && !b.Rename(kSessionPath,kSessionPreviousPath))return PatternFileResult::RecoveryRequired;
        if(!b.Rename(kSessionCandidatePath,kSessionPath)) {
            if(old && !b.Rename(kSessionPreviousPath,kSessionPath))return PatternFileResult::RecoveryRequired;
            if(!b.Remove(kSessionCandidatePath))return PatternFileResult::RecoveryRequired;
            return PatternFileResult::Io;
        }
        if(old && !b.Remove(kSessionPreviousPath))return PatternFileResult::BackupRetained;
        return PatternFileResult::Ok;
    }
    PatternFileResult Load(Backend& b,WorkingSession& s) noexcept {
        Lease lease(b,false);if(!lease.held)return PatternFileResult::Busy;
        auto r=Read(b,kSessionPath,s);
        if(r==PatternFileResult::Ok)return (b.Exists(kSessionCandidatePath)||b.Exists(kSessionPreviousPath))?PatternFileResult::BackupRetained:r;
        // Previous is the last known committed copy. Candidate is never assumed committed.
        const auto previous=Read(b,kSessionPreviousPath,s);
        if(previous==PatternFileResult::Ok)return PatternFileResult::BackupRetained;
        return b.Exists(kSessionCandidatePath)||b.Exists(kSessionPreviousPath)?PatternFileResult::RecoveryRequired:r;
    }
private:
    struct Lease {Backend& b;bool write,held;Lease(Backend& v,bool w):b(v),write(w),held(b.Acquire(w)){} ~Lease(){if(held)b.Release(write);}};
    PatternFileResult Read(Backend& b,const char* path,WorkingSession& s) noexcept {
        if(!b.Exists(path))return PatternFileResult::Missing;
        std::size_t n=0;auto r=b.Read(path,bytes_,sizeof(bytes_),n);if(r!=PatternFileResult::Ok)return r;
        // Legacy sessions had one published record, hence no unpublished state to recover.
        if(n>=4 && !std::memcmp(bytes_,"BTSS",4)) {
            r=DecodePatternFile(PatternFileKind::Session,bytes_,n,s.published,s.published_tempo);
            if(r==PatternFileResult::Ok){s.working=s.published;s.working_tempo=s.published_tempo;}return r;
        }
        return DecodeWorkingSession(bytes_,n,s);
    }
    std::uint8_t bytes_[kWorkingSessionCapacity]{};
};
}

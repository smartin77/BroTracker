#pragma once
#include "pattern_file_store.h"
#include "instrument_definition.h"
namespace BroTracker {
// Temporary BTM stores one working Pattern/BPM and the complete definition table.
// Legacy BTWS/BTSS remain readable. No transport/request identity is persisted.
constexpr std::size_t kWorkingSessionCapacity=32+kPatternFileCapacity+99*65;
struct SessionView {
    std::uint8_t row=0,channel=0,field=0,header=0;
};
inline SessionView ValidatedSessionView(SessionView v,const RealtimePattern& p) noexcept {
    if(v.row>=p.active_rows)v.row=p.active_rows-1;
    if(v.channel>=p.active_channels)v.channel=p.active_channels-1;
    if(v.field>1)v.field=0;
    if(v.header>1)v.header=0;
    if(v.header)v.row=0;
    return v;
}
struct WorkingSession {
    InstrumentDefinitions definitions;
    RealtimePattern published,working;
    SessionView view{};
    std::uint8_t mute_mask=0;
    std::uint32_t published_tempo=12500,working_tempo=12500;
};
inline PatternFileResult DecodeWorkingSession(const std::uint8_t* b,std::size_t n,WorkingSession& out) noexcept {
    if(b && n>=32 && !std::memcmp(b,"BTM1",4)) {
        if(b[4]!=1 || b[5] || b[6]!=24 || b[7])return PatternFileResult::Unsupported;
        const auto a=PatternFileU32(b+8),count=PatternFileU32(b+12);
        if(count>99 || a>kPatternFileCapacity || n!=32+a+count*65 || PatternFileU32(b+16) || PatternFileU32(b+20)!=PatternFileCrc(b,n))return PatternFileResult::Invalid;
        InstrumentDefinitions definitions(false);
        for(unsigned i=0;i<count;++i) {
            const auto* d=b+24+a+i*65;
            const auto* r=reinterpret_cast<const char*>(d+1);
            const auto* end=static_cast<const char*>(std::memchr(r,0,64));
            if(!end)return PatternFileResult::Invalid;
            for(const char* c=end;c<r+64;++c)if(*c)return PatternFileResult::Invalid;
            if(!definitions.Append(i,d[0],*r?r:nullptr))return PatternFileResult::Invalid;
        }
        const auto* m=b+n-8;if(m[5] || m[6] || m[7])return PatternFileResult::Invalid;
        if(ValidatePatternFile(PatternFileKind::Session,b+24,a)!=PatternFileResult::Ok)return PatternFileResult::Invalid;
        DecodePatternFile(PatternFileKind::Session,b+24,a,out.working,out.working_tempo);
        out.published=out.working;out.published_tempo=out.working_tempo;out.definitions=definitions;
        out.view=ValidatedSessionView({m[0],m[1],m[2],m[3]},out.working);out.mute_mask=m[4];return PatternFileResult::Ok;
    }
    if(!b || n<24 || n>kWorkingSessionCapacity)return PatternFileResult::Invalid;
    if(std::memcmp(b,"BTWS",4) || (b[4]!=1 && b[4]!=2) || b[5] || b[6]!=24 || b[7])return PatternFileResult::Unsupported;
    for(unsigned i=16;i<20;++i)if(b[i])return PatternFileResult::Invalid;
    const auto a=PatternFileU32(b+8),c=PatternFileU32(b+12);
    if(a>kPatternFileCapacity || c>kPatternFileCapacity || n!=24+a+c+(b[4]==2?8:0) || PatternFileU32(b+20)!=PatternFileCrc(b,n))return PatternFileResult::Invalid;
    if(a<24 || c<24 || b[32]!=b[24+a+8] || b[33]!=b[24+a+9])return PatternFileResult::Invalid;
    // Validate metadata and both records before touching caller state.
    if(b[4]==2) {
        const auto* m=b+24+a+c;
        if(m[5] || m[6] || m[7])return PatternFileResult::Invalid;
    }
    auto r=ValidatePatternFile(PatternFileKind::Session,b+24,a);if(r!=PatternFileResult::Ok)return r;
    r=ValidatePatternFile(PatternFileKind::Session,b+24+a,c);if(r!=PatternFileResult::Ok)return r;
    DecodePatternFile(PatternFileKind::Session,b+24,a,out.published,out.published_tempo);
    DecodePatternFile(PatternFileKind::Session,b+24+a,c,out.working,out.working_tempo);
    out.definitions.Reset();out.view={};out.mute_mask=0;
    if(b[4]==2) {
        const auto* m=b+24+a+c;
        out.view=ValidatedSessionView({m[0],m[1],m[2],m[3]},out.working);out.mute_mask=m[4];
    }
    return PatternFileResult::Ok;
}
template<class Backend> class WorkingSessionStore {
public:
    // Preserve invalid canonical bytes before automatic config-fallback persistence.
    // Never overwrite an earlier quarantine or bypass interrupted-write markers.
    bool PreserveInvalid(Backend& b) noexcept {
        Lease lease(b,true);if(!lease.held)return false;
        constexpr const char* invalid="/BroTracker/current.btsession.invalid";
        if(b.Exists(invalid) || b.Exists(kSessionCandidatePath) || b.Exists(kSessionPreviousPath) || !b.RegularFile(kSessionPath))return false;
        return b.Rename(kSessionPath,invalid);
    }
    PatternFileResult Save(Backend& b,const WorkingSession& s) noexcept {
        std::size_t a=0;
        auto r=EncodePatternFile(PatternFileKind::Session,s.working,s.working_tempo,bytes_+24,kPatternFileCapacity,a);
        if(r!=PatternFileResult::Ok)return r;
        std::memset(bytes_,0,24);std::memcpy(bytes_,"BTM1",4);bytes_[4]=1;bytes_[6]=24;
        PatternFilePut32(bytes_+8,a);PatternFilePut32(bytes_+12,s.definitions.Count());
        for(unsigned i=0;i<s.definitions.Count();++i) {
            const auto* d=s.definitions.At(i);auto* record=bytes_+24+a+i*65;std::memset(record,0,65);
            record[0]=InstrumentTypeCode(d->type);if(d->resource)std::strcpy(reinterpret_cast<char*>(record+1),d->resource);
        }
        const auto v=ValidatedSessionView(s.view,s.working);
        const auto n=32+a+s.definitions.Count()*65;auto* m=bytes_+n-8;std::memset(m,0,8);
        m[0]=v.row;m[1]=v.channel;m[2]=v.field;m[3]=v.header;m[4]=s.mute_mask;
        PatternFilePut32(bytes_+20,PatternFileCrc(bytes_,n));
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
            if(r==PatternFileResult::Ok){s.working=s.published;s.working_tempo=s.published_tempo;s.definitions.Reset();s.view={};s.mute_mask=0;}return r;
        }
        return DecodeWorkingSession(bytes_,n,s);
    }
    std::uint8_t bytes_[kWorkingSessionCapacity]{};
};
}

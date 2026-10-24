#pragma once
#include "pattern_file.h"
#include "pattern_fixture.h"
#include "pattern_publication.h"

namespace BroTracker {
constexpr const char* kDevelopmentThemePath="/BroTracker/development/theme.btdev";
constexpr const char* kDevelopmentMidiPath="/BroTracker/development/midi.btdev";
constexpr const char* kSessionPath="/BroTracker/current.btsession";
constexpr const char* kSessionCandidatePath="/BroTracker/current.btsession.candidate";
constexpr const char* kSessionPreviousPath="/BroTracker/current.btsession.previous";
inline void SetDevelopmentMidiBassPart(RealtimePattern& p) noexcept {
    // Replace the entire CH5 column, including otherwise ignored OFF metadata.
    for(unsigned row=0;row<32;++row) {
        p.cells[row][4]={NOTE_EMPTY,kNoInstrumentUpdate};
        p.note_off_timing[row][4]=NoteOffTiming::Arrival;
    }
    constexpr unsigned rows[]={0,6,8,12,16,22,24,28};
    constexpr unsigned notes[]={36,43,48,43,41,48,41,43};
    constexpr unsigned offs[]={3,7,11,15,19,23,27,31};
    for(unsigned i=0;i<8;++i) {
        p.cells[rows[i]][4]={static_cast<Note>(notes[i]),1};
        p.cells[offs[i]][4]={NOTE_OFF,kNoInstrumentUpdate};
    }
}
inline void MakeStoredDevelopmentPattern(RealtimePattern& p,bool midi) noexcept {
    p=MakeDevelopmentPattern(32);
    if(midi) SetDevelopmentMidiBassPart(p);
}
// Replay validated storage through the existing publication validator/identity namespace.
// The resulting frozen staging retains its usual lifetime through audio-owner ACK.
inline bool StageStoredPattern(PatternPublication& transfer,const RealtimePattern& p,std::uint32_t tempo,std::uint32_t now) noexcept {
    if(transfer.Busy() || !ValidStoredPattern(p,tempo) || !transfer.NextIdentity()) return false;
    const auto id=transfer.NextIdentity(); const unsigned count=p.active_rows*p.active_channels;
    if(transfer.Accept({PublicationKind::Begin,{id,p.active_rows,p.active_channels,count,tempo},2},now)!=PublicationStatus::Receiving) return false;
    auto hash=SnapshotHeaderHash(p.active_rows,p.active_channels,tempo);
    for(unsigned i=0;i<count;++i) {
        const auto r=i/p.active_channels,c=i%p.active_channels; const auto& cell=p.cells[r][c];
        const auto timing=static_cast<std::uint8_t>(p.note_off_timing[r][c]);
        hash=SnapshotHashByte(SnapshotHashByte(SnapshotHashByte(hash,cell.note),cell.instrument),timing);
        if(transfer.Accept({PublicationKind::Cell,{id,i,cell.note,cell.instrument,timing},2},now)!=PublicationStatus::Receiving) {
            transfer.Cancel(); return false;
        }
    }
    if(transfer.Accept({PublicationKind::Commit,{id,count,hash},2},now)!=PublicationStatus::Ready) {transfer.Cancel();return false;}
    return transfer.Freeze();
}
// Backend is the existing SD owner: Acquire/Release, directory and fixed-file operations.
// One instance, main context only. No retries, heap, ISR work or unbounded file reads.
template<class Backend> class PatternFileStore {
public:
    PatternFileResult Load(Backend& b,const char* path,PatternFileKind kind,RealtimePattern& p,std::uint32_t& tempo) noexcept {
        Lease lease(b,false); if(!lease) return PatternFileResult::Busy;
        if(!b.Exists(path)) return PatternFileResult::Missing;
        std::size_t size=0; const auto result=b.Read(path,bytes_,sizeof(bytes_),size);
        if(result!=PatternFileResult::Ok) return result;
        return DecodePatternFile(kind,bytes_,size,p,tempo);
    }
    PatternFileResult CreateDevelopment(Backend& b,bool midi,RealtimePattern& scratch) noexcept {
        MakeStoredDevelopmentPattern(scratch,midi);
        return Write(b,midi?kDevelopmentMidiPath:kDevelopmentThemePath,
            midi?"/BroTracker/development/midi.btdev.candidate":"/BroTracker/development/theme.btdev.candidate",
            nullptr,PatternFileKind::Development,scratch,kDevelopmentFixtureTempo);
    }
    PatternFileResult SaveSession(Backend& b,const RealtimePattern& published,std::uint32_t tempo) noexcept {
        return Write(b,kSessionPath,kSessionCandidatePath,kSessionPreviousPath,PatternFileKind::Session,published,tempo);
    }
    // Explicit narrowly scoped migration: read actual SD data, preserve every
    // non-CH5 cell, require the known development dimensions/tempo. Never SEED.
    PatternFileResult ReplaceDevelopmentMidi(Backend& b,RealtimePattern& scratch) noexcept {
        std::uint32_t tempo=0;
        const auto result=Load(b,kDevelopmentMidiPath,PatternFileKind::Development,scratch,tempo);
        if(result!=PatternFileResult::Ok) return result;
        if(scratch.active_rows!=32 || scratch.active_channels!=8 || tempo!=kDevelopmentFixtureTempo)
            return PatternFileResult::Invalid;
        SetDevelopmentMidiBassPart(scratch);
        return Write(b,kDevelopmentMidiPath,"/BroTracker/development/midi.btdev.candidate",
            "/BroTracker/development/midi.btdev.previous",PatternFileKind::Development,scratch,tempo);
    }
private:
    struct Lease {
        Backend& backend; bool writer,held;
        Lease(Backend& b,bool w):backend(b),writer(w),held(b.Acquire(w)) {}
        ~Lease(){if(held) backend.Release(writer);}
        explicit operator bool() const {return held;}
    };
    PatternFileResult Write(Backend& b,const char* path,const char* candidate,const char* previous,
        PatternFileKind kind,const RealtimePattern& p,std::uint32_t tempo) noexcept {
        std::size_t size=0;
        if(EncodePatternFile(kind,p,tempo,bytes_,sizeof(bytes_),size)!=PatternFileResult::Ok) return PatternFileResult::Invalid;
        Lease lease(b,true); if(!lease) return PatternFileResult::Busy;
        if(!previous && b.Exists(path)) return PatternFileResult::Exists;
        if(b.Exists(candidate) || (previous && b.Exists(previous))) return PatternFileResult::RecoveryRequired;
        if(previous && b.Exists(path) && !b.RegularFile(path)) return PatternFileResult::Io;
        if(!b.Directory("/BroTracker") || (kind==PatternFileKind::Development && !b.Directory("/BroTracker/development"))) return PatternFileResult::Io;
        // O_EXCL candidate; checked full write, sync and close, then byte-for-byte reopen verification.
        if(!b.WriteNew(candidate,bytes_,size) || !b.Verify(candidate,bytes_,size)) {
            // Only this operation's candidate is removable; never touch canonical/previous on this path.
            if(b.Exists(candidate) && !b.Remove(candidate)) return PatternFileResult::RecoveryRequired;
            return PatternFileResult::Io;
        }
        const bool had_previous=previous && b.Exists(path);
        if(had_previous && !b.Rename(path,previous)) return PatternFileResult::RecoveryRequired;
        if(!b.Rename(candidate,path)) {
            if(had_previous && !b.Rename(previous,path)) return PatternFileResult::RecoveryRequired;
            if(!b.Remove(candidate)) return PatternFileResult::RecoveryRequired;
            return PatternFileResult::Io;
        }
        if(had_previous && !b.Remove(previous)) return PatternFileResult::BackupRetained;
        return PatternFileResult::Ok;
    }
    std::uint8_t bytes_[kPatternFileCapacity]{};
};
}

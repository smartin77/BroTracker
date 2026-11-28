#include "fs_save.h"
#include <cstdio>
#include <cstring>

namespace btt::prototype {
namespace {
bool Reserved(const char* path,const char* directory) noexcept {
    if(*directory=='/')++directory;
    const auto lower=[](char c){return c>='A' && c<='Z'?char(c-'A'+'a'):c;};
    while(*directory && *path && lower(*directory)==lower(*path)){++directory;++path;}
    return !*directory && (!*path || *path=='/');
}
}
bool FsSave::Rename(const char* from,const char* to) noexcept {
    Scope lease;
    return lease && !SD.sdfs.exists(to) && SD.sdfs.rename(from,to);
}
bool FsSave::OpenRead(const char* path) noexcept {
    readable_=false;
    if(!backend_.ReadOpen(file_,path,length_))return false;
    readable_=true;return true;
}
bool FsSave::finish() {
    const bool synced=backend_.Sync(file_);
    const bool closed=backend_.Close(file_); // Check close even on sync failure.
    return synced && closed;
}
ReaderOwner FsSave::reopen() {
    return OpenRead(candidate_path_)?ReaderOwner::Pin(*this):ReaderOwner{};
}
bool FsSave::promote() {
    // Save has finished every read/compare. Consume the disposable scratch pin
    // before closing or renaming its storage; no reader outlives immutable bytes.
    // Close failure retains the wrapper even if SdFat closed its inner file.
    if(!scratch_)return false;
    scratch_->backing.reset();scratch_->accepted=false;
    if(refs_)return false;
    readable_=false;
    if(!backend_.Close(file_))return false;
    if(previous_)return false;
    bool exists;
    {Scope lease;if(!lease)return false;exists=SD.sdfs.exists(path_);}
    if(exists) {
        if(!Rename(path_,previous_path_)){outcome_=SaveOutcome::PromotionFailed;return false;}
        previous_=true;
    }
    if(!Rename(candidate_path_,path_)) {
        outcome_=SaveOutcome::PromotionFailed;
        if(previous_) {
            if(!Rename(previous_path_,path_)){outcome_=SaveOutcome::RollbackFailed;return false;}
            previous_=false;
        }
        return false;
    }
    candidate_=false;promoted_=true;return true;
}
bool FsSave::Cleanup(bool ready) noexcept {
    if(!ready || refs_)return false;
    readable_=false;
    if(!backend_.Close(file_))return false;
    if(previous_) {
        if(promoted_) {if(!backend_.Remove(previous_path_))return false;}
        else if(!Rename(previous_path_,path_))return false;
        previous_=false;
    }
    if(candidate_) {
        if(!backend_.Remove(candidate_path_))return false;
        candidate_=false;
    }
    return true;
}
SaveOutcome FsSave::Run(const Document& source,FsBackings& registry,FsBackings::Token token,
    const char* destination,Document& scratch,Workspace& work,bool ready,uint32_t bpm) noexcept {
    // Busy must preserve the current transaction's paths and recovery history.
    if(!ready || !CanDestroy())return SaveOutcome::Busy;
    if(&source==&scratch || !source.accepted || !source.backing || !destination ||
       !BroTracker::ValidSamplePath(destination) || std::strlen(destination)>255-10 ||
       Reserved(destination,"/BroTracker/.btt-prototype") || Reserved(destination,"/BroTracker/.btt-load") ||
       Reserved(destination,BTT_BACKING_DIRECTORY) || source.backing->identity()!=registry.Identity(token))
        return SaveOutcome::Invalid;
    // Acquire proves the matching token is still sealed and owned, not merely
    // an address of a retired/reused slot. All legal sources are private registry
    // files; rejecting that namespace (case-insensitively) excludes FAT aliasing.
    ReaderOwner pin;
    if(registry.Acquire(token,pin)!=Code::Ok)return SaveOutcome::Invalid;
    std::strcpy(path_,destination);
    std::snprintf(candidate_path_,sizeof(candidate_path_),"%s.candidate",path_);
    std::snprintf(previous_path_,sizeof(previous_path_),"%s.previous",path_);
    outcome_=SaveOutcome::Failed;detail_=Result::Ok;promoted_=false;
    bool exists;
    {
        Scope lease;if(!lease)return outcome_=SaveOutcome::Busy;
        if(SD.sdfs.exists(candidate_path_) || SD.sdfs.exists(previous_path_))return outcome_=SaveOutcome::Collision;
        exists=SD.sdfs.exists(path_);
    }
    const auto release=[&scratch](){scratch.backing.reset();scratch.accepted=false;};
    release();
    // Do not move an invalid canonical into a backup slot. Validation is full,
    // using the same caller-owned Document index, never another file buffer.
    if(exists) {
        if(!OpenRead(path_))detail_=Result::Io;
        else detail_=Validate(ReaderOwner::Pin(*this),scratch,work);
        release();readable_=false;
        const bool closed=backend_.Close(file_);
        if(detail_!=Result::Ok || !closed) {
            if(!closed)detail_=Result::Io;
            if(!Cleanup(ready))outcome_=SaveOutcome::CleanupRequired;
            return outcome_;
        }
    }
    if(!backend_.Create(file_,candidate_path_)) {
        // Failed exclusive open grants no deletion rights (including collision).
        detail_=Result::Io;
    } else {
        candidate_=true;
        scratch_=&scratch;
        detail_=btt::Save(source,*this,*this,scratch,work,bpm);
        scratch_=nullptr;
    }
    release();readable_=false;
    if(outcome_==SaveOutcome::RollbackFailed)return outcome_; // Explicit recovery retry only.
    if(promoted_)outcome_=SaveOutcome::Saved;
    if(!Cleanup(ready)) {
        if(promoted_)outcome_=SaveOutcome::BackupRetained;
        else if(outcome_!=SaveOutcome::RollbackFailed)outcome_=SaveOutcome::CleanupRequired;
    }
    return outcome_;
}
}

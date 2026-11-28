#pragma once
#include "fs_backend.h"
#include "bringup/resource_path.h"

namespace btt::prototype {
enum class SaveOutcome {
    Idle, Saved, BackupRetained, Invalid, Busy, Collision, Failed,
    PromotionFailed, RollbackFailed, CleanupRequired
};
// Synchronous foreground-only adapter; NOT a transport/UI entry point.
// Caller guarantees confirmed stopped/settled transport, no pending publication,
// audition, legacy storage work or concurrent pathname mutation throughout Run
// and Cleanup. Never call in audio callbacks/interrupts. Each SD action uses the
// existing exclusion guard (do not hold an outer SD lease across these calls).
// Owner, backend registry and source pins must outlive the transaction. No
// destructor cleanup: CanDestroy() must be true before destroying this object
// (FsFile itself has a library destructor). Caller supplies reusable scratch;
// it must be distinct from source and is released before Run returns.
class FsSave final:private Writer,private Store,private Reader,private ReaderControl {
    FsBackend backend_{true};
    mutable FsFile file_;
    Document* scratch_=nullptr; // Borrowed only during synchronous Save.
    uint32_t length_=0,refs_=0;
    bool readable_=false,candidate_=false,previous_=false,promoted_=false;
    char path_[256]{},candidate_path_[266]{},previous_path_[265]{};
    SaveOutcome outcome_=SaveOutcome::Idle;
    SaveOutcome cause_=SaveOutcome::Idle;
    Result detail_=Result::Ok;
    class Scope {
        bool held_=BroTracker::SdAccess::AcquireWrite();
    public:
        ~Scope(){if(held_)BroTracker::SdAccess::ReleaseWrite();}
        explicit operator bool()const noexcept{return held_;}
    };
    void retain() noexcept override {++refs_;}
    void release() noexcept override {--refs_;} // Explicit cleanup only.
    const Reader* reader() const noexcept override {return readable_?this:nullptr;}
    uint32_t count() const noexcept override {return refs_;}
    uint32_t size() const override {return length_;}
    const void* identity() const override {return this;}
    unsigned read(uint32_t at,void* p,unsigned n) const override {
        return readable_ && const_cast<FsBackend&>(backend_).Read(file_,at,p,n)?n:0;
    }
    unsigned write(uint32_t at,const void* p,unsigned n) override {return backend_.Write(file_,at,p,n)?n:0;}
    bool resize(uint32_t n) override {return backend_.Resize(file_,n);}
    bool finish() override;
    ReaderOwner reopen() override;
    bool promote() override;
    bool Rename(const char*,const char*) noexcept;
    bool OpenRead(const char*) noexcept;
public:
    FsSave() noexcept=default;
    FsSave(const FsSave&)=delete;
    FsSave& operator=(const FsSave&)=delete;
    BTT_FLASHMEM("fs_save_run") SaveOutcome Run(const Document& source,FsBackings& registry,FsBackings::Token,
        const char* destination,Document& scratch,Workspace&,bool stopped_settled,uint32_t bpm=0) noexcept;
    // Retries only owned artifacts. Failed rollback restores previous first;
    // never deletes the sole previous valid copy. Does not change outcome history.
    BTT_FLASHMEM("fs_save_cleanup") bool Cleanup(bool stopped_settled) noexcept;
    bool CanDestroy() const noexcept {return !refs_ && !file_.isOpen() && !candidate_ && !previous_;}
    SaveOutcome outcome() const noexcept {return outcome_;}
    SaveOutcome cause() const noexcept {return cause_;} // Before cleanup masks a promotion failure.
    Result detail() const noexcept {return detail_;}
    bool candidate_retained() const noexcept {return candidate_;}
    bool previous_retained() const noexcept {return previous_;}
    bool promoted() const noexcept {return promoted_;}
};
}

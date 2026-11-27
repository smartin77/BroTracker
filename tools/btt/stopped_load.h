#pragma once
// Production foreground transaction. SD/media exclusivity is a caller contract;
// arbitrary input files are copied into sealed, registry-owned private backing.
#include "fs_backend.h"
#include "sd_source_service.h"
#include "bringup/instrument_commit.h"
#include "bringup/pattern_file_store.h"
#include "bringup/pattern_player_selection.h"

namespace btt::production {
using namespace BroTracker;
using namespace prototype;
class LoadWriteScope {
    bool held_=SdAccess::AcquireWrite();
public:
    ~LoadWriteScope(){if(held_)SdAccess::ReleaseWrite();}
    explicit operator bool() const noexcept {return held_;}
};
enum class LoadPhase { Empty, Open, Copy, Seal, Validate, Scan, Prepare, Project, Ready, Submitted, Cleanup, Complete, Failed };
class StoppedLoad {
    FsBackend backend_{true};
    FsBackings backings_{backend_};
    FsBackings::Output output_;
    FsBackings::Token tokens_[2]{};
    bool token_live_[2]{};
    Candidate owners_[2];
    int active_=-1;
    unsigned staging_=0;
    SdSourceService sources_;
    FsFile input_;
    ReaderOwner sealed_;
    Document document_;
    Workspace work_;
    Pattern pattern_;
    AdmissionScratch scratch_;
    RealtimePattern projection_;
    InstrumentDefinitions definitions_{false};
    VariableRateSampleBindings bindings_;
    struct RuntimeMap {uint32_t ids[99]{};unsigned count=0;} maps_[2];
    uint32_t bytes_=0,offset_=0,pattern_id_=0,tempo_=0,publication_=0;
    unsigned omitted_=0;
    char path_[kSamplePathMaximum+1]{};
    LoadPhase phase_=LoadPhase::Empty;
    const char* reason_="none";
    bool committed_cleanup_=false;
    void ReleaseDocument() noexcept {
        document_.backing.reset();document_.accepted=false;document_.count=0;
    }
    void Fail(const char* reason) noexcept {reason_=reason;phase_=LoadPhase::Cleanup;}
    BTT_FLASHMEM("load_close") bool CloseInput() noexcept;
    BTT_FLASHMEM("load_cleanup") void Cleanup() noexcept;
    BTT_FLASHMEM("load_project") bool Project() noexcept;
public:
    explicit StoppedLoad(SdSampleStreaming& producer) noexcept:sources_(producer){}
    StoppedLoad(const StoppedLoad&)=delete;
    StoppedLoad& operator=(const StoppedLoad&)=delete;
    // ready must come from control.StorageReady() under interrupt exclusion,
    // plus platform storage/audition/legacy-operation exclusion.
    BTT_FLASHMEM("load_begin") bool Begin(const char* path,uint32_t initial_pattern,bool ready) noexcept;
    BTT_FLASHMEM("load_step") void Step(bool ready) noexcept;
    BTT_FLASHMEM("load_cancel") bool Cancel() noexcept;
    bool Busy() const noexcept {return phase_>=LoadPhase::Open && phase_<=LoadPhase::Cleanup;}
    bool RetainsTune() const noexcept {return active_>=0;}
    LoadPhase phase() const noexcept {return phase_;}
    const char* reason() const noexcept {return reason_;}
    unsigned omitted() const noexcept {return omitted_;}
    uint32_t pattern_id() const noexcept {return pattern_id_;}
    uint32_t tempo() const noexcept {return tempo_;}
    const Candidate* active() const noexcept {return active_<0?nullptr:&owners_[active_];}
    uint32_t StableId(unsigned slot) const noexcept {return active_<0 || slot>=maps_[active_].count?0:maps_[active_].ids[slot];}
    const RealtimePattern& projection() const noexcept {return projection_;}
    const InstrumentDefinitions& definitions() const noexcept {return definitions_;}
    const VariableRateSampleBindings& bindings() const noexcept {return bindings_;}
    // Caller excludes audio. Uses the ordinary publication identity/frozen
    // pattern owner and FIFO; no separate publication-ID domain.
    BTT_FLASHMEM("load_submit") bool Submit(SelectedPatternBringUpControl& control,PatternPublication& publication,uint32_t now) noexcept;
    // Exactly this transaction's ACK, before ordinary readback synchronization.
    BTT_FLASHMEM("load_complete") bool Complete(const PatternAppliedRequest&,InstrumentDefinitions&,VariableRateSampleBindings&,
                  InstrumentAvailability*) noexcept;
};
static_assert(sizeof(StoppedLoad)<=65536,"Stopped-load simultaneous state exceeds RAM2 reservation");
}

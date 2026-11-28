#pragma once
// Production foreground transaction. SD/media exclusivity is a caller contract;
// arbitrary input files are copied into sealed, registry-owned private backing.
#include "fs_backend.h"
#include "fs_save.h"
#include "export.h"
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
enum class SaveStatus { Saved, BackupRetained, Rejected, Failed, RecoveryRequired };
struct SaveReport {
    uint32_t id=0;
    SaveStatus status=SaveStatus::Rejected;
    SaveOutcome outcome=SaveOutcome::Idle,cause=SaveOutcome::Idle;
    Result detail=Result::Ok;
    bool committed=false;
    const char* reason="none";
};
enum class RecoveryStatus { Cleaned, Rejected, Required };
struct RecoveryReport {uint32_t id=0,target=0;RecoveryStatus status=RecoveryStatus::Rejected;const char* reason="none";};
class StoppedLoad {
    FsBackend backend_{true};
    FsBackings backings_{backend_};
    FsBackings::Output output_;
    FsBackings::Token tokens_[2]{};
    bool token_live_[2]{};
    // Candidates release source leases during destruction; service outlives them.
    SdSourceService sources_;
    Candidate owners_[2];
    int active_=-1;
    unsigned staging_=0;
    FsFile input_;
    ReaderOwner sealed_;
    Document document_;
    Workspace work_;
    // Only one is live: load decoding or synchronous Save comparison. Neither
    // is an active playback buffer. All comparison pins are released by Run.
    union ScratchBuffers {
        Pattern pattern;
        Document comparison;
        ScratchBuffers():pattern(){}
        ~ScratchBuffers(){}
    } buffers_;
    bool comparing_=false;
    FsSave save_;
    Export export_;
    uint32_t accepted_generation_=0;
    SaveReport last_save_,transaction_;
    RecoveryReport last_recovery_;
    AdmissionScratch scratch_;
    RealtimePattern projection_;
    InstrumentDefinitions definitions_{false};
    VariableRateSampleBindings bindings_;
    struct RuntimeMap {uint32_t ids[99]{};unsigned count=0;uint32_t whole_crc=0;} maps_[2];
    uint32_t bytes_=0,offset_=0,pattern_id_=0,tempo_=0,publication_=0;
    unsigned omitted_=0;
    char path_[kSamplePathMaximum+1]{};
    char active_path_[kSamplePathMaximum+1]{};
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
    void UsePattern() noexcept;
    void UseComparison() noexcept;
public:
    explicit StoppedLoad(SdSampleStreaming& producer) noexcept:sources_(producer){}
    ~StoppedLoad() {if(comparing_)buffers_.comparison.~Document();else buffers_.pattern.~Pattern();}
    StoppedLoad(const StoppedLoad&)=delete;
    StoppedLoad& operator=(const StoppedLoad&)=delete;
    // ready must come from control.StorageReady() under interrupt exclusion,
    // plus platform storage/audition/legacy-operation exclusion.
    BTT_FLASHMEM("load_begin") bool Begin(const char* path,uint32_t initial_pattern,bool ready) noexcept;
    BTT_FLASHMEM("load_step") void Step(bool ready) noexcept;
    BTT_FLASHMEM("load_cancel") bool Cancel() noexcept;
    bool Busy() const noexcept {return phase_>=LoadPhase::Open && phase_<=LoadPhase::Cleanup;}
    bool ExportActive() const noexcept {return export_.Active();}
    void ExportDisconnect() noexcept {export_.NewConnection();}
    BTT_FLASHMEM("load_export") const char* ExportCommand(const BroTracker::BttExportRequest& r,const char* boot,bool ready,uint32_t now,char* reply) noexcept {
        return export_.Command(r,active_>=0?owners_[active_].backing():ReaderOwner{},accepted_generation_,
            active_>=0?maps_[active_].whole_crc:0,boot,ready && !Busy() && !SaveRecoveryPending(),now,reply);
    }
    BTT_FLASHMEM("load_export_poll") bool ExportPoll(uint32_t now,bool ready,char* reply) noexcept {return export_.Poll(now,ready,reply);}
    bool RetainsTune() const noexcept {return active_>=0;}
    const char* active_path() const noexcept {return active_path_;}
    bool SaveRecoveryPending() const noexcept {return !save_.CanDestroy();}
    // ready is sampled with the existing initialized IDLE/settled core+MIDI,
    // publication, audition and storage exclusion under interrupt protection.
    // Do not hold interrupt exclusion during these synchronous foreground calls.
    // IDs are strictly increasing per verb for this boot, never reset by HELLO
    // or LOAD. Only the latest request is replayable; older IDs reject without IO.
    BTT_FLASHMEM("stopped_save") SaveReport Save(uint32_t id,bool ready) noexcept;
    BTT_FLASHMEM("stopped_save_get") SaveReport SavedStatus(uint32_t id) const noexcept;
    BTT_FLASHMEM("stopped_recover") RecoveryReport Recover(uint32_t id,uint32_t target,bool ready) noexcept;
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

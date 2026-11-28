#include "stopped_load.h"
#include <cstring>

namespace btt::production {
bool StoppedLoad::CloseInput() noexcept {
    if(!input_.isOpen())return true;
    LoadWriteScope lease;
    return lease && input_.close();
}
bool StoppedLoad::Begin(const char* path,uint32_t initial_pattern,bool ready) noexcept {
    if(!ready || Busy() || !initial_pattern || !path || !ValidSamplePath(path))return false;
    // The reserved registry namespace is never an arbitrary input domain.
    // FAT names are case-insensitive; rejecting only literal spelling could
    // allow retirement of an old snapshot to remove a requested "original".
    const char* directory=BTT_BACKING_DIRECTORY+1;
    const auto lower=[](char c){return c>='A' && c<='Z'?char(c-'A'+'a'):c;};
    unsigned prefix=0;
    while(directory[prefix] && path[prefix] && lower(directory[prefix])==lower(path[prefix]))++prefix;
    if(!directory[prefix] && (!path[prefix] || path[prefix]=='/'))return false;
    staging_=active_==0?1:0;
    if(token_live_[staging_] || !owners_[staging_].CanDestroy() || input_.isOpen())return false;
    definitions_.Reset(false);bindings_={};ReleaseDocument();sealed_.reset();
    bytes_=offset_=publication_=omitted_=0;pattern_id_=initial_pattern;tempo_=0;
    std::strcpy(path_,path);committed_cleanup_=false;reason_="pending";phase_=LoadPhase::Open;return true;
}
bool StoppedLoad::Cancel() noexcept {
    if(phase_==LoadPhase::Submitted || committed_cleanup_)return false; // No rollback after publication.
    if(!Busy())return true;
    Fail("cancelled");return true;
}
void StoppedLoad::Cleanup() noexcept {
    if(!CloseInput())return; // Quarantined wrapper remains owned, retry explicitly/next turn.
    if(owners_[staging_].Discard()!=Code::Ok)return;
    definitions_.Reset(false);bindings_={};sealed_.reset();ReleaseDocument();
    (void)output_.Cancel();
    if(token_live_[staging_]) {
        if(backings_.Discard(tokens_[staging_])!=Code::Ok)return;
        token_live_[staging_]=false;
    }
    phase_=committed_cleanup_?LoadPhase::Complete:LoadPhase::Failed;
    if(committed_cleanup_){committed_cleanup_=false;reason_="ok";}
}
void StoppedLoad::Step(bool ready) noexcept {
    // Service advances at most one producer action and itself yields to refills.
    (void)sources_.Service();
    if(phase_==LoadPhase::Cleanup){Cleanup();return;}
    if(!Busy() || phase_==LoadPhase::Submitted || phase_==LoadPhase::Ready)return;
    if(!ready){Fail("device-no-longer-idle");return;}
    switch(phase_) {
    case LoadPhase::Open: {
        LoadWriteScope lease;if(!lease)return;
        // SdFat mkdir fails when the final directory already exists. Reuse it
        // only after a checked directory open/close; a regular file is no namespace.
        // Keep this handle in input_ so a failed close stays owned for Cleanup.
        if((!SD.sdfs.exists(BTT_BACKING_DIRECTORY) && !SD.sdfs.mkdir(BTT_BACKING_DIRECTORY,true)) ||
           !input_.open(BTT_BACKING_DIRECTORY,O_RDONLY)) {Fail("input-open-or-namespace");return;}
        const bool directory=input_.isDirectory();
        if(!input_.close()){Fail("namespace-close-quarantine");return;}
        if(!directory || !input_.open(path_,O_RDONLY)) {Fail("input-open-or-namespace");return;}
        if(input_.isDirectory() || !input_.size() || input_.size()>FsBackings::kMaxBytes){Fail("input-size-capacity");return;}
        bytes_=static_cast<uint32_t>(input_.size());
        // Do not nest registry's SD write guard.
        phase_=LoadPhase::Copy;return;
    }
    case LoadPhase::Copy: {
        if(!token_live_[staging_]) {
            const auto result=backings_.Create(output_,tokens_[staging_]);
            if(result!=Code::Ok){
                FsBackings::Resources resources;
                token_live_[staging_]=backings_.InspectResources(tokens_[staging_],resources);
                Fail("snapshot-create-collision-or-io");return;
            }
            token_live_[staging_]=true;return;
        }
        const unsigned n=std::min<uint32_t>(work_.block.size(),bytes_-offset_);
        {
            SdReadScope lease;if(!lease)return;
            if(input_.size()!=bytes_ || !input_.seekSet(offset_) || input_.read(work_.block.data(),n)!=int(n) || input_.getError()){
                Fail("snapshot-read-or-source-change");return;
            }
        }
        if(output_.write(offset_,work_.block.data(),n)!=n){Fail("snapshot-write");return;}
        offset_+=n;if(offset_==bytes_)phase_=LoadPhase::Seal;return;
    }
    case LoadPhase::Seal:
        if(!CloseInput()){Fail("input-close-quarantine");return;}
        if(output_.Finish(sealed_)!=Code::Ok){Fail("snapshot-seal-or-reopen");return;}
        phase_=LoadPhase::Validate;return;
    case LoadPhase::Validate: {
        const auto result=btt::Validate(sealed_,document_,work_);
        if(result!=Result::Ok){Fail(result==Result::Capacity?"btt-capacity":result==Result::Unsupported?"btt-required-unsupported":"btt-validation-or-io");return;}
        if(!ValidInteractiveTempo(document_.bpm)){Fail("unsupported-bpm");return;}
        tempo_=document_.bpm;phase_=LoadPhase::Scan;return;
    }
    case LoadPhase::Scan:
        if(owners_[staging_].Begin(document_,{pattern_id_,{}},work_,pattern_,scratch_)!=Result::Ok){Fail("initial-pattern-or-definition-scan");return;}
        if(pattern_.rows>kRealtimePatternRowCapacity || pattern_.channels>kRealtimePatternChannelCapacity){Fail("runtime-pattern-capacity");return;}
        for(unsigned i=0;i<pattern_.rows*pattern_.channels;++i)
            for(unsigned slot=0;slot<3;++slot)if(pattern_.fx[i][slot]){Fail("initial-pattern-fx-unsupported");return;}
        ReleaseDocument();sealed_.reset(); // Candidate retains every original byte.
        phase_=LoadPhase::Prepare;return;
    case LoadPhase::Prepare: {
        const auto state=owners_[staging_].Step(sources_,scratch_);
        if(state==Admission::Ready)phase_=LoadPhase::Project;
        else if(state==Admission::Blocked || state==Admission::Io) {
            const auto& owner=owners_[staging_];
            const char* reason=owner.unresolved_requirements()?"unresolved-initial-instrument":"required-settings-or-path-unsupported";
            for(unsigned i=0;i<owner.count();++i) {
                const auto* d=owner.At(i);if(!d->required)continue;
                if(d->path_status==PathStatus::Capacity)reason="path-capacity";
                switch(d->source_status) {
                case SourceStatus::Missing:reason="sample-missing";break;
                case SourceStatus::Unsupported:reason="sample-format-unsupported";break;
                case SourceStatus::Capacity:reason="sample-capacity";break;
                case SourceStatus::PathCapacity:reason="path-capacity";break;
                case SourceStatus::Quarantined:reason="sample-close-quarantine";break;
                case SourceStatus::Io:reason="sample-io";break;
                case SourceStatus::StorageUnavailable:reason="sample-storage-unavailable";break;
                case SourceStatus::GenerationExhausted:reason="sample-generation-or-ticket-exhausted";break;
                case SourceStatus::Stale:reason="sample-generation-stale";break;
                default:break;
                }
            }
            Fail(reason);
        }
        return;
    }
    case LoadPhase::Project:
        if(Project())phase_=LoadPhase::Ready;
        return;
    default:return;
    }
}
bool StoppedLoad::Project() noexcept {
    auto& owner=owners_[staging_];
    definitions_.Reset(false);bindings_={};omitted_=0;maps_[staging_].count=0;
    for(unsigned i=0;i<owner.count();++i) {
        const auto* d=owner.At(i);const auto& saved=d->saved;
        // Runtime slots contain representable definitions only. Stable IDs for
        // every omitted definition and its exact settings remain in the owner.
        if(d->settings!=SettingStatus::Supported || (saved.type==1 && d->path_status!=PathStatus::Valid && d->path_status!=PathStatus::Owned)) {
            if(d->required){Fail("required-definition-unsupported");return false;}
            ++omitted_;continue;
        }
        const char* path=nullptr;
        if(saved.type==1) {
            const auto n=saved.resource_length-1;
            if(n>kSamplePathMaximum || owner.backing()->read(saved.resource_offset+1,scratch_.path,n)!=n){Fail("definition-path-io");return false;}
            scratch_.path[n]=0;path=scratch_.path;
        }
        const unsigned slot=definitions_.Count();
        if(!definitions_.Append(slot,saved.type,path,d->effective_midi,
            saved.type==1?SampleTuning{d->effective_root,d->effective_fine}:kDefaultSampleTuning)){
            Fail("runtime-definition-or-path-capacity");return false;
        }
        maps_[staging_].ids[slot]=saved.id;++maps_[staging_].count;
        VariableRateSampleBinding binding;binding.instrument=slot;
        binding.type=saved.type==1?InstrumentType::Sample:saved.type==2?InstrumentType::MIDI:InstrumentType::Synth;
        binding.midi_channel=d->effective_midi;
        if(saved.type==1) {
            if(!d->source)continue; // Unused Sample definition: saved, no runtime PCM.
            const char* source_path=nullptr;
            if(!sources_.Access(d->source.token(),binding.sample,source_path)){Fail("sample-generation-stale");return false;}
            binding.reference_note=d->effective_root;binding.fine_tune_cents=d->effective_fine;binding.sample_level_percent=80;
        }
        if(bindings_.count==kVariableRateSampleBindingCapacity){Fail("runtime-binding-capacity");return false;}
        bindings_.bindings[bindings_.count++]=binding;
    }
    projection_={};projection_.active_rows=pattern_.rows;projection_.active_channels=pattern_.channels;
    for(unsigned i=0;i<pattern_.rows*pattern_.channels;++i) {
        const auto cell=pattern_.At(i);unsigned slot=kNoInstrumentUpdate;
        if(cell.instrument) {
            slot=0;while(slot<definitions_.Count() && maps_[staging_].ids[slot]!=cell.instrument)++slot;
            if(slot==definitions_.Count()){Fail("initial-instrument-not-representable");return false;}
        }
        const unsigned row=i/pattern_.channels,channel=i%pattern_.channels;
        projection_.cells[row][channel]={cell.note,static_cast<uint8_t>(slot)};
        projection_.note_off_timing[row][channel]=static_cast<NoteOffTiming>(cell.timing);
    }
    if(VariableRatePatternPlayer::ValidateTune(projection_,bindings_)!=VariableRatePatternPlayerStatus::Success){Fail("initial-runtime-musical-projection");return false;}
    return true;
}
bool StoppedLoad::Submit(SelectedPatternBringUpControl& control,PatternPublication& publication,uint32_t now) noexcept {
    if(phase_!=LoadPhase::Ready || !control.StorageReady() || publication.Busy())return false;
    if(!StageStoredPattern(publication,projection_,tempo_,now)){Fail("publication-capacity-or-identity");return false;}
    if(control.SubmitStoppedTune(*publication.Frozen(),bindings_,publication.Identity(),tempo_)!=PatternRequestStatus::Accepted){
        publication.Finish();Fail("publication-rejected");return false;
    }
    publication_=publication.Identity();phase_=LoadPhase::Submitted;return true;
}
bool StoppedLoad::Complete(const PatternAppliedRequest& applied,InstrumentDefinitions& definitions,
    VariableRateSampleBindings& bindings,InstrumentAvailability* availability) noexcept {
    if(phase_!=LoadPhase::Submitted || applied.request!=PatternRequest::Replace || applied.publication_id!=publication_)return false;
    if(applied.result!=PatternPlayerStatus::Success){Fail("audio-publication-rejected");return true;}
    // Candidate path refs keep assignment fallible allocation out of the handoff;
    // all matching strings already exist in the singleton pool.
    definitions=definitions_;bindings=bindings_;
    if(availability)for(unsigned slot=0;slot<99;++slot) {
        availability[slot]=InstrumentAvailability::Undefined;
        const auto* d=definitions.At(slot);if(!d)continue;
        availability[slot]=d->type==InstrumentType::Sample?InstrumentAvailability::NotPrepared:InstrumentAvailability::Ready;
        for(unsigned i=0;i<bindings.count;++i)if(bindings.bindings[i].instrument==slot)availability[slot]=InstrumentAvailability::Ready;
    }
    const int previous=active_;active_=staging_;
    definitions_.Reset(false);bindings_={};
    reason_="ok";phase_=LoadPhase::Complete;
    if(previous>=0) {
        // Audio Configure has detached the old voices/bindings. Its producer
        // publication completed before this foreground ACK was consumed.
        staging_=previous;
        if(owners_[previous].Discard()!=Code::Ok || backings_.Discard(tokens_[previous])!=Code::Ok){
            committed_cleanup_=true;reason_="committed-old-backing-cleanup-pending";phase_=LoadPhase::Cleanup;
        } else token_live_[previous]=false;
    }
    return true;
}
}

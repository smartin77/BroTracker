#pragma once
#include "host_pattern_publication.h"
namespace BroTracker {
constexpr std::uint32_t kPreviewDebounceMs=75, kPreviewMaximumDebounceMs=250;
enum class HostPreviewPhase { Recovering, Ready, Sending, Waiting, Accepted, Synchronized, Rejected };
// Single-owner portable coordinator. Fixed frozen TX + latest desired revision;
// accepted metadata and readback staging never alias editing. No I/O or callbacks.
class HostPatternPreview {
public:
    void SetBaseline(const DevicePatternSnapshot& baseline) noexcept {
        if(baseline.version==2 && baseline.tempo_hundredths && baseline.pattern.active_rows &&
            baseline.pattern.active_rows<=kRealtimePatternRowCapacity && baseline.pattern.active_channels &&
            baseline.pattern.active_channels<=kRealtimePatternChannelCapacity) {
            tempo_=baseline.tempo_hundredths; rows_=baseline.pattern.active_rows; channels_=baseline.pattern.active_channels;
        }
    }
    void SetWorkingTempo(std::uint32_t value) noexcept { if(value) tempo_=value; }
    bool Supported() const noexcept { return supported_; }
    HostPreviewPhase Phase() const noexcept { return phase_; }
    const char* Status() const noexcept { return status_; }
    bool Recovering() const noexcept { return phase_==HostPreviewPhase::Recovering; }
    bool Sending() const noexcept { return phase_==HostPreviewPhase::Sending; }
    bool TransferBusy() const noexcept { return Sending() || phase_==HostPreviewPhase::Waiting; }
    bool CanPublish() const noexcept { return !TransferBusy() && !Recovering(); }
    void RetainLocal(bool dirty) noexcept { retain_local_=dirty; }
    void Disconnect() noexcept {
        if(!retain_local_ && phase_==HostPreviewPhase::Synchronized) latest_valid_=attempted_valid_=false;
        reconnecting_=true;
        sparse_supported_=false;
        supported_=next_fresh_=state_fresh_=false; next_=0; accepted_=current_=0; recovered_ready_=false;
        readback_.Clear(); read_active_=read_label_=false; verified_=pending_verified_=false;
        phase_=HostPreviewPhase::Recovering; Set("Preview outcome unknown; recovering device state");
    }
    void DiscoveryRequested() noexcept { discovery_=true; }
    bool NeedsStatus() const noexcept { return supported_ && (!next_fresh_ || (Recovering() && !state_fresh_)); }
    void Observe(const RealtimePattern& draft,std::uint32_t now) noexcept {
        if (!latest_valid_ || !SamePattern(latest_,draft)) {
            latest_=draft; latest_valid_=true; changed_=now;
            if(!debouncing_) { first_change_=now; debouncing_=true; }
            if(!Recovering() && !TransferBusy()) {
                if(phase_==HostPreviewPhase::Rejected) phase_=HostPreviewPhase::Ready;
                Set("Preview pending transfer");
            }
        }
    }
    // Captured coordinates/value, never the current cursor. One latest desired slot per cell.
    void ObserveCell(const SparsePreviewCell& cell,std::uint32_t now) noexcept {
        if(!latest_valid_ || cell.row>=kRealtimePatternRowCapacity || cell.channel>=kRealtimePatternChannelCapacity ||
            cell.row>=latest_.active_rows || cell.channel>=latest_.active_channels) return;
        auto& previous=latest_.cells[cell.row][cell.channel];
        auto& timing=latest_.note_off_timing[cell.row][cell.channel];
        if(previous.note==cell.note && previous.instrument==cell.instrument && timing==cell.timing) return;
        previous={cell.note,cell.instrument}; timing=cell.timing; changed_=now;
        if(!debouncing_) {first_change_=now;debouncing_=true;}
        if(!Recovering() && !TransferBusy()) {if(phase_==HostPreviewPhase::Rejected)phase_=HostPreviewPhase::Ready;Set("Preview pending transfer");}
    }
    const RealtimePattern* RecoveredDraft() const noexcept {
        return recovered_ready_ ? &latest_ : nullptr;
    }
    void RecoveredInstalled() noexcept { recovered_ready_=false; }
    // Only complete checksum-verified working/pending metadata, never local intent.
    const RealtimePattern* VerifiedWorking() const noexcept {
        return verified_ && (!pending_id_ || pending_verified_) && !Recovering() ?
            (pending_id_ ? &pending_ : &applied_) : nullptr;
    }
    bool BootWorkingState() const noexcept {
        return VerifiedWorking() && !published_id_ && !working_id_ && !pending_id_;
    }
    void ConfirmMatchingWorking() noexcept {
        if(const auto* remote=VerifiedWorking()) {
            latest_=attempted_=*remote; latest_valid_=attempted_valid_=true;
            recovered_ready_=false; retain_local_=false;
            phase_=pending_id_ ? HostPreviewPhase::Accepted : HostPreviewPhase::Synchronized;
            Set("Retained working pattern matches readback");
        }
    }

    // One explicit host recovery attempt, only after verified stopped readback.
    // Use a full transfer so APPLIED is followed by complete checksum readback.
    bool RecoverRetained(const RealtimePattern& draft,std::uint32_t now) noexcept {
        if(!VerifiedWorking() || TransferBusy() || draft.active_rows!=rows_ || draft.active_channels!=channels_) return false;
        latest_=draft;latest_valid_=true;attempted_valid_=false;recovered_ready_=false;
        changed_=first_change_=now;debouncing_=true;force_full_=true;
        phase_=HostPreviewPhase::Ready;suspended_=false;Set("Recovering retained host edits to working RAM; not an SD-save confirmation");
        return true;
    }
    void FileLoaded() noexcept {
        latest_valid_=attempted_valid_=retain_local_=recovered_ready_=false;
        verified_=pending_verified_=state_fresh_=next_fresh_=false;
        readback_.Clear();read_active_=read_label_=false;
        phase_=HostPreviewPhase::Recovering;suspended_=true;
    }
    void Published(const RealtimePattern& pattern) noexcept {
        latest_=applied_=attempted_=pattern; latest_valid_=attempted_valid_=true;
        accepted_=pending_id_=working_id_=0; suspended_=false;
        verified_=pending_verified_=true; phase_=HostPreviewPhase::Synchronized;
        next_fresh_=false; retain_local_=false; Set("Working pattern synchronized; published baseline");
    }
    void Suspend(bool value) noexcept { suspended_=value; }
    bool TryBegin(const DevicePatternSnapshot& baseline,std::uint32_t now) noexcept {
        if(supported_ && !suspended_ && !Recovering() && !TransferBusy() && latest_valid_ &&
            (latest_.active_rows!=rows_ || latest_.active_channels!=channels_)) {
            phase_=HostPreviewPhase::Rejected;Set("Preview rejected: dimensions differ from device");return false;
        }
        // Undo/Restore can reach a verified revision without another transaction.
        // Do not leave an obsolete rejection/pending label, or consume an identity.
        if(sparse_supported_ && !suspended_ && !Recovering() && !TransferBusy() && latest_valid_ && verified_ &&
            (!pending_id_ || pending_verified_) && SamePattern(latest_,pending_id_?pending_:applied_)) {
            attempted_=latest_; attempted_valid_=true;
            if(pending_id_) {
                if(phase_!=HostPreviewPhase::Accepted) started_=now;
                phase_=HostPreviewPhase::Accepted; Set("Preview accepted; waiting for row application");
            } else {phase_=HostPreviewPhase::Synchronized;Set("Working preview synchronized");}
            return false;
        }
        if(supported_ && next_fresh_ && !next_) { Set("Preview identities exhausted; device restart required"); return false; }
        if(!supported_ || suspended_ || Recovering() || TransferBusy() || !next_fresh_ || !next_ || !latest_valid_ ||
            (attempted_valid_ && SamePattern(latest_,attempted_)) ||
            (now-changed_<kPreviewDebounceMs && now-first_change_<kPreviewMaximumDebounceMs)) return false;
        delta_transfer_=sparse_supported_ && verified_ && !force_full_;force_full_=false;
        delta_count_=delta_line_=0;
        if(delta_transfer_) {
            const auto& base=pending_id_ && pending_verified_?pending_:applied_;
            delta_base_=pending_id_ && pending_verified_?pending_id_:working_id_;
            for(unsigned row=0;row<rows_;++row) for(unsigned channel=0;channel<channels_;++channel)
                if(latest_.cells[row][channel].note!=base.cells[row][channel].note ||
                    latest_.cells[row][channel].instrument!=base.cells[row][channel].instrument ||
                    latest_.note_off_timing[row][channel]!=base.note_off_timing[row][channel])
                    delta_indices_[delta_count_++]=static_cast<std::uint16_t>(row*kRealtimePatternChannelCapacity+channel);
            if(!delta_count_) {attempted_=latest_;attempted_valid_=true;return false;}
            delta_hash_=SparsePreviewHeaderHash(next_,delta_base_,delta_count_);
            for(unsigned i=0;i<delta_count_;++i) delta_hash_=SparsePreviewHashCell(delta_hash_,DeltaCell(latest_,i));
        } else {
            serializer_=HostPatternPublication{};
            if(!serializer_.Begin(next_,baseline,latest_,now)) return false;
        }
        current_=next_; attempted_high_water_=current_; next_=0; next_fresh_=false; debouncing_=false; attempted_=latest_; attempted_valid_=true;
        receiving_=submitted_=commit_possible_=false; phase_=HostPreviewPhase::Sending; started_=now;
        Set("Preview sending"); return true;
    }
    bool FormatLine(char* text,std::size_t capacity,std::size_t& length) const noexcept {
        if(!Sending()) return false;
        if(!delta_transfer_) return serializer_.FormatLine(text,capacity,length,true);
        int n=0;
        if(!delta_line_) n=std::snprintf(text,capacity,"BTPREVIEW1 DBEGIN 1 %" PRIu32 " %" PRIu32 " %u\n",current_,delta_base_,delta_count_);
        else if(delta_line_<=delta_count_) {
            const auto cell=DeltaCell(attempted_,delta_line_-1);
            n=std::snprintf(text,capacity,"BTPREVIEW1 DCELL 1 %" PRIu32 " %u %u %u %u %u %u\n",current_,
                delta_line_-1,unsigned(cell.row),unsigned(cell.channel),unsigned(cell.note),unsigned(cell.instrument),unsigned(cell.timing));
        } else n=std::snprintf(text,capacity,"BTPREVIEW1 DCOMMIT 1 %" PRIu32 " %u %" PRIu32 "\n",current_,delta_count_,delta_hash_);
        if(n<0 || static_cast<std::size_t>(n)>=capacity) return false;
        length=static_cast<std::size_t>(n);return true;
    }
    void BeforeWrite() noexcept { if(Sending() && CommitLine()) commit_possible_=true; }
    void LineComplete() noexcept {
        if(!Sending()) return;
        const bool commit=CommitLine();
        if(delta_transfer_) ++delta_line_; else serializer_.LineComplete();
        if(commit) {phase_=HostPreviewPhase::Waiting;Set("Preview awaiting device validation");}
    }
    void Tick(std::uint32_t now) noexcept {
        if((TransferBusy() || phase_==HostPreviewPhase::Accepted) && now-started_>=kHostPublicationTimeoutMs) Recover();
        if(readback_.Tick(now)==SnapshotAssemblyStatus::Timeout) ReadFailure();
    }
    void ReadFailure() noexcept { read_active_=read_label_=false; Recover(); Set("Preview readback unavailable; recovering"); }
    bool BeginRead(std::uint32_t id,std::uint32_t now,char* text,std::size_t capacity,std::size_t& length) noexcept {
        if(!supported_ || !Recovering() || !state_fresh_ || !tempo_ || read_active_ || !id) return false;
        selector_=verified_ ? 1 : 0;
        if(selector_ && (!pending_id_ || pending_verified_)) return false;
        const int n=std::snprintf(text,capacity,"BTPREVIEW1 GET 1 %" PRIu32 " %u\n",id,selector_);
        if(n<0 || static_cast<std::size_t>(n)>=capacity) return false;
        if(!readback_.Request(id,now,2)) return false;
        read_id_=id; read_active_=true; read_label_=false; length=static_cast<std::size_t>(n); return true;
    }
    bool Response(const char* line,std::size_t length,std::uint32_t now) noexcept {
        if(!line || length>=kPatternSnapshotLineCapacity) return false;
        if(length==sizeof("BTPREVIEW1 CAP 1")-1 && !std::memcmp(line,"BTPREVIEW1 CAP 1",length)) {
            supported_=true;
            if(Recovering() && !status_[0]) Set("Recovering device working pattern");
            return true;
        }
        if(length==sizeof(kSparsePreviewCapability)-1 && !std::memcmp(line,kSparsePreviewCapability,length)) {
            if(!supported_) return false; sparse_supported_=true; return true;
        }
        std::uint32_t f[5]{};
        if(SnapshotFields(line,length,"BTPREVIEW1 NEXT ",f,2) && f[0]==1 && supported_ && discovery_) {
            // NEXT is a device-lifetime high-water mark, including rejected BEGINs.
            // A rollback after reconnect proves the old working context is gone.
            if(reconnecting_) {
                if(f[1] && f[1]-1 < consumed_high_water_) {
                    latest_valid_=attempted_valid_=false; debouncing_=false;
                    retain_local_=false; consumed_high_water_=attempted_high_water_=0;
                    Set("Device identities reset; loading authoritative working pattern");
                }
                reconnecting_=false;
            }
            const auto consumed=f[1] ? f[1]-1 : UINT32_MAX;
            if(consumed>consumed_high_water_) consumed_high_water_=consumed;
            // BEGIN can be rejected before staging consumes its identity. Accept
            // authoritative NEXT, but never reuse a local attempted identity.
            next_=f[1];
            if(next_ && next_<=attempted_high_water_)
                next_=attempted_high_water_==UINT32_MAX ? 0 : attempted_high_water_+1;
            next_fresh_=true; return true;
        }
        if(SnapshotFields(line,length,"BTPREVIEW1 STATE ",f,4) && f[0]==1 && supported_ && discovery_) {
            if(f[3] && f[3]<=f[2]) return false;
            if(Recovering() && (published_id_!=f[1] || working_id_!=f[2] || pending_id_!=f[3])) {
                readback_.Clear(); read_active_=read_label_=false; verified_=pending_verified_=false;
            }
            // Revision IDs and verified cell copies are one ownership unit in every
            // phase. Relabeling old cells while Ready/Rejected/Sending can produce
            // a valid checksum against an unrelated base and falsely claim sync.
            if((verified_ || pending_verified_ || phase_==HostPreviewPhase::Accepted ||
                (sparse_supported_ && phase_==HostPreviewPhase::Synchronized)) &&
                (published_id_!=f[1] || working_id_!=f[2] || pending_id_!=f[3])) Recover();
            published_id_=f[1]; working_id_=f[2]; pending_id_=f[3]; state_fresh_=true;
            return true;
        }
        if(SnapshotFields(line,length,"BTPREVIEW1 RECEIVING 1 ",f,1)) {
            if((!Sending() && phase_!=HostPreviewPhase::Waiting) || f[0]!=current_ || receiving_) return false;
            receiving_=true;
            if(current_>consumed_high_water_) consumed_high_water_=current_;
            return true;
        }
        if(SnapshotFields(line,length,"BTPREVIEW1 SUBMITTED 1 ",f,1)) {
            if(phase_!=HostPreviewPhase::Waiting || f[0]!=current_ || !receiving_ || submitted_) return false;
            submitted_=true; return true;
        }
        if(SnapshotFields(line,length,"BTPREVIEW1 ACCEPTED 1 ",f,1)) {
            if(phase_!=HostPreviewPhase::Waiting || f[0]!=current_ || !submitted_) return false;
            if(delta_transfer_) {pending_=attempted_;pending_verified_=true;}
            accepted_=current_; pending_id_=current_; phase_=HostPreviewPhase::Accepted; started_=now;
            Set("Preview accepted; waiting for row application"); return true;
        }
        if(SnapshotFields(line,length,"BTPREVIEW1 APPLIED 1 ",f,1)) {
            if(!accepted_ || f[0]!=accepted_) return false;
            const bool newer_ready=!TransferBusy() && accepted_==current_ && attempted_valid_ &&
                latest_valid_ && !SamePattern(latest_,attempted_);
            const auto applied_id=accepted_;
            if(sparse_supported_ && pending_verified_) {applied_=pending_;verified_=true;}
            working_id_=accepted_; accepted_=0; pending_id_=0;
            if(sparse_supported_ && delta_transfer_) {
                pending_verified_=false;
                if(!TransferBusy() && (applied_id==current_ || (latest_valid_ && SamePattern(latest_,applied_)))) {
                    phase_=latest_valid_ && SamePattern(latest_,applied_)?HostPreviewPhase::Synchronized:HostPreviewPhase::Ready;
                    next_fresh_=false; Set(phase_==HostPreviewPhase::Synchronized ? "Working preview synchronized" : "Newer draft pending");
                }
                return true;
            }
            // Legacy full transfers still require exact readback. Their previous
            // cache must not become a sparse base if capability arrives later.
            verified_=pending_verified_=false;
            if(newer_ready) {
                // A matched SUBMITTED/ACCEPTED/APPLIED chain establishes receipt
                // of the frozen candidate. Do not read obsolete cells before
                // sending the newer desired revision. Final convergence still
                // requires exact readback; uncertainty always uses recovery.
                phase_=HostPreviewPhase::Ready; next_fresh_=false;
                Set("Newer draft pending; intermediate preview applied");
                return true;
            }
            if(!TransferBusy()) { Recover(); Set("Preview verifying working readback"); }
            return true;
        }
        if(SnapshotFields(line,length,"BTPREVIEW1 SUPERSEDED 1 ",f,1)) {
            if(!accepted_ || f[0]!=accepted_) return false;
            accepted_=pending_id_=0;
            if(!TransferBusy()) Recover();
            return true;
        }
        if(SnapshotFields(line,length,"BTPREVIEW1 ERROR 1 ",f,2)) {
            if(!TransferBusy() || f[0]!=current_ || f[1]<2 || f[1]>12) return false;
            if(f[1]==12 || (commit_possible_ && (f[1]==3 || f[1]==4))) Recover();
            else { phase_=HostPreviewPhase::Rejected; std::snprintf(status_,sizeof(status_),"Preview rejected: protocol error %u",unsigned(f[1])); }
            return true;
        }
        constexpr const char* reasons[]={"not-configured","missing-instrument","unknown-instrument","unsupported-pitch","invalid-pattern","handoff-error"};
        for(const auto* reason:reasons) {
            char expected[96]; const int n=std::snprintf(expected,sizeof(expected),"BTPREVIEW1 REJECTED 1 %" PRIu32 " %s",current_,reason);
            if(n>=0 && static_cast<std::size_t>(n)==length && !std::memcmp(line,expected,length)) {
                if(phase_!=HostPreviewPhase::Waiting || !submitted_) return false;
                if(!std::strcmp(reason,"handoff-error")) Recover();
                else { phase_=HostPreviewPhase::Rejected; std::snprintf(status_,sizeof(status_),"Preview rejected: %s; draft not playing",reason); }
                return true;
            }
        }
        if(length==sizeof("BTPREVIEW1 READERR 1")-1 && !std::memcmp(line,"BTPREVIEW1 READERR 1",length) && read_active_) {
            ReadFailure(); return true;
        }
        if(SnapshotFields(line,length,"BTPREVIEW1 READ 1 ",f,4)) {
            if(!read_active_ || read_label_ || f[0]!=read_id_ || f[1]!=selector_ || f[2]!=published_id_ ||
                f[3]!=(selector_ ? pending_id_ : working_id_)) return false;
            read_label_=true; return true;
        }
        if(read_active_ && read_label_ && length>10 && !std::memcmp(line,"BTPREVIEW1",10)) {
            char normalized[kPatternSnapshotLineCapacity]{};
            std::memcpy(normalized,line,length); std::memcpy(normalized,"BTPATTERN1",10);
            const auto result=readback_.Accept(normalized,length,now);
            if(result==SnapshotAssemblyStatus::Rejected || result==SnapshotAssemblyStatus::Timeout) ReadFailure();
            if(result==SnapshotAssemblyStatus::Complete) {
                const auto* snapshot=readback_.Snapshot();
                if(!tempo_ || snapshot->tempo_hundredths!=tempo_ || snapshot->pattern.active_rows!=rows_ ||
                    snapshot->pattern.active_channels!=channels_) { ReadFailure(); return true; }
                if(selector_) { pending_=readback_.Snapshot()->pattern; pending_verified_=true; }
                else { applied_=readback_.Snapshot()->pattern; verified_=true; }
                read_active_=read_label_=false;
                if(verified_ && (!pending_id_ || pending_verified_)) {
                    const auto& remote=pending_id_ ? pending_ : applied_;
                    if(!latest_valid_) { latest_=remote; latest_valid_=true; }
                    recovered_ready_=true; accepted_=pending_id_;
                    if(SamePattern(latest_,remote)) {
                        attempted_=latest_; attempted_valid_=true;
                        phase_=pending_id_ ? HostPreviewPhase::Accepted : HostPreviewPhase::Synchronized;
                        started_=now; Set(pending_id_ ? "Preview waiting for application" : "Working pattern synchronized");
                    } else if(attempted_valid_ && SamePattern(latest_,attempted_)) {
                        phase_=HostPreviewPhase::Rejected; Set("Draft not applied; local edits retained (no automatic retry)");
                    } else { phase_=HostPreviewPhase::Ready; Set("Local edits retained; preview pending"); }
                }
            }
            return result!=SnapshotAssemblyStatus::Ignored;
        }
        return false;
    }
private:
    bool CommitLine() const noexcept {return delta_transfer_?delta_line_==delta_count_+1:serializer_.CommitLine();}
    SparsePreviewCell DeltaCell(const RealtimePattern& p,unsigned index) const noexcept {
        const auto row=delta_indices_[index]/kRealtimePatternChannelCapacity;
        const auto channel=delta_indices_[index]%kRealtimePatternChannelCapacity;
        return {static_cast<std::uint8_t>(row),static_cast<std::uint8_t>(channel),p.cells[row][channel].note,
            p.cells[row][channel].instrument,p.note_off_timing[row][channel]};
    }
    bool sparse_supported_=false,delta_transfer_=false,force_full_=false;
    std::uint16_t delta_indices_[kSparsePreviewCellCapacity]{};
    unsigned delta_count_=0,delta_line_=0;
    std::uint32_t delta_base_=0,delta_hash_=0;
    void Set(const char* s) noexcept { std::snprintf(status_,sizeof(status_),"%s",s); }
    void Recover() noexcept {
        phase_=HostPreviewPhase::Recovering; state_fresh_=false; verified_=pending_verified_=false; recovered_ready_=false;
        readback_.Cancel(); read_active_=read_label_=false; Set("Preview outcome uncertain; verifying device state");
    }
    RealtimePattern latest_,attempted_,applied_,pending_;
    HostPatternPublication serializer_;
    PatternSnapshotAssembler readback_;
    HostPreviewPhase phase_=HostPreviewPhase::Recovering;
    bool supported_=false,discovery_=false,next_fresh_=false,state_fresh_=false,suspended_=false;
    bool reconnecting_=false;
    std::uint32_t consumed_high_water_=0,attempted_high_water_=0;
    bool retain_local_=false,debouncing_=false,latest_valid_=false,attempted_valid_=false,receiving_=false,submitted_=false,commit_possible_=false;
    bool read_active_=false,read_label_=false,verified_=false,pending_verified_=false,recovered_ready_=false;
    unsigned selector_=0;
    std::uint32_t current_=0,accepted_=0,next_=0,published_id_=0,working_id_=0,pending_id_=0,read_id_=0;
    std::uint32_t changed_=0,first_change_=0,started_=0,tempo_=0;
    unsigned rows_=0,channels_=0;
    char status_[96]{};
};
}

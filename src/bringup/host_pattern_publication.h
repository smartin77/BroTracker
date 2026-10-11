#pragma once
#include "pattern_publication.h"

namespace BroTracker {
    constexpr std::uint32_t kHostPublicationTimeoutMs = 6000;
    enum class HostPublicationPhase { Idle, Sending, Waiting, Verifying, Uncertain, Success, Rejected, Mismatch };
    inline bool SamePattern(const RealtimePattern& a, const RealtimePattern& b) noexcept {
        if (!a.active_rows || a.active_rows>kRealtimePatternRowCapacity || !a.active_channels || a.active_channels>kRealtimePatternChannelCapacity) return false;
        if (a.active_rows != b.active_rows || a.active_channels != b.active_channels) return false;
        for (unsigned r=0;r<a.active_rows;++r) for (unsigned c=0;c<a.active_channels;++c)
            if (a.cells[r][c].note != b.cells[r][c].note || a.cells[r][c].instrument != b.cells[r][c].instrument ||
                a.note_off_timing[r][c] != b.note_off_timing[r][c]) return false;
        return true;
    }
    // Shared single-owner host transaction; fixed immutable submission copy and
    // bounded line formatting/parsing. No serial/input/UI work or automatic retry.
    // A caller marks possible COMMIT delivery BEFORE attempting an async write.
    class HostPatternPublication {
    public:
        std::uint32_t LastIdentity() const noexcept { return last_id_; }
        HostPublicationPhase Phase() const noexcept { return phase_; }
        bool Blocking() const noexcept { return phase_==HostPublicationPhase::Sending || phase_==HostPublicationPhase::Waiting ||
            phase_==HostPublicationPhase::Verifying || phase_==HostPublicationPhase::Uncertain; }
        bool NeedsReadback() const noexcept { return phase_==HostPublicationPhase::Verifying || phase_==HostPublicationPhase::Uncertain; }
        const char* Status() const noexcept { return status_; }
        bool TakeConfirmed() noexcept { const bool result=confirmed_; confirmed_=false; return result; }
        const RealtimePattern& Frozen() const noexcept { return pattern_; }
        bool Begin(std::uint32_t id, const DevicePatternSnapshot& baseline, const RealtimePattern& draft, std::uint32_t now) noexcept {
            if (!id || id<=last_id_ || Blocking() || baseline.version != 2 || !draft.active_rows || draft.active_rows>kRealtimePatternRowCapacity ||
                !draft.active_channels || draft.active_channels>kRealtimePatternChannelCapacity) return false;
            auto hash=PublicationHeaderHash(draft.active_rows,draft.active_channels);
            for(unsigned r=0;r<draft.active_rows;++r) for(unsigned c=0;c<draft.active_channels;++c) {
                const auto cell=draft.cells[r][c]; const auto timing=draft.note_off_timing[r][c];
                if (!IsPatternNote(cell.note) || !ValidNoteOffTiming(timing)) return false;
                hash=SnapshotHashByte(SnapshotHashByte(SnapshotHashByte(hash,cell.note),cell.instrument),static_cast<std::uint8_t>(timing));
            }
            last_id_=id; pattern_=draft; tempo_=baseline.tempo_hundredths; id_=id; hash_=hash; line_index_=0; started_=now;
            commit_possible_=submitted_=confirmed_=false; phase_=HostPublicationPhase::Sending;
            Set("Publication sending"); return true;
        }
        bool CommitLine() const noexcept { return line_index_==Count()+1; }
        bool FormatLine(char* output,std::size_t capacity,std::size_t& length) const noexcept {
            if (phase_!=HostPublicationPhase::Sending || !output || !capacity) return false;
            int written;
            if (!line_index_) written=std::snprintf(output,capacity,"BTPUBLISH1 BEGIN 1 %" PRIu32 " %u %u %u\n",
                id_,unsigned(pattern_.active_rows),unsigned(pattern_.active_channels),Count());
            else if (CommitLine()) written=std::snprintf(output,capacity,"BTPUBLISH1 COMMIT 1 %" PRIu32 " %u %" PRIu32 "\n",id_,Count(),hash_);
            else {
                const auto index=line_index_-1, row=index/pattern_.active_channels, channel=index%pattern_.active_channels;
                const auto cell=pattern_.cells[row][channel];
                written=std::snprintf(output,capacity,"BTPUBLISH1 CELL 1 %" PRIu32 " %u %u %u %u\n",
                    id_,index,unsigned(cell.note),unsigned(cell.instrument),unsigned(pattern_.note_off_timing[row][channel]));
            }
            if(written<0 || static_cast<std::size_t>(written)>=capacity) return false;
            length=static_cast<std::size_t>(written); return true;
        }
        void BeforeWrite() noexcept { if (phase_==HostPublicationPhase::Sending && CommitLine()) commit_possible_=true; }
        void LineComplete() noexcept {
            if (phase_!=HostPublicationPhase::Sending) return;
            if(CommitLine()) { phase_=HostPublicationPhase::Waiting; Set("Publication awaiting application"); }
            else ++line_index_;
        }
        void StopRequested() noexcept {
            if (phase_==HostPublicationPhase::Sending && !commit_possible_) {
                phase_=HostPublicationPhase::Rejected; Set("Publication cancelled before COMMIT");
            }
        }
        void Disconnect() noexcept {
            if (!Blocking()) return;
            if(commit_possible_) Uncertain();
            else { phase_=HostPublicationPhase::Rejected; Set("Publication disconnected before COMMIT"); }
        }
        void Tick(std::uint32_t now) noexcept {
            if (Blocking() && phase_!=HostPublicationPhase::Uncertain && now-started_>=kHostPublicationTimeoutMs) {
                if(commit_possible_) Uncertain();
                else { phase_=HostPublicationPhase::Rejected; Set("Publication timed out before COMMIT"); }
            }
        }
        void ReadbackFailed() noexcept { if(NeedsReadback()) Uncertain(); }
        bool Verify(const DevicePatternSnapshot& snapshot) noexcept {
            if (!NeedsReadback() || snapshot.version!=2) return false;
            if(snapshot.tempo_hundredths==tempo_ && SamePattern(snapshot.pattern,pattern_)) {
                phase_=HostPublicationPhase::Success; confirmed_=true; Set("Publication confirmed by readback");
            } else { phase_=HostPublicationPhase::Mismatch; Set("Publication readback differs; draft retained"); }
            return true;
        }
        void Unavailable() noexcept { if(!Blocking()) Set("Publish unavailable: stop, dirty draft, v2 and NEXT required"); }
        // Wrong identity/version/phase, duplicate and malformed responses are ignored.
        bool Response(const char* line,std::size_t length,std::uint32_t now) noexcept {
            if(!line || length>=kPatternSnapshotLineCapacity) return false;
            std::uint32_t f[2]{};
            if (SnapshotFields(line,length,"BTPUBLISH1 SUBMITTED 1 ",f,1)) {
                if(phase_!=HostPublicationPhase::Waiting || f[0]!=id_ || submitted_) return false;
                submitted_=true; return true;
            }
            if (SnapshotFields(line,length,"BTPUBLISH1 APPLIED 1 ",f,1)) {
                if(phase_!=HostPublicationPhase::Waiting || f[0]!=id_ || !submitted_) return false;
                phase_=HostPublicationPhase::Verifying; started_=now; Set("Publication verifying device readback"); return true;
            }
            if (SnapshotFields(line,length,"BTPUBLISH1 ERROR 1 ",f,2)) {
                if((phase_!=HostPublicationPhase::Sending && phase_!=HostPublicationPhase::Waiting) ||
                    f[0]!=id_ || submitted_ || f[1]<2 || f[1]>11) return false;
                if(commit_possible_ && (f[1]==3 || f[1]==4)) { Uncertain(); return true; }
                phase_=HostPublicationPhase::Rejected;
                std::snprintf(status_,sizeof(status_),"Publication rejected: protocol error %u",unsigned(f[1])); return true;
            }
            constexpr const char* reasons[]={"playing-or-paused","not-configured","missing-instrument","unknown-instrument",
                "unsupported-pitch","invalid-pattern","handoff-error"};
            for(const auto* reason:reasons) {
                char expected[96]; const auto written=std::snprintf(expected,sizeof(expected),"BTPUBLISH1 REJECTED 1 %" PRIu32 " %s",id_,reason);
                if(written>=0 && static_cast<std::size_t>(written)==length && !std::memcmp(line,expected,length)) {
                    if(phase_!=HostPublicationPhase::Waiting || !submitted_) return false;
                    if(!std::strcmp(reason,"handoff-error")) { Uncertain(); return true; }
                    phase_=HostPublicationPhase::Rejected; std::snprintf(status_,sizeof(status_),"Publication rejected: %s",reason); return true;
                }
            }
            return false;
        }
    private:
        unsigned Count() const noexcept { return unsigned(pattern_.active_rows)*pattern_.active_channels; }
        void Set(const char* text) noexcept { std::snprintf(status_,sizeof(status_),"%s",text); }
        void Uncertain() noexcept { phase_=HostPublicationPhase::Uncertain; Set("Publication uncertain - F5 requests readback"); }
        RealtimePattern pattern_;
        std::uint32_t id_=0,last_id_=0,hash_=0,tempo_=0,started_=0;
        unsigned line_index_=0;
        bool commit_possible_=false,submitted_=false,confirmed_=false;
        HostPublicationPhase phase_=HostPublicationPhase::Idle;
        char status_[96]{};
    };
}

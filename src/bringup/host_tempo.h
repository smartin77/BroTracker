#pragma once
#include "pattern_tempo.h"
namespace BroTracker {
enum class HostTempoPhase { Unavailable, Ready, Sending, Waiting, Uncertain };
// One in-flight frozen SET plus one accumulating desired value. Never replay a
// possibly delivered SET after timeout/reconnect; GET settles authoritative state.
class HostTempo {
public:
    void Disconnect() noexcept { *this=HostTempo{}; }
    bool Supported() const noexcept { return supported_; }
    bool Available() const noexcept { return supported_ && verified_ && phase_!=HostTempoPhase::Uncertain; }
    bool Synchronized() const noexcept { return Available() && !current_ && !dirty_ && !read_ && !state_.pending_id; }
    const TempoState& State() const noexcept { return state_; }
    std::uint32_t Desired() const noexcept { return dirty_?desired_:state_.tempo; }
    HostTempoPhase Phase() const noexcept { return phase_; }
    bool Request(std::uint32_t value) noexcept {
        if(!Available() || !ValidInteractiveTempo(value)) return false;
        desired_=value; dirty_=true; return true;
    }
    bool Step(int amount) noexcept { return Request(StepTempo(Desired(),amount)); }
    void StopRequested() noexcept {
        dirty_=false; read_=true; read_sent_=false;
        if(current_ && !attempted_) { current_=0; phase_=HostTempoPhase::Ready; }
        else if(current_) phase_=HostTempoPhase::Uncertain;
    }
    void Refresh() noexcept { read_=true; read_sent_=false; }
    bool Sending() const noexcept { return phase_==HostTempoPhase::Sending; }
    bool NeedsRead(std::uint32_t now) const noexcept { return supported_ && read_ && (!read_sent_ || now-read_at_>=1000); }
    void ReadSent(std::uint32_t now) noexcept { read_sent_=true; read_at_=now; }
    void BeforeWrite(std::uint32_t now) noexcept { if(!attempted_) {attempted_=true; started_=now;} }
    void LineComplete() noexcept { if(Sending()) phase_=HostTempoPhase::Waiting; }
    void Tick(std::uint32_t now) noexcept {
        if(current_ && attempted_ && now-started_>=5000) { phase_=HostTempoPhase::Uncertain; dirty_=false; read_=true; }
    }
    bool Begin() noexcept {
        if(!Available() || current_ || !dirty_ || read_ || state_.pending_id || !state_.next_id) return false;
        if(desired_==state_.tempo) {dirty_=false; return false;}
        current_=state_.next_id; frozen_=desired_; attempted_=false; phase_=HostTempoPhase::Sending; return true;
    }
    bool FormatLine(char* line,std::size_t size,std::size_t& length) const noexcept {
        if(!Sending()) return false;
        int n=std::snprintf(line,size,"BTTEMPO1 SET 1 %" PRIu32 " %" PRIu32 "\n",current_,frozen_);
        if(n<0 || static_cast<std::size_t>(n)>=size) return false; length=n; return true;
    }
    bool Response(const char* line,std::size_t size) noexcept {
        if(size==sizeof("BTTEMPO1 CAP 1")-1 && !std::memcmp(line,"BTTEMPO1 CAP 1",size)) {
            supported_=true; read_=true; return true;
        }
        if(!supported_) return false;
        TempoState next;
        if(ParseTempoState(line,size,next)) {
            state_=next; verified_=true;
            if(current_ && Sending()) return true;
            if(next.pending_id || (current_ && next.settled<current_)) {if(!current_) phase_=HostTempoPhase::Uncertain;read_=true;return true;}
            if(current_) {
                if(next.applied_id!=current_ || next.tempo!=frozen_ || phase_==HostTempoPhase::Uncertain) dirty_=false;
                else if(desired_==frozen_) dirty_=false;
            }
            current_=0; phase_=HostTempoPhase::Ready; read_=false; return true;
        }
        std::uint32_t id=0;
        if(SnapshotFields(line,size,"BTTEMPO1 APPLIED 1 ",&id,1) ||
           SnapshotFields(line,size,"BTTEMPO1 CANCELLED 1 ",&id,1) ||
           SnapshotFields(line,size,"BTTEMPO1 REJECTED 1 ",&id,1)) {
            if(current_ && id!=current_) return false;
            read_=true; read_sent_=false;
            if(std::strstr(line,"CANCELLED") || std::strstr(line,"REJECTED")) {dirty_=false;current_=0;phase_=HostTempoPhase::Ready;}
            return true;
        }
        return false;
    }
private:
    TempoState state_;
    std::uint32_t desired_=0,current_=0,frozen_=0,started_=0,read_at_=0;
    bool supported_=false,verified_=false,dirty_=false,read_=false,read_sent_=false,attempted_=false;
    HostTempoPhase phase_=HostTempoPhase::Unavailable;
};
}

#pragma once
#include "pattern_gains.h"
#include <cstring>
#include <initializer_list>
namespace BroTracker {
struct HostGainState { std::uint32_t applied_id=0,next_id=0; Pcm16ChannelGains gains; };
// Dedicated bounded ten-field parser. Existing snapshot/transfer parsers retain
// their field limits and behavior. No signs, extra spaces/fields or overflow.
inline bool ParseHostGainState(const char* line,std::size_t length,HostGainState& output) noexcept {
    constexpr char prefix[]="BTGAIN1 STATE 1 ";
    if(!line || length>=kPatternGainLineCapacity || length<sizeof(prefix) ||
        std::memcmp(line,prefix,sizeof(prefix)-1)) return false;
    std::uint32_t fields[10]{}; std::size_t at=sizeof(prefix)-1;
    for(unsigned i=0;i<10;++i) {
        const auto start=at;
        while(at<length && line[at]>='0' && line[at]<='9') {
            const unsigned digit=unsigned(line[at++]-'0');
            if(fields[i]>(UINT32_MAX-digit)/10) return false;
            fields[i]=fields[i]*10+digit;
        }
        if(at==start) return false;
        if(i==9) { if(at!=length) return false; }
        else if(at==length || line[at++]!=' ') return false;
    }
    if(fields[1] && fields[1]<=fields[0]) return false;
    HostGainState parsed; parsed.applied_id=fields[0]; parsed.next_id=fields[1];
    for(unsigned c=0;c<8;++c) { if(fields[c+2]>kPcm16UnityGain) return false; parsed.gains.channels[c]=fields[c+2]; }
    output=parsed; return true;
}
enum class HostGainPhase { Unavailable, Ready, Sending, Waiting, Submitted, Verifying, Uncertain, Rejected, Exhausted };
// Single-owner portable coordination: fixed frozen request plus one latest goal.
// Initial/reconnect state is device-authoritative; disconnect discards local goals.
// No allocation, callbacks, transport I/O or pattern/editor ownership. Each method
// scans at most eight gains or one <=95-byte line. SET timeout starts at the first
// attempted write (including asynchronous/zero-progress attempts); recovery polls
// GET at most once per second. Never resend a SET after a possibly submitted result.
class HostChannelGains {
public:
    bool Supported() const noexcept { return supported_; }
    bool Available() const noexcept { return supported_ && verified_; }
    HostGainPhase Phase() const noexcept { return phase_; }
    const HostGainState* Authoritative() const noexcept { return Available()?&state_:nullptr; }
    const Pcm16ChannelGains* Desired() const noexcept { return desired_valid_?&desired_:nullptr; }
    const char* Status() const noexcept {
        switch(phase_) {
        case HostGainPhase::Unavailable:return "Channel gains unavailable";
        case HostGainPhase::Sending:return "Sending channel gains";
        case HostGainPhase::Waiting:return "Waiting for gain receipt";
        case HostGainPhase::Submitted:return "Channel gains submitted; awaiting application";
        case HostGainPhase::Verifying:return "Verifying channel gains";
        case HostGainPhase::Uncertain:return "Gain outcome uncertain; reading device state";
        case HostGainPhase::Rejected:return "Channel gains rejected";
        case HostGainPhase::Exhausted:return "Gain identities exhausted";
        default:return desired_valid_?"Channel gains pending":"Channel gains synchronized";
        }
    }
    void Disconnect() noexcept { *this=HostChannelGains{}; } // never retain prior-session desired values
    bool Request(const Pcm16ChannelGains& gains) noexcept {
        for(auto gain:gains.channels) if(gain>kPcm16UnityGain) return false;
        if(!Available() || phase_==HostGainPhase::Uncertain || phase_==HostGainPhase::Exhausted) return false;
        desired_=gains; desired_valid_=true;
        if(!current_ && phase_==HostGainPhase::Rejected) phase_=HostGainPhase::Ready;
        return true;
    }
    bool Synchronized() const noexcept { return Available() && phase_==HostGainPhase::Ready && !desired_valid_ && !current_; }
    bool Sending() const noexcept { return phase_==HostGainPhase::Sending; }
    bool NeedsRead(std::uint32_t now) const noexcept {
        return supported_ && read_needed_ && (!read_sent_ || now-read_at_>=1000);
    }
    void ReadSent(std::uint32_t now) noexcept { read_sent_=true; read_at_=now; }
    void Tick(std::uint32_t now) noexcept {
        if(current_ && attempted_ && now-started_>=5000 && phase_!=HostGainPhase::Rejected) {
            phase_=HostGainPhase::Uncertain; read_needed_=true;
        }
    }
    void BeforeWrite(std::uint32_t now) noexcept { if(!attempted_) { attempted_=true; started_=now; } }
    void LineComplete() noexcept { if(Sending()) phase_=HostGainPhase::Waiting; }
    bool Begin(std::uint32_t now) noexcept {
        if(!Available() || current_ || read_needed_ || !desired_valid_) return false;
        if(Equal(desired_,state_.gains)) { desired_valid_=false; phase_=HostGainPhase::Ready; return false; }
        if(!state_.next_id || last_id_==UINT32_MAX) { phase_=HostGainPhase::Exhausted; return false; }
        const auto id=state_.next_id>last_id_ ? state_.next_id:last_id_+1;
        current_=last_id_=id; frozen_=desired_; started_=now; attempted_=false; phase_=HostGainPhase::Sending;
        return true;
    }
    bool FormatLine(char* output,std::size_t capacity,std::size_t& length) const noexcept {
        if(!Sending() || !output || !capacity) return false;
        const int n=std::snprintf(output,capacity,"BTGAIN1 SET 1 %" PRIu32
            " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 "\n",
            current_,frozen_.channels[0],frozen_.channels[1],frozen_.channels[2],frozen_.channels[3],
            frozen_.channels[4],frozen_.channels[5],frozen_.channels[6],frozen_.channels[7]);
        if(n<0 || static_cast<std::size_t>(n)>=capacity) return false;
        length=static_cast<std::size_t>(n); return true;
    }
    bool Response(const char* line,std::size_t length,std::uint32_t now) noexcept {
        if(!line || length>=kPatternGainLineCapacity) return false;
        if(length==sizeof("BTGAIN1 CAP 1")-1 && !std::memcmp(line,"BTGAIN1 CAP 1",length)) {
            supported_=true; read_needed_=!verified_; return true;
        }
        if(!supported_) return false;
        HostGainState parsed;
        if(ParseHostGainState(line,length,parsed)) {
            state_=parsed; verified_=true;
            if(current_) {
                if(Sending()) return true;
                if(parsed.applied_id<current_) { read_needed_=true; return true; }
                const bool matched=parsed.applied_id==current_ && Equal(parsed.gains,frozen_);
                if(desired_valid_ && Equal(desired_,frozen_)) desired_valid_=false;
                current_=0; read_needed_=false;
                phase_=matched?HostGainPhase::Ready:HostGainPhase::Rejected;
            } else {
                // Accepted-but-unapplied work from another/prior host session.
                const auto high=parsed.next_id?parsed.next_id-1:UINT32_MAX;
                read_needed_=parsed.applied_id<high;
                phase_=read_needed_?HostGainPhase::Uncertain:!parsed.next_id?HostGainPhase::Exhausted:
                    phase_==HostGainPhase::Rejected?HostGainPhase::Rejected:HostGainPhase::Ready;
            }
            return true;
        }
        std::uint32_t id=0;
        if(SnapshotFields(line,length,"BTGAIN1 SUBMITTED 1 ",&id,1)) {
            if(id!=current_ || (phase_!=HostGainPhase::Waiting && phase_!=HostGainPhase::Uncertain)) return false;
            phase_=HostGainPhase::Submitted; return true;
        }
        if(SnapshotFields(line,length,"BTGAIN1 APPLIED 1 ",&id,1)) {
            if(id!=current_ || (phase_!=HostGainPhase::Waiting && phase_!=HostGainPhase::Submitted && phase_!=HostGainPhase::Uncertain)) return false;
            phase_=HostGainPhase::Verifying; read_needed_=true; read_sent_=false; return true;
        }
        if(SnapshotFields(line,length,"BTGAIN1 REJECTED 1 ",&id,1)) return Reject(id);
        constexpr char prefix[]="BTGAIN1 ERROR 1 ";
        if(length>sizeof(prefix)-1 && !std::memcmp(line,prefix,sizeof(prefix)-1)) {
            std::size_t end=sizeof(prefix)-1;
            while(end<length && line[end]!=' ') ++end;
            if(end==length || !SnapshotFields(line,end,prefix,&id,1)) return false;
            const char* reason=line+end+1; const auto n=length-end-1;
            for(const auto* known:{"malformed","not-connected","queue-full","identity","invalid-gain"})
                if(n==std::strlen(known) && !std::memcmp(reason,known,n)) return Reject(id);
        }
        (void)now; return false;
    }
private:
    bool Reject(std::uint32_t id) noexcept {
        if(!current_ || id!=current_) return false;
        if(desired_valid_ && Equal(desired_,frozen_)) desired_valid_=false;
        current_=0; phase_=HostGainPhase::Rejected; read_needed_=true; read_sent_=false; return true;
    }
    static bool Equal(const Pcm16ChannelGains& a,const Pcm16ChannelGains& b) noexcept {
        for(unsigned c=0;c<8;++c) if(a.channels[c]!=b.channels[c]) return false;
        return true;
    }
    HostGainState state_;
    Pcm16ChannelGains desired_,frozen_;
    bool supported_=false,verified_=false,desired_valid_=false,read_needed_=false,read_sent_=false,attempted_=false;
    std::uint32_t current_=0,last_id_=0,started_=0,read_at_=0;
    HostGainPhase phase_=HostGainPhase::Unavailable;
};
}

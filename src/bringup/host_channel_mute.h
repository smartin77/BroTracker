#pragma once
#include "pattern_snapshot.h"
#include <cstdio>
#include <cstring>
namespace BroTracker {
inline bool ParseMuteState(const char* line,std::size_t length,std::uint32_t (&fields)[3]) noexcept {
    constexpr char prefix[]="BTMUTE1 STATE 1 ";
    if(!line || length>=96 || length<=sizeof(prefix)-1 || std::memcmp(line,prefix,sizeof(prefix)-1)) return false;
    std::size_t at=sizeof(prefix)-1;std::uint32_t parsed[3]{};
    for(unsigned i=0;i<3;++i) {
        const auto start=at;
        while(at<length && line[at]>='0' && line[at]<='9') {
            const unsigned digit=line[at++]-'0';if(parsed[i]>(UINT32_MAX-digit)/10) return false;
            parsed[i]=parsed[i]*10+digit;
        }
        if(at==start || (i==2?at!=length:at==length || line[at++]!=' ')) return false;
    }
    for(unsigned i=0;i<3;++i) fields[i]=parsed[i];return true;
}
// Single host owner. One frozen SET and a coalesced XOR goal. Reconnect discards
// intent; STATE is authoritative. Never replay a possibly delivered SET.
class HostChannelMute {
public:
    void Disconnect() noexcept { *this=HostChannelMute{}; }
    bool Available() const noexcept { return supported_ && known_ && !uncertain_; }
    bool Known() const noexcept { return supported_ && known_; }
    unsigned Mask() const noexcept { return mask_; }
    const char* Status() const noexcept {
        return !supported_?"Mute unavailable":!known_?"Reading channel mute":uncertain_?"Mute uncertain; reading state":
            rejected_?"Mute rejected":(!next_ || last_id_==UINT32_MAX)?"Mute identities exhausted":current_ || goal_!=mask_?"Mute pending":"Mute synchronized";
    }
    bool Toggle(unsigned channel) noexcept {
        if(channel>=8 || !Available() || !next_ || last_id_==UINT32_MAX) return false;
        goal_^=1u<<channel; rejected_=false; return true;
    }
    void Tick(unsigned now) noexcept { if(current_ && attempted_ && now-started_>=5000) {uncertain_=true;read_=true;} }
    bool NeedsRead(unsigned now) const noexcept {return supported_ && read_ && (!read_sent_ || now-read_at_>=1000);}
    void ReadSent(unsigned now) noexcept {read_at_=now;read_sent_=true;}
    bool Sending() const noexcept {return sending_;}
    bool Begin() noexcept {
        if(!Available() || current_ || goal_==mask_ || !next_ || last_id_==UINT32_MAX || read_) return false;
        current_=next_>last_id_?next_:last_id_+1;last_id_=current_; base_=mask_; frozen_=goal_; sending_=true; rejected_=false; attempted_=false; return true;
    }
    void BeforeWrite(unsigned now) noexcept {if(!attempted_) {started_=now;attempted_=true;}}
    void LineComplete() noexcept {sending_=false;}
    bool FormatLine(char* out,std::size_t capacity,std::size_t& length) const noexcept {
        if(!sending_) return false;
        const int n=std::snprintf(out,capacity,"BTMUTE1 SET 1 %lu %u\n",(unsigned long)current_,frozen_);
        if(n<0 || std::size_t(n)>=capacity) return false; length=n;return true;
    }
    bool Response(const char* line,std::size_t length) noexcept {
        if(!line || length>=96) return false;
        if(length==13 && !std::memcmp(line,"BTMUTE1 CAP 1",13)) {supported_=true;read_=!known_;return true;}
        if(!supported_) return false;
        std::uint32_t f[3]{};
        if(ParseMuteState(line,length,f)) {
            if(f[2]>255 || (f[1] && f[1]<=f[0])) return false;
            const bool first=!known_; mask_=f[2];next_=f[1];known_=true;
            if(first) goal_=mask_;
            if(current_) {
                if(f[0]<current_) {read_=true;return true;}
                rejected_=f[0]!=current_ || mask_!=frozen_;
                if(rejected_) goal_=mask_ ^ ((goal_^frozen_) & ~(frozen_^base_));
                current_=0;sending_=false;uncertain_=false;
            } else if(first || goal_==previous_) goal_=mask_;
            previous_=mask_;
            // Accepted but not yet applied requests from an earlier host session.
            uncertain_=next_ ? f[0]+1<next_ : f[0]!=UINT32_MAX; read_=uncertain_;read_sent_=false;
            return true;
        }
        if(SnapshotFields(line,length,"BTMUTE1 SUBMITTED 1 ",f,1)) return f[0]==current_ && current_;
        if(SnapshotFields(line,length,"BTMUTE1 APPLIED 1 ",f,1)) {if(f[0]!=current_ || !current_) return false;read_=true;read_sent_=false;return true;}
        if(SnapshotFields(line,length,"BTMUTE1 REJECTED 1 ",f,1)) {
            if(!current_ || f[0]!=current_) return false;
            goal_=mask_ ^ ((goal_^frozen_) & ~(frozen_^base_));
            current_=0;sending_=false;rejected_=true;uncertain_=false;read_=true;read_sent_=false;return true;
        }
        return false;
    }
private:
    bool supported_=false,known_=false,uncertain_=false,rejected_=false,read_=false,read_sent_=false,sending_=false,attempted_=false;
    unsigned mask_=0,previous_=0,goal_=0,frozen_=0,current_=0,next_=0,started_=0,read_at_=0,base_=0,last_id_=0;
};
}

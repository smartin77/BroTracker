#pragma once
#include "master_volume_protocol.h"
#include <cstdio>
#include <cstring>
namespace BroTracker {
// One frozen command and one coalesced absolute goal. Never replay after reconnect.
class HostMasterVolume {
public:
    void Disconnect() noexcept {*this=HostMasterVolume{};}
    bool Supported() const noexcept {return supported_;}
    bool Available() const noexcept {return supported_ && known_ && !uncertain_ && next_;}
    unsigned Percent() const noexcept {return percent_;}
    unsigned Desired() const noexcept {return goal_;}
    bool Pending() const noexcept {return current_ || goal_!=percent_ || uncertain_;}
    bool Step(int direction) noexcept {
        if(!Available())return false;
        if(!direction)return false;
        if(direction>16)direction=16;if(direction< -16)direction=-16;
        const int n=int(goal_)+direction*5;goal_=unsigned(n<0?0:n>100?100:n);return true;
    }
    void Tick(std::uint32_t now) noexcept {if(current_ && attempted_ && now-started_>=5000){uncertain_=true;read_=true;}}
    bool NeedsRead(std::uint32_t now) const noexcept {return supported_ && read_ && (!read_sent_ || now-read_at_>=1000);}
    void ReadSent(std::uint32_t now) noexcept {read_at_=now;read_sent_=true;}
    bool Begin() noexcept {
        if(!Available() || current_ || read_ || goal_==percent_ || last_==UINT32_MAX)return false;
        current_=next_>last_?next_:last_+1;last_=current_;frozen_=goal_;sending_=true;attempted_=false;return true;
    }
    bool Sending() const noexcept {return sending_;}
    void BeforeWrite(std::uint32_t now) noexcept {if(!attempted_){attempted_=true;started_=now;}}
    void LineComplete() noexcept {sending_=false;}
    bool FormatLine(char* p,std::size_t cap,std::size_t& size) const noexcept {
        if(!sending_)return false;
        const auto n=std::snprintf(p,cap,"BTVOL1 SET 1 %lu %u\n",(unsigned long)current_,frozen_);
        if(n<0 || std::size_t(n)>=cap)return false;size=n;return true;
    }
    bool Response(const char* p,std::size_t n) noexcept {
        if(!p || n>=96)return false;
        if(n==12 && !std::memcmp(p,"BTVOL1 CAP 1",12)){supported_=true;read_=!known_;return true;}
        if(!supported_)return false;
        std::uint32_t f[4]{};
        if(ParseVolumeState(p,n,f)) {
            if(f[2]>100 || f[3]>65536 || (f[1] && f[1]<=f[0]))return false;
            const bool first=!known_;known_=true;percent_=f[2];next_=f[1];
            if(first)goal_=percent_;
            if(current_) {
                if(sending_ || f[0]<current_){read_=true;return true;}
                if(f[0]!=current_ || percent_!=frozen_)goal_=percent_;
                current_=0;uncertain_=false;
            } else if(goal_==previous_)goal_=percent_;
            previous_=percent_;
            const auto high=next_?next_-1:UINT32_MAX;
            uncertain_=f[0]<high;read_=uncertain_;read_sent_=false;return true;
        }
        if(SnapshotFields(p,n,"BTVOL1 APPLIED 1 ",f,1)) {
            if(!current_ || current_!=f[0])return false;read_=true;read_sent_=false;return true;
        }
        if(SnapshotFields(p,n,"BTVOL1 SUBMITTED 1 ",f,1))return current_ && current_==f[0];
        constexpr char error[]="BTVOL1 ERROR 1 ";
        if(n>sizeof(error)-1 && !std::memcmp(p,error,sizeof(error)-1)) {
            auto end=sizeof(error)-1;while(end<n && p[end]!=' ')++end;
            if(end==n || !SnapshotFields(p,end,error,f,1) || !current_ || f[0]!=current_)return false;
            current_=0;sending_=false;goal_=percent_;read_=true;read_sent_=false;uncertain_=true;return true;
        }
        return false;
    }
private:
    bool supported_=false,known_=false,uncertain_=false,read_=false,read_sent_=false,sending_=false,attempted_=false;
    unsigned percent_=100,goal_=100,previous_=100,frozen_=100;
    std::uint32_t next_=0,last_=0,current_=0,started_=0,read_at_=0;
};
}

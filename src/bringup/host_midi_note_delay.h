#pragma once
#include "midi_note_delay_protocol.h"
#include "audio_output_selection.h"
namespace BroTracker {
// Nonblocking setup observer. No transport, editor or file ownership.
class HostMidiNoteDelay {
public:
    void Configure(unsigned ms,bool invalid=false){intent_=ms;invalid_=invalid || ms>kMaximumMidiNoteDelayMs;}
    void Disconnect(){supported_=confirmed_=pending_=failed_=set_=wait_stop_=have_effective_=false;polling_=false;attempts_=0;next_=0;reason_="waiting for capability/audio confirmation";}
    void Capability(){supported_=true;}
    bool Supported()const{return supported_;}
    bool Ready()const{return !invalid_ && ((!supported_ && intent_==0) || confirmed_);}
    bool Confirmed()const{return confirmed_;}
    unsigned EffectiveMs()const{return effective_;}
    void StopAcknowledged(std::uint32_t now) {
        if(!supported_ || invalid_)return;
        pending_=confirmed_=failed_=set_=wait_stop_=false;polling_=true;poll_started_=next_=now;
        attempts_=0;reason_="waiting for stopped-state verification";
    }
    void Tick(std::uint32_t now) {
        if(pending_ && now-started_>=6000) {pending_=false;failed_=true;reason_="acknowledgment timeout; STOP to verify state again";}
    }
    bool Begin(std::uint32_t now,bool audio_ready,AudioOutputChoice route,bool stopped) {
        if(!audio_ready || invalid_ || confirmed_ || pending_ || failed_)return false;
        if(!supported_) {if(intent_){failed_=true;reason_="unsupported firmware; update Teensy";}return false;}
        expected_=route==AudioOutputChoice::Bridge?intent_:0;
        if(wait_stop_ && !stopped)return false;
        if(wait_stop_ && stopped){wait_stop_=false;polling_=true;poll_started_=now;next_=now;}
        if(polling_ && now-poll_started_>=6000){failed_=true;reason_="cleanup not ready; STOP to retry verification";return false;}
        if(static_cast<std::int32_t>(now-next_)<0)return false;
        if(set_ && !stopped){set_=false;wait_stop_=true;reason_="STOP required to change delay";return false;}
        if(set_ && attempts_>=3){failed_=true;reason_="setup attempts exhausted; STOP to verify again";return false;}
        if(next_id_==UINT32_MAX){failed_=true;reason_="request identity exhausted; restart terminal";return false;}
        id_=++next_id_;if(set_)++attempts_;pending_=true;started_=now;reason_=set_?"waiting for SET acknowledgment":"waiting for live state";return true;
    }
    bool FormatLine(char* out,std::size_t cap,std::size_t& size)const {
        if(!pending_)return false;
        const int n=set_?std::snprintf(out,cap,"BTMIDIDELAY1 SET 1 %lu %u\n",static_cast<unsigned long>(id_),intent_):
            std::snprintf(out,cap,"BTMIDIDELAY1 GET 1 %lu\n",static_cast<unsigned long>(id_));
        if(n<0 || static_cast<std::size_t>(n)>=cap)return false;
        size=static_cast<std::size_t>(n);return true;
    }
    bool Response(const char* line,std::size_t size,std::uint32_t now) {
        std::uint32_t f[3]{};
        if(!pending_)return false;
        const bool state=SnapshotFields(line,size,"BTMIDIDELAY1 STATE 1 ",f,3);
        const bool selected=!state && SnapshotFields(line,size,"BTMIDIDELAY1 SELECTED 1 ",f,3);
        const bool error=!state && !selected && SnapshotFields(line,size,"BTMIDIDELAY1 ERROR 1 ",f,2);
        if((!state && !selected && !error) || f[0]!=id_)return false;
        if((state && set_) || (selected && !set_))return false; // Unrelated reply kind is not confirmation.
        pending_=false;
        if(error) {
            if(f[1]==2 || f[1]==3 || f[1]==4){Wait(now,f[1]);return true;}
            failed_=true;reason_="setting rejected; STOP to verify state again";return true;
        }
        if(state) {
            if(f[1]>160 || (f[2]!=0 && f[2]!=2 && f[2]!=3 && f[2]!=4 && f[2]!=5))return Mismatch();
            effective_=f[1];have_effective_=true;
            if(f[2]==5){failed_=true;reason_="audio initialization not ready; delay unconfirmed";return true;}
            if(effective_==expected_){confirmed_=true;reason_="live effective delay verified";return true;}
            if(f[2]){Wait(now,f[2]);return true;}
            set_=true;polling_=false;next_=now;return true;
        }
        if(f[1]!=intent_ || f[2]!=expected_)return Mismatch();
        effective_=f[2];have_effective_=true;confirmed_=true;reason_="setting acknowledged";return true;
    }
    const char* Status()const {
        const char* reason=invalid_?"invalid configuration: expected integer 0..160; playback blocked":
            !supported_ && !intent_?"legacy zero (firmware capability absent)":reason_;
        if(have_effective_)std::snprintf(status_,sizeof(status_),"MIDI delay configured=%ums effective=%ums %s: %s",intent_,effective_,confirmed_?"confirmed":"unconfirmed",reason);
        else std::snprintf(status_,sizeof(status_),"MIDI delay configured=%ums effective=unknown unconfirmed: %s",intent_,reason);
        return status_;
    }
private:
    bool Mismatch(){failed_=true;reason_="mismatched acknowledgment; STOP to verify state again";return true;}
    void Wait(std::uint32_t now,unsigned condition){set_=false;wait_stop_=condition==2;next_=now+250;
        if(!polling_){polling_=true;poll_started_=now;}
        reason_=condition==2?"STOP required to change delay":"waiting for pending notes/MIDI cleanup; STOP remains available";
    }
    unsigned intent_=0,effective_=0,expected_=0,attempts_=0;
    bool invalid_=false,supported_=false,confirmed_=false,pending_=false,failed_=false,set_=false,wait_stop_=false,have_effective_=false,polling_=false;
    std::uint32_t next_id_=0,id_=0,started_=0,next_=0,poll_started_=0;
    const char* reason_="waiting for capability/audio confirmation";
    mutable char status_[256]{};
};
}

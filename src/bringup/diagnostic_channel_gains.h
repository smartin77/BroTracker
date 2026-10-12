#pragma once
#include "core/audio/pcm16_gain_mixer.h"
namespace BroTracker {
enum class DiagnosticGainConfiguration { Absent, Invalid, Valid };
// Strict bounded environment-value parser, fixed storage, no I/O or allocation.
// Null means absent. Empty, signs, whitespace, missing/extra fields and overflow
// reject without changing output. Bound: eight at-most-10-digit uint32 fields + commas (87 bytes).
inline DiagnosticGainConfiguration ParseDiagnosticChannelGains(const char* text,Pcm16ChannelGains& output) noexcept {
    if(!text) return DiagnosticGainConfiguration::Absent;
    Pcm16ChannelGains parsed; unsigned at=0;
    for(unsigned c=0;c<8;++c) {
        unsigned value=0,digits=0;
        while(at<88 && text[at]>='0' && text[at]<='9') {
            const unsigned digit=unsigned(text[at++]-'0');
            if(++digits>10 || value>(kPcm16UnityGain-digit)/10) return DiagnosticGainConfiguration::Invalid;
            value=value*10+digit;
        }
        if(!digits || at>=88) return DiagnosticGainConfiguration::Invalid;
        parsed.channels[c]=value;
        if(c==7) { if(text[at]!='\0') return DiagnosticGainConfiguration::Invalid; }
        else if(text[at++]!=',') return DiagnosticGainConfiguration::Invalid;
    }
    output=parsed; return DiagnosticGainConfiguration::Valid;
}
// Windows-only caller opts in; portable policy is testable without getenv/SDL.
// One attempt during the first connected session, never retry or reconnect replay.
class DiagnosticChannelGainRequest {
public:
    explicit DiagnosticChannelGainRequest(const char* value) noexcept : configuration_(ParseDiagnosticChannelGains(value,gains_)) {}
    DiagnosticGainConfiguration Configuration() const noexcept { return configuration_; }
    const char* Status() const noexcept {
        if(configuration_==DiagnosticGainConfiguration::Absent) return "";
        if(configuration_==DiagnosticGainConfiguration::Invalid) return "Invalid diagnostic gains";
        if(closed_ && !attempted_) return "Diagnostic gains not sent; session ended";
        return attempted_ ? "Diagnostic gains requested once" : "Diagnostic gains waiting for device";
    }
    bool TakeRequest(bool connected,bool supported,bool synchronized,Pcm16ChannelGains& output) noexcept {
        if(connected) seen_connection_=true;
        else if(seen_connection_) closed_=true;
        if(configuration_!=DiagnosticGainConfiguration::Valid || closed_ || attempted_ ||
            !connected || !supported || !synchronized) return false;
        attempted_=true; output=gains_; return true;
    }
private:
    Pcm16ChannelGains gains_;
    DiagnosticGainConfiguration configuration_;
    bool seen_connection_=false,closed_=false,attempted_=false;
};
}

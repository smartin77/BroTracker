#pragma once
#include <cstdint>
#include <cstdio>
namespace BroTracker {
struct HostMidiHealth {
    bool connected=false, supported=false, known=false, configured=false, fault=false, cleanup=false;
    unsigned reason=0;
    std::uint32_t age_ms=UINT32_MAX;
};
inline void FormatMidiHealth(const HostMidiHealth& h,bool stopped,char* out,unsigned size) {
    if(!h.connected) std::snprintf(out,size,"Teensy MIDI: CDC unavailable; health unknown");
    else if(!h.supported) std::snprintf(out,size,"MIDI unsupported by firmware");
    else if(!h.known) std::snprintf(out,size,"Teensy MIDI health unknown/stale; delivery unverified");
    else if(h.fault) std::snprintf(out,size,"MIDI fault %u%s; %s; %s",h.reason,
        h.reason==8?" (completion stall)":"",
        h.cleanup?"cleanup uncertain": "external delivery unknown",
        !h.configured?"restore USB; STOP; wait STOPPED + cleanup=0; then START":
        stopped && !h.cleanup?"stopped + cleanup=0: START requests recovery":
        "STOP; wait STOPPED + cleanup=0; then START");
    else std::snprintf(out,size,"Teensy MIDI USB %s; %s",h.configured?(h.cleanup?"configured":"healthy"):"unavailable",
        h.cleanup?"cleanup uncertain":"external delivery unknown");
}
}

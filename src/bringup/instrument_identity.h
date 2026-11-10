#pragma once
#include "pattern_snapshot.h"
#include "core/instrument_type.h"
namespace BroTracker {
// Read-only facts from the frozen engine bindings, never a host ID convention.
template<class Bindings> unsigned InstrumentBindingIdentity(const Bindings& bindings,unsigned id) noexcept {
    if(id>=99 || bindings.count>sizeof(bindings.bindings)/sizeof(bindings.bindings[0]))return 0;
    for(unsigned i=0;i<bindings.count;++i) {
        const auto& binding=bindings.bindings[i];
        if(binding.instrument==id)
            return binding.type==InstrumentType::Sample?1:binding.type==InstrumentType::MIDI?2:binding.type==InstrumentType::Synth?3:0;
    }
    return 0;
}
// Keep the protocol version as SnapshotFields' nonzero first field; raw IDs
// are zero-based binding indices and may be zero. Snapshot request IDs stay strict.
inline bool ParseInstrumentIdentityRequest(const char* line,std::size_t length,unsigned& id) noexcept {
    std::uint32_t fields[2]{};
    if(!SnapshotFields(line,length,"BTINS1 GET ",fields,2) || std::memcmp(line,"BTINS1 GET 1 ",13) || fields[0]!=1 || fields[1]>=99)return false;
    id=fields[1];return true;
}
inline bool ParseInstrumentIdentity(const char* line,std::size_t length,unsigned& id,unsigned& type) noexcept {
    std::uint32_t fields[3]{};
    if(!SnapshotFields(line,length,"BTINS1 STATE ",fields,3) || std::memcmp(line,"BTINS1 STATE 1 ",15) || fields[0]!=1 || fields[1]>=99 || fields[2]>3)return false;
    id=fields[1];type=fields[2];return true;
}
}

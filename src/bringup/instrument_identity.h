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
inline bool ParseInstrumentIdentity(const char* line,std::size_t length,unsigned& id,unsigned& type) noexcept {
    std::uint32_t fields[2]{};
    if(!SnapshotFields(line,length,"BTINS1 STATE 1 ",fields,2) || fields[0]>=99 || fields[1]>3)return false;
    id=fields[0];type=fields[1];return true;
}
}

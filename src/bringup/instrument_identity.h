#pragma once
#include "pattern_snapshot.h"
#include "instrument_definition.h"
#include <cstdio>
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

struct InstrumentFacts {
    unsigned id=0,type=0; // type 0 is undefined, not an unavailable defined Sample.
    InstrumentAvailability availability=InstrumentAvailability::Undefined;
    char resource[64]{};
};
inline InstrumentFacts InstrumentDefinitionFacts(unsigned id,const InstrumentDefinition* definition,InstrumentAvailability availability) noexcept {
    InstrumentFacts facts;facts.id=id;
    if(definition) {
        facts.type=InstrumentTypeCode(definition->type);facts.availability=availability;
        std::snprintf(facts.resource,sizeof(facts.resource),"%s",definition->resource?definition->resource:"-");
    } else std::strcpy(facts.resource,"-");
    return facts;
}
inline InstrumentFacts DevelopmentInstrumentFacts(unsigned id,InstrumentAvailability availability) noexcept {
    return InstrumentDefinitionFacts(id,DevelopmentInstrumentDefinition(id),availability);
}
inline bool ParseInstrumentFactsRequest(const char* line,std::size_t length,unsigned& id) noexcept {
    std::uint32_t fields[2]{};
    if(!SnapshotFields(line,length,"BTINS1 GET ",fields,2) || std::memcmp(line,"BTINS1 GET 2 ",13) || fields[0]!=2 || fields[1]>=99)return false;
    id=fields[1];return true;
}
inline bool ValidInstrumentFacts(const InstrumentFacts& f) noexcept {
    if(f.id>=99 || f.type>3 || unsigned(f.availability)>7)return false;
    std::size_t n=0;while(n<sizeof(f.resource) && f.resource[n])++n;if(!n || n==sizeof(f.resource))return false;
    for(std::size_t i=0;i<n;++i) {
        const char c=f.resource[i];
        if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='/' || c=='_' || c=='-' || c=='.'))return false;
    }
    const bool none=!std::strcmp(f.resource,"-");
    if(!f.type)return f.availability==InstrumentAvailability::Undefined && none;
    if(f.type==2)return f.availability==InstrumentAvailability::ExternalUnknown && none;
    if(f.type==3)return f.availability==InstrumentAvailability::Ready && none;
    return f.availability>=InstrumentAvailability::Ready && f.availability<=InstrumentAvailability::StorageUnavailable &&
        ((f.availability==InstrumentAvailability::NoResource)==none);
}
inline bool FormatInstrumentFacts(const InstrumentFacts& facts,char* output,std::size_t capacity) noexcept {
    if(!ValidInstrumentFacts(facts))return false;
    const int n=std::snprintf(output,capacity,"BTINS1 STATE 2 %u %u %u %s\n",facts.id,facts.type,unsigned(facts.availability),facts.resource);
    return n>0 && static_cast<std::size_t>(n)<capacity;
}
inline bool ParseInstrumentFacts(const char* line,std::size_t length,InstrumentFacts& output) noexcept {
    if(!line || length>=kPatternSnapshotLineCapacity)return false;
    std::size_t split=length;while(split && line[split-1]!=' ')--split;
    if(!split || split==length || length-split>=sizeof(output.resource))return false;
    std::uint32_t fields[4]{};
    if(!SnapshotFields(line,split-1,"BTINS1 STATE ",fields,4) || std::memcmp(line,"BTINS1 STATE 2 ",15) || fields[0]!=2 || fields[3]>7)return false;
    InstrumentFacts parsed;parsed.id=fields[1];parsed.type=fields[2];parsed.availability=static_cast<InstrumentAvailability>(fields[3]);
    std::memcpy(parsed.resource,line+split,length-split);parsed.resource[length-split]=0;
    // Embedded NULs must not turn an invalid resource token into a shorter one.
    if(std::strlen(parsed.resource)!=length-split || !ValidInstrumentFacts(parsed))return false;
    output=parsed;return true;
}
}

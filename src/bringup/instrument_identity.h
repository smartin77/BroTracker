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
    char resource[kSamplePathMaximum+1]{};
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
    if(!SnapshotFields(line,length,"BTINS1 GET ",fields,2) || (fields[0]!=2 && fields[0]!=3) || fields[1]>=99)return false;
    id=fields[1];return true;
}
inline bool ValidInstrumentFacts(const InstrumentFacts& f) noexcept {
    if(f.id>=99 || f.type>3 || unsigned(f.availability)>7)return false;
    std::size_t n=0;while(n<sizeof(f.resource) && f.resource[n])++n;if(!n || n==sizeof(f.resource))return false;
    const bool none=!std::strcmp(f.resource,"-");
    if(!none && !InstrumentDefinitions::ValidResource(f.resource))return false;
    if(!f.type)return f.availability==InstrumentAvailability::Undefined && none;
    if(f.type==2)return f.availability==InstrumentAvailability::ExternalUnknown && none;
    if(f.type==3)return f.availability==InstrumentAvailability::Ready && none;
    return f.availability>=InstrumentAvailability::Ready && f.availability<=InstrumentAvailability::StorageUnavailable &&
        ((f.availability==InstrumentAvailability::NoResource)==none);
}
inline bool FormatInstrumentFacts(const InstrumentFacts& facts,char* output,std::size_t capacity,unsigned version=2) noexcept {
    if(!ValidInstrumentFacts(facts))return false;char token[kPathProtocolCapacity];
    if(version==3){if(!EncodePathToken(facts.resource,token,sizeof(token)))return false;}
    else {if(std::strlen(facts.resource)>63 || std::strchr(facts.resource,' '))return false;std::strcpy(token,facts.resource);}
    const int n=std::snprintf(output,capacity,"BTINS1 STATE %u %u %u %u %s\n",version,facts.id,facts.type,unsigned(facts.availability),token);
    return n>0 && static_cast<std::size_t>(n)<capacity;
}
inline bool ParseInstrumentFacts(const char* line,std::size_t length,InstrumentFacts& output) noexcept {
    if(!line || length<15 || length>=kPathProtocolCapacity ||
        (std::memcmp(line,"BTINS1 STATE 2 ",15) && std::memcmp(line,"BTINS1 STATE 3 ",15)))return false;
    std::size_t split=length;while(split && line[split-1]!=' ')--split;if(!split)return false;
    std::uint32_t fields[4]{};
    if(!SnapshotFields(line,split-1,"BTINS1 STATE ",fields,4) || (fields[0]!=2 && fields[0]!=3) || fields[3]>7)return false;
    InstrumentFacts parsed;parsed.id=fields[1];parsed.type=fields[2];parsed.availability=static_cast<InstrumentAvailability>(fields[3]);
    if(!DecodePathToken(std::string_view(line+split,length-split),parsed.resource,sizeof(parsed.resource),fields[0]==3) || !ValidInstrumentFacts(parsed))return false;
    output=parsed;return true;
}
inline bool ParseInstrumentCollectionCount(const char* line,std::size_t length,unsigned& count) noexcept {
    std::uint32_t fields[2]{};
    if(!SnapshotFields(line,length,"BTINS1 COLLECTION STATE ",fields,2) ||
       std::memcmp(line,"BTINS1 COLLECTION STATE 1 ",26) || fields[0]!=1 || fields[1]>99)return false;
    count=fields[1];return true;
}

struct InstrumentDefineRequest {unsigned request=0,expected=0,type=0;char resource[kSamplePathMaximum+1]{};unsigned midi_channel=0;SampleTuning tuning;};
// Put the applied channel before the existing ACK in a single TX queue entry.
// Older terminals can ignore the supplemental line and still consume the ACK.
inline void FormatLiveInstrumentReply(char (&reply)[128],unsigned request,unsigned result,
    const InstrumentDefinitions& definitions,unsigned slot) noexcept {
    int prefix=0;
    const auto* definition=result?nullptr:definitions.At(slot);
    if(definition && definition->type==InstrumentType::MIDI &&
        definition->midi_channel>=1 && definition->midi_channel<=16)
        prefix=std::snprintf(reply,sizeof(reply),"BTINS1 APPLIEDCHANNEL 1 %u %u %u\n",
            request,slot,unsigned(definition->midi_channel));
    else if(definition && definition->type==InstrumentType::Sample)
        prefix=std::snprintf(reply,sizeof(reply),"BTINS1 APPLIEDTUNING 1 %u %u %u %u\n",
            request,slot,definition->tuning.root,definition->tuning.cents);
    std::snprintf(reply+prefix,sizeof(reply)-prefix,"BTINS1 REPLACED 1 %u %u %u\n",
        request,result,definitions.Count());
}
inline bool ParseInstrumentDefine(const char* line,std::size_t length,InstrumentDefineRequest& output) noexcept {
    if(!line || length<15 || length>=kPathProtocolCapacity ||
        (std::memcmp(line,"BTINS1 DEFINE 1 ",15) && std::memcmp(line,"BTINS1 DEFINE 2 ",15)))return false;
    std::size_t split=length;while(split && line[split-1]!=' ')--split;if(!split)return false;
    std::uint32_t f[4]{};
    if(!SnapshotFields(line,split-1,"BTINS1 DEFINE ",f,4) || (f[0]!=1 && f[0]!=2) || !f[1] || f[2]>=99 || f[3]<1 || f[3]>3)return false;
    InstrumentFacts facts;facts.id=f[2];facts.type=f[3];facts.availability=f[3]==2?InstrumentAvailability::ExternalUnknown:InstrumentAvailability::Ready;
    if(!DecodePathToken(std::string_view(line+split,length-split),facts.resource,sizeof(facts.resource),f[0]==2) || !ValidInstrumentFacts(facts))return false;
    InstrumentDefineRequest value;value.request=f[1];value.expected=f[2];value.type=f[3];std::strcpy(value.resource,facts.resource);output=value;return true;
}
inline bool ParseInstrumentRelink(const char* line,std::size_t length,InstrumentDefineRequest& out) noexcept {
    if(!line || length>=kPathProtocolCapacity || length<14 || std::memcmp(line,"BTINS1 RELINK ",14))return false;
    char normalized[kPathProtocolCapacity];std::memcpy(normalized,line,length);
    std::memcpy(normalized,"BTINS1 DEFINE ",14);
    InstrumentDefineRequest request;
    if(!ParseInstrumentDefine(normalized,length,request) || request.type!=1)return false;
    out=request;return true;
}
inline bool ParseInstrumentReplace(const char* line,std::size_t length,InstrumentDefineRequest& out) noexcept {
    constexpr unsigned prefix=15;
    if(!line || length>kPathProtocolCapacity || length<prefix || std::memcmp(line,"BTINS1 REPLACE ",prefix))return false;
    char normalized[kPathProtocolCapacity]{};std::memcpy(normalized,"BTINS1 DEFINE ",14);std::memcpy(normalized+14,line+prefix,length-prefix);
    InstrumentDefineRequest candidate;
    if(!ParseInstrumentDefine(normalized,length-1,candidate) || (candidate.type==1 && !InstrumentDefinitions::ValidResource(candidate.resource)))return false;
    out=candidate;return true;
}
inline bool ParseInstrumentEdit(const char* line,std::size_t length,InstrumentDefineRequest& out) noexcept {
    std::uint32_t tuning[6]{};
    if(SnapshotFields(line,length,"BTINS1 EDIT ",tuning,6) &&
        tuning[0]==4 && tuning[1] && tuning[2]<99 && tuning[3]==1 && SampleTuning{tuning[4],tuning[5]}.Valid()) {
        out={};out.request=tuning[1];out.expected=tuning[2];out.type=1;out.tuning={tuning[4],tuning[5]};return true;
    }
    std::uint32_t fields[5]{};
    if(SnapshotFields(line,length,"BTINS1 EDIT ",fields,5) &&
        fields[0]==3 && fields[1] && fields[2]<99 && fields[3]==2 && fields[4]>=1 && fields[4]<=16) {
        out={};out.request=fields[1];out.expected=fields[2];out.type=2;out.midi_channel=fields[4];return true;
    }
    constexpr unsigned prefix=12;
    if(!line || length>=kPathProtocolCapacity-2 || length<prefix || std::memcmp(line,"BTINS1 EDIT ",prefix))return false;
    char normalized[kPathProtocolCapacity]{};
    std::memcpy(normalized,"BTINS1 DEFINE ",14);std::memcpy(normalized+14,line+prefix,length-prefix);
    return ParseInstrumentDefine(normalized,length+2,out);
}
inline bool ParseInstrumentDefined(const char* line,std::size_t length,unsigned& request,unsigned& result,unsigned& count) noexcept {
    std::uint32_t f[4]{};if(!SnapshotFields(line,length,"BTINS1 DEFINED ",f,4) || std::memcmp(line,"BTINS1 DEFINED 1 ",16) || f[0]!=1 || !f[1] || (f[2]>7 && f[2]!=9) || f[3]>99)return false;
    request=f[1];result=f[2];count=f[3];return true;
}
}

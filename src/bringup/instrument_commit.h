#pragma once
#include "instrument_identity.h"
#include "core/playback/variable_rate_sample_commands.h"
namespace BroTracker {
// Main owner, with audio excluded by the caller. One append, no allocation/I/O.
// Resource references alias existing immutable prepared assets; unavailable assets
// retain their definition without inventing a playable binding.
template<class AppendBinding>
unsigned CommitInstrumentDefinition(InstrumentDefinitions& definitions,VariableRateSampleBindings& bindings,
    const InstrumentDefineRequest& request,bool ready,AppendBinding append) noexcept {
    if(request.expected!=definitions.Count() || definitions.Count()>=99)return 2;
    if(!ready)return 3;
    const char* resource=nullptr;bool playable=request.type!=1;VariableRateSampleBinding binding;
    binding.instrument=request.expected;binding.type=request.type==1?InstrumentType::Sample:request.type==2?InstrumentType::MIDI:InstrumentType::Synth;
    if(request.type==1)for(unsigned i=0;i<definitions.Count();++i) {
        const auto* definition=definitions.At(i);
        if(definition->resource && !std::strcmp(definition->resource,request.resource)) {
            resource=definition->resource;
            for(unsigned j=0;j<bindings.count;++j)if(bindings.bindings[j].instrument==i && bindings.bindings[j].type==InstrumentType::Sample) {binding=bindings.bindings[j];binding.instrument=request.expected;playable=true;}
        }
    }
    if(!definitions.CanAppend(request.expected,request.type,resource))return 4;
    if(playable && (bindings.count>=kVariableRateSampleBindingCapacity || !append(binding)))return 5;
    if(playable)bindings.bindings[bindings.count++]=binding;
    (void)definitions.Append(request.expected,request.type,resource);return 0;
}
}

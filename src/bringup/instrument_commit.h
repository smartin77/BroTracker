#pragma once
#include "instrument_identity.h"
#include "core/playback/variable_rate_sample_commands.h"
namespace BroTracker {
// Boot-only derivation. Prepared PCM remains owned by the existing asset loader.
// Missing resources deliberately produce no binding; definitions remain authoritative.
inline VariableRateSampleBindings RestoreInstrumentBindings(const InstrumentDefinitions& definitions,
    const VariableRateSampleBindings& assets) noexcept {
    VariableRateSampleBindings restored{};
    for(unsigned id=0;id<definitions.Count();++id) {
        const auto* d=definitions.At(id);
        VariableRateSampleBinding binding;bool ready=d->type!=InstrumentType::Sample;binding.type=d->type;
        if(d->type==InstrumentType::Sample)for(unsigned j=0;j<assets.count;++j) {
            const auto& asset=assets.bindings[j];const auto* source=DevelopmentInstrumentDefinition(asset.instrument);
            if(asset.type==InstrumentType::Sample && source && source->resource && !std::strcmp(source->resource,d->resource)) {
                binding=asset;ready=true;break;
            }
        }
        binding.instrument=id;if(ready)restored.bindings[restored.count++]=binding;
    }
    return restored;
}
// Restoring a structurally valid session never deletes failed Sample definitions.
template<class Prepare> VariableRateSampleBindings PrepareSessionBindings(const InstrumentDefinitions& definitions,Prepare prepare) {
    VariableRateSampleBindings result;
    for(unsigned id=0;id<definitions.Count();++id) {
        const auto* d=definitions.At(id);VariableRateSampleBinding binding;
        binding.instrument=id;binding.type=d->type;
        if(d->type==InstrumentType::Sample) {
            if(prepare(id,d->resource,binding.sample)!=InstrumentAvailability::Ready)continue;
            binding.reference_note=60;binding.sample_level_percent=80;
        }
        result.bindings[result.count++]=binding;
    }
    return result;
}
// Main owner, with audio excluded by the caller. One append, no allocation/I/O.
// The stopped foreground prepares new WAVs before supplying a binding here.
// The no-prepared overload retains legacy asset-alias compatibility.
template<class AppendBinding>
unsigned CommitInstrumentDefinition(InstrumentDefinitions& definitions,VariableRateSampleBindings& bindings,
    const InstrumentDefineRequest& request,bool ready,AppendBinding append,const VariableRateSampleBinding* prepared=nullptr) noexcept {
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
    if(request.type==1 && prepared) {
        if(prepared->type!=InstrumentType::Sample || !prepared->sample.data || !prepared->sample.frame_count)return 4;
        binding=*prepared;binding.instrument=request.expected;resource=request.resource;playable=true;
    }
    if(!definitions.CanAppend(request.expected,request.type,resource))return 4;
    if(playable && (bindings.count>=kVariableRateSampleBindingCapacity || !append(binding)))return 5;
    if(playable)bindings.bindings[bindings.count++]=binding;
    (void)definitions.Append(request.expected,request.type,resource);return 0;
}
}

namespace BroTracker {
// Caller has prepared the candidate in foreground and excludes audio for this
// bounded transaction. Rejection leaves definition, old binding and count intact.
template<class Install> unsigned RelinkSampleDefinition(InstrumentDefinitions& definitions,VariableRateSampleBindings& bindings,
    const InstrumentDefineRequest& request,const VariableRateSampleBinding& prepared,bool ready,Install install) noexcept {
    const auto* definition=definitions.At(request.expected);
    if(!definition || definition->type!=InstrumentType::Sample || request.type!=1)return 2;
    if(!ready)return 3;
    if(!InstrumentDefinitions::ValidResource(request.resource) || prepared.type!=InstrumentType::Sample || !prepared.sample.data || !prepared.sample.frame_count)return 4;
    auto candidate=prepared;candidate.instrument=request.expected;
    unsigned index=0;while(index<bindings.count && bindings.bindings[index].instrument!=request.expected)++index;
    if(index<bindings.count) {
        if(bindings.bindings[index].type!=InstrumentType::Sample)return 4;
        candidate.reference_note=bindings.bindings[index].reference_note;
        candidate.sample_level_percent=bindings.bindings[index].sample_level_percent;
    } else if(bindings.count>=kVariableRateSampleBindingCapacity)return 5;
    if(!install(candidate))return 5;
    (void)definitions.ReplaceSample(request.expected,request.resource);
    if(index==bindings.count)++bindings.count;
    bindings.bindings[index]=candidate;return 0;
}
}

#pragma once
#include "instrument_commit.h"
#include <algorithm>
namespace BroTracker {
// Fixed RAM snapshot. The platform pins original streaming sources while Active.
// Requests use the existing bounded definition transaction for prepared bindings.
class LiveInstrumentVisit {
public:
    bool Begin(std::uint32_t id,const InstrumentDefinitions& definitions,const VariableRateSampleBindings& bindings) {
        if(!id || (id_ && id_!=id))return false;
        if(!id_){original_definitions_=definitions;original_bindings_=bindings;id_=id;cancelled_=0;}
        return true;
    }
    bool MidiChannelsChanged(const InstrumentDefinitions& definitions)const {
        if(!Active())return false;
        for(unsigned i=0;i<definitions.Count();++i) {
            const auto* previous=original_definitions_.At(i);
            if(previous && previous->midi_channel!=definitions.At(i)->midi_channel)return true;
        }
        return false;
    }
    const InstrumentDefinitions& OriginalDefinitions()const{return original_definitions_;}
    bool Active()const{return id_!=0;}
    std::uint32_t Identity()const{return id_;}
    const VariableRateSampleBindings& OriginalBindings()const{return original_bindings_;}
    void Cancel(std::uint32_t request){cancelled_=std::max(cancelled_,request);}
    bool Cancelled(std::uint32_t request)const{return request && request<=cancelled_;}
    bool End(std::uint32_t id){if(id!=id_)return false;id_=0;original_definitions_.Reset(false);original_bindings_={};return true;}
    template<class Install> unsigned Restore(unsigned slot,InstrumentDefinitions& definitions,VariableRateSampleBindings& bindings,Install install)const {
        const auto* d=original_definitions_.At(slot);if(!Active() || !d || !definitions.At(slot))return 2;
        const VariableRateSampleBinding* original=nullptr;
        for(unsigned i=0;i<original_bindings_.count;++i)if(original_bindings_.bindings[i].instrument==slot)original=&original_bindings_.bindings[i];
        if(!install(slot,original))return 5;
        unsigned index=0;while(index<bindings.count && bindings.bindings[index].instrument!=slot)++index;
        if(original){if(index==bindings.count)++bindings.count;bindings.bindings[index]=*original;}
        else if(index<bindings.count){for(unsigned i=index+1;i<bindings.count;++i)bindings.bindings[i-1]=bindings.bindings[i];--bindings.count;}
        (void)definitions.Replace(slot,InstrumentTypeCode(d->type),d->resource,d->midi_channel);return 0;
    }
private:
    InstrumentDefinitions original_definitions_{false};VariableRateSampleBindings original_bindings_{};
    std::uint32_t id_=0,cancelled_=0;
};
}

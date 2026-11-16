#pragma once
#include "core/instrument_type.h"
#include "resource_path.h"
#include <cstddef>
#include <cstring>
namespace BroTracker {
// Boot definitions, independent of playable resources. Runtime append uses the
// same identities below; temporary BTM persists the runtime table. Array position is the raw ID.
struct InstrumentDefinition { InstrumentType type; const char* resource; };
inline constexpr InstrumentDefinition kDevelopmentInstrumentDefinitions[]={
    {InstrumentType::Sample,"Samples/test2.wav"}, {InstrumentType::MIDI,nullptr},
    {InstrumentType::Synth,nullptr}, {InstrumentType::Sample,"Samples/test3.wav"},
    {InstrumentType::Sample,"Samples/test.wav"}
};
static_assert(sizeof(kDevelopmentInstrumentDefinitions)/sizeof(InstrumentDefinition)<=99,"Instrument bound");
inline constexpr unsigned DevelopmentInstrumentCount() noexcept {return sizeof(kDevelopmentInstrumentDefinitions)/sizeof(InstrumentDefinition);}
inline const InstrumentDefinition* DevelopmentInstrumentDefinition(unsigned raw) noexcept {
    return raw<DevelopmentInstrumentCount()?&kDevelopmentInstrumentDefinitions[raw]:nullptr;
}
inline unsigned InstrumentTypeCode(InstrumentType type) noexcept {
    return type==InstrumentType::Sample?1:type==InstrumentType::MIDI?2:3;
}
enum class InstrumentAvailability : unsigned char {
    Undefined, Ready, NoResource, ResourceUnavailable, PreparationFailed, NotPrepared,
    StorageUnavailable, ExternalUnknown, PathCapacity
};

inline constexpr unsigned InstrumentSelectionMaximum(unsigned count) noexcept {return count<99?count+1:99;}
struct InstrumentDefinitionDraft {
    unsigned type=1;bool explicit_type=false;const char* sample=nullptr;
    bool Qualifies() const noexcept {return type==1?(sample && *sample):explicit_type && (type==2 || type==3);}
};
// The runtime authority; the development array above is initialization data only.
class InstrumentDefinitions {
public:
    explicit InstrumentDefinitions(bool development=true) noexcept {Reset(development);}
    InstrumentDefinitions(const InstrumentDefinitions& other) noexcept { *this=other; }
    InstrumentDefinitions& operator=(const InstrumentDefinitions& other) noexcept {
        if(this!=&other) { Reset(false);for(unsigned i=0;i<other.Count();++i) {const auto* d=other.At(i);Append(i,InstrumentTypeCode(d->type),d->resource);} }return *this;
    }
    void Reset(bool development=true) noexcept {for(auto& r:resources_)r.Reset();count_=0;if(development)for(const auto& d:kDevelopmentInstrumentDefinitions)Append(count_,InstrumentTypeCode(d.type),d.resource);}
    static bool ValidResource(const char* r) noexcept {return r && ValidSamplePath(r);}
    unsigned Count() const noexcept {return count_;}
    const InstrumentDefinition* At(unsigned id) const noexcept {return id<count_?&definitions_[id]:nullptr;}
    bool CanAppend(unsigned expected,unsigned type,const char* resource) const noexcept {
        return expected==count_ && count_<99 && (type==1?(ValidResource(resource)&&ResourcePaths().CanAcquire(resource)):(type==2 || type==3) && !resource);
    }
    bool Append(unsigned expected,unsigned type,const char* resource) noexcept {
        if(!CanAppend(expected,type,resource))return false;
        if(!resources_[count_].Assign(resource))return false;
        definitions_[count_]={type==1?InstrumentType::Sample:type==2?InstrumentType::MIDI:InstrumentType::Synth,resources_[count_].Get()};++count_;return true;
    }
    bool CanReplace(unsigned slot,unsigned type,const char* resource)const noexcept {
        return slot<count_ && (type==1?(ValidResource(resource)&&ResourcePaths().CanAcquire(resource)):(type==2 || type==3) && !resource);
    }
    bool Replace(unsigned slot,unsigned type,const char* resource) noexcept {
        if(!CanReplace(slot,type,resource))return false;
        if(!resources_[slot].Assign(resource))return false;
        definitions_[slot]={type==1?InstrumentType::Sample:type==2?InstrumentType::MIDI:InstrumentType::Synth,resources_[slot].Get()};return true;
    }
    bool ReplaceSample(unsigned slot,const char* resource) noexcept {
        if(slot>=count_ || definitions_[slot].type!=InstrumentType::Sample || !ValidResource(resource))return false;
        if(!resources_[slot].Assign(resource))return false;definitions_[slot].resource=resources_[slot].Get();return true;
    }
private:
    ResourcePathRef resources_[99];InstrumentDefinition definitions_[99]{};unsigned count_=0;
};
}

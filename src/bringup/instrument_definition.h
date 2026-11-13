#pragma once
#include "core/instrument_type.h"
#include <cstddef>
namespace BroTracker {
// Boot definitions, independent of playable resources. Runtime append uses the
// same identities below; no durable persistence. Array position is the raw ID.
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
    StorageUnavailable, ExternalUnknown
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
    void Reset(bool development=true) noexcept {count_=development?DevelopmentInstrumentCount():0;for(unsigned i=0;i<count_;++i)definitions_[i]=kDevelopmentInstrumentDefinitions[i];}
    unsigned Count() const noexcept {return count_;}
    const InstrumentDefinition* At(unsigned id) const noexcept {return id<count_?&definitions_[id]:nullptr;}
    bool CanAppend(unsigned expected,unsigned type,const char* resource) const noexcept {
        return expected==count_ && count_<99 && (type==1?(resource && *resource):(type==2 || type==3) && !resource);
    }
    bool Append(unsigned expected,unsigned type,const char* resource) noexcept {
        if(!CanAppend(expected,type,resource))return false;
        definitions_[count_++]={type==1?InstrumentType::Sample:type==2?InstrumentType::MIDI:InstrumentType::Synth,resource};return true;
    }
private:
    InstrumentDefinition definitions_[99]{};unsigned count_=0;
};
}

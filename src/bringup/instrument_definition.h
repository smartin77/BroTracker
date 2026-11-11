#pragma once
#include "core/instrument_type.h"
#include <cstddef>
namespace BroTracker {
// Firmware-initialized definitions, independent of playable resources. No user
// creation/persistence yet. Array position is the raw instance ID (maximum 99).
struct InstrumentDefinition { InstrumentType type; const char* resource; };
inline constexpr InstrumentDefinition kDevelopmentInstrumentDefinitions[]={
    {InstrumentType::Sample,"Samples/test2.wav"}, {InstrumentType::MIDI,nullptr},
    {InstrumentType::Synth,nullptr}, {InstrumentType::Sample,"Samples/test3.wav"},
    {InstrumentType::Sample,"Samples/test.wav"}
};
static_assert(sizeof(kDevelopmentInstrumentDefinitions)/sizeof(InstrumentDefinition)<=99,"Instrument bound");
inline const InstrumentDefinition* DevelopmentInstrumentDefinition(unsigned raw) noexcept {
    return raw<sizeof(kDevelopmentInstrumentDefinitions)/sizeof(InstrumentDefinition)?&kDevelopmentInstrumentDefinitions[raw]:nullptr;
}
inline unsigned InstrumentTypeCode(InstrumentType type) noexcept {
    return type==InstrumentType::Sample?1:type==InstrumentType::MIDI?2:3;
}
enum class InstrumentAvailability : unsigned char {
    Undefined, Ready, NoResource, ResourceUnavailable, PreparationFailed, NotPrepared,
    StorageUnavailable, ExternalUnknown
};
}

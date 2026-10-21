#pragma once
#include <cstdint>

namespace BroTracker {
// Categories, not instance IDs. Multiple bindings may share a category.
// MIDI is a reserved unsupported category in the current renderer.
enum class InstrumentType : std::uint8_t { Sample, MIDI, Synth };
}

#pragma once
#include <cstdint>
namespace BroTracker {
// Caller holds USB lifecycle exclusion through comparison and submission.
// Never submit a packet computed from a prior USB session.
template<class Submit>
bool SubmitMidiForGeneration(std::uint32_t observed,std::uint32_t current,Submit submit) noexcept {
    return observed==current && submit();
}
}

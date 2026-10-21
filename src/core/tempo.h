#pragma once
#include <cstdint>
namespace BroTracker {
constexpr std::uint32_t kDefaultTempo = 12500, kMinimumTempo = 3200, kMaximumTempo = 30000;
constexpr bool ValidInteractiveTempo(std::uint32_t value) noexcept { return value>=kMinimumTempo && value<=kMaximumTempo; }
inline std::uint32_t StepTempo(std::uint32_t value, int delta) noexcept {
    const auto next=static_cast<std::int64_t>(value)+delta;
    return next<kMinimumTempo ? kMinimumTempo : next>kMaximumTempo ? kMaximumTempo : static_cast<std::uint32_t>(next);
}
constexpr std::uint32_t TempoTenths(std::uint32_t value) noexcept { return value/10+(value%10>=5); }
}

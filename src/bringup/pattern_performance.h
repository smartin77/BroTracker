#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cinttypes>
#include <limits>

namespace BroTracker {
struct PatternPerformance {
    std::uint32_t callbacks=0, steady_callbacks=0, initialization_cycles=0;
    std::uint32_t maximum_cycles=0, over_budget=0, budget_cycles=0;
};
// AUDIO_SAMPLE_RATE_EXACT may be fractional. Floor the cycle budget; values must
// fit one counter period. Output unchanged on invalid metadata.
inline bool PatternCycleBudget(std::uint32_t cpu_hz, std::uint32_t frames,
    double sample_rate_hz, std::uint32_t& output) noexcept {
    if(!cpu_hz || !frames || !(sample_rate_hz>0)) return false;
    const double cycles=double(cpu_hz)*frames/sample_rate_hz;
    if(!(cycles>=1 && cycles<=std::numeric_limits<std::uint32_t>::max())) return false;
    output=static_cast<std::uint32_t>(cycles); return true;
}
inline void RecordPatternCycles(PatternPerformance& stats, std::uint32_t begin,
    std::uint32_t end, bool initialization, std::uint32_t budget) noexcept {
    const auto increment=[](std::uint32_t& n) { if(n!=UINT32_MAX) ++n; };
    const std::uint32_t elapsed=end-begin; // modulo arithmetic, duration < 2^32 cycles
    increment(stats.callbacks); stats.budget_cycles=budget;
    if(initialization) { stats.initialization_cycles=elapsed; return; }
    increment(stats.steady_callbacks);
    if(elapsed>stats.maximum_cycles) stats.maximum_cycles=elapsed;
    if(elapsed>budget) increment(stats.over_budget);
}
constexpr std::size_t kPatternPerformanceLineCapacity=96;
inline bool FormatPatternPerformance(const PatternPerformance& s, char* out,
    std::size_t capacity, std::size_t& length) noexcept {
    if(!out || !capacity) return false;
    const int n=std::snprintf(out,capacity,"BTPATTERN1 PERF 1 %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 "\n",
        s.callbacks,s.steady_callbacks,s.initialization_cycles,s.maximum_cycles,s.over_budget,s.budget_cycles);
    if(n<0 || static_cast<std::size_t>(n)>=capacity) return false;
    length=static_cast<std::size_t>(n); return true;
}
}

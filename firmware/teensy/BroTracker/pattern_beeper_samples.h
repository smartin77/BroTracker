#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace BroTracker {
constexpr std::size_t kPatternBeeperFrames=8192;
constexpr std::int32_t kDevelopmentBeeperAmplitudeMultiplier=4;
// The largest pre-division magnitude is 600 * 8191 = 4914600.
static_assert(600*(kPatternBeeperFrames-1)<=std::uint32_t(INT32_MAX),"Decay product fits int32");
static_assert(600*kDevelopmentBeeperAmplitudeMultiplier<=INT16_MAX,"Scaled waveform fits PCM16");
namespace PatternBeeperDetail {
constexpr std::int32_t Frame(bool triangle,std::size_t i,bool development) noexcept {
    const auto phase=static_cast<std::int32_t>(i%(triangle?64:100));
    const std::int32_t wave=triangle ? (phase<32 ? phase*32-512 : 1536-phase*32) :
        (phase<50 ? 600 : -600);
    const auto legacy=wave*static_cast<std::int32_t>(kPatternBeeperFrames-1-i)/
        static_cast<std::int32_t>(kPatternBeeperFrames-1);
    // Scale the original quantized PCM to preserve every legacy sample exactly.
    return legacy*(development?kDevelopmentBeeperAmplitudeMultiplier:1);
}
constexpr bool Fits(bool triangle) noexcept {
    for(std::size_t i=0;i<kPatternBeeperFrames;++i) {
        const auto value=Frame(triangle,i,true);
        if(value<std::numeric_limits<std::int16_t>::min() ||
            value>std::numeric_limits<std::int16_t>::max()) return false;
    }
    return true;
}
}
static_assert(PatternBeeperDetail::Fits(false) && PatternBeeperDetail::Fits(true),
    "Every generated development frame fits before narrowing to PCM16");
// Immutable boot fixture generation only, never audio-time synthesis.
constexpr std::array<std::int16_t,kPatternBeeperFrames> MakePatternBeeperSample(
    bool triangle,bool development) noexcept {
    std::array<std::int16_t,kPatternBeeperFrames> pcm{};
    for(std::size_t i=0;i<pcm.size();++i)
        pcm[i]=static_cast<std::int16_t>(PatternBeeperDetail::Frame(triangle,i,development));
    return pcm;
}
}

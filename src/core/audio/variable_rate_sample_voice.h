#pragma once
#include "ram_sample_voice.h"
namespace BroTracker {
constexpr std::uint64_t kSourceFrameStepUnity = std::uint64_t{1} << 32;
enum class VariableVoiceStatus { Success, InvalidSampleRate, NotConfigured, InvalidSample,
    SampleRateMismatch, InvalidStep, InvalidDestination, RangeOverflow };

// Single-owner PCM16 mono one-shot. Immutable non-owning view and actual memory
// extents/lifetimes are caller guarantees, until Stop/Reset/Configure, replacement
// or completion releases it. Destination must not overlap PCM, this object or
// request metadata. No alias/extent inference via pointer comparisons.
// Explicit positive unsigned Q32.32 source-frame step per output frame, exact
// unity=2^32; source/output rates must match. No note/reference-pitch inference.
// Integer position (size_t) and uint32 fractional phase are separate: Q32 packing
// never limits sample length to 2^32 frames. Metadata byte limits match RamSampleVoice.
// Fixed-point linear interpolation is an initial quality choice, NOT band-limited
// resampling. Upward transposition may alias; changing rate changes pitch AND
// duration, not time stretching. No floating point, allocation, locks, I/O or clock.
class VariableRateSampleVoice {
public:
    static constexpr std::size_t kMaximumFrameCount = RamSampleVoice::kMaximumFrameCount;
    // Valid configuration stops playback; failure preserves all state.
    [[nodiscard]] VariableVoiceStatus Configure(std::uint32_t rate) noexcept {
        if(!rate) return VariableVoiceStatus::InvalidSampleRate;
        rate_=rate; Stop(); return VariableVoiceStatus::Success;
    }
    void Stop() noexcept { sample_={}; position_=0; phase_=0; step_=0; }
    void Reset() noexcept { Stop(); } // retains configured output rate
    [[nodiscard]] VariableVoiceStatus Trigger(Pcm16MonoView sample,std::uint64_t step_q32_32) noexcept {
        if(!rate_) return VariableVoiceStatus::NotConfigured;
        if(!sample.data || !sample.frame_count || !sample.sample_rate_hz) return VariableVoiceStatus::InvalidSample;
        if(sample.frame_count>kMaximumFrameCount) return VariableVoiceStatus::RangeOverflow;
        if(sample.sample_rate_hz!=rate_) return VariableVoiceStatus::SampleRateMismatch;
        if(!step_q32_32) return VariableVoiceStatus::InvalidStep;
        sample_=sample; position_=0; phase_=0; step_=step_q32_32;
        return VariableVoiceStatus::Success;
    }
    // Preflight arguments before writing/mutating. Zero count is a no-op, null OK.
    // Silent/unconfigured voices emit zero. Caller guarantees count writable frames.
    // Clamp the second endpoint to the last source frame while position is inside
    // the sample. After advancing to/past end, release view and zero-fill remainder.
    // Work O(output count), independent of source length or step; no skipped-frame
    // scan. Invalid requests preserve voice AND destination. Partition-independent.
    [[nodiscard]] VariableVoiceStatus Render(std::int16_t* destination,std::size_t count) noexcept {
        if(!count) return VariableVoiceStatus::Success;
        if(!destination) return VariableVoiceStatus::InvalidDestination;
        if(count>kMaximumFrameCount) return VariableVoiceStatus::RangeOverflow;
        for(std::size_t i=0;i<count;++i) {
            if(!IsActive()) { destination[i]=0; continue; }
            const auto next=position_==sample_.frame_count-1 ? position_ : position_+1;
            // Exact weighted sum in [-2^47, 2^47-2^32]; rounding headroom 2^31
            // fits int64. Convex endpoints ensure rounded result stays in PCM16.
            // Round ONCE to nearest, ties away from zero. Signed division truncates
            // toward zero (C++17); never shift negative signed values.
            constexpr auto unit=static_cast<std::int64_t>(kSourceFrameStepUnity);
            const auto fraction=static_cast<std::int64_t>(phase_);
            const std::int64_t sum=static_cast<std::int64_t>(sample_.data[position_])*(unit-fraction)+
                static_cast<std::int64_t>(sample_.data[next])*fraction;
            destination[i]=static_cast<std::int16_t>((sum>=0 ? sum+unit/2 : sum-unit/2)/unit);
            Advance();
        }
        return VariableVoiceStatus::Success;
    }
    bool IsActive() const noexcept { return sample_.data!=nullptr; }
    std::size_t GetFramePosition() const noexcept { return position_; }
    std::uint32_t GetFractionalPhase() const noexcept { return phase_; }
    std::uint64_t GetStep() const noexcept { return step_; }
    std::uint32_t GetOutputSampleRate() const noexcept { return rate_; }
private:
    void Advance() noexcept {
        static_assert(sizeof(std::size_t)<=sizeof(std::uint64_t), "Position arithmetic needs at most uint64");
        const std::uint64_t phase_sum=static_cast<std::uint64_t>(phase_)+static_cast<std::uint32_t>(step_);
        // <=2^32 whole frames, even for maximum step plus fractional carry.
        const std::uint64_t advance=(step_>>32)+(phase_sum>>32);
        const auto remaining=static_cast<std::uint64_t>(sample_.frame_count-position_);
        if(advance>=remaining) { Stop(); return; }
        // advance<remaining proves narrowing/addition representable in size_t.
        position_+=static_cast<std::size_t>(advance);
        phase_=static_cast<std::uint32_t>(phase_sum); // intentional modulo-2^32 fraction, carry consumed
    }
    friend struct VariableRateSampleVoiceTestAccess;
    Pcm16MonoView sample_;
    std::size_t position_=0;
    std::uint32_t phase_=0,rate_=0;
    std::uint64_t step_=0;
};
}

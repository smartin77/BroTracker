#pragma once
#include "ram_sample_voice.h"
#include <algorithm>
namespace BroTracker {
constexpr std::uint64_t kSourceFrameStepUnity = std::uint64_t{1} << 32;
enum class VariableVoiceStatus { Success, InvalidSampleRate, NotConfigured, InvalidSample,
    SampleRateMismatch, InvalidStep, InvalidDestination, RangeOverflow };

// Non-owning native interleaved L/R PCM16. Counts are stereo frames.
// A resident view spans frame_count pairs. A streamed view points to the
// descriptor's immutable prefix; its published rings also contain interleaved pairs.
struct Pcm16StereoView {
    const std::int16_t* data=nullptr;
    std::size_t frame_count=0;
    std::uint32_t sample_rate_hz=0;
    const StreamedSample* streaming=nullptr;
};

// Single-owner PCM16 one-shot. Immutable non-owning view and actual memory
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
    void Stop() noexcept { ++generation_; sample_={}; stereo_=false; position_=0; phase_=0; step_=0; }
    void SetStreamChannel(unsigned channel) noexcept { stream_channel_=channel; }
    StreamedVoiceSnapshot StreamSnapshot() const noexcept {
        return {sample_.streaming,generation_,step_,static_cast<std::uint32_t>(position_),phase_,underruns_,eofs_,min_ready_,hash_};
    }
    void Reset() noexcept { Stop(); } // retains configured output rate
    [[nodiscard]] VariableVoiceStatus Trigger(Pcm16MonoView sample,std::uint64_t step_q32_32) noexcept {
        return TriggerSource<1>(sample,step_q32_32);
    }
    [[nodiscard]] VariableVoiceStatus TriggerStereo(Pcm16StereoView sample,std::uint64_t step_q32_32) noexcept {
        return TriggerSource<2>(sample,step_q32_32);
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
        if(stereo_) return VariableVoiceStatus::InvalidSample;
        RenderFrames<1>(destination,nullptr,count);
        return VariableVoiceStatus::Success;
    }
    // Planar output. The caller supplies disjoint writable spans and immutable
    // PCM for the lifetime of the note. A mono source is copied to both sides.
    [[nodiscard]] VariableVoiceStatus RenderStereo(std::int16_t* left,std::int16_t* right,std::size_t count) noexcept {
        if(!count) return VariableVoiceStatus::Success;
        if(!left || !right || left==right) return VariableVoiceStatus::InvalidDestination;
        if(count>kMaximumFrameCount) return VariableVoiceStatus::RangeOverflow;
        if(stereo_) RenderFrames<2>(left,right,count);
        else {
            RenderFrames<1>(left,nullptr,count);
            std::copy_n(left,count,right);
        }
        return VariableVoiceStatus::Success;
    }
    bool IsActive() const noexcept { return sample_.data!=nullptr; }
    bool IsStereo() const noexcept { return stereo_; }
    std::size_t GetFramePosition() const noexcept { return position_; }
    std::uint32_t GetFractionalPhase() const noexcept { return phase_; }
    std::uint64_t GetStep() const noexcept { return step_; }
    std::uint32_t GetOutputSampleRate() const noexcept { return rate_; }
private:
    template<unsigned Channels,class View>
    VariableVoiceStatus TriggerSource(View sample,std::uint64_t step) noexcept {
        if(!rate_) return VariableVoiceStatus::NotConfigured;
        if(!sample.data || !sample.frame_count || !sample.sample_rate_hz) return VariableVoiceStatus::InvalidSample;
        if(sample.frame_count>kMaximumFrameCount/Channels) return VariableVoiceStatus::RangeOverflow;
        if((Channels==2 && rate_!=44100) ||
            (sample.sample_rate_hz!=rate_ &&
                (!sample.streaming || rate_!=44100 || !IsStreamingPcm16Rate(sample.sample_rate_hz))))
            return VariableVoiceStatus::SampleRateMismatch;
        if(!step) return VariableVoiceStatus::InvalidStep;
        if(const auto* source=sample.streaming) {
            if(!source->start || !source->voices || source->frames!=sample.frame_count ||
                source->format.channels!=Channels || source->format.bits_per_sample!=16 ||
                source->format.sample_rate_hz!=sample.sample_rate_hz ||
                stream_channel_>=kStreamVoiceCount) return VariableVoiceStatus::InvalidSample;
        }
        ++generation_;
        sample_={sample.data,sample.frame_count,sample.streaming};
        stereo_=Channels==2;
        position_=0; phase_=0; step_=step; hash_=2166136261u;
        return VariableVoiceStatus::Success;
    }
    // Exact weighted sum fits in int64_t with rounding headroom. Round once,
    // ties away from zero. The same phase is used for both components.
    static std::int16_t Interpolate(std::int16_t a,std::int16_t b,std::uint32_t fraction) noexcept {
        constexpr auto unit=static_cast<std::int64_t>(kSourceFrameStepUnity);
        const auto sum=std::int64_t(a)*(unit-fraction)+std::int64_t(b)*fraction;
        return static_cast<std::int16_t>((sum>=0 ? sum+unit/2 : sum-unit/2)/unit);
    }
    void Hash(std::int16_t value) noexcept {
        const auto word=static_cast<std::uint16_t>(value);
        hash_=(hash_^(word&255u))*16777619u; hash_=(hash_^(word>>8))*16777619u;
    }
    template<unsigned Channels>
    void RenderFrames(std::int16_t* left,std::int16_t* right,std::size_t count) noexcept {
        const auto* source=sample_.streaming;
        const auto prefix=source ? source->PrefixFrames() : 0;
        const auto* ready=source ? &source->voices[stream_channel_] : nullptr;
        const bool current=ready && ready->sample==source && ready->generation==generation_;
        // All supported byte geometries have a power-of-two ring frame capacity.
        const std::size_t ring_mask=source ? source->layout.ring_frames-1 : 0;
        const auto fetch=[&](std::size_t at) -> const std::int16_t* {
            if(!source) return sample_.data+at*Channels;
            if(at<prefix) return source->start+at*Channels;
            if(current && at>=ready->begin && at<ready->end)
                return ready->ring+(at&ring_mask)*Channels;
            return nullptr;
        };
        for(std::size_t i=0;i<count;++i) {
            if(!IsActive()) {
                std::fill_n(left+i,count-i,0);
                if constexpr(Channels==2) std::fill_n(right+i,count-i,0);
                break;
            }
            const auto* a=fetch(position_);
            const auto* b=phase_ && position_+1<sample_.frame_count ? fetch(position_+1) : a;
            left[i]=a && b ? Interpolate(a[0],b[0],phase_) : 0;
            if constexpr(Channels==2) right[i]=a && b ? Interpolate(a[1],b[1],phase_) : 0;
            if(source) {
                if(!a || !b) ++underruns_;
                auto end=std::max(prefix,static_cast<std::uint32_t>(position_));
                if(current && ready->begin<=end && ready->end>end) end=ready->end;
                const auto available=end-static_cast<std::uint32_t>(position_);
                if(end<sample_.frame_count && available<min_ready_) min_ready_=available;
                Hash(left[i]);
                if constexpr(Channels==2) Hash(right[i]); // Native interleaved byte order.
            }
            Advance();
        }
    }
    void Advance() noexcept {
        static_assert(sizeof(std::size_t)<=sizeof(std::uint64_t), "Position arithmetic needs at most uint64");
        const std::uint64_t phase_sum=static_cast<std::uint64_t>(phase_)+static_cast<std::uint32_t>(step_);
        // <=2^32 whole frames, even for maximum step plus fractional carry.
        const std::uint64_t advance=(step_>>32)+(phase_sum>>32);
        const auto remaining=static_cast<std::uint64_t>(sample_.frame_count-position_);
        if(advance>=remaining) { if(sample_.streaming) ++eofs_; Stop(); return; }
        // advance<remaining proves narrowing/addition representable in size_t.
        position_+=static_cast<std::size_t>(advance);
        phase_=static_cast<std::uint32_t>(phase_sum); // intentional modulo-2^32 fraction, carry consumed
    }
    friend struct VariableRateSampleVoiceTestAccess;
    unsigned stream_channel_=0;
    bool stereo_=false;
    std::uint64_t generation_=0;
    std::uint32_t underruns_=0,eofs_=0,min_ready_=UINT32_MAX,hash_=2166136261u;
    struct {
        const std::int16_t* data=nullptr;
        std::size_t frame_count=0;
        const StreamedSample* streaming=nullptr;
    } sample_;
    std::size_t position_=0;
    std::uint32_t phase_=0,rate_=0;
    std::uint64_t step_=0;
};
}

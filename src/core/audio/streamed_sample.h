#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
namespace BroTracker {
constexpr bool IsStreamingPcm16Rate(std::uint32_t rate) noexcept {
    return rate==44100 || rate==22050 || rate==11025 || rate==8000;
}
// Compose an already quantized Q32.32 pitch ratio with the rational rate
// conversion, rounding to nearest with ties upward. Splitting the quotient
// bounds both products, even at the largest positive input step.
inline bool ComposeStreamingStep(std::uint64_t pitch,std::uint32_t source_rate,
    std::uint32_t output_rate,std::uint64_t& result) noexcept {
    if(!pitch || !source_rate || !output_rate) return false;
    if(source_rate==output_rate) {result=pitch;return true;}
    if(output_rate!=44100 || !IsStreamingPcm16Rate(source_rate)) return false;
    const auto step=(pitch/output_rate)*source_rate+
        ((pitch%output_rate)*source_rate+output_rate/2)/output_rate;
    if(!step) return false;
    result=step;return true;
}
// Immutable prepared source. File ownership belongs to the foreground adapter.
// Ring spans are published only while the audio consumer is excluded.
constexpr std::uint32_t kStreamRingBytes=8192, kStreamStartBytes=4096, kStreamReadBytes=4096;
constexpr std::uint32_t kStreamRingFrames=kStreamRingBytes/sizeof(std::int16_t),
    kStreamStartFrames=kStreamStartBytes/sizeof(std::int16_t),
    kStreamReadFrames=kStreamReadBytes/sizeof(std::int16_t);
constexpr unsigned kStreamVoiceCount=9; // eight Pattern voices plus independent WAV audition
struct StreamSourceFormat {
    std::uint32_t sample_rate_hz=44100;
    std::uint16_t channels=1,bits_per_sample=16;

    // Geometry for integer PCM. Playback admission is checked separately.
    constexpr std::uint16_t BytesPerFrame() const noexcept {
        if(!sample_rate_hz || (channels!=1 && channels!=2) ||
            (bits_per_sample!=8 && bits_per_sample!=16)) return 0;
        return channels*(bits_per_sample/8);
    }
    constexpr bool IsPcm16Mono() const noexcept {
        return channels==1 && bits_per_sample==16;
    }
};
struct StreamBufferLayout {
    std::uint16_t bytes_per_frame=0,start_frames=0,ring_frames=0,read_frames=0;
    constexpr explicit StreamBufferLayout(StreamSourceFormat format) noexcept
        : bytes_per_frame(format.BytesPerFrame()),
          start_frames(bytes_per_frame ? kStreamStartBytes/bytes_per_frame : 0),
          ring_frames(bytes_per_frame ? kStreamRingBytes/bytes_per_frame : 0),
          read_frames(bytes_per_frame ? kStreamReadBytes/bytes_per_frame : 0) {}
};
struct StreamedSample;
struct StreamedVoiceBuffer {
    // Native interleaved PCM16, indexed using the source layout in frames.
    std::int16_t* ring=nullptr;
    std::uint64_t generation=0;
    const StreamedSample* sample=nullptr;
    std::uint32_t begin=0,end=0;
};
struct StreamedSample {
    // Native interleaved PCM16 prefix. The layout describes the byte budget in frames.
    const std::int16_t* start=nullptr;
    std::uint32_t frames=0;
    StreamedVoiceBuffer* voices=nullptr; // independent voice rings, shared start
    StreamSourceFormat format{};
    StreamBufferLayout layout{format};

    std::uint32_t PrefixFrames() const noexcept {
        return std::min<std::uint32_t>(layout.start_frames,frames);
    }
};
struct StreamedVoiceSnapshot {
    const StreamedSample* sample=nullptr;
    std::uint64_t generation=0,step=0;
    std::uint32_t position=0,phase=0;
    std::uint32_t underruns=0,eofs=0,min_ready=UINT32_MAX,hash=2166136261u;
};
// End of the contiguous readable span at position, or position on starvation.
inline std::uint32_t StreamReadyEnd(const StreamedSample& source, const StreamedVoiceBuffer& ready,
    std::uint64_t generation, std::uint32_t position) noexcept {
    const auto prefix=source.PrefixFrames();
    auto end=std::max(position,prefix);
    if(ready.sample==&source && ready.generation==generation && ready.begin<=end && ready.end>end)
        end=ready.end;
    return end;
}
// Conservative output-time coverage. Fractional playback can need the next
// source frame before it reaches the end of the ready span. At EOF the voice
// reuses its final sample, and integer playback needs no interpolation guard.
inline std::uint64_t StreamReadyOutputFrames(const StreamedVoiceSnapshot& state,
    std::uint32_t end) noexcept {
    if(!state.step || end<=state.position) return 0;
    auto ready=end-state.position;
    if(end<state.sample->frames && (state.phase || std::uint32_t(state.step))) --ready;
    return (std::uint64_t(ready)<<32)/state.step;
}
// Source frames determine ring capacity; output frames determine urgency.
// Higher pitch may make a larger ready span more urgent than a smaller one.
inline std::uint64_t StreamRefillUrgency(const StreamedVoiceSnapshot& state,
    const StreamedVoiceBuffer& ready) noexcept {
    if(!state.sample || !state.step || ready.sample!=state.sample || ready.generation!=state.generation ||
        ready.end>=state.sample->frames || state.position>=state.sample->frames) return UINT64_MAX;
    const auto& source=*state.sample;
    const auto ring_frames=source.layout.ring_frames, read_frames=source.layout.read_frames;
    if(!read_frames) return UINT64_MAX;
    const auto consumed=std::max({state.position,source.PrefixFrames(),ready.begin});
    const auto used=ready.end>consumed ? ready.end-consumed : 0;
    const auto end=StreamReadyEnd(source,ready,state.generation,state.position);
    const auto available=end-state.position;
    const auto until=StreamReadyOutputFrames(state,end);
    // Preserve the mono half-ring threshold and the existing output-time threshold.
    const auto half_ring=std::uint32_t(ring_frames)/2;
    if(used>ring_frames || ring_frames-used<read_frames ||
        (available>half_ring && until>kStreamRingFrames/2)) return UINT64_MAX;
    return until;
}
// Caller excludes the audio consumer during publication; a stale completion
// can never expose its bytes to a new note or replacement source.
inline bool PublishStreamRead(StreamedVoiceBuffer& ready,const StreamedVoiceSnapshot& request,
    std::uint32_t at,std::uint32_t count) noexcept {
    if(ready.generation!=request.generation || ready.sample!=request.sample || !request.sample) return false;
    const auto ring_frames=request.sample->layout.ring_frames;
    if(!count || count>ring_frames || at<ready.end || at>request.sample->frames ||
        count>request.sample->frames-at) return false;
    if(at>ready.end) ready.begin=at;
    ready.end=at+count;
    // Contiguous refills overwrite old slots; a forward gap still starts at at.
    const auto oldest=ready.end>ring_frames ? ready.end-ring_frames : 0;
    ready.begin=std::max(ready.begin,oldest);
    return true;
}
}

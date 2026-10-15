#pragma once
#include "ram_sample_voice.h"
#include <cstring>

namespace BroTracker {
// Single-owner preparation reader. Read returns bytes actually read (<=capacity),
// short reads are errors. Size/Seek return false on I/O failure. No ownership transfer.
class WavBufferReader {
public:
    virtual ~WavBufferReader() = default;
    virtual bool Size(std::uint64_t& bytes) noexcept = 0;
    virtual bool Seek(std::uint64_t offset) noexcept = 0;
    virtual std::size_t Read(std::uint8_t* destination, std::size_t count) noexcept = 0;
};
enum class WavBufferStatus {
    Success, IoError, InvalidDestination, InvalidCapacity, InvalidContainer,
    Truncated, InvalidChunk, DuplicateChunk, MissingChunk, InvalidFormat,
    UnsupportedFormat, InvalidData, InsufficientCapacity, ChunkLimit
};
namespace WavBufferDetail {
inline std::uint16_t U16(const std::uint8_t* p) noexcept { return std::uint16_t(p[0]) | (std::uint16_t(p[1])<<8); }
inline std::uint32_t U32(const std::uint8_t* p) noexcept {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1])<<8) | (std::uint32_t(p[2])<<16) | (std::uint32_t(p[3])<<24);
}
inline bool ReadAt(WavBufferReader& reader, std::uint64_t offset, std::uint8_t* p, std::size_t n) noexcept {
    return reader.Seek(offset) && reader.Read(p,n)==n;
}
}
// Header-only inspection; diagnostic metadata is explicitly partial on failure.
// Never reads PCM. At most chunk_limit chunks and 18 format bytes per chunk;
// payloads are skipped by checked seek. Trailing bytes outside RIFF are ignored.
struct WavMetadata {
    bool file_known=false,format_known=false,data_known=false,duration_known=false;
    std::uint64_t file_bytes=0,data_offset=0,data_bytes=0,frames=0,duration_us=0,required_pcm_bytes=0;
    std::uint32_t sample_rate=0,byte_rate=0;
    std::uint16_t format=0,channels=0,bits=0,block_align=0;
};
constexpr std::uint32_t kWavInspectionChunkLimit=1024;
namespace WavBufferDetail {
inline WavBufferStatus Inspect(WavBufferReader& reader,WavMetadata& metadata,std::uint32_t chunk_limit) noexcept {
    std::uint64_t size=0;
    if(!reader.Size(size)) return WavBufferStatus::IoError;
    metadata.file_bytes=size; metadata.file_known=true;
    if(size<12) return WavBufferStatus::Truncated;
    std::uint8_t header[18]{};
    if(!ReadAt(reader,0,header,12)) return WavBufferStatus::IoError;
    if(std::memcmp(header,"RIFF",4) || std::memcmp(header+8,"WAVE",4)) return WavBufferStatus::InvalidContainer;
    const std::uint64_t end=std::uint64_t(U32(header+4))+8; // promotion before addition
    if(end<12) return WavBufferStatus::InvalidContainer;
    if(end>size) return WavBufferStatus::Truncated;
    bool have_format=false, have_data=false;
    std::uint64_t data_bytes=0;
    std::uint32_t chunks=0;
    bool supported=false;
    for(std::uint64_t offset=12; offset<end;) {
        if(chunks++>=chunk_limit) return WavBufferStatus::ChunkLimit;
        if(end-offset<8) return WavBufferStatus::InvalidChunk;
        if(!ReadAt(reader,offset,header,8)) return WavBufferStatus::IoError;
        const auto bytes=std::uint64_t(U32(header+4));
        const auto payload=offset+8; // bounded by end above
        if(bytes>end-payload) return WavBufferStatus::InvalidChunk;
        const auto padding=bytes&1;
        if(padding>end-payload-bytes) return WavBufferStatus::InvalidChunk;
        if(!std::memcmp(header,"fmt ",4)) {
            if(have_format) return WavBufferStatus::DuplicateChunk;
            have_format=true;
            if(bytes<16 || bytes==17) return WavBufferStatus::InvalidFormat;
            if(!ReadAt(reader,payload,header,bytes==16 ? 16 : 18)) return WavBufferStatus::IoError;
            if(bytes!=16 && U16(header+16)!=bytes-18) return WavBufferStatus::InvalidFormat;
            metadata.format=U16(header); metadata.channels=U16(header+2);
            metadata.sample_rate=U32(header+4); metadata.bits=U16(header+14);
            metadata.block_align=U16(header+12); metadata.byte_rate=U32(header+8); metadata.format_known=true;
            supported=metadata.format==1 && metadata.channels==1 && metadata.sample_rate==44100 && metadata.bits==16;
            if(supported && (metadata.block_align!=2 || metadata.byte_rate!=88200)) return WavBufferStatus::InvalidFormat;
        } else if(!std::memcmp(header,"data",4)) {
            if(have_data) return WavBufferStatus::DuplicateChunk;
            have_data=true; data_bytes=bytes;
            metadata.data_offset=payload; metadata.data_bytes=bytes; metadata.data_known=true;
        }
        offset=payload+bytes+padding; // each addition bounded by container end
    }
    if(!have_format || !have_data) return WavBufferStatus::MissingChunk;
    if(!data_bytes) return WavBufferStatus::InvalidData;
    const std::uint64_t expected_align=std::uint64_t(metadata.channels)*((metadata.bits+7)/8);
    const bool layout=metadata.format==1 && metadata.channels && metadata.bits && metadata.bits%8==0 &&
        metadata.sample_rate && metadata.block_align==expected_align &&
        std::uint64_t(metadata.byte_rate)==std::uint64_t(metadata.sample_rate)*expected_align;
    if(layout) {
        if(data_bytes%metadata.block_align) return WavBufferStatus::InvalidData;
        metadata.frames=data_bytes/metadata.block_align;
        // RIFF data bytes <= UINT32_MAX; multiplication fits uint64_t.
        metadata.duration_us=metadata.frames*1000000/metadata.sample_rate;
        metadata.duration_known=true;
    }
    if(!supported) return WavBufferStatus::UnsupportedFormat;
    if(data_bytes%2) return WavBufferStatus::InvalidData;
    metadata.required_pcm_bytes=data_bytes;
    return WavBufferStatus::Success;
}
} // namespace WavBufferDetail
// Output contains only evidence read before rejection; flags identify known fields.
// Duration is integer microseconds (floor), not tempo or reference pitch.
inline WavBufferStatus InspectWavMetadata(WavBufferReader& reader,WavMetadata& output,
    std::uint32_t chunk_limit=kWavInspectionChunkLimit) noexcept {
    WavMetadata candidate;
    const auto status=WavBufferDetail::Inspect(reader,candidate,chunk_limit);
    output=candidate; return status;
}
// Non-realtime loading into unpublished caller-owned storage. All chunks are
// validated before PCM copying; I/O failure during copy may modify destination.
// Metadata is unchanged on failure. Reader, input, output and actual destination
// extents/lifetimes/non-overlap are caller guarantees. No active playback views
// may reference destination storage while loading; no concurrent file mutation.
// No allocations or logging here; a reader/SD implementation may allocate internally.
[[nodiscard]] inline WavBufferStatus LoadWavToBuffer(WavBufferReader& reader,
    std::int16_t* destination, std::uint64_t frame_capacity, Pcm16MonoView& output) noexcept {
    if(!destination) return WavBufferStatus::InvalidDestination;
    if(frame_capacity>std::numeric_limits<std::size_t>::max()/sizeof(std::int16_t)) return WavBufferStatus::InvalidCapacity;
    WavMetadata metadata;
    // Preserve loader's previous full RIFF traversal; inspection uses a tighter budget.
    const auto status=InspectWavMetadata(reader,metadata,UINT32_MAX);
    if(status!=WavBufferStatus::Success) return status;
    const auto frames=metadata.frames, data_bytes=metadata.data_bytes, data_offset=metadata.data_offset;
    using namespace WavBufferDetail;
    if(frames>frame_capacity) return WavBufferStatus::InsufficientCapacity;
    if(!reader.Seek(data_offset)) return WavBufferStatus::IoError;
    std::uint8_t pcm[256]; std::size_t written=0;
    for(std::uint64_t remaining=data_bytes;remaining;) {
        const auto count=static_cast<std::size_t>(remaining<sizeof(pcm) ? remaining : sizeof(pcm));
        if(reader.Read(pcm,count)!=count) return WavBufferStatus::IoError;
        for(std::size_t i=0;i<count;i+=2) {
            const auto raw=U16(pcm+i);
            const auto signed_value=raw>=32768 ? std::int32_t(raw)-65536 : std::int32_t(raw);
            destination[written++]=static_cast<std::int16_t>(signed_value);
        }
        remaining-=count;
    }
    output={destination,static_cast<std::size_t>(frames),44100};
    return WavBufferStatus::Success;
}
}

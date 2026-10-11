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
    UnsupportedFormat, InvalidData, InsufficientCapacity
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
// Non-realtime preparation: O(chunks + PCM bytes), fixed 256-byte scratch.
// Validates ALL RIFF chunks before touching PCM destination. RIFF size defines
// the container; trailing bytes outside it are ignored. Unknown chunks and their
// mandatory odd-byte padding must fit within it. fmt must be 16-byte PCM or have
// a consistent extension length (18+cbSize). Required fmt/data are unique, either order.
// Caller guarantees writable actual storage extents, valid reader/storage/output
// lifetimes and non-overlap with reader, metadata and input bytes. Use unpublished
// storage with NO active playback views: I/O failure during copying can leave a
// partially modified destination. Metadata is unchanged on every failure; success
// publishes a non-owning view. No allocations/logging/audio work; reader must not
// allocate if allocation-free preparation is required. No concurrent file mutation.
[[nodiscard]] inline WavBufferStatus LoadWavToBuffer(WavBufferReader& reader,
    std::int16_t* destination, std::uint64_t frame_capacity, Pcm16MonoView& output) noexcept {
    using namespace WavBufferDetail;
    if(!destination) return WavBufferStatus::InvalidDestination;
    if(frame_capacity>std::numeric_limits<std::size_t>::max()/sizeof(std::int16_t)) return WavBufferStatus::InvalidCapacity;
    std::uint64_t size=0;
    if(!reader.Size(size)) return WavBufferStatus::IoError;
    if(size<12) return WavBufferStatus::Truncated;
    std::uint8_t header[18]{};
    if(!ReadAt(reader,0,header,12)) return WavBufferStatus::IoError;
    if(std::memcmp(header,"RIFF",4) || std::memcmp(header+8,"WAVE",4)) return WavBufferStatus::InvalidContainer;
    const std::uint64_t end=std::uint64_t(U32(header+4))+8; // promotion before addition
    if(end<12) return WavBufferStatus::InvalidContainer;
    if(end>size) return WavBufferStatus::Truncated;
    bool have_format=false, have_data=false;
    std::uint64_t data_offset=0, data_bytes=0;
    for(std::uint64_t offset=12; offset<end;) {
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
            if(U16(header)!=1 || U16(header+2)!=1 || U32(header+4)!=44100 || U16(header+14)!=16)
                return WavBufferStatus::UnsupportedFormat;
            if(U16(header+12)!=2 || U32(header+8)!=88200) return WavBufferStatus::InvalidFormat;
        } else if(!std::memcmp(header,"data",4)) {
            if(have_data) return WavBufferStatus::DuplicateChunk;
            have_data=true; data_offset=payload; data_bytes=bytes;
        }
        offset=payload+bytes+padding; // each addition bounded by container end
    }
    if(!have_format || !have_data) return WavBufferStatus::MissingChunk;
    if(!data_bytes || data_bytes%2) return WavBufferStatus::InvalidData;
    const auto frames=data_bytes/2;
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

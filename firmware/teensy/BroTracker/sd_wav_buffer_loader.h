#pragma once
#include <sd_access.h>
#include "core/audio/wav_buffer_loader.h"

namespace BroTracker {
enum class SdWavBufferStatus { Success, InvalidPath, OpenFailed, Directory, LoadFailed };
struct SdWavBufferResult {
    SdWavBufferStatus status;
    WavBufferStatus load_status; // Meaningful for Success/LoadFailed only.
};
// Non-realtime, single-owner adapter. SdReader holds D0046 shared read ownership
// throughout validation and copying. No direct SD.open, logging or PCM allocation.
// SD initialization is external; the SD library itself may allocate internally.
class SdWavBufferReader final : public WavBufferReader {
public:
    SdWavBufferStatus Open(const char* path) noexcept {
        file_.close();
        if(!path || !*path) return SdWavBufferStatus::InvalidPath;
        if(!file_.open(path)) return SdWavBufferStatus::OpenFailed;
        if(file_.isDirectory()) { file_.close(); return SdWavBufferStatus::Directory; }
        return SdWavBufferStatus::Success;
    }
    void Close() noexcept { file_.close(); }
    bool Size(std::uint64_t& bytes) noexcept override {
        if(!file_) return false;
        bytes=file_.size(); return true;
    }
    bool Seek(std::uint64_t offset) noexcept override {
        return offset<=UINT32_MAX && file_ && file_.seek(static_cast<std::uint32_t>(offset));
    }
    std::size_t Read(std::uint8_t* destination,std::size_t count) noexcept override {
        if(!file_ || (!destination && count) || count>static_cast<std::size_t>(INT32_MAX)) return 0;
        const int result=file_.read(destination,count);
        return result<0 ? 0 : static_cast<std::size_t>(result);
    }
private:
    SdReader file_; // RAII closes file and releases lock on every exit/destruction.
};
// Load into unpublished caller-owned PCM with no active playback views. Caller
// supplies valid extents/lifetimes/non-overlap as in LoadWavToBuffer. Metadata is
// unchanged on ALL failures; copying I/O failures may partially modify PCM.
// Never call from an audio callback. This does not prove the SD stack allocation-free.
[[nodiscard]] inline SdWavBufferResult LoadWavBufferFromSd(const char* path,
    std::int16_t* pcm,std::uint64_t capacity,Pcm16MonoView& output) noexcept {
    SdWavBufferReader reader;
    const auto opened=reader.Open(path);
    if(opened!=SdWavBufferStatus::Success) return {opened,WavBufferStatus::IoError};
    const auto loaded=LoadWavToBuffer(reader,pcm,capacity,output);
    return {loaded==WavBufferStatus::Success ? SdWavBufferStatus::Success : SdWavBufferStatus::LoadFailed,loaded};
}
}

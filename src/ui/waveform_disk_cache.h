#pragma once

#include "sample_waveform.h"

#include <filesystem>

namespace BroTracker {

class WaveformDiskCache {
public:
    WaveformDiskCache(std::filesystem::path root, std::uint64_t budget);
    ~WaveformDiskCache();

    static std::filesystem::path UserCachePath();
    bool SetLineage(std::string_view lineage);
    bool Lookup(std::uint64_t ticket, std::string_view lineage, std::string_view path,
                WaveformSource source, WaveformFingerprint fingerprint);
    bool Save(std::uint64_t ticket, std::uint32_t offset, const std::uint8_t* pcm, unsigned size);
    struct Result {
        std::uint64_t ticket = 0;
        std::unique_ptr<SampleWaveformCache> cache;
    };
    Result Poll();

private:
    struct Worker;
    std::unique_ptr<Worker> worker_;
};

}

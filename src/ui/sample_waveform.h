#pragma once

#include "core/audio/wav_inspection_job.h"

#include <array>

namespace BroTracker {

class SampleWaveformCache {
public:
    static constexpr std::size_t kColumns = 598;
    static constexpr std::size_t kChunkBytes = 4096;

    struct Peak {
        std::int16_t minimum = 32767;
        std::int16_t maximum = -32768;
    };
    enum class State { Loading, Ready, Unavailable };

    SampleWaveformCache();
    void Step();
    // For a caller-supplied WAV, use the same source for every step.
    void Step(WavBufferReader& reader);
    State Status() const { return state_; }
    const WavMetadata& Metadata() const { return inspection_.Metadata(); }
    std::uint64_t FramesLoaded() const { return frames_loaded_; }
    std::size_t Columns() const { return columns_; }
    const Peak& Column(std::size_t index) const { return peaks_[index]; }

private:
    WavInspectionJob inspection_;
    std::array<Peak, kColumns> peaks_{};
    State state_ = State::Loading;
    std::uint64_t frames_loaded_ = 0;
    std::size_t columns_ = 0;
};

} // namespace BroTracker

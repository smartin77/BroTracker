#include "sample_waveform.h"

#include <algorithm>

namespace BroTracker {
namespace {

constexpr std::uint8_t fixture[] = {
#include "sample_waveform_fixture.inc"
};

class FixtureReader final : public WavBufferReader {
public:
    bool Size(std::uint64_t& bytes) noexcept override {
        bytes = sizeof(fixture);
        return true;
    }
    bool Seek(std::uint64_t offset) noexcept override {
        if (offset > sizeof(fixture)) return false;
        offset_ = static_cast<std::size_t>(offset);
        return true;
    }
    std::size_t Read(std::uint8_t* destination, std::size_t count) noexcept override {
        count = std::min(count, sizeof(fixture) - offset_);
        std::memcpy(destination, fixture + offset_, count);
        offset_ += count;
        return count;
    }

private:
    std::size_t offset_ = 0;
};

} // namespace

SampleWaveformCache::SampleWaveformCache() {
    inspection_.Begin(true, true, true, true);
}

void SampleWaveformCache::Step() {
    FixtureReader reader;
    Step(reader);
}

void SampleWaveformCache::Step(WavBufferReader& reader) {
    if (state_ != State::Loading) return;
    const auto& wav = Metadata();
    if (!inspection_.Done()) {
        inspection_.Step(reader);
        if (inspection_.Done()) {
            if (inspection_.Status() != WavBufferStatus::Success) {
                state_ = State::Unavailable;
                return;
            }
            columns_ = static_cast<std::size_t>(std::min<std::uint64_t>(wav.frames, kColumns));
        }
        return;
    }

    std::uint8_t bytes[kChunkBytes];
    const auto count = std::min<std::uint64_t>(wav.frames - frames_loaded_, kChunkBytes / wav.block_align);
    const auto offset = wav.data_offset + frames_loaded_ * wav.block_align;
    if (!WavBufferDetail::ReadAt(reader, offset, bytes, count * wav.block_align)) {
        state_ = State::Unavailable;
        return;
    }

    const auto sample_bytes = wav.bits / 8;
    for (std::size_t frame = 0; frame < count; ++frame, ++frames_loaded_) {
        auto& peak = peaks_[frames_loaded_ * columns_ / wav.frames];
        const auto* sample = bytes + frame * wav.block_align;
        for (unsigned channel = 0; channel < wav.channels; ++channel, sample += sample_bytes) {
            const auto raw = sample_bytes == 2 ? WavBufferDetail::U16(sample) : 0;
            const auto value = static_cast<std::int16_t>(sample_bytes == 1
                ? (int(*sample) - 128) * 256
                : (raw >= 32768 ? int(raw) - 65536 : int(raw)));
            peak.minimum = std::min(peak.minimum, value);
            peak.maximum = std::max(peak.maximum, value);
        }
    }
    if (frames_loaded_ == wav.frames) state_ = State::Ready;
}

} // namespace BroTracker

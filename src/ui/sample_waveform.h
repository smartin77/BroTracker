#pragma once

#include "bringup/sample_waveform_protocol.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace BroTracker {

// Source-frame coverage and native PCM belong to the terminal. FIT peaks are
// derived from received frames only, using a single downmix for stereo.
class SampleWaveformCache {
public:
    static constexpr std::size_t kColumns = 598;
    struct Peak {
        std::int16_t minimum = 32767, maximum = -32768;
        std::uint32_t frames = 0;
    };
    struct Range { std::uint32_t first, end; }; // Half-open, in source frames.
    enum class State { Loading, Ready, Unavailable };

    bool Begin(WaveformSource source);
    bool Append(std::uint32_t offset, const std::uint8_t* bytes, unsigned size);
    void Fail() { state_ = State::Unavailable; }
    State Status() const { return state_; }
    const WaveformSource& Metadata() const { return source_; }
    std::uint32_t BytesLoaded() const { return bytes_loaded_; }
    unsigned Percent() const {
        return source_.Bytes() ? static_cast<unsigned>(std::uint64_t(bytes_loaded_)*100/source_.Bytes()) : 0;
    }
    std::size_t Columns() const { return columns_; }
    const Peak& Column(std::size_t index) const { return peaks_[index]; }
    bool ColumnExact(std::size_t index) const {
        if(index>=columns_)return false;
        const auto first=(std::uint64_t(index)*source_.frames+columns_-1)/columns_;
        const auto end=(std::uint64_t(index+1)*source_.frames+columns_-1)/columns_;
        return peaks_[index].frames==end-first;
    }
    const std::vector<Range>& Coverage() const { return coverage_; }
    // Returns end if fully covered, otherwise the first missing source frame.
    std::uint32_t FirstMissing(std::uint32_t first, std::uint32_t end) const;
    bool ReadFrame(std::uint32_t frame, std::uint8_t* pcm) const;
    bool OverviewReady() const { return window_count_ && !overview_pending_; }
    std::uint32_t NextOverviewFrame() const;
    std::uint64_t OverviewPlayingEstimateMs() const;
    bool StorageLimited() const { return storage_limited_; }

private:
    static constexpr unsigned kPageBytes = 16*1024;
    static constexpr unsigned kMaxPages = 32*1024*1024/kPageBytes;
    static constexpr unsigned kMaxWindows = 64;
    using Page = std::array<std::uint8_t,kPageBytes>;
    void AddCoverage(Range range);

    std::array<Peak,kColumns> peaks_{};
    std::array<Range,kMaxWindows> windows_{};
    std::map<std::uint32_t,std::unique_ptr<Page>> pages_;
    std::vector<Range> coverage_;
    WaveformSource source_{};
    State state_ = State::Loading;
    std::uint32_t bytes_loaded_ = 0;
    std::size_t columns_ = 0;
    unsigned window_count_ = 0;
    bool overview_pending_ = true;
    bool storage_limited_ = false;
};

class SampleWaveformTransfer {
public:
    void Select(unsigned number,std::string_view path,bool supported);
    void Disconnect();
    void Tick(std::uint32_t now,bool playing);
    std::size_t Command(char* line,std::size_t capacity,std::uint32_t now);
    void Response(std::string_view line);
    const SampleWaveformCache& View() const;
    bool Deferred() const;

private:
    enum class Phase { Idle, Get, Metadata, Next, Data, Complete };
    unsigned& Allowance(std::uint32_t token);
    void AdmitPlaying();
    void Fail();
    std::size_t Get(char* line,std::size_t capacity,std::uint32_t now);
    std::array<SampleWaveformCache,4> cache_{};
    SampleWaveformCache* active_ = nullptr;
    std::size_t next_cache_ = 0;
    unsigned number_ = 0;
    std::string path_;
    bool supported_ = false;
    bool playing_ = false, ticked_ = false, first_credit_ = false, playing_admitted_ = false;
    std::uint32_t last_tick_ = 0, cursor_ = 0, requested_offset_ = 0;
    struct Budget { std::uint32_t token=0; unsigned used=0; };
    std::array<Budget,16> budgets_{};
    std::uint32_t retired_through_ = 0;
    unsigned* used_ = nullptr;
    unsigned metadata_playing_ms_ = 0;
    Phase phase_ = Phase::Idle;
    std::uint32_t sequence_ = 0, id_ = 0, cancel_ = 0, sent_at_ = 0;
};

} // namespace BroTracker

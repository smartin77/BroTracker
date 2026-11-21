#pragma once

#include "bringup/sample_waveform_protocol.h"

#include <array>
#include <string>

namespace BroTracker {

// Streaming aggregation of source PCM, in time-position frames. The renderer
// consumes only completed peaks.
class SampleWaveformCache {
public:
    static constexpr std::size_t kColumns=598;
    struct Peak {
        std::int16_t minimum=32767, maximum=-32768;
    };
    enum class State { Loading, Ready, Unavailable };

    bool Begin(WaveformSource source);
    bool Append(std::uint32_t offset,const std::uint8_t* bytes,unsigned size);
    void Fail() { state_=State::Unavailable; }
    State Status() const { return state_; }
    const WaveformSource& Metadata() const { return source_; }
    std::uint32_t BytesLoaded() const { return bytes_loaded_; }
    unsigned Percent() const {
        return source_.Bytes()?static_cast<unsigned>(std::uint64_t(bytes_loaded_)*100/source_.Bytes()):0;
    }
    std::size_t Columns() const { return columns_; }
    const Peak& Column(std::size_t index) const { return peaks_[index]; }

private:
    std::array<Peak,kColumns> peaks_{};
    WaveformSource source_{};
    State state_=State::Loading;
    std::uint32_t bytes_loaded_=0;
    std::size_t columns_=0;
};

// Owns the current pull and four completed resources for this serial session.
// A slot/path selection always revalidates its source token before cache reuse.
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
    bool Pulling() const { return phase_==Phase::Next || phase_==Phase::Data; }
    unsigned& Allowance(std::uint32_t token);
    void AdmitPlaying();
    void Fail();
    std::array<SampleWaveformCache,4> cache_{};
    SampleWaveformCache working_;
    const SampleWaveformCache* ready_=nullptr;
    std::size_t next_cache_=0;
    unsigned number_=0;
    std::string path_;
    bool supported_=false;
    bool playing_=false,ticked_=false,resume_=false,playing_admitted_=false;
    std::uint32_t last_tick_=0;
    struct Budget { std::uint32_t token=0;unsigned used=0; };
    std::array<Budget,16> budgets_{};
    std::uint32_t retired_through_=0;
    unsigned* used_=nullptr;
    unsigned metadata_playing_ms_=0;
    Phase phase_=Phase::Idle;
    std::uint32_t sequence_=0, id_=0, cancel_=0, sent_at_=0;
};

} // namespace BroTracker

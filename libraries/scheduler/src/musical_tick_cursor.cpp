#include "musical_tick_cursor.h"

#include <limits>

namespace BroTracker
{
    TickCursorStatus MusicalTickCursor::Configure(std::uint32_t tempo_hundredths,
                                                 std::uint32_t sample_rate_hz) noexcept
    {
        if (tempo_hundredths == 0 || sample_rate_hz == 0)
            return TickCursorStatus::InvalidConfiguration;
        tempo_hundredths_ = tempo_hundredths;
        sample_rate_hz_ = sample_rate_hz;
        return Reset();
    }

    TickCursorStatus MusicalTickCursor::Reset() noexcept
    {
        if (tempo_hundredths_ == 0)
            return TickCursorStatus::NotConfigured;
        segment_tick_ = segment_sample_ = 0;
        next_tick_ = block_start_ = block_end_ = 0;
        has_block_ = false;
        drained_ = true;
        exhausted_ = false;
        return TickCursorStatus::Success;
    }

    TickCursorStatus MusicalTickCursor::BeginBlock(std::uint64_t block_start,
                                                  std::uint64_t sample_count) noexcept
    {
        if (tempo_hundredths_ == 0) return TickCursorStatus::NotConfigured;
        if (exhausted_) return TickCursorStatus::ArithmeticExhausted;
        if (!drained_) return TickCursorStatus::BlockNotDrained;
        if (block_start != block_end_) return TickCursorStatus::NonconsecutiveBlock;
        if (sample_count > std::numeric_limits<std::uint64_t>::max() - block_start)
            return TickCursorStatus::RangeOverflow;
        block_start_ = block_start;
        block_end_ = block_start + sample_count;
        has_block_ = true;
        drained_ = false;
        return TickCursorStatus::Success;
    }

    TickCursorStatus MusicalTickCursor::ResetAtTick(std::uint64_t tick_index,
                                                   std::uint64_t& sample_position) noexcept
    {
        if (tempo_hundredths_ == 0) return TickCursorStatus::NotConfigured;
        std::uint64_t sample = 0;
        if (SampleAtTick(tick_index, sample) != TickCursorStatus::Success) return TickCursorStatus::RangeOverflow;
        next_tick_ = tick_index;
        block_start_ = block_end_ = sample;
        has_block_ = false; drained_ = true; exhausted_ = false;
        sample_position = sample;
        return TickCursorStatus::Success;
    }

    TickCursorStatus MusicalTickCursor::SampleAtTick(std::uint64_t tick, std::uint64_t& sample) const noexcept {
        if(!tempo_hundredths_) return TickCursorStatus::NotConfigured;
        if(tick<segment_tick_) return TickCursorStatus::RangeOverflow;
        std::uint64_t delta=0;
        if(TickToSamplePosition(tick-segment_tick_,tempo_hundredths_,sample_rate_hz_,delta)!=TickToSampleStatus::Success ||
            delta>UINT64_MAX-segment_sample_) return TickCursorStatus::RangeOverflow;
        sample=segment_sample_+delta; return TickCursorStatus::Success;
    }
    TickCursorStatus MusicalTickCursor::BeginSegment(std::uint32_t tempo,std::uint32_t rate,
        std::uint64_t tick,std::uint64_t sample) noexcept {
        if(!tempo || !rate) return TickCursorStatus::InvalidConfiguration;
        tempo_hundredths_=tempo; sample_rate_hz_=rate;
        segment_tick_=next_tick_=tick; segment_sample_=block_start_=block_end_=sample;
        has_block_=false; drained_=true; exhausted_=false; return TickCursorStatus::Success;
    }
    TickCursorStatus MusicalTickCursor::SetTempoAtTick(std::uint64_t tick,std::uint32_t tempo) noexcept {
        if(!tempo || tick==UINT64_MAX || next_tick_!=tick+1 || tick%kTicksPerRow)
            return TickCursorStatus::InvalidConfiguration;
        std::uint64_t sample=0;
        const auto status=SampleAtTick(tick,sample); if(status!=TickCursorStatus::Success) return status;
        segment_tick_=tick; segment_sample_=sample; tempo_hundredths_=tempo;
        return TickCursorStatus::Success;
    }

    TickCursorStatus MusicalTickCursor::Pull(MusicalTick& tick) noexcept
    {
        if (tempo_hundredths_ == 0) return TickCursorStatus::NotConfigured;
        if (exhausted_) return TickCursorStatus::ArithmeticExhausted;
        if (!has_block_) return TickCursorStatus::NoBlock;
        if (drained_) return TickCursorStatus::BlockComplete;
        if (block_start_ == block_end_)
        {
            drained_ = true;
            return TickCursorStatus::BlockComplete;
        }

        std::uint64_t sample_position = 0;
        if (SampleAtTick(next_tick_, sample_position) != TickCursorStatus::Success)
        {
            exhausted_ = true;
            return TickCursorStatus::ArithmeticExhausted;
        }
        if (sample_position >= block_end_)
        {
            drained_ = true;
            return TickCursorStatus::BlockComplete;
        }

        tick = {next_tick_, sample_position, sample_position - block_start_};
        if (next_tick_ == std::numeric_limits<std::uint64_t>::max())
            exhausted_ = true;
        else
            ++next_tick_;
        return TickCursorStatus::Tick;
    }
}

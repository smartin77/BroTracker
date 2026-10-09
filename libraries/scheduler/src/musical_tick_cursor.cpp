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
        if (TickToSamplePosition(next_tick_, tempo_hundredths_, sample_rate_hz_,
                                sample_position) != TickToSampleStatus::Success)
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

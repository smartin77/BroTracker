#include "pattern_position.h"

namespace BroTracker
{
    PatternPositionStatus TickToPatternPosition(std::uint64_t tick_index,
        std::uint64_t pattern_length_rows, PatternPosition& position) noexcept
    {
        if (pattern_length_rows == 0)
            return PatternPositionStatus::InvalidPatternLength;

        const std::uint64_t absolute_row = tick_index / kTicksPerRow;
        position = {absolute_row, static_cast<std::uint32_t>(tick_index % kTicksPerRow),
                    absolute_row % pattern_length_rows, absolute_row / pattern_length_rows};
        return PatternPositionStatus::Success;
    }
}

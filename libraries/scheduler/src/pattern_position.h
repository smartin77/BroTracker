#pragma once

#include "musical_timing.h"

namespace BroTracker
{
    struct PatternPosition
    {
        std::uint64_t absolute_row = 0;
        std::uint32_t tick_in_row = 0;
        std::uint64_t pattern_row = 0;
        std::uint64_t loop_index = 0;

        // Tick zero and every row boundary are row starts; no separate flag state.
        bool IsRowStart() const noexcept { return tick_in_row == 0; }
    };

    enum class PatternPositionStatus { Success, InvalidPatternLength };

    // Map any uint64_t absolute tick using quotient/remainder and kTicksPerRow.
    // Supports pattern_length_rows in [1, UINT64_MAX], independently of the host
    // Pattern data model. Zero length returns InvalidPatternLength and leaves
    // position unchanged. Row and loop indices are zero-based; UI numbering is
    // separate. No pattern-length multiplication, allocation or timeline state.
    // Logical mapping only: no pattern contents, clock selection or synchronization.
    // Query order has no effect; live pattern-length changes are outside scope.
    [[nodiscard]] PatternPositionStatus TickToPatternPosition(
        std::uint64_t tick_index, std::uint64_t pattern_length_rows,
        PatternPosition& position) noexcept;
}

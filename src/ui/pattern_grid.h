#pragma once
#include <algorithm>

namespace PatternGrid {
constexpr unsigned first_beat_row = 0;
constexpr unsigned beat_rows = 4;
constexpr unsigned visible_rows = 32;
constexpr bool IsBeatRow(unsigned row) noexcept {
    return row >= first_beat_row && (row - first_beat_row) % beat_rows == 0;
}
// Strictly directional, visible destinations only; no destination means no move.
inline unsigned BeatDestination(unsigned row, unsigned active_rows, bool forward) noexcept {
    const auto limit = std::min(active_rows, visible_rows);
    auto destination = row;
    for (unsigned beat = first_beat_row; beat < limit; beat += beat_rows) {
        if (forward && beat > row) return beat;
        if (!forward && beat < row) destination = beat;
    }
    return destination;
}
}

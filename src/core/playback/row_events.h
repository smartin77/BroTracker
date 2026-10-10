#pragma once

#include "core/event.h"
#include "musical_tick_cursor.h"
#include "pattern_position.h"

namespace BroTracker
{
    // Initial realtime storage capacities, not permanent pattern-format limits.
    constexpr std::uint8_t kRealtimePatternRowCapacity = 16;
    constexpr std::uint8_t kRealtimePatternChannelCapacity = 8;
    constexpr std::uint8_t kNoInstrumentUpdate = 0xFF;

    // Core metadata, never encoded as an instrument ID or UI glyph.
    enum class NoteOffTiming : std::uint8_t { Arrival, EndOfPosition };
    inline bool ValidNoteOffTiming(NoteOffTiming timing) noexcept {
        return timing == NoteOffTiming::Arrival || timing == NoteOffTiming::EndOfPosition;
    }

    struct RealtimePattern
    {
        std::uint8_t active_rows = kRealtimePatternRowCapacity;
        std::uint8_t active_channels = kRealtimePatternChannelCapacity;
        // Row-major cells reuse Event's NOTE_EMPTY / 0xFF defaults.
        Event cells[kRealtimePatternRowCapacity][kRealtimePatternChannelCapacity]{};
        // Meaningful only for NOTE_OFF. Omitted metadata defaults to Arrival.
        NoteOffTiming note_off_timing[kRealtimePatternRowCapacity][kRealtimePatternChannelCapacity]{};
    };

    struct RowEvent
    {
        std::uint8_t channel = 0;
        std::uint8_t pattern_row = 0;
        std::uint64_t loop_index = 0;
        std::uint64_t tick_index = 0;
        std::uint64_t sample_position = 0;
        std::uint64_t sample_offset = 0;
        Note note = NOTE_EMPTY;
        std::uint8_t instrument = kNoInstrumentUpdate;
        NoteOffTiming note_off_timing = NoteOffTiming::Arrival;
    };

    struct RowEventBatch
    {
        std::uint8_t count = 0;
        RowEvent events[kRealtimePatternChannelCapacity]{};
    };

    enum class RowEventStatus { Success, InvalidDimensions, InvalidNote, InvalidNoteOffTiming };

    // Stateless playback/core interpretation, separate from timing and Clock/Sync.
    // Pattern must remain immutable during consumption; caller supplies each
    // cursor-emitted tick exactly once. No independent progression or deduplication.
    // Validate dimensions on every call; validate notes only in active channels
    // and timing enums of the consumed row at row-start ticks. Other/inactive
    // cells are not read; valid timing metadata on non-OFF cells is ignored.
    // Valid notes: 24..127, NOTE_EMPTY, NOTE_OFF. Raw instrument IDs are preserved;
    // 0xFF means no update. No instrument memory, default instrument or sample
    // trigger resolution is performed; instrument-only commands are not new notes.
    // Failure leaves the entire batch unchanged. Success replaces it, including
    // an empty batch for non-row-start ticks or completely empty active cells.
    // At most eight cells and eight events; no truncation, allocation, I/O or callbacks.
    [[nodiscard]] inline RowEventStatus GenerateRowEvents(const MusicalTick& tick,
        const RealtimePattern& pattern, RowEventBatch& batch) noexcept
    {
        if (pattern.active_rows == 0 || pattern.active_rows > kRealtimePatternRowCapacity ||
            pattern.active_channels == 0 || pattern.active_channels > kRealtimePatternChannelCapacity)
            return RowEventStatus::InvalidDimensions;

        PatternPosition position;
        // Active row count was validated above, so mapping cannot fail.
        const auto status = TickToPatternPosition(tick.tick_index, pattern.active_rows, position);
        if (status != PatternPositionStatus::Success) return RowEventStatus::InvalidDimensions;
        RowEventBatch result;
        if (position.IsRowStart())
        {
            for (std::uint8_t channel = 0; channel < pattern.active_channels; ++channel)
            {
                const Event& cell = pattern.cells[position.pattern_row][channel];
                if (!IsPatternNote(cell.note))
                    return RowEventStatus::InvalidNote;
                if (!ValidNoteOffTiming(pattern.note_off_timing[position.pattern_row][channel]))
                    return RowEventStatus::InvalidNoteOffTiming;
                if (cell.note == NOTE_EMPTY && cell.instrument == kNoInstrumentUpdate) continue;
                result.events[result.count++] = {channel,
                    static_cast<std::uint8_t>(position.pattern_row), position.loop_index,
                    tick.tick_index, tick.sample_position, tick.sample_offset, cell.note, cell.instrument, pattern.note_off_timing[position.pattern_row][channel]};
            }
        }
        batch = result;
        return RowEventStatus::Success;
    }
}

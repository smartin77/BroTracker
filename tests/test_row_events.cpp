#include "test_framework.h"
#include "core/playback/row_events.h"

#include <vector>

using namespace BroTracker;

namespace
{
    void CheckEvent(const RowEvent& actual, const RowEvent& expected)
    {
        CHECK_EQ(actual.channel, expected.channel);
        CHECK_EQ(actual.pattern_row, expected.pattern_row);
        CHECK_EQ(actual.loop_index, expected.loop_index);
        CHECK_EQ(actual.tick_index, expected.tick_index);
        CHECK_EQ(actual.sample_position, expected.sample_position);
        CHECK_EQ(actual.sample_offset, expected.sample_offset);
        CHECK_EQ(actual.note, expected.note);
        CHECK_EQ(actual.instrument, expected.instrument);
    }
}

TEST_CASE(RowEvents_EmptyAndInactiveCells)
{
    RealtimePattern pattern;
    for (const auto& row : pattern.cells)
        for (const auto& cell : row)
        {
            CHECK_EQ(cell.note, NOTE_EMPTY);
            CHECK_EQ(cell.instrument, 0xFF);
        }
    RowEventBatch batch;
    batch.count = 1;
    CHECK_EQ(GenerateRowEvents({0, 0, 0}, pattern, batch), RowEventStatus::Success);
    CHECK_EQ(batch.count, 0);
    pattern.active_rows = 1;
    pattern.active_channels = 1;
    pattern.cells[0][1] = {128, 7}; // Invalid inactive cells are not consumed.
    pattern.cells[1][0] = {128, 7};
    CHECK_EQ(GenerateRowEvents({96, 100, 5}, pattern, batch), RowEventStatus::Success);
    CHECK_EQ(batch.count, 0);
}

TEST_CASE(RowEvents_RawCommandsChannelOrderAndTiming)
{
    RealtimePattern pattern;
    pattern.cells[0][0] = {0, 0xFF};
    pattern.cells[0][1] = {NOTE_OFF, 0xFF};
    pattern.cells[0][2] = {NOTE_EMPTY, 254};
    pattern.cells[0][3] = {127, 0};
    for (std::uint8_t channel = 4; channel < 8; ++channel)
        pattern.cells[0][channel] = {60, channel};
    RowEventBatch batch;
    CHECK_EQ(GenerateRowEvents({1536, 83000, 17}, pattern, batch), RowEventStatus::Success);
    CHECK_EQ(batch.count, 8);
    for (std::uint8_t channel = 0; channel < batch.count; ++channel)
        CheckEvent(batch.events[channel], {channel, 0, 1, 1536, 83000, 17,
            pattern.cells[0][channel].note, pattern.cells[0][channel].instrument});
    CHECK_EQ(GenerateRowEvents({1537, 83054, 71}, pattern, batch), RowEventStatus::Success);
    CHECK_EQ(batch.count, 0);
    pattern.cells[15][7] = {NOTE_OFF, 7};
    CHECK_EQ(GenerateRowEvents({1440, 78000, 4}, pattern, batch), RowEventStatus::Success);
    CHECK_EQ(batch.count, 1);
    CheckEvent(batch.events[0], {7, 15, 0, 1440, 78000, 4, NOTE_OFF, 7});
    CHECK_EQ(GenerateRowEvents({1535, 82945, 9}, pattern, batch), RowEventStatus::Success);
    CHECK_EQ(batch.count, 0);
}

TEST_CASE(RowEvents_ValidationLeavesWholeBatchUnchanged)
{
    RealtimePattern pattern;
    RowEventBatch batch;
    batch.count = 3;
    for (auto& event : batch.events) event = {7, 9, 11, 13, 15, 17, NOTE_OFF, 19};
    const auto original = batch;
    const auto check_unchanged = [&]() {
        CHECK_EQ(batch.count, original.count);
        for (std::uint8_t i = 0; i < 8; ++i) CheckEvent(batch.events[i], original.events[i]);
    };
    for (const auto invalid : {0, 17, 255})
    {
        pattern.active_rows = static_cast<std::uint8_t>(invalid);
        CHECK_EQ(GenerateRowEvents({1, 0, 0}, pattern, batch), RowEventStatus::InvalidDimensions);
        check_unchanged();
    }
    pattern.active_rows = 16;
    for (const auto invalid : {0, 9, 255})
    {
        pattern.active_channels = static_cast<std::uint8_t>(invalid);
        CHECK_EQ(GenerateRowEvents({0, 0, 0}, pattern, batch), RowEventStatus::InvalidDimensions);
        check_unchanged();
    }
    pattern.active_channels = 8;
    pattern.cells[0][0] = {60, 1}; // A later invalid cell must not publish partial output.
    for (unsigned int note = 128; note < NOTE_OFF; ++note)
    {
        pattern.cells[0][7].note = static_cast<Note>(note);
        CHECK_EQ(GenerateRowEvents({0, 0, 0}, pattern, batch), RowEventStatus::InvalidNote);
        check_unchanged();
    }
    CHECK_EQ(GenerateRowEvents({1, 1, 1}, pattern, batch), RowEventStatus::Success);
    CHECK_EQ(batch.count, 0); // Non-row-start calls do not validate cells.
}

TEST_CASE(RowEvents_CursorCompositionPartitionIndependence)
{
    RealtimePattern pattern;
    pattern.active_rows = 7;
    pattern.active_channels = 3;
    for (std::uint8_t row = 0; row < pattern.active_rows; ++row)
    {
        pattern.cells[row][0] = {static_cast<Note>(60 + row), 0xFF};
        pattern.cells[row][2] = {NOTE_EMPTY, row};
    }
    const auto collect = [&](bool irregular) {
        MusicalTickCursor cursor;
        CHECK_EQ(cursor.Configure(12753, 44100), TickCursorStatus::Success);
        std::vector<RowEvent> events;
        constexpr std::uint64_t end = 128 * 4096;
        std::uint64_t start = 0, part = 0;
        while (start < end)
        {
            const auto requested = irregular ? (part++ * 73) % 997 + 1 : 128;
            const auto count = requested < end - start ? requested : end - start;
            CHECK_EQ(cursor.BeginBlock(start, count), TickCursorStatus::Success);
            MusicalTick tick;
            TickCursorStatus status;
            while ((status = cursor.Pull(tick)) == TickCursorStatus::Tick)
            {
                RowEventBatch batch;
                CHECK_EQ(GenerateRowEvents(tick, pattern, batch), RowEventStatus::Success);
                CHECK_EQ(batch.count, tick.tick_index % kTicksPerRow == 0 ? 2 : 0);
                for (std::uint8_t i = 0; i < batch.count; ++i)
                {
                    CHECK_EQ(batch.events[i].sample_position, tick.sample_position);
                    CHECK_EQ(batch.events[i].sample_offset, tick.sample_position - start);
                    CHECK(batch.events[i].sample_offset < count);
                    events.push_back(batch.events[i]);
                }
            }
            CHECK_EQ(status, TickCursorStatus::BlockComplete);
            start += count;
        }
        return events;
    };
    const auto regular = collect(false), irregular = collect(true);
    CHECK_EQ(regular.size(), irregular.size());
    for (std::size_t i = 0; i < regular.size() && i < irregular.size(); ++i)
    {
        auto absolute = irregular[i];
        absolute.sample_offset = regular[i].sample_offset; // Offsets depend on partition.
        CheckEvent(regular[i], absolute);
        const auto row = i / 2;
        CHECK_EQ(regular[i].tick_index, row * kTicksPerRow);
        CHECK_EQ(regular[i].pattern_row, row % 7);
        CHECK_EQ(regular[i].loop_index, row / 7);
        CHECK_EQ(regular[i].channel, i % 2 == 0 ? 0 : 2);
    }
    CHECK(!regular.empty());
    std::uint64_t next_row_sample = 0;
    CHECK_EQ(TickToSamplePosition((regular.size() / 2) * kTicksPerRow, 12753, 44100,
        next_row_sample), TickToSampleStatus::Success);
    CHECK(next_row_sample >= 128 * 4096); // No missing final row.
}

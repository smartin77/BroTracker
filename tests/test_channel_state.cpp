#include "test_framework.h"
#include "core/playback/channel_state.h"

#include <vector>

using namespace BroTracker;

namespace
{
    void CheckState(const LogicalChannelState& actual, const LogicalChannelState& expected)
    {
        CHECK_EQ(actual.note, expected.note);
        CHECK_EQ(actual.instrument, expected.instrument);
    }

    void CheckApplication(const RowEventApplication& actual, const RowEventApplication& expected)
    {
        CHECK_EQ(actual.event.channel, expected.event.channel);
        CHECK_EQ(actual.event.pattern_row, expected.event.pattern_row);
        CHECK_EQ(actual.event.loop_index, expected.event.loop_index);
        CHECK_EQ(actual.event.tick_index, expected.event.tick_index);
        CHECK_EQ(actual.event.sample_position, expected.event.sample_position);
        CHECK_EQ(actual.event.sample_offset, expected.event.sample_offset);
        CHECK_EQ(actual.event.note, expected.event.note);
        CHECK_EQ(actual.event.instrument, expected.event.instrument);
        CheckState(actual.before, expected.before);
        CheckState(actual.after, expected.after);
        CHECK_EQ(actual.note_updated, expected.note_updated);
        CHECK_EQ(actual.instrument_updated, expected.instrument_updated);
    }

    RowEventApplication ApplyFields(ChannelPlaybackState& state, Note note, std::uint8_t instrument,
                                    std::uint8_t channel = 0)
    {
        const RowEvent event{channel, 3, 4, 1234, 5678, 90, note, instrument};
        RowEventApplication result;
        CHECK_EQ(state.Apply(event, result), ChannelStateStatus::Success);
        RowEventApplication expected{event, result.before, result.after,
            note != NOTE_EMPTY, instrument != kNoInstrumentUpdate};
        CheckApplication(result, expected);
        return result;
    }
}

TEST_CASE(ChannelState_InitialResetAndChannelIndependence)
{
    ChannelPlaybackState state;
    for (std::uint32_t i = 0; i < kRealtimePatternChannelCapacity; ++i)
    {
        LogicalChannelState snapshot{1, 2};
        CHECK_EQ(state.GetChannel(i, snapshot), ChannelStateStatus::Success);
        CheckState(snapshot, {});
        ApplyFields(state, static_cast<Note>(60 + i), static_cast<std::uint8_t>(i),
                    static_cast<std::uint8_t>(i));
    }
    for (std::uint32_t i = 0; i < kRealtimePatternChannelCapacity; ++i)
    {
        LogicalChannelState snapshot;
        CHECK_EQ(state.GetChannel(i, snapshot), ChannelStateStatus::Success);
        CheckState(snapshot, {static_cast<Note>(60 + i), static_cast<std::uint8_t>(i)});
    }
    state.Reset();
    state.Reset();
    for (std::uint32_t i = 0; i < kRealtimePatternChannelCapacity; ++i)
    {
        LogicalChannelState snapshot;
        CHECK_EQ(state.GetChannel(i, snapshot), ChannelStateStatus::Success);
        CheckState(snapshot, {});
    }
}

TEST_CASE(ChannelState_FieldContinuationAndExplicitRepeatedUpdates)
{
    ChannelPlaybackState state;
    auto result = ApplyFields(state, 60, 7);
    CheckState(result.before, {});
    CheckState(result.after, {60, 7});
    result = ApplyFields(state, 62, kNoInstrumentUpdate);
    CheckState(result.before, {60, 7});
    CheckState(result.after, {62, 7});
    result = ApplyFields(state, NOTE_EMPTY, 9);
    CheckState(result.after, {62, 9});
    CHECK(!result.note_updated && result.instrument_updated);
    result = ApplyFields(state, NOTE_EMPTY, kNoInstrumentUpdate);
    CheckState(result.before, {62, 9});
    CheckState(result.after, {62, 9});
    CHECK(!result.note_updated && !result.instrument_updated);
    result = ApplyFields(state, 62, 9);
    CheckState(result.before, result.after);
    CHECK(result.note_updated && result.instrument_updated);
    result = ApplyFields(state, NOTE_OFF, kNoInstrumentUpdate);
    CheckState(result.after, {NOTE_OFF, 9});
    result = ApplyFields(state, NOTE_EMPTY, 254);
    CheckState(result.after, {NOTE_OFF, 254});
    result = ApplyFields(state, NOTE_OFF, 0);
    CheckState(result.after, {NOTE_OFF, 0});
    CHECK(result.note_updated && result.instrument_updated);
    result = ApplyFields(state, NOTE_OFF, 0);
    CheckState(result.before, result.after);
    CHECK(result.note_updated && result.instrument_updated);
    state.Reset();
    result = ApplyFields(state, NOTE_EMPTY, 3);
    CheckState(result.after, {NOTE_EMPTY, 3});
    CHECK(!result.note_updated);
    state.Reset();
    result = ApplyFields(state, 127, kNoInstrumentUpdate);
    CheckState(result.after, {127, kNoInstrumentUpdate});
    result = ApplyFields(state, 0, kNoInstrumentUpdate);
    CheckState(result.after, {0, kNoInstrumentUpdate});
}

TEST_CASE(ChannelState_InvalidInputPreservesAllStateAndOutput)
{
    ChannelPlaybackState state;
    for (std::uint8_t i = 0; i < kRealtimePatternChannelCapacity; ++i) ApplyFields(state, i, i, i);
    auto result = ApplyFields(state, 0, 0);
    const auto original = result;
    const auto unchanged = [&]() {
        CheckApplication(result, original);
        for (std::uint32_t i = 0; i < kRealtimePatternChannelCapacity; ++i)
        {
            LogicalChannelState snapshot;
            CHECK_EQ(state.GetChannel(i, snapshot), ChannelStateStatus::Success);
            CheckState(snapshot, {static_cast<Note>(i), static_cast<std::uint8_t>(i)});
        }
    };
    for (const auto channel : {8, 255})
    {
        RowEvent event;
        event.channel = static_cast<std::uint8_t>(channel);
        event.note = 60;
        event.instrument = 99;
        CHECK_EQ(state.Apply(event, result), ChannelStateStatus::InvalidChannel);
        unchanged();
    }
    for (unsigned int note = 128; note < NOTE_OFF; ++note)
    {
        RowEvent event;
        event.note = static_cast<Note>(note);
        event.instrument = 99;
        CHECK_EQ(state.Apply(event, result), ChannelStateStatus::InvalidNote);
        unchanged();
    }
    LogicalChannelState snapshot{60, 7};
    CHECK_EQ(state.GetChannel(8, snapshot), ChannelStateStatus::InvalidChannel);
    CheckState(snapshot, {60, 7});
    CHECK_EQ(state.GetChannel(0xFFFFFFFFu, snapshot), ChannelStateStatus::InvalidChannel);
    CheckState(snapshot, {60, 7});
}

TEST_CASE(ChannelState_CompositionAcrossLoopsAndPartitions)
{
    RealtimePattern pattern;
    pattern.active_rows = 4;
    pattern.active_channels = 2;
    pattern.cells[0][0] = {NOTE_EMPTY, 7};
    pattern.cells[1][0] = {60, kNoInstrumentUpdate};
    pattern.cells[2][0] = {NOTE_OFF, kNoInstrumentUpdate};
    pattern.cells[3][0] = {NOTE_EMPTY, 9};
    pattern.cells[0][1] = {62, kNoInstrumentUpdate};
    const auto collect = [&](bool irregular) {
        MusicalTickCursor cursor;
        ChannelPlaybackState state;
        CHECK_EQ(cursor.Configure(12753, 44100), TickCursorStatus::Success);
        std::vector<RowEventApplication> applications;
        constexpr std::uint64_t end = 128 * 1024;
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
                for (std::uint8_t i = 0; i < batch.count; ++i)
                {
                    RowEventApplication application;
                    CHECK_EQ(state.Apply(batch.events[i], application), ChannelStateStatus::Success);
                    CHECK_EQ(application.event.sample_position, tick.sample_position);
                    CHECK_EQ(application.event.sample_offset, tick.sample_position - start);
                    CHECK(application.event.sample_offset < count);
                    if (application.event.channel == 0 && application.event.pattern_row == 0 &&
                        application.event.loop_index != 0)
                    {
                        CheckState(application.before, {NOTE_OFF, 9});
                        CheckState(application.after, {NOTE_OFF, 7});
                        CHECK(!application.note_updated && application.instrument_updated);
                    }
                    applications.push_back(application);
                }
            }
            CHECK_EQ(status, TickCursorStatus::BlockComplete);
            start += count;
        }
        return applications;
    };
    const auto regular = collect(false), irregular = collect(true);
    CHECK_EQ(regular.size(), irregular.size());
    CHECK(regular.size() > 10);
    CHECK(regular.back().event.loop_index > 0);
    for (std::size_t i = 0; i < regular.size() && i < irregular.size(); ++i)
    {
        auto absolute = irregular[i];
        absolute.event.sample_offset = regular[i].event.sample_offset;
        CheckApplication(regular[i], absolute);
    }
}

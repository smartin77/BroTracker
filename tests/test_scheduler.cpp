/*
 * BroTracker
 *
 * Description: Scheduler foundation tests.
 *
 * Tests for the logical playback timeline abstraction.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include "test_framework.h"

#include "scheduler.h"
#include "musical_timing.h"
#include "musical_tick_cursor.h"
#include "pattern_position.h"

#include <limits>
#include <vector>

TEST_CASE(Scheduler_InitializesWithPositionZero)
{
    BroTracker::Scheduler scheduler;
    CHECK(scheduler.Initialize());
    CHECK_EQ(scheduler.GetPosition(), 0);
}

TEST_CASE(Scheduler_PositionAdvancesBySampleCount)
{
    BroTracker::Scheduler scheduler;
    scheduler.Initialize();

    scheduler.AdvanceSamples(1000);
    CHECK_EQ(scheduler.GetPosition(), 1000);

    scheduler.AdvanceSamples(2000);
    CHECK_EQ(scheduler.GetPosition(), 3000);
}

TEST_CASE(Scheduler_ResetReturnsPositionToInitialState)
{
    BroTracker::Scheduler scheduler;
    scheduler.Initialize();

    scheduler.AdvanceSamples(5000);
    CHECK_EQ(scheduler.GetPosition(), 5000);

    scheduler.Reset();
    CHECK_EQ(scheduler.GetPosition(), 0);
}

TEST_CASE(Scheduler_MultipleSmallAdvancesEqualOnelargeAdvance)
{
    BroTracker::Scheduler scheduler1;
    BroTracker::Scheduler scheduler2;

    scheduler1.Initialize();
    scheduler2.Initialize();

    // Advance scheduler1 in small steps
    for (int i = 0; i < 100; ++i)
    {
        scheduler1.AdvanceSamples(100);
    }

    // Advance scheduler2 in one large step
    scheduler2.AdvanceSamples(10000);

    CHECK_EQ(scheduler1.GetPosition(), scheduler2.GetPosition());
}

TEST_CASE(Scheduler_PositionDeterministicAcrossMultipleResets)
{
    BroTracker::Scheduler scheduler;
    scheduler.Initialize();

    for (int iteration = 0; iteration < 10; ++iteration)
    {
        CHECK_EQ(scheduler.GetPosition(), 0);
        scheduler.AdvanceSamples(4410);  // Common chunk size
        CHECK_EQ(scheduler.GetPosition(), 4410);
        scheduler.Reset();
    }
}

TEST_CASE(Scheduler_PositionLargeSampleCount)
{
    BroTracker::Scheduler scheduler;
    scheduler.Initialize();

    // At 44100 Hz, this represents ~1 second of audio
    scheduler.AdvanceSamples(44100);
    CHECK_EQ(scheduler.GetPosition(), 44100);

    // Advance to represent ~1 minute of audio
    scheduler.AdvanceSamples(44100 * 59);
    CHECK_EQ(scheduler.GetPosition(), 44100 * 60);
}

namespace
{
    void CheckTickPosition(std::uint64_t tick_index, std::uint32_t tempo_hundredths,
                           std::uint32_t sample_rate_hz, std::uint64_t expected)
    {
        std::uint64_t position = std::numeric_limits<std::uint64_t>::max();
        CHECK_EQ(BroTracker::TickToSamplePosition(tick_index, tempo_hundredths,
                 sample_rate_hz, position), BroTracker::TickToSampleStatus::Success);
        CHECK_EQ(position, expected);
    }
}

TEST_CASE(MusicalTiming_KnownTicksRowsAndQuarterNotes)
{
    CHECK_EQ(BroTracker::kRowsPerQuarterNote, 4);
    CHECK_EQ(BroTracker::kTicksPerRow, 96);
    CHECK_EQ(BroTracker::kTicksPerQuarterNote, 384);
    CheckTickPosition(0, 12000, 44100, 0);
    CheckTickPosition(1, 12000, 44100, 57);
    CheckTickPosition(2, 12000, 44100, 114);
    CheckTickPosition(3, 12000, 44100, 172);
    CheckTickPosition(BroTracker::kTicksPerRow, 12000, 44100, 5512);
    CheckTickPosition(2 * BroTracker::kTicksPerRow, 12000, 44100, 11025);
    CheckTickPosition(BroTracker::kTicksPerQuarterNote, 12000, 44100, 22050);
}

TEST_CASE(MusicalTiming_FractionalTempoAndHundredths)
{
    CheckTickPosition(1, 12750, 44100, 54);
    // Fractional remainder eventually produces a 55-sample interval, not 54.
    CheckTickPosition(22, 12750, 44100, 1188);
    CheckTickPosition(23, 12750, 44100, 1243);
    CheckTickPosition(384, 12750, 44100, 20752);
    CheckTickPosition(384, 12753, 44100, 20748);
}

TEST_CASE(MusicalTiming_LongRunAbsoluteRounding)
{
    // Independent exact reference products fit uint64_t at these positions.
    // A billion ticks is roughly 14 days at 127.50 BPM.
    for (const std::uint32_t tempo_hundredths : {12000u, 12750u, 12753u})
        for (const std::uint64_t tick_index : {1000000ULL, 1000000000ULL})
            CheckTickPosition(tick_index, tempo_hundredths, 44100,
                tick_index * 44100ULL * 6000 / (std::uint64_t{tempo_hundredths} * 384));
}

TEST_CASE(MusicalTiming_QueryOrderDoesNotChangePositions)
{
    // Query the same targets directly and after dense/sparse intermediate queries.
    for (const std::uint64_t target : {384ULL, 12345ULL, 1000000ULL})
    {
        std::uint64_t direct = 0, intermediate = 0;
        CHECK_EQ(BroTracker::TickToSamplePosition(target, 12753, 44100, direct),
                 BroTracker::TickToSampleStatus::Success);
        for (const std::uint64_t stride : {1ULL, 97ULL, 4096ULL})
        {
            for (std::uint64_t tick_index = 0; tick_index < target; tick_index += stride)
                CHECK_EQ(BroTracker::TickToSamplePosition(tick_index, 12753, 44100,
                         intermediate), BroTracker::TickToSampleStatus::Success);
            CheckTickPosition(target, 12753, 44100, direct);
        }
        CheckTickPosition(0, 12753, 44100, 0); // Backward queries also have no state.
        CheckTickPosition(target, 12753, 44100, direct);
    }
}

TEST_CASE(MusicalTiming_ExplicitSampleRate)
{
    CheckTickPosition(1, 12000, 48000, 62);
    CheckTickPosition(2, 12000, 48000, 125);
    CheckTickPosition(96, 12000, 48000, 6000);
    CheckTickPosition(384, 12000, 48000, 24000);
    CheckTickPosition(384, 12753, 48000, 288000000ULL / 12753);
}

TEST_CASE(MusicalTiming_InvalidConfigurationLeavesOutputUnchanged)
{
    for (const std::uint64_t tick_index : {0ULL, 384ULL})
    {
        std::uint64_t position = 123;
        CHECK_EQ(BroTracker::TickToSamplePosition(tick_index, 0, 44100, position),
                 BroTracker::TickToSampleStatus::InvalidConfiguration);
        CHECK_EQ(position, 123);
        CHECK_EQ(BroTracker::TickToSamplePosition(tick_index, 12000, 0, position),
                 BroTracker::TickToSampleStatus::InvalidConfiguration);
        CHECK_EQ(position, 123);
        CHECK_EQ(BroTracker::TickToSamplePosition(tick_index, 0, 0, position),
                 BroTracker::TickToSampleStatus::InvalidConfiguration);
        CHECK_EQ(position, 123);
    }
}

TEST_CASE(MusicalTiming_LargeIntermediateProductsAndOverflow)
{
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    constexpr auto maximum_config = std::numeric_limits<std::uint32_t>::max();
    // Exactly one sample per tick; the unreduced numerator product overflows.
    CheckTickPosition(maximum, 125, 8, maximum);
    // Equal sample rate and tempo cancel: ratio is 6000/384 = 125/8.
    CheckTickPosition(1000000000000000000ULL, maximum_config, maximum_config,
                      15625000000000000000ULL);
    // A very small ratio still accepts the full 64-bit tick range.
    CheckTickPosition(maximum, maximum_config, 1,
                      67108864015ULL);
    // Result boundary at ratio 125/8, including a nonzero fractional remainder.
    constexpr std::uint64_t last_tick = 1180591620717411303ULL;
    CheckTickPosition(last_tick, maximum_config, maximum_config,
                      18446744073709551609ULL);
    std::uint64_t position = 123;
    CHECK_EQ(BroTracker::TickToSamplePosition(last_tick + 1, maximum_config,
             maximum_config, position), BroTracker::TickToSampleStatus::Overflow);
    CHECK_EQ(position, 123);
    CHECK_EQ(BroTracker::TickToSamplePosition(maximum, 1, maximum_config, position),
             BroTracker::TickToSampleStatus::Overflow);
    CHECK_EQ(position, 123);
}

namespace BroTracker
{
    struct MusicalTickCursorTestAccess
    {
        static void Seed(MusicalTickCursor& cursor, std::uint64_t next_tick,
                         std::uint64_t completed_end)
        {
            cursor.next_tick_ = next_tick;
            cursor.block_end_ = completed_end;
        }
    };
}

namespace
{
    using BroTracker::MusicalTick;
    using BroTracker::MusicalTickCursor;
    using BroTracker::TickCursorStatus;

    void ExpectTick(MusicalTickCursor& cursor, std::uint64_t index,
                    std::uint64_t position, std::uint64_t offset)
    {
        MusicalTick tick;
        CHECK_EQ(cursor.Pull(tick), TickCursorStatus::Tick);
        CHECK_EQ(tick.tick_index, index);
        CHECK_EQ(tick.sample_position, position);
        CHECK_EQ(tick.sample_offset, offset);
    }

    std::vector<MusicalTick> CollectTicks(std::uint32_t tempo_hundredths,
        std::uint32_t sample_rate_hz, const std::vector<std::uint64_t>& partitions)
    {
        MusicalTickCursor cursor;
        CHECK_EQ(cursor.Configure(tempo_hundredths, sample_rate_hz), TickCursorStatus::Success);
        std::vector<MusicalTick> ticks;
        std::uint64_t start = 0;
        for (const auto count : partitions)
        {
            CHECK_EQ(cursor.BeginBlock(start, count), TickCursorStatus::Success);
            MusicalTick tick;
            TickCursorStatus status;
            while ((status = cursor.Pull(tick)) == TickCursorStatus::Tick)
            {
                CHECK(tick.sample_position >= start && tick.sample_position < start + count);
                CHECK_EQ(tick.sample_offset, tick.sample_position - start);
                std::uint64_t absolute = 0;
                CHECK_EQ(BroTracker::TickToSamplePosition(tick.tick_index, tempo_hundredths,
                    sample_rate_hz, absolute), BroTracker::TickToSampleStatus::Success);
                CHECK_EQ(tick.sample_position, absolute);
                CHECK_EQ(tick.tick_index, ticks.size());
                ticks.push_back(tick);
            }
            CHECK_EQ(status, TickCursorStatus::BlockComplete);
            CHECK_EQ(cursor.Pull(tick), TickCursorStatus::BlockComplete);
            start += count;
        }
        // The first omitted tick is outside the entire interval.
        std::uint64_t pending = 0;
        CHECK_EQ(BroTracker::TickToSamplePosition(ticks.size(), tempo_hundredths,
            sample_rate_hz, pending), BroTracker::TickToSampleStatus::Success);
        CHECK(pending >= start);
        return ticks;
    }
}

TEST_CASE(TickCursor_HalfOpenBoundariesAndOffsets)
{
    MusicalTickCursor cursor;
    CHECK_EQ(cursor.Configure(12000, 44100), TickCursorStatus::Success);
    CHECK_EQ(cursor.BeginBlock(0, 114), TickCursorStatus::Success);
    ExpectTick(cursor, 0, 0, 0);
    ExpectTick(cursor, 1, 57, 57);
    MusicalTick tick{999, 999, 999};
    CHECK_EQ(cursor.Pull(tick), TickCursorStatus::BlockComplete);
    CHECK_EQ(tick.tick_index, 999);
    CHECK_EQ(tick.sample_position, 999);
    CHECK_EQ(tick.sample_offset, 999);
    CHECK_EQ(cursor.BeginBlock(114, 58), TickCursorStatus::Success);
    ExpectTick(cursor, 2, 114, 0);
    CHECK_EQ(cursor.Pull(tick), TickCursorStatus::BlockComplete);
    CHECK_EQ(cursor.BeginBlock(172, 1), TickCursorStatus::Success);
    ExpectTick(cursor, 3, 172, 0);
    CHECK_EQ(cursor.Pull(tick), TickCursorStatus::BlockComplete);
}

TEST_CASE(TickCursor_PartitionIndependenceAndLongRuns)
{
    constexpr std::uint64_t total = 128 * 4096;
    std::vector<std::uint64_t> regular(4096, 128);
    std::vector<std::uint64_t> irregular;
    std::uint64_t remaining = total;
    for (std::uint64_t count = 1; remaining != 0; count = count % 997 + 1)
    {
        const auto size = count < remaining ? count : remaining;
        irregular.push_back(size);
        remaining -= size;
    }
    for (const auto tempo_hundredths : {12000u, 12750u, 12753u})
        for (const auto sample_rate_hz : {44100u, 48000u})
        {
            const auto direct = CollectTicks(tempo_hundredths, sample_rate_hz, {total});
            const auto blocks = CollectTicks(tempo_hundredths, sample_rate_hz, regular);
            const auto varied = CollectTicks(tempo_hundredths, sample_rate_hz, irregular);
            CHECK_EQ(direct.size(), blocks.size());
            CHECK_EQ(direct.size(), varied.size());
            for (std::size_t index = 0; index < direct.size() &&
                 index < blocks.size() && index < varied.size(); ++index)
            {
                CHECK_EQ(direct[index].tick_index, blocks[index].tick_index);
                CHECK_EQ(direct[index].tick_index, varied[index].tick_index);
                CHECK_EQ(direct[index].sample_position, blocks[index].sample_position);
                CHECK_EQ(direct[index].sample_position, varied[index].sample_position);
            }
        }
}

TEST_CASE(TickCursor_CoincidentTicksAndEmptyBlocks)
{
    MusicalTickCursor cursor;
    // 6000 / (125 * 384) = 1/8 sample per tick.
    CHECK_EQ(cursor.Configure(125, 1), TickCursorStatus::Success);
    MusicalTick tick;
    CHECK_EQ(cursor.BeginBlock(0, 0), TickCursorStatus::Success);
    CHECK_EQ(cursor.Pull(tick), TickCursorStatus::BlockComplete);
    CHECK_EQ(cursor.Pull(tick), TickCursorStatus::BlockComplete);
    CHECK_EQ(cursor.BeginBlock(0, 1), TickCursorStatus::Success);
    for (std::uint64_t index = 0; index < 8; ++index) ExpectTick(cursor, index, 0, 0);
    CHECK_EQ(cursor.Pull(tick), TickCursorStatus::BlockComplete);
    CHECK_EQ(cursor.BeginBlock(1, 0), TickCursorStatus::Success);
    CHECK_EQ(cursor.Pull(tick), TickCursorStatus::BlockComplete);
    CHECK_EQ(cursor.BeginBlock(1, 1), TickCursorStatus::Success);
    for (std::uint64_t index = 8; index < 16; ++index) ExpectTick(cursor, index, 1, 0);
    CHECK_EQ(cursor.Pull(tick), TickCursorStatus::BlockComplete);
    CHECK_EQ(cursor.Reset(), TickCursorStatus::Success);
    CHECK_EQ(cursor.Pull(tick), TickCursorStatus::NoBlock);
    CHECK_EQ(cursor.BeginBlock(0, 1), TickCursorStatus::Success);
    ExpectTick(cursor, 0, 0, 0);
    CHECK_EQ(cursor.Reset(), TickCursorStatus::Success); // Also discards unfinished blocks.
    CHECK_EQ(cursor.BeginBlock(0, 1), TickCursorStatus::Success);
    ExpectTick(cursor, 0, 0, 0);
}

TEST_CASE(TickCursor_InvalidOperationsPreservePendingTicks)
{
    MusicalTickCursor cursor;
    MusicalTick tick{999, 999, 999};
    CHECK_EQ(cursor.Pull(tick), TickCursorStatus::NotConfigured);
    CHECK_EQ(cursor.Reset(), TickCursorStatus::NotConfigured);
    CHECK_EQ(cursor.BeginBlock(0, 1), TickCursorStatus::NotConfigured);
    CHECK_EQ(cursor.Configure(0, 44100), TickCursorStatus::InvalidConfiguration);
    CHECK_EQ(cursor.Configure(12000, 0), TickCursorStatus::InvalidConfiguration);
    CHECK_EQ(cursor.Configure(12000, 44100), TickCursorStatus::Success);
    CHECK_EQ(cursor.BeginBlock(1, 1), TickCursorStatus::NonconsecutiveBlock);
    CHECK_EQ(cursor.Pull(tick), TickCursorStatus::NoBlock);
    CHECK_EQ(cursor.BeginBlock(0, 57), TickCursorStatus::Success);
    CHECK_EQ(cursor.BeginBlock(0, 57), TickCursorStatus::BlockNotDrained);
    CHECK_EQ(cursor.Configure(0, 0), TickCursorStatus::InvalidConfiguration);
    ExpectTick(cursor, 0, 0, 0);
    CHECK_EQ(cursor.BeginBlock(57, 1), TickCursorStatus::BlockNotDrained);
    CHECK_EQ(cursor.Pull(tick), TickCursorStatus::BlockComplete);
    CHECK_EQ(cursor.BeginBlock(58, 1), TickCursorStatus::NonconsecutiveBlock);
    CHECK_EQ(cursor.BeginBlock(56, 1), TickCursorStatus::NonconsecutiveBlock);
    CHECK_EQ(cursor.BeginBlock(0, 1), TickCursorStatus::NonconsecutiveBlock);
    CHECK_EQ(cursor.BeginBlock(57, std::numeric_limits<std::uint64_t>::max()),
             TickCursorStatus::RangeOverflow);
    CHECK_EQ(cursor.BeginBlock(57, 1), TickCursorStatus::Success);
    ExpectTick(cursor, 1, 57, 0);
    CHECK_EQ(cursor.Pull(tick), TickCursorStatus::BlockComplete);
    // A representable maximum endpoint accepts empty blocks, never wraps.
    BroTracker::MusicalTickCursorTestAccess::Seed(cursor, 2,
        std::numeric_limits<std::uint64_t>::max());
    CHECK_EQ(cursor.BeginBlock(std::numeric_limits<std::uint64_t>::max(), 1),
             TickCursorStatus::RangeOverflow);
    CHECK_EQ(cursor.BeginBlock(std::numeric_limits<std::uint64_t>::max(), 0),
             TickCursorStatus::Success);
    CHECK_EQ(cursor.Pull(tick), TickCursorStatus::BlockComplete);
}

TEST_CASE(TickCursor_ArithmeticExhaustionAndRecovery)
{
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    constexpr auto maximum_config = std::numeric_limits<std::uint32_t>::max();
    MusicalTickCursor cursor;
    MusicalTick tick{999, 999, 999};
    CHECK_EQ(cursor.Configure(maximum_config, maximum_config), TickCursorStatus::Success);
    // Seed the state after draining all prior ticks; no public seeking API.
    constexpr std::uint64_t last_tick = 1180591620717411303ULL;
    constexpr std::uint64_t last_position = 18446744073709551609ULL;
    BroTracker::MusicalTickCursorTestAccess::Seed(cursor, last_tick, last_position);
    CHECK_EQ(cursor.BeginBlock(last_position, 6), TickCursorStatus::Success);
    ExpectTick(cursor, last_tick, last_position, 0);
    CHECK_EQ(cursor.Pull(tick), TickCursorStatus::ArithmeticExhausted);
    CHECK_EQ(tick.tick_index, 999);
    CHECK_EQ(tick.sample_position, 999);
    CHECK_EQ(tick.sample_offset, 999);
    CHECK_EQ(cursor.Pull(tick), TickCursorStatus::ArithmeticExhausted);
    CHECK_EQ(cursor.BeginBlock(maximum, 0), TickCursorStatus::ArithmeticExhausted);
    CHECK_EQ(cursor.Configure(0, 0), TickCursorStatus::InvalidConfiguration);
    CHECK_EQ(cursor.Pull(tick), TickCursorStatus::ArithmeticExhausted);
    CHECK_EQ(cursor.Reset(), TickCursorStatus::Success);
    CHECK_EQ(cursor.BeginBlock(0, 1), TickCursorStatus::Success);
    ExpectTick(cursor, 0, 0, 0);

    // Tick-index exhaustion with a representable sample position must never wrap.
    CHECK_EQ(cursor.Configure(maximum_config, 1), TickCursorStatus::Success);
    constexpr std::uint64_t final_position = 67108864015ULL;
    BroTracker::MusicalTickCursorTestAccess::Seed(cursor, maximum, final_position);
    CHECK_EQ(cursor.BeginBlock(final_position, 1), TickCursorStatus::Success);
    ExpectTick(cursor, maximum, final_position, 0);
    CHECK_EQ(cursor.Pull(tick), TickCursorStatus::ArithmeticExhausted);
    CHECK_EQ(cursor.BeginBlock(final_position + 1, 1), TickCursorStatus::ArithmeticExhausted);
    CHECK_EQ(cursor.Configure(12000, 48000), TickCursorStatus::Success);
    CHECK_EQ(cursor.BeginBlock(0, 1), TickCursorStatus::Success);
    ExpectTick(cursor, 0, 0, 0);
}

namespace
{
    void CheckPatternPosition(std::uint64_t tick_index, std::uint64_t length,
        std::uint64_t absolute_row, std::uint32_t tick_in_row,
        std::uint64_t pattern_row, std::uint64_t loop_index)
    {
        BroTracker::PatternPosition position;
        CHECK_EQ(BroTracker::TickToPatternPosition(tick_index, length, position),
                 BroTracker::PatternPositionStatus::Success);
        CHECK_EQ(position.absolute_row, absolute_row);
        CHECK_EQ(position.tick_in_row, tick_in_row);
        CHECK_EQ(position.pattern_row, pattern_row);
        CHECK_EQ(position.loop_index, loop_index);
        CHECK_EQ(position.IsRowStart(), tick_in_row == 0);
    }

    void CheckSamePosition(const BroTracker::PatternPosition& actual,
                           const BroTracker::PatternPosition& expected)
    {
        CHECK_EQ(actual.absolute_row, expected.absolute_row);
        CHECK_EQ(actual.tick_in_row, expected.tick_in_row);
        CHECK_EQ(actual.pattern_row, expected.pattern_row);
        CHECK_EQ(actual.loop_index, expected.loop_index);
        CHECK_EQ(actual.IsRowStart(), expected.IsRowStart());
    }
}

TEST_CASE(PatternPosition_RowAndSixteenRowLoopBoundaries)
{
    CheckPatternPosition(0, 16, 0, 0, 0, 0);
    CheckPatternPosition(95, 16, 0, 95, 0, 0);
    CheckPatternPosition(96, 16, 1, 0, 1, 0);
    CheckPatternPosition(1535, 16, 15, 95, 15, 0);
    CheckPatternPosition(1536, 16, 16, 0, 0, 1);
    CheckPatternPosition(4608, 16, 48, 0, 0, 3);
    CheckPatternPosition(4801, 16, 50, 1, 2, 3);
}

TEST_CASE(PatternPosition_ExplicitLengthsAndMaximumTick)
{
    CheckPatternPosition(95, 1, 0, 95, 0, 0);
    CheckPatternPosition(96, 1, 1, 0, 0, 1);
    CheckPatternPosition(4801, 1, 50, 1, 0, 50);
    CheckPatternPosition(671, 7, 6, 95, 6, 0);
    CheckPatternPosition(672, 7, 7, 0, 0, 1);
    CheckPatternPosition(1440, 7, 15, 0, 1, 2);
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    // Independent known quotient/remainder at UINT64_MAX.
    constexpr std::uint64_t maximum_row = 192153584101141162ULL;
    CheckPatternPosition(maximum, 1, maximum_row, 63, 0, maximum_row);
    CheckPatternPosition(maximum, 16, maximum_row, 63, 10, 12009599006321322ULL);
    CheckPatternPosition(maximum, 7, maximum_row, 63, 3, 27450512014448737ULL);
    // Full length range is valid even when length * ticks-per-row would overflow.
    CheckPatternPosition(maximum, maximum, maximum_row, 63, maximum_row, 0);
}

TEST_CASE(PatternPosition_InvalidLengthAndQueryOrder)
{
    BroTracker::PatternPosition position{123, 45, 6, 7};
    const auto original = position;
    CHECK_EQ(BroTracker::TickToPatternPosition(0, 0, position),
             BroTracker::PatternPositionStatus::InvalidPatternLength);
    CheckSamePosition(position, original);
    CHECK_EQ(BroTracker::TickToPatternPosition(
        std::numeric_limits<std::uint64_t>::max(), 0, position),
        BroTracker::PatternPositionStatus::InvalidPatternLength);
    CheckSamePosition(position, original);
    BroTracker::PatternPosition expected;
    CHECK_EQ(BroTracker::TickToPatternPosition(4801, 7, expected),
             BroTracker::PatternPositionStatus::Success);
    for (const std::uint64_t tick_index : {1536ULL, 0ULL, 95ULL, 1000000ULL, 96ULL})
    {
        CHECK_EQ(BroTracker::TickToPatternPosition(tick_index, 7, position),
                 BroTracker::PatternPositionStatus::Success);
        CHECK_EQ(BroTracker::TickToPatternPosition(4801, 7, position),
                 BroTracker::PatternPositionStatus::Success);
        CheckSamePosition(position, expected);
    }
}

TEST_CASE(PatternPosition_CursorCompositionAcrossPartitions)
{
    constexpr std::uint64_t total = 128 * 2048;
    const std::vector<std::uint64_t> regular(2048, 128);
    std::vector<std::uint64_t> irregular;
    std::uint64_t remaining = total;
    for (std::uint64_t count = 1; remaining != 0; count = count % 613 + 1)
    {
        const auto size = count < remaining ? count : remaining;
        irregular.push_back(size);
        remaining -= size;
    }
    const auto regular_ticks = CollectTicks(12753, 48000, regular);
    const auto irregular_ticks = CollectTicks(12753, 48000, irregular);
    CHECK_EQ(regular_ticks.size(), irregular_ticks.size());
    for (const std::uint64_t length : {1ULL, 16ULL, 7ULL})
    {
        std::vector<std::uint64_t> regular_starts, irregular_starts;
        for (std::size_t index = 0; index < regular_ticks.size() &&
             index < irregular_ticks.size(); ++index)
        {
            // Carry cursor sample metadata through mapping without recomputing it.
            auto regular_tick = regular_ticks[index];
            auto irregular_tick = irregular_ticks[index];
            BroTracker::PatternPosition regular_position, irregular_position;
            CHECK_EQ(BroTracker::TickToPatternPosition(regular_tick.tick_index, length,
                regular_position), BroTracker::PatternPositionStatus::Success);
            CHECK_EQ(BroTracker::TickToPatternPosition(irregular_tick.tick_index, length,
                irregular_position), BroTracker::PatternPositionStatus::Success);
            CheckSamePosition(regular_position, irregular_position);
            CHECK_EQ(regular_tick.tick_index, irregular_tick.tick_index);
            CHECK_EQ(regular_tick.sample_position, irregular_tick.sample_position);
            CHECK_EQ(regular_tick.sample_position, regular_ticks[index].sample_position);
            CHECK_EQ(regular_tick.sample_offset, regular_ticks[index].sample_offset);
            CHECK_EQ(irregular_tick.sample_position, irregular_ticks[index].sample_position);
            CHECK_EQ(irregular_tick.sample_offset, irregular_ticks[index].sample_offset);
            if (regular_position.IsRowStart()) regular_starts.push_back(regular_tick.tick_index);
            if (irregular_position.IsRowStart()) irregular_starts.push_back(irregular_tick.tick_index);
        }
        CHECK(regular_starts == irregular_starts);
        CHECK(!regular_starts.empty());
        for (std::size_t index = 0; index < regular_starts.size(); ++index)
            CHECK_EQ(regular_starts[index], index * BroTracker::kTicksPerRow);
    }
}

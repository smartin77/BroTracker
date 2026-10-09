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

#include <limits>

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

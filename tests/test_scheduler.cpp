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

#include "runtime/scheduler.h"

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

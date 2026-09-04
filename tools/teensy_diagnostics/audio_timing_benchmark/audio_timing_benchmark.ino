/*
 * BroTracker
 *
 * Description: Teensy 4.1 realtime audio timing benchmark.
 *
 *              Establishes the baseline realtime processing characteristics
 *              of the audio platform before implementing the complete
 *              BroTracker audio engine.
 *
 *              This benchmark measures:
 *              - deterministic audio-block processing behaviour
 *              - execution time, jitter and processing margin
 *              - Scheduler advancement overhead and correctness
 *              - processing overruns
 *              - realtime timing stability
 *
 *              This benchmark does NOT implement:
 *              - audio synthesis
 *              - sample playback
 *              - mixing
 *              - SD streaming during audio processing
 *              - MIDI output
 *              - tracker playback
 *
 *              Dependencies:
 *              - Real BroTracker Scheduler (src/runtime/scheduler.h/cpp)
 *              - Real BroTracker Diagnostics (firmware/teensy/BroTracker/diagnostics.h/cpp)
 *
 *              Results are written to: BroTracker/audio_timing_benchmark.log
 *
 *              This is an experimental diagnostic tool.
 *              It is not the final BroTracker Audio Engine.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include <Arduino.h>
#include <cstdint>
#include <cstdio>

// ============================================================================
// Real BroTracker Modules
// ============================================================================
// This sketch uses the actual, reusable BroTracker implementations from the
// repository without duplication or copying.
//
// Arduino IDE Integration (Per D0038 - Arduino IDE Integration for Reusable Components):
//
// Arduino IDE automatically discovers libraries from the repository libraries/ directory
// when the BroTracker repository root is configured as the Arduino Sketchbook location.
//
// This sketch references:
//   - BroTrackerScheduler from libraries/scheduler/ (platform-independent)
//   - BroTrackerDiagnostics from libraries/teensy/diagnostics/ (Teensy-specific)
//
// The sketch includes these libraries using standard Arduino syntax:

#include <scheduler.h>
#include <diagnostics.h>

// ============================================================================
// Data Types and Structures
// ============================================================================

struct BenchmarkConfig
{
    uint32_t sample_rate;
    uint16_t block_size;

    uint32_t block_duration_us() const
    {
        return (uint32_t)(((uint64_t)block_size * 1000000) / sample_rate);
    }
};

struct MeasurementStats
{
    uint32_t min_time_us;
    uint32_t max_time_us;
    uint64_t total_time_us;
    uint32_t overrun_count;
    uint32_t measured_blocks;
};

// ============================================================================
// Logging
// ============================================================================

void PrintLine(const char* message)
{
    Serial.println(message);
    BroTracker::DiagnosticLog(message);
}

void PrintValue(const char* label, uint32_t value)
{
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%s %lu", label, (unsigned long)value);
    PrintLine(buffer);
}

void PrintValue64(const char* label, uint64_t value)
{
    char buffer[128];
    uint32_t high = (uint32_t)(value >> 32);
    uint32_t low = (uint32_t)(value & 0xFFFFFFFF);
    if (high > 0)
        snprintf(buffer, sizeof(buffer), "%s %lu%08lu", label, (unsigned long)high, (unsigned long)low);
    else
        snprintf(buffer, sizeof(buffer), "%s %lu", label, (unsigned long)low);
    PrintLine(buffer);
}

// ============================================================================
// Deterministic Audio Workload
// ============================================================================

void ProcessAudioBlock(uint16_t* buffer, uint16_t block_size)
{
    for (uint16_t i = 0; i < block_size; ++i)
    {
        uint16_t sample = buffer[i];
        buffer[i] = (sample << 1) | (sample >> 15);
    }
}

// ============================================================================
// Measurement Clock
// ============================================================================

inline uint32_t MeasurementTimeUs()
{
    return micros();
}

uint32_t ElapsedSinceUs(uint32_t start_time)
{
    uint32_t now = MeasurementTimeUs();
    if (now >= start_time)
        return now - start_time;
    else
        return (UINT32_MAX - start_time) + now + 1;
}

// ============================================================================
// Benchmark Execution
// ============================================================================

void RunBenchmark(const BenchmarkConfig& config)
{
    PrintLine("\n");
    PrintLine("=== Audio Timing Benchmark ===");
    PrintValue("Sample Rate (Hz):", config.sample_rate);
    PrintValue("Block Size (samples):", config.block_size);
    PrintValue("Block Duration (us):", config.block_duration_us());

    BroTracker::Scheduler scheduler;
    if (!scheduler.Initialize())
    {
        PrintLine("ERROR: Scheduler initialization failed");
        return;
    }

    uint16_t* audio_buffer = new uint16_t[config.block_size];
    if (!audio_buffer)
    {
        PrintLine("ERROR: Failed to allocate audio buffer");
        return;
    }

    for (uint16_t i = 0; i < config.block_size; ++i)
    {
        audio_buffer[i] = (uint16_t)(i ^ 0xAAAA);
    }

    const uint32_t kMeasuredBlocks = 1000;
    const uint32_t kWarmupBlocks = 100;

    uint32_t block_duration_us = config.block_duration_us();
    MeasurementStats stats = {
        UINT32_MAX,
        0,
        0,
        0,
        0,
    };

    PrintLine("Starting measurement...");

    for (uint32_t i = 0; i < kWarmupBlocks; ++i)
    {
        ProcessAudioBlock(audio_buffer, config.block_size);
        scheduler.AdvanceSamples(config.block_size);
    }

    for (uint32_t i = 0; i < kMeasuredBlocks; ++i)
    {
        uint32_t start_time = MeasurementTimeUs();
        ProcessAudioBlock(audio_buffer, config.block_size);
        scheduler.AdvanceSamples(config.block_size);
        uint32_t elapsed_us = ElapsedSinceUs(start_time);

        if (elapsed_us < stats.min_time_us)
            stats.min_time_us = elapsed_us;
        if (elapsed_us > stats.max_time_us)
            stats.max_time_us = elapsed_us;

        stats.total_time_us += elapsed_us;
        stats.measured_blocks++;

        if (elapsed_us > block_duration_us)
            stats.overrun_count++;

        if (i % 100 == 0)
            delay(0);
    }

    PrintLine("");
    PrintValue("Blocks measured:", stats.measured_blocks);
    PrintValue("Min processing time (us):", stats.min_time_us);

    uint32_t avg_time_us = stats.total_time_us / stats.measured_blocks;
    PrintValue("Avg processing time (us):", avg_time_us);
    PrintValue("Max processing time (us):", stats.max_time_us);

    uint32_t jitter_us = stats.max_time_us - stats.min_time_us;
    PrintValue("Jitter (us):", jitter_us);

    int32_t margin_us = (int32_t)block_duration_us - (int32_t)stats.max_time_us;
    PrintValue("Processing margin (us):", (uint32_t)margin_us);

    PrintValue("Processing overruns:", stats.overrun_count);

    uint64_t expected_position = (uint64_t)config.block_size * (kWarmupBlocks + kMeasuredBlocks);
    uint64_t actual_position = scheduler.GetPosition();
    PrintValue64("Expected scheduler position:", expected_position);
    PrintValue64("Actual scheduler position:", actual_position);

    if (actual_position == expected_position)
    {
        PrintLine("Scheduler advancement: PASS");
    }
    else
    {
        PrintLine("Scheduler advancement: FAIL");
    }

    delete[] audio_buffer;
}

// ============================================================================
// Arduino Setup and Loop
// ============================================================================

void setup()
{
    Serial.begin(115200);
    delay(1000);

    PrintLine("BroTracker Audio Timing Benchmark");
    PrintLine("Starting initialization...");

    if (!BroTracker::DiagnosticsInitialize())
    {
        PrintLine("Warning: Diagnostics initialization failed");
        PrintLine("Results will appear in Serial console only");
    }
    else
    {
        PrintLine("Diagnostics initialized - results logged to SD card");
    }

    PrintLine("");
    PrintLine("Testing BroTracker Scheduler independence from wall-clock timing");
    PrintLine("Measuring audio processing block performance");
    PrintLine("");

    const BenchmarkConfig configs[] = {
        {44100, 256},
        {44100, 512},
        {44100, 1024},
    };

    const int num_configs = sizeof(configs) / sizeof(configs[0]);

    for (int i = 0; i < num_configs; ++i)
    {
        RunBenchmark(configs[i]);
        delay(100);
    }

    PrintLine("");
    PrintLine("=== Benchmark Complete ===");
    PrintLine("All measurements finished");

    PrintLine("Indicating completion with LED flash...");
    BroTracker::DiagnosticBlink(3);

    PrintLine("Benchmark finished. Device is safe to unplug.");
}

void loop()
{
    // Benchmark runs once in setup()
    delay(1000);
}

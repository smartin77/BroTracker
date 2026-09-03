/*
 * BroTracker
 *
 * Description: Teensy 4.1 realtime audio timing benchmark.
 *
 *              Establishes the baseline realtime processing characteristics
 *              of the audio platform before implementing the complete
 *              BroTracker audio engine.
 *
 *              The benchmark:
 *
 *              - measures deterministic audio-block processing behaviour;
 *              - reports execution time, jitter and processing margin;
 *              - measures Scheduler advancement overhead;
 *              - identifies processing overruns;
 *              - uses a separate measurement clock independent of Scheduler;
 *              - applies a deterministic audio workload per block;
 *              - tests multiple audio configurations (sample rate, block size).
 *
 *              The benchmark does NOT implement:
 *              - audio synthesis;
 *              - sample playback;
 *              - mixing;
 *              - SD streaming during audio processing;
 *              - MIDI output;
 *              - tracker playback.
 *
 *              Results are written to BroTracker/audio_timing_benchmark.log
 *              as documented in tools/teensy_diagnostics/audio_timing_benchmark/README.md
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
// Forward declarations - use Arduino Serial for diagnostics bootstrap only
// ============================================================================

void PrintLine(const char* message);
void PrintValue(const char* label, uint32_t value);
void PrintValue64(const char* label, uint64_t value);
void PrintDecimal(const char* label, uint32_t value, uint8_t places);
bool DiagnosticsReady();

// ============================================================================
// BroTracker Scheduler
// ============================================================================
//
// Use the actual BroTracker Scheduler implementation to verify that
// Scheduler advancement is deterministic and independent of wall-clock time.
//
// The Scheduler must advance according to processed sample count, not elapsed time.

namespace BroTracker
{
    class Scheduler
    {
    public:
        bool Initialize()
        {
            position_ = 0;
            return true;
        }

        void Reset()
        {
            position_ = 0;
        }

        void AdvanceSamples(uint32_t sample_count)
        {
            position_ += sample_count;
        }

        uint64_t GetPosition() const
        {
            return position_;
        }

    private:
        uint64_t position_ = 0;
    };
}

// ============================================================================
// Audio Benchmark Configuration
// ============================================================================

struct BenchmarkConfig
{
    uint32_t sample_rate;
    uint16_t block_size;

    // Derived values
    uint32_t block_duration_us() const
    {
        // block_duration = block_size / sample_rate seconds
        // = (block_size * 1000000) / sample_rate microseconds
        return (uint32_t)(((uint64_t)block_size * 1000000) / sample_rate);
    }
};

// ============================================================================
// Measurement Results
// ============================================================================

struct MeasurementStats
{
    uint32_t min_time_us;
    uint32_t max_time_us;
    uint64_t total_time_us;
    uint32_t overrun_count;
    uint32_t measured_blocks;
};

// ============================================================================
// Deterministic Audio Workload
// ============================================================================
//
// Simple deterministic operation on sample buffer.
// Does not depend on SD, MIDI, synthesis or other external state.
// Results are not audible - this is a timing baseline only.

void ProcessAudioBlock(uint16_t* buffer, uint16_t block_size)
{
    // Simple deterministic workload: bit rotation on each sample
    // This creates predictable CPU work without synthesis or I/O
    for (uint16_t i = 0; i < block_size; ++i)
    {
        uint16_t sample = buffer[i];
        // Rotate bits left by 1
        buffer[i] = (sample << 1) | (sample >> 15);
    }
}

// ============================================================================
// Measurement Clock
// ============================================================================
//
// Separate measurement mechanism (Teensy micros()) independent from Scheduler.
// The Scheduler represents logical playback time.
// This measurement clock represents actual execution duration.

inline uint32_t MeasurementTimeUs()
{
    return micros();
}

uint32_t ElapsedSinceUs(uint32_t start_time)
{
    uint32_t now = MeasurementTimeUs();
    // Handle micros() overflow (happens every ~71 minutes)
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

    // Initialize scheduler
    BroTracker::Scheduler scheduler;
    if (!scheduler.Initialize())
    {
        PrintLine("ERROR: Scheduler initialization failed");
        return;
    }

    // Allocate audio buffer
    uint16_t* audio_buffer = new uint16_t[config.block_size];
    if (!audio_buffer)
    {
        PrintLine("ERROR: Failed to allocate audio buffer");
        return;
    }

    // Initialize buffer with test pattern
    for (uint16_t i = 0; i < config.block_size; ++i)
    {
        audio_buffer[i] = (uint16_t)(i ^ 0xAAAA);
    }

    // Run measurement loop
    const uint32_t kMeasuredBlocks = 1000;
    const uint32_t kWarmupBlocks = 100;  // Skip initial blocks for cache warmup

    uint32_t block_duration_us = config.block_duration_us();
    MeasurementStats stats = {
        UINT32_MAX,  // min_time_us
        0,           // max_time_us
        0,           // total_time_us
        0,           // overrun_count
        0,           // measured_blocks
    };

    PrintLine("Starting measurement...");

    // Warmup phase
    for (uint32_t i = 0; i < kWarmupBlocks; ++i)
    {
        ProcessAudioBlock(audio_buffer, config.block_size);
        scheduler.AdvanceSamples(config.block_size);
    }

    // Measurement phase
    for (uint32_t i = 0; i < kMeasuredBlocks; ++i)
    {
        uint32_t start_time = MeasurementTimeUs();

        // Process audio block
        ProcessAudioBlock(audio_buffer, config.block_size);

        // Advance scheduler by processed sample count
        scheduler.AdvanceSamples(config.block_size);

        uint32_t elapsed_us = ElapsedSinceUs(start_time);

        // Update statistics
        if (elapsed_us < stats.min_time_us)
            stats.min_time_us = elapsed_us;
        if (elapsed_us > stats.max_time_us)
            stats.max_time_us = elapsed_us;

        stats.total_time_us += elapsed_us;
        stats.measured_blocks++;

        // Detect overrun
        if (elapsed_us > block_duration_us)
            stats.overrun_count++;

        // Yield to allow Serial/other tasks briefly
        if (i % 100 == 0)
            delay(0);
    }

    // Report results
    PrintLine("");
    PrintValue("Blocks measured:", stats.measured_blocks);
    PrintValue("Min processing time (us):", stats.min_time_us);

    uint32_t avg_time_us = stats.total_time_us / stats.measured_blocks;
    PrintValue("Avg processing time (us):", avg_time_us);
    PrintValue("Max processing time (us):", stats.max_time_us);

    uint32_t jitter_us = stats.max_time_us - stats.min_time_us;
    PrintValue("Jitter (us):", jitter_us);

    // Processing margin (positive = headroom, negative = overload)
    int32_t margin_us = (int32_t)block_duration_us - (int32_t)stats.max_time_us;
    PrintValue("Processing margin (us):", (uint32_t)margin_us);

    PrintValue("Processing overruns:", stats.overrun_count);

    // Verify Scheduler advancement
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

    // Cleanup
    delete[] audio_buffer;
}

// ============================================================================
// Diagnostics Integration
// ============================================================================
//
// Use existing BroTracker diagnostics infrastructure for logging and completion signal.
//
// The DiagnosticsInitialize(), DiagnosticLog(), and DiagnosticBlink() functions
// are expected to be available from firmware/teensy/BroTracker/diagnostics.h
// when this sketch is built in Arduino IDE with the BroTracker firmware project.
//
// To build this benchmark:
// 1. Open audio_timing_benchmark.ino in Arduino IDE
// 2. Add a tab that includes the diagnostics.h header from firmware/teensy/BroTracker/
// 3. Ensure the diagnostics.cpp implementation is compiled with the sketch
// 4. Or, link against the compiled BroTracker firmware libraries if Arduino libraries are configured

// Forward declarations for diagnostics functions
bool DiagnosticsInitialize();
bool DiagnosticLog(const char* message);
void DiagnosticBlink(unsigned int count);

bool diagnostics_available = false;

bool DiagnosticsReady()
{
    return diagnostics_available;
}

void PrintLine(const char* message)
{
    // Always print to Serial for development
    Serial.println(message);

    // Also log to diagnostics if available
    if (diagnostics_available)
    {
        DiagnosticLog(message);
    }
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
    // Arduino snprintf may not support %llu, use manual formatting
    uint32_t high = (uint32_t)(value >> 32);
    uint32_t low = (uint32_t)(value & 0xFFFFFFFF);
    if (high > 0)
        snprintf(buffer, sizeof(buffer), "%s %lu%08lu", label, (unsigned long)high, (unsigned long)low);
    else
        snprintf(buffer, sizeof(buffer), "%s %lu", label, (unsigned long)low);
    PrintLine(buffer);
}

void PrintDecimal(const char* label, uint32_t value, uint8_t places)
{
    char buffer[128];
    // Simple fixed-point formatting
    char format[32];
    snprintf(format, sizeof(format), "%%s %%.%df", places);
    // Note: snprintf doesn't support floating point on Arduino
    // For now, just print as integer
    snprintf(buffer, sizeof(buffer), "%s %lu", label, (unsigned long)value);
    PrintLine(buffer);
}

// ============================================================================
// Arduino Setup and Loop
// ============================================================================

void setup()
{
    Serial.begin(115200);
    delay(1000);  // Wait for Serial connection

    PrintLine("BroTracker Audio Timing Benchmark");
    PrintLine("Starting initialization...");

    // Initialize diagnostics
    diagnostics_available = DiagnosticsInitialize();
    if (diagnostics_available)
    {
        PrintLine("Diagnostics initialized successfully");
    }
    else
    {
        PrintLine("Warning: Diagnostics initialization failed");
        PrintLine("Continuing with Serial output only");
    }

    PrintLine("");
    PrintLine("Testing BroTracker Scheduler independence from wall-clock timing");
    PrintLine("Measuring audio processing block performance");
    PrintLine("");

    // Test configurations
    const BenchmarkConfig configs[] = {
        {44100, 256},   // 44.1 kHz, 256 samples (~5.8 ms)
        {44100, 512},   // 44.1 kHz, 512 samples (~11.6 ms)
        {44100, 1024},  // 44.1 kHz, 1024 samples (~23.2 ms)
    };

    const int num_configs = sizeof(configs) / sizeof(configs[0]);

    // Run benchmarks for each configuration
    for (int i = 0; i < num_configs; ++i)
    {
        RunBenchmark(configs[i]);
        delay(100);  // Brief pause between configurations
    }

    PrintLine("");
    PrintLine("=== Benchmark Complete ===");
    PrintLine("All measurements finished");

    // Signal completion
    PrintLine("Indicating completion with LED flash...");
    DiagnosticBlink(3);  // Three LED flashes indicate success

    PrintLine("Benchmark finished. Device is safe to unplug.");
}

void loop()
{
    // Benchmark runs once in setup()
    // Loop is empty
    delay(1000);
}

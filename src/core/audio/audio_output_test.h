#pragma once

#include <cstdint>
#include "audio_engine.h"

/**
 * @brief Test implementation of AudioOutput interface.
 *
 * TestAudioOutput is a minimal, platform-independent concrete implementation
 * of the AudioOutput abstract interface. It provides simple instrumentation
 * to verify the delegation path from AudioEngine to AudioOutput without
 * requiring any hardware, Teensy types, or Arduino APIs.
 *
 * This is a test/demonstration backend only and must not be treated as
 * a real audio device.
 */
class TestAudioOutput : public AudioOutput
{
public:
    /**
     * @brief Initialize the test output.
     *
     * Resets internal state and returns success.
     *
     * @return true (always succeeds).
     */
    bool Initialize() override;

    /**
     * @brief Reset the test output state.
     *
     * Resets the processed sample counter to zero.
     */
    void Reset() override;

    /**
     * @brief Record audio samples processed.
     *
     * Accumulates the number of samples received.
     *
     * @param sample_count Number of audio samples to record.
     */
    void Process(uint32_t sample_count) override;

    /**
     * @brief Get the total number of samples processed by this output.
     *
     * @return Number of samples accumulated since last reset.
     */
    uint64_t GetProcessedSamples() const;

private:
    // Logical number of audio samples processed by this output.
    uint64_t processed_samples_ = 0;
};

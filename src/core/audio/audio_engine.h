#pragma once

#include <cstdint>

/**
 * @brief Abstract audio output interface.
 *
 * AudioOutput defines the platform-independent contract for audio hardware
 * or software output devices. Concrete implementations (e.g., PT8211 DAC,
 * USB Audio, loopback) inherit this interface and provide device-specific
 * initialization, reset, and sample processing.
 *
 * The audio engine delegates all output operations to an AudioOutput instance,
 * keeping the core engine logic free of platform/device-specific code.
 */
class AudioOutput
{
public:
    virtual ~AudioOutput() = default;

    /**
     * @brief Initialize the audio output device.
     *
     * Performs any device-specific setup required before processing begins.
     * Must not perform blocking I/O or real-time-unsafe operations.
     *
     * @return true if initialization succeeded, false otherwise.
     */
    virtual bool Initialize() = 0;

    /**
     * @brief Reset the audio output device state.
     *
     * Resets any internal buffers, filters, or processing state to a known
     * clean condition. Does not reinitialize the device itself.
     */
    virtual void Reset() = 0;

    /**
     * @brief Process a block of audio samples for output.
     *
     * Receives a block of mixed/final audio samples and transmits them to
     * the output device. The realtime scheduling boundary is enforced here;
     * the implementation must complete within a bounded time.
     *
     * @param sample_count Number of audio samples to process.
     */
    virtual void Process(uint32_t sample_count) = 0;
};

/**
* @brief Core audio processing interface.
*
* AudioEngine provides the boundary between the BroTracker scheduler
* and the realtime audio processing layer.
*
* The engine operates on the audio sample timeline. The scheduler
* remains the authoritative source of playback timing.
*
* This initial implementation contains no DSP or platform-specific
* audio output. It only establishes the basic audio processing API.
  */
  class AudioEngine
  {
    public:

    /**
     * @brief Initialize the audio engine.
     *
     * Initializes the engine and resets its processing state.
        */
        void Initialize();

    /**
    * @brief Reset the audio processing state.
    *
    * Resets the processed sample position to zero.
        */
        void Reset();

    /**
    * @brief Process a block of audio samples.
    *
    * This is the realtime processing entry point.
    *
    * The initial implementation only advances the internal
    * processed-sample counter. Audio generation and DSP will
    * be added in later stages.
    *
    * @param sample_count Number of audio samples to process.
        */
        void Process(uint32_t sample_count);

    /**
    * @brief Get the number of processed audio samples.
    *
    *
    * Provides access to the current logical audio processing
    *  position for validation and testing.
    * @return Number of audio samples processed since the last reset.
    */
    uint64_t GetProcessedSamples() const;

    private:
    // Logical number of audio samples processed by the engine.
    uint64_t processed_samples_ = 0;
};

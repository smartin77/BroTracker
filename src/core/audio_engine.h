#pragma once

#include <cstdint>

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

    private:
    // Logical number of audio samples processed by the engine.
    uint64_t processed_samples_ = 0;
};

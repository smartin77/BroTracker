#pragma once

#include <cstdint>

namespace BroTracker
{
    // Scheduler
    //
    // Owns the logical playback position as a sample-based timeline.
    // Represents audio samples processed, independent of wall-clock time,
    // CPU cycles, or platform-specific timing mechanisms.
    //
    // The audio processing boundary will later advance this timeline
    // according to the number of processed audio samples.
    class Scheduler
    {
    public:
        bool Initialize();
        void Reset();

        // Advance playback position by the number of samples processed.
        // The number of samples is independent of sample rate or wall-clock time.
        // It represents the quantity of audio samples that have been processed
        // by the realtime engine.
        void AdvanceSamples(std::uint32_t sample_count);

        // Get current logical playback position in audio samples.
        std::uint64_t GetPosition() const;

    private:
        std::uint64_t position_ = 0;
    };
}
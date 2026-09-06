#include "audio_test_source.h"

namespace BroTracker
{
namespace
{
    constexpr uint32_t kToneFrequencyHz = 440;
    constexpr int16_t kAmplitude = 8000;
    constexpr uint32_t kSampleRateHz = 44100;
    constexpr uint32_t kHalfPeriodSamples = kSampleRateHz / (2 * kToneFrequencyHz);
}

    AudioTestSource::AudioTestSource(Scheduler& scheduler)
        : AudioStream(0, nullptr), scheduler_(scheduler)
    {
    }

    void AudioTestSource::update()
    {
        audio_block_t* block = allocate();
        if (block != nullptr)
        {
            for (int i = 0; i < AUDIO_BLOCK_SAMPLES; ++i)
            {
                const bool high_phase = ((sample_index_ / kHalfPeriodSamples) % 2) == 0;
                block->data[i] = high_phase ? kAmplitude : static_cast<int16_t>(-kAmplitude);
                ++sample_index_;
            }
            transmit(block);
            release(block);
        }

        // Scheduler advances by exactly the number of samples processed in
        // this audio block, per the sample-based timing model.
        scheduler_.AdvanceSamples(AUDIO_BLOCK_SAMPLES);
    }
}

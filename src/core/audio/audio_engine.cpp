#include "audio_engine.h"

void AudioEngine::Initialize()
{
    Reset();
}

void AudioEngine::Reset()
{
    processed_samples_ = 0;
}

void AudioEngine::Process(uint32_t sample_count)
{
    processed_samples_ += sample_count;
}

uint64_t AudioEngine::GetProcessedSamples() const
{
    return processed_samples_;
}

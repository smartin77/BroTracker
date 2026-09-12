#include "audio_output_test.h"

bool TestAudioOutput::Initialize()
{
    Reset();
    return true;
}

void TestAudioOutput::Reset()
{
    processed_samples_ = 0;
}

void TestAudioOutput::Process(uint32_t sample_count)
{
    processed_samples_ += sample_count;
}

uint64_t TestAudioOutput::GetProcessedSamples() const
{
    return processed_samples_;
}

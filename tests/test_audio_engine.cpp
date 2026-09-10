#include <cassert>

#include "core/audio/audio_engine.h"

/**
* @brief Verify basic AudioEngine initialization and processing.
*/
void TestAudioEngine()
{
    AudioEngine audio_engine;

    // Initialize must start at the beginning of the audio timeline.
    audio_engine.Initialize();
    assert(audio_engine.GetProcessedSamples() == 0);

    // Processing samples must advance the audio timeline.
    audio_engine.Process(128);
    assert(audio_engine.GetProcessedSamples() == 128);

    // Multiple processing blocks must accumulate correctly.
    audio_engine.Process(256);
    assert(audio_engine.GetProcessedSamples() == 384);

    // Reset must return the engine to the beginning.
    audio_engine.Reset();
    assert(audio_engine.GetProcessedSamples() == 0);
}

int main()
{
    TestAudioEngine();

    return 0;
}

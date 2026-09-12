#include <cassert>

#include "core/audio/audio_engine.h"
#include "core/audio/audio_output_test.h"

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

/**
 * @brief Verify AudioEngine delegation to AudioOutput.
 */
void TestAudioEngineOutputDelegation()
{
    AudioEngine audio_engine;
    TestAudioOutput test_output;

    // AudioEngine must work normally without an output attached.
    audio_engine.Initialize();
    assert(audio_engine.GetProcessedSamples() == 0);

    audio_engine.Process(128);
    assert(audio_engine.GetProcessedSamples() == 128);

    // TestAudioOutput can be attached using SetOutput().
    audio_engine.SetOutput(&test_output);

    // Initialize test output.
    test_output.Initialize();
    assert(test_output.GetProcessedSamples() == 0);

    // Processing samples through AudioEngine must also increment
    // TestAudioOutput's processed sample count.
    audio_engine.Process(256);
    assert(audio_engine.GetProcessedSamples() == 384);
    assert(test_output.GetProcessedSamples() == 256);

    // Multiple Process() calls must accumulate correctly in both.
    audio_engine.Process(512);
    assert(audio_engine.GetProcessedSamples() == 896);
    assert(test_output.GetProcessedSamples() == 768);

    // Resetting the AudioEngine does not reset the output
    // (output lifetime is independent of engine).
    audio_engine.Reset();
    assert(audio_engine.GetProcessedSamples() == 0);
    assert(test_output.GetProcessedSamples() == 768);  // unchanged

    // After reset, processing continues to delegate to output.
    audio_engine.Process(64);
    assert(audio_engine.GetProcessedSamples() == 64);
    assert(test_output.GetProcessedSamples() == 832);

    // Detaching the output with SetOutput(nullptr) stops delegation
    // while AudioEngine itself continues processing normally.
    audio_engine.SetOutput(nullptr);
    audio_engine.Process(100);
    assert(audio_engine.GetProcessedSamples() == 164);
    assert(test_output.GetProcessedSamples() == 832);  // unchanged

    // Resetting output manually is independent of engine reset.
    test_output.Reset();
    assert(test_output.GetProcessedSamples() == 0);
    assert(audio_engine.GetProcessedSamples() == 164);  // unchanged
}

int main()
{
    TestAudioEngine();
    TestAudioEngineOutputDelegation();

    return 0;
}

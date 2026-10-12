#include "test_framework.h"
#include "core/audio/ram_sample_voice.h"

using namespace BroTracker;

namespace
{
    constexpr std::int16_t pcm[] = {32767, -32768, 123, -456, 7};
    const Pcm16MonoView sample{pcm, 5, 44100};
    void CheckOutput(const std::int16_t* actual, const std::int16_t* expected, std::size_t count)
    {
        for (std::size_t i = 0; i < count; ++i) CHECK_EQ(actual[i], expected[i]);
    }
}

TEST_CASE(RamVoice_InitialSilenceAndCompletion)
{
    RamSampleVoice voice;
    CHECK(!voice.IsActive());
    CHECK_EQ(voice.GetFramePosition(), 0);
    CHECK_EQ(voice.GetOutputSampleRate(), 0);
    std::int16_t output[8] = {1, 2, 3};
    const std::int16_t silence[8]{};
    CHECK_EQ(voice.Render(output, 8), RamVoiceStatus::Success);
    CheckOutput(output, silence, 8);
    CHECK_EQ(voice.Configure(44100), RamVoiceStatus::Success);
    CHECK_EQ(voice.Trigger(sample), RamVoiceStatus::Success);
    CHECK_EQ(voice.Render(output, 8), RamVoiceStatus::Success);
    const std::int16_t expected[] = {32767, -32768, 123, -456, 7, 0, 0, 0};
    CheckOutput(output, expected, 8);
    CHECK(!voice.IsActive());
    CHECK_EQ(voice.GetFramePosition(), 0);
    CHECK_EQ(voice.Render(output, 8), RamVoiceStatus::Success);
    CheckOutput(output, silence, 8);
    CHECK_EQ(voice.Trigger(sample), RamVoiceStatus::Success);
    CHECK_EQ(voice.Render(output, 5), RamVoiceStatus::Success);
    CheckOutput(output, pcm, 5);
    CHECK(!voice.IsActive());
}

TEST_CASE(RamVoice_PartitionsRetriggerReplacementStopReset)
{
    for (const std::size_t stride : {1u, 2u, 3u, 8u})
    {
        RamSampleVoice voice;
        CHECK_EQ(voice.Configure(44100), RamVoiceStatus::Success);
        CHECK_EQ(voice.Trigger(sample), RamVoiceStatus::Success);
        std::int16_t output[8]{};
        for (std::size_t offset = 0; offset < 8;)
        {
            const auto count = stride < 8 - offset ? stride : 8 - offset;
            CHECK_EQ(voice.Render(output + offset, count), RamVoiceStatus::Success);
            offset += count;
        }
        const std::int16_t expected[] = {32767, -32768, 123, -456, 7, 0, 0, 0};
        CheckOutput(output, expected, 8);
    }
    RamSampleVoice voice;
    CHECK_EQ(voice.Configure(44100), RamVoiceStatus::Success);
    CHECK_EQ(voice.Trigger(sample), RamVoiceStatus::Success);
    std::int16_t output[3];
    CHECK_EQ(voice.Render(output, 2), RamVoiceStatus::Success);
    CHECK_EQ(voice.GetFramePosition(), 2);
    CHECK_EQ(voice.Trigger(sample), RamVoiceStatus::Success);
    CHECK_EQ(voice.Render(output, 2), RamVoiceStatus::Success);
    CheckOutput(output, pcm, 2);
    const std::int16_t replacement[] = {-3, 99};
    CHECK_EQ(voice.Trigger({replacement, 2, 44100}), RamVoiceStatus::Success);
    CHECK_EQ(voice.Render(output, 1), RamVoiceStatus::Success);
    CHECK_EQ(output[0], -3);
    voice.Stop();
    CHECK(!voice.IsActive());
    CHECK_EQ(voice.Render(output, 3), RamVoiceStatus::Success);
    const std::int16_t silence[3]{};
    CheckOutput(output, silence, 3);
    CHECK_EQ(voice.Trigger(sample), RamVoiceStatus::Success);
    voice.Reset();
    CHECK(!voice.IsActive());
    CHECK_EQ(voice.GetFramePosition(), 0);
    CHECK_EQ(voice.GetOutputSampleRate(), 44100);
    CHECK_EQ(voice.Render(output, 3), RamVoiceStatus::Success);
    CheckOutput(output, silence, 3);
    CHECK_EQ(voice.Trigger(sample), RamVoiceStatus::Success);
    CHECK_EQ(voice.Configure(44100), RamVoiceStatus::Success);
    CHECK(!voice.IsActive());
}

TEST_CASE(RamVoice_InvalidOperationsAndExplicitRates)
{
    RamSampleVoice voice;
    CHECK_EQ(voice.Trigger(sample), RamVoiceStatus::NotConfigured);
    CHECK_EQ(voice.Configure(44100), RamVoiceStatus::Success);
    CHECK_EQ(voice.Trigger(sample), RamVoiceStatus::Success);
    std::int16_t output[3] = {11, 22, 33};
    const std::int16_t original[] = {11, 22, 33};
    CHECK_EQ(voice.Render(output, 1), RamVoiceStatus::Success);
    output[0] = original[0];
    CHECK_EQ(voice.Configure(0), RamVoiceStatus::InvalidSampleRate);
    CHECK_EQ(voice.Trigger({nullptr, 5, 44100}), RamVoiceStatus::InvalidSample);
    CHECK_EQ(voice.Trigger({pcm, 0, 44100}), RamVoiceStatus::InvalidSample);
    CHECK_EQ(voice.Trigger({pcm, 5, 0}), RamVoiceStatus::InvalidSample);
    CHECK_EQ(voice.Trigger({pcm, 5, 48000}), RamVoiceStatus::SampleRateMismatch);
    const auto maximum = std::numeric_limits<std::size_t>::max();
    CHECK_EQ(voice.Trigger({pcm, maximum, 44100}), RamVoiceStatus::RangeOverflow);
    CHECK_EQ(voice.Render(nullptr, 1), RamVoiceStatus::InvalidDestination);
    CHECK_EQ(voice.Render(output, maximum), RamVoiceStatus::RangeOverflow);
    CHECK_EQ(voice.Render(nullptr, 0), RamVoiceStatus::Success);
    CHECK_EQ(voice.Render(output, 0), RamVoiceStatus::Success);
    CheckOutput(output, original, 3);
    CHECK(voice.IsActive());
    CHECK_EQ(voice.GetFramePosition(), 1);
    CHECK_EQ(voice.GetOutputSampleRate(), 44100);
    CHECK_EQ(voice.Render(output, 3), RamVoiceStatus::Success);
    CheckOutput(output, pcm + 1, 3); // Original view survived every invalid call.
    CHECK_EQ(voice.Configure(48000), RamVoiceStatus::Success);
    CHECK(!voice.IsActive());
    CHECK_EQ(voice.Trigger(sample), RamVoiceStatus::SampleRateMismatch);
    CHECK_EQ(voice.Trigger({pcm, 5, 48000}), RamVoiceStatus::Success);
    CHECK_EQ(voice.Render(output, 3), RamVoiceStatus::Success);
    CheckOutput(output, pcm, 3);
}

TEST_CASE(RamVoice_SplitSpanTriggerAndStopOffsets)
{
    for (const std::size_t offset : {0u, 1u, 3u, 8u})
    {
        RamSampleVoice voice;
        CHECK_EQ(voice.Configure(44100), RamVoiceStatus::Success);
        std::int16_t block[8];
        CHECK_EQ(voice.Render(block, offset), RamVoiceStatus::Success);
        CHECK_EQ(voice.Trigger(sample), RamVoiceStatus::Success);
        CHECK_EQ(voice.Render(block + offset, 8 - offset), RamVoiceStatus::Success);
        for (std::size_t i = 0; i < 8; ++i)
            CHECK_EQ(block[i], i >= offset && i - offset < 5 ? pcm[i - offset] : 0);
        if (offset == 8)
        {
            CHECK_EQ(voice.Render(block, 1), RamVoiceStatus::Success);
            CHECK_EQ(block[0], pcm[0]); // Trigger at block end affects next block.
        }
        // A longer sample stays active throughout the prefix, including offset 8.
        const std::int16_t long_pcm[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
        CHECK_EQ(voice.Trigger({long_pcm, 9, 44100}), RamVoiceStatus::Success);
        CHECK_EQ(voice.Render(block, offset), RamVoiceStatus::Success);
        voice.Stop();
        CHECK_EQ(voice.Render(block + offset, 8 - offset), RamVoiceStatus::Success);
        for (std::size_t i = 0; i < 8; ++i) CHECK_EQ(block[i], i < offset ? long_pcm[i] : 0);
        CHECK_EQ(voice.Render(block, 1), RamVoiceStatus::Success);
        CHECK_EQ(block[0], 0);
    }
}

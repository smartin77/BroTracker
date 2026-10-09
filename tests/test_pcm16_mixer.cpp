#include "test_framework.h"
#include "core/audio/pcm16_mixer.h"
#include "core/audio/ram_voice_block_renderer.h"
#include <array>
#include <vector>

using namespace BroTracker;
namespace
{
    struct Fixture
    {
        std::int16_t data[8][4]{};
        Pcm16MixInputs inputs;
        Fixture() { for (std::size_t c = 0; c < 8; ++c) inputs.channels[c] = data[c]; }
    };
}

TEST_CASE(PcmMix_SilencePassthroughAndInputPreservation)
{
    Fixture fixture;
    std::int16_t output[4] = {1,2,3,4};
    CHECK_EQ(MixPcm16Mono(fixture.inputs,output,4), Pcm16MixStatus::Success);
    for (auto value : output) CHECK_EQ(value,0);
    const std::int16_t expected[] = {-32768,32767,-123,456};
    for (std::size_t i = 0; i < 4; ++i) fixture.data[3][i] = expected[i];
    CHECK_EQ(MixPcm16Mono(fixture.inputs,output,4), Pcm16MixStatus::Success);
    for (std::size_t i = 0; i < 4; ++i) CHECK_EQ(output[i],expected[i]);
    for (std::size_t c = 0; c < 8; ++c)
        for (std::size_t i = 0; i < 4; ++i) CHECK_EQ(fixture.data[c][i],c == 3 ? expected[i] : 0);
}

TEST_CASE(PcmMix_SaturationCancellationAndChannelOrder)
{
    Fixture fixture;
    for (std::size_t c = 0; c < 8; ++c)
    {
        fixture.data[c][0] = static_cast<std::int16_t>(c + 1);
        fixture.data[c][1] = 32767;
        fixture.data[c][2] = -32768;
        fixture.data[c][3] = c < 4 ? 32767 : -32768;
    }
    std::int16_t output[4];
    CHECK_EQ(MixPcm16Mono(fixture.inputs,output,4), Pcm16MixStatus::Success);
    CHECK_EQ(output[0],36); CHECK_EQ(output[1],32767);
    CHECK_EQ(output[2],-32768); CHECK_EQ(output[3],-4);
    // Rotate order: intermediate sums exceed PCM16 range in both polarities.
    for (std::size_t rotation = 0; rotation < 8; ++rotation)
    {
        Pcm16MixInputs reordered;
        for (std::size_t c = 0; c < 8; ++c) reordered.channels[c] = fixture.data[(c + rotation) % 8];
        std::int16_t actual[4];
        CHECK_EQ(MixPcm16Mono(reordered,actual,4), Pcm16MixStatus::Success);
        for (std::size_t i = 0; i < 4; ++i) CHECK_EQ(actual[i],output[i]);
    }
    // Inputs may alias each other read-only.
    const std::int16_t shared[] = {10,-10,4000,-4000};
    for (auto& channel : fixture.inputs.channels) channel = shared;
    CHECK_EQ(MixPcm16Mono(fixture.inputs,output,4), Pcm16MixStatus::Success);
    const std::int16_t expected[] = {80,-80,32000,-32000};
    for (std::size_t i = 0; i < 4; ++i) CHECK_EQ(output[i],expected[i]);
}

TEST_CASE(PcmMix_ValidationAndZeroFrames)
{
    Fixture fixture;
    std::int16_t output[] = {11,22,33,44};
    CHECK_EQ(MixPcm16Mono({},nullptr,0), Pcm16MixStatus::Success);
    CHECK_EQ(MixPcm16Mono({},output,0), Pcm16MixStatus::Success);
    CHECK_EQ(MixPcm16Mono(fixture.inputs,nullptr,4), Pcm16MixStatus::InvalidDestination);
    for (std::size_t c = 0; c < 8; ++c)
    {
        auto invalid = fixture.inputs; invalid.channels[c] = nullptr;
        CHECK_EQ(MixPcm16Mono(invalid,output,4), Pcm16MixStatus::InvalidInput);
    }
    CHECK_EQ(MixPcm16Mono(fixture.inputs,output,UINT64_MAX), Pcm16MixStatus::RangeOverflow);
    const auto oversized = static_cast<std::uint64_t>(SIZE_MAX / sizeof(std::int16_t)) + 1;
    CHECK_EQ(MixPcm16Mono(fixture.inputs,output,oversized), Pcm16MixStatus::RangeOverflow);
    const std::int16_t original[] = {11,22,33,44};
    for (std::size_t i = 0; i < 4; ++i) CHECK_EQ(output[i],original[i]);
    for (const auto& channel : fixture.data) for (auto value : channel) CHECK_EQ(value,0);
}

TEST_CASE(PcmMix_RendererCompositionAcrossPartitions)
{
    const std::int16_t a[] = {100,200,300,400,500,600};
    const std::int16_t b[] = {-10,-20,-30,-40};
    const Pcm16MonoView sample_a{a,6,44100}, sample_b{b,4,44100};
    // Absolute offsets in this test fixture only, converted to local block offsets.
    const RamVoiceCommand sequence[] = {
        {0,0,RamVoiceAction::Trigger,sample_a}, {1,0,RamVoiceAction::Trigger,sample_b},
        {0,3,RamVoiceAction::Trigger,sample_a}, {1,4,RamVoiceAction::Stop,{}},
        {1,6,RamVoiceAction::Trigger,sample_b}, {0,8,RamVoiceAction::Stop,{}}
    };
    const auto collect = [&](const std::vector<std::size_t>& partitions) {
        RamVoiceBlockRenderer renderer;
        CHECK_EQ(renderer.Configure(44100),RamBlockStatus::Success);
        const RamVoiceCommandBatch empty_batch{};
        CHECK_EQ(renderer.Render({},0,empty_batch),RamBlockStatus::Success);
        std::vector<std::int16_t> mixed(12);
        std::size_t start = 0;
        for (const auto count : partitions)
        {
            std::array<std::vector<std::int16_t>,8> channels;
            RamVoiceOutputs outputs; Pcm16MixInputs inputs;
            for (std::size_t c = 0; c < 8; ++c)
            {
                channels[c].resize(count);
                outputs.channels[c] = channels[c].data(); inputs.channels[c] = channels[c].data();
            }
            RamVoiceCommandBatch batch;
            for (const auto& command : sequence)
                if (command.sample_offset >= start && command.sample_offset < start + count)
                {
                    auto local = command; local.sample_offset -= start;
                    batch.commands[batch.count++] = local;
                }
            CHECK_EQ(renderer.Render(outputs,count,batch),RamBlockStatus::Success);
            CHECK_EQ(MixPcm16Mono(inputs,mixed.data() + start,count),Pcm16MixStatus::Success);
            start += count;
        }
        CHECK_EQ(start,12);
        return mixed;
    };
    const auto regular = collect({3,3,3,3}), irregular = collect({1,4,2,5});
    const std::vector<std::int16_t> expected = {90,180,270,60,200,300,390,480,-30,-40,0,0};
    CHECK(regular == expected);
    CHECK(irregular == expected);
}

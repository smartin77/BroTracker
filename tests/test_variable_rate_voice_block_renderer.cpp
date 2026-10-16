#include "test_framework.h"
#include "core/audio/variable_rate_voice_block_renderer.h"
#include <array>
#include <algorithm>
#include <vector>

using namespace BroTracker;
namespace
{
    constexpr std::int16_t pcm[] = {10, 20, 30, 40, 50, 60, 70, 80};
    constexpr std::int16_t other[] = {-1, -2, -3};
    const Pcm16MonoView sample{pcm, 8, 44100}, replacement{other, 3, 44100};
    struct Outputs
    {
        std::int16_t data[8][8]{};
        VariableRateVoiceOutputs view;
        Outputs() { for (std::size_t i = 0; i < 8; ++i) view.channels[i] = data[i]; }
    };
    VariableRateVoiceCommand Trigger(std::uint32_t channel, std::uint64_t offset, Pcm16MonoView view = sample)
    { return {channel, offset, VariableRateVoiceAction::Trigger, view}; }
    VariableRateVoiceCommand Stop(std::uint32_t channel, std::uint64_t offset)
    { return {channel, offset, VariableRateVoiceAction::Stop, {}}; }
    void Check(const std::int16_t* data, std::initializer_list<std::int16_t> expected)
    { std::size_t i = 0; for (auto value : expected) CHECK_EQ(data[i++], value); }
}

TEST_CASE(VariableBlock_ExactOffsetsSimultaneousAndInputOrder)
{
    VariableRateVoiceBlockRenderer renderer;
    Outputs out;
    CHECK_EQ(renderer.Configure(44100), VariableRateBlockStatus::Success);
    VariableRateVoiceCommandBatch batch;
    batch.count = 6;
    batch.commands[0] = Trigger(0, 0);
    batch.commands[1] = Trigger(1, 2);
    batch.commands[2] = Stop(0, 4);
    batch.commands[3] = Trigger(1, 4, replacement);
    batch.commands[4] = Stop(2, 4);
    batch.commands[5] = Trigger(2, 4);
    CHECK_EQ(renderer.Render(out.view, 8, batch), VariableRateBlockStatus::Success);
    Check(out.data[0], {10,20,30,40,0,0,0,0});
    Check(out.data[1], {0,0,10,20,-1,-2,-3,0});
    Check(out.data[2], {0,0,0,0,10,20,30,40});
    for (std::size_t c = 3; c < 8; ++c) Check(out.data[c], {0,0,0,0,0,0,0,0});
    batch.count = 3;
    batch.commands[0] = Trigger(0, 0);
    batch.commands[1] = Stop(0, 0);
    batch.commands[2] = Trigger(2, 0); // Same-sample retrigger.
    CHECK_EQ(renderer.Render(out.view, 8, batch), VariableRateBlockStatus::Success);
    Check(out.data[0], {0,0,0,0,0,0,0,0});
    Check(out.data[2], {10,20,30,40,50,60,70,80});
    CHECK(!renderer.GetVoice(2)->IsActive());
}

TEST_CASE(VariableBlock_ContinuationBoundaryResetAndCapacity)
{
    const VariableRateVoiceCommandBatch empty_batch{};
    VariableRateVoiceBlockRenderer renderer;
    CHECK_EQ(renderer.Configure(44100), VariableRateBlockStatus::Success);
    Outputs out;
    VariableRateVoiceCommandBatch batch;
    batch.count = 1; batch.commands[0] = Trigger(0, 6);
    CHECK_EQ(renderer.Render(out.view, 8, batch), VariableRateBlockStatus::Success);
    Check(out.data[0], {0,0,0,0,0,0,10,20});
    CHECK_EQ(renderer.Render(out.view, 8, empty_batch), VariableRateBlockStatus::Success);
    Check(out.data[0], {30,40,50,60,70,80,0,0});
    batch.commands[0] = Trigger(0, 8);
    CHECK_EQ(renderer.Render(out.view, 8, batch), VariableRateBlockStatus::InvalidOffset);
    batch.commands[0] = Trigger(0, 0);
    CHECK_EQ(renderer.Render(out.view, 8, batch), VariableRateBlockStatus::Success);
    Check(out.data[0], {10,20,30,40,50,60,70,80});
    batch.count = kVariableRateVoiceCommandCapacity;
    for (std::size_t i = 0; i < batch.count; ++i) batch.commands[i] = Trigger(0, 7);
    CHECK_EQ(renderer.Render(out.view, 8, batch), VariableRateBlockStatus::Success);
    Check(out.data[0], {0,0,0,0,0,0,0,10});
    renderer.Reset();
    CHECK_EQ(renderer.Render(out.view, 8, empty_batch), VariableRateBlockStatus::Success);
    Check(out.data[0], {0,0,0,0,0,0,0,0});
    CHECK_EQ(renderer.Configure(48000), VariableRateBlockStatus::Success);
    CHECK_EQ(renderer.GetVoice(0)->GetOutputSampleRate(), 48000);
    CHECK(renderer.GetVoice(8) == nullptr);
}

TEST_CASE(VariableBlock_RejectionsPreserveAllVoicesAndOutput)
{
    const VariableRateVoiceCommandBatch empty_batch{};
    VariableRateVoiceBlockRenderer renderer;
    Outputs out;
    CHECK_EQ(renderer.Render(out.view, 8, empty_batch), VariableRateBlockStatus::NotConfigured);
    CHECK_EQ(renderer.Configure(44100), VariableRateBlockStatus::Success);
    VariableRateVoiceCommandBatch seed;
    seed.count = 8;
    for (std::size_t c = 0; c < 8; ++c) seed.commands[c] = Trigger(c, 7);
    CHECK_EQ(renderer.Render(out.view, 8, seed), VariableRateBlockStatus::Success);
    CHECK_EQ(renderer.Configure(0), VariableRateBlockStatus::InvalidSampleRate);
    const auto reject = [&](VariableRateVoiceCommandBatch batch, VariableRateBlockStatus expected,
                            std::uint64_t frames = 8, int null_channel = -1) {
        for (auto& channel : out.data) for (auto& value : channel) value = 123;
        auto view = out.view;
        if (null_channel >= 0) view.channels[null_channel] = nullptr;
        CHECK_EQ(renderer.Render(view, frames, batch), expected);
        for (std::size_t c = 0; c < 8; ++c)
        {
            CHECK(renderer.GetVoice(c)->IsActive());
            CHECK_EQ(renderer.GetVoice(c)->GetFramePosition(), 1);
            CHECK_EQ(renderer.GetVoice(c)->GetOutputSampleRate(), 44100);
            for (auto value : out.data[c]) CHECK_EQ(value, 123);
        }
    };
    VariableRateVoiceCommandBatch batch;
    batch.count = 2; batch.commands[0] = Stop(0, 0); batch.commands[1] = Trigger(1, 2);
    batch.commands[1].channel = 8; reject(batch, VariableRateBlockStatus::InvalidChannel);
    batch.commands[1] = Trigger(1, 2); batch.commands[1].action = static_cast<VariableRateVoiceAction>(99);
    reject(batch, VariableRateBlockStatus::InvalidAction);
    batch.commands[1] = Trigger(1, 8); reject(batch, VariableRateBlockStatus::InvalidOffset);
    batch.commands[1].sample_offset = UINT64_MAX; reject(batch, VariableRateBlockStatus::InvalidOffset);
    batch.commands[0] = Stop(0, 3); batch.commands[1] = Trigger(1, 2);
    reject(batch, VariableRateBlockStatus::UnorderedOffsets);
    batch.commands[0] = Stop(0, 0);
    for (const auto invalid : {Pcm16MonoView{nullptr,8,44100}, Pcm16MonoView{pcm,0,44100},
                              Pcm16MonoView{pcm,8,0}})
    { batch.commands[1] = Trigger(1, 2, invalid); reject(batch, VariableRateBlockStatus::InvalidSample); }
    batch.commands[1] = Trigger(1, 2, {pcm,8,48000}); reject(batch, VariableRateBlockStatus::SampleRateMismatch);
    batch.commands[1] = Trigger(1, 2, {pcm, SIZE_MAX,44100}); reject(batch, VariableRateBlockStatus::RangeOverflow);
    batch.commands[1] = Trigger(1, 2);
    batch.commands[1].step_q32_32 = 0; reject(batch, VariableRateBlockStatus::InvalidStep);
    batch.commands[1] = Trigger(1, 2);
    reject(batch, VariableRateBlockStatus::InvalidDestination, 8, 7);
    reject(batch, VariableRateBlockStatus::RangeOverflow, UINT64_MAX);
    reject(batch, VariableRateBlockStatus::InvalidOffset, 0);
    batch.count = 17; reject(batch, VariableRateBlockStatus::InvalidCommandCount);
    CHECK_EQ(renderer.Render({}, 0, empty_batch), VariableRateBlockStatus::Success);
    CHECK_EQ(renderer.GetVoice(0)->GetFramePosition(), 1);
    CHECK_EQ(renderer.Render(out.view, 8, empty_batch), VariableRateBlockStatus::Success);
    for (auto& channel : out.data) Check(channel, {20,30,40,50,60,70,80,0});
}


TEST_CASE(VariableBlockFractionalCommandsAndPartitionIndependence) {
    const std::int16_t short_pcm[] = {10,20,30,40};
    const Pcm16MonoView short_sample{short_pcm,4,44100};
    const VariableRateVoiceCommand events[] = {
        {0,0,VariableRateVoiceAction::Trigger,short_sample,kSourceFrameStepUnity/2},
        {1,2,VariableRateVoiceAction::Trigger,short_sample,kSourceFrameStepUnity*3/2},
        {0,4,VariableRateVoiceAction::Trigger,short_sample,kSourceFrameStepUnity*2},
        {0,7,VariableRateVoiceAction::Trigger,replacement,kSourceFrameStepUnity/2},
        {0,10,VariableRateVoiceAction::Stop,{},0},
        {0,20,VariableRateVoiceAction::Trigger,short_sample,kSourceFrameStepUnity}
    };
    const auto collect = [&](bool irregular) {
        VariableRateVoiceBlockRenderer renderer;
        CHECK_EQ(renderer.Configure(44100),VariableRateBlockStatus::Success);
        std::array<std::array<std::int16_t,24>,8> result{};
        std::size_t start=0,part=0;
        while (start<24) {
            const std::size_t requested=irregular ? (part++%3==0 ? 1 : 5) : 4;
            const auto count=std::min(requested,24-start);
            VariableRateVoiceCommandBatch batch{};
            for(const auto& event:events) if(event.sample_offset>=start && event.sample_offset<start+count) {
                CHECK(batch.count<kVariableRateVoiceCommandCapacity);
                auto command=event; command.sample_offset-=start;
                batch.commands[batch.count++]=command;
            }
            VariableRateVoiceOutputs output;
            for(std::size_t c=0;c<8;++c) output.channels[c]=result[c].data()+start;
            CHECK_EQ(renderer.Render(output,count,batch),VariableRateBlockStatus::Success);
            start+=count;
        }
        return result;
    };
    const auto regular=collect(false),irregular=collect(true);
    const std::int16_t expected0[]={10,15,20,25,10,30,0,-1,-2,-2,0,0,0,0,0,0,0,0,0,0,10,20,30,40};
    const std::int16_t expected1[]={0,0,10,25,40,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    for(std::size_t c=0;c<8;++c) for(std::size_t i=0;i<24;++i) {
        CHECK_EQ(regular[c][i],irregular[c][i]);
        CHECK_EQ(regular[c][i],c==0 ? expected0[i] : c==1 ? expected1[i] : 0);
    }
}
TEST_CASE(VariableBlockFractionalRollbackRetainsPhaseAndStep) {
    VariableRateVoiceBlockRenderer renderer;
    CHECK_EQ(renderer.Configure(44100),VariableRateBlockStatus::Success);
    Outputs out;
    VariableRateVoiceCommandBatch batch{}; batch.count=8;
    for(unsigned c=0;c<8;++c) {
        batch.commands[c]=Trigger(c,0);
        batch.commands[c].step_q32_32=kSourceFrameStepUnity/2;
    }
    CHECK_EQ(renderer.Render(out.view,1,batch),VariableRateBlockStatus::Success);
    auto reference=renderer;
    batch.count=2; batch.commands[0]=Stop(0,0); batch.commands[1]=Trigger(7,1);
    batch.commands[1].step_q32_32=0;
    for(auto& channel:out.data) for(auto& value:channel) value=999;
    CHECK_EQ(renderer.Render(out.view,8,batch),VariableRateBlockStatus::InvalidStep);
    for(unsigned c=0;c<8;++c) {
        CHECK_EQ(renderer.GetVoice(c)->GetFractionalPhase(),UINT32_C(2147483648));
        CHECK_EQ(renderer.GetVoice(c)->GetFramePosition(),0);
        CHECK_EQ(renderer.GetVoice(c)->GetStep(),kSourceFrameStepUnity/2);
        for(auto value:out.data[c]) CHECK_EQ(value,999);
    }
    const VariableRateVoiceCommandBatch empty_batch{};
    Outputs expected;
    CHECK_EQ(renderer.Render(out.view,8,empty_batch),VariableRateBlockStatus::Success);
    CHECK_EQ(reference.Render(expected.view,8,empty_batch),VariableRateBlockStatus::Success);
    for(unsigned c=0;c<8;++c) for(unsigned i=0;i<8;++i) CHECK_EQ(out.data[c][i],expected.data[c][i]);
}

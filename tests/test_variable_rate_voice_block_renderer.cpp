#include "test_framework.h"
#include "core/audio/variable_rate_voice_block_renderer.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace BroTracker;
namespace {
struct MixedReference {
    static constexpr unsigned Frames=128;
    VariableRateMixedVoiceCommandBatch mixed;
    VariableRateVoiceCommandBatch left,right;
    void Add(unsigned channel,unsigned offset,VariableRateVoiceAction action) {
        const auto i=mixed.count++;
        ++left.count; ++right.count;
        mixed.commands[i]={channel,offset,action,{}};
        left.commands[i]=right.commands[i]={channel,offset,action,{}};
    }
    void AddMono(unsigned channel,unsigned offset,Pcm16MonoView pcm) {
        Add(channel,offset,VariableRateVoiceAction::Trigger);
        SetSample(Pcm16MixedSampleView(pcm),pcm,pcm,100);
    }
    void AddStereo(unsigned channel,unsigned offset,Pcm16StereoView pcm,
        Pcm16MonoView l,Pcm16MonoView r,std::uint8_t level=100) {
        Add(channel,offset,VariableRateVoiceAction::Trigger);
        SetSample(Pcm16MixedSampleView(pcm),l,r,level);
    }
    void SetSample(Pcm16MixedSampleView view,Pcm16MonoView l,Pcm16MonoView r,std::uint8_t level) {
        auto& command=mixed.commands[mixed.count-1];
        command.sample=view;
        command.sample_level_percent=level;
        left.commands[left.count-1].sample=l;
        right.commands[right.count-1].sample=r;
        left.commands[left.count-1].sample_level_percent=level;
        right.commands[right.count-1].sample_level_percent=level;
    }
    // Render the original mono path independently for each side, then mix by
    // calculating each bus directly with its own exact binary Q16 scale.
    struct MonoBus {
        VariableRateVoiceBlockRenderer renderer;
        std::int16_t channels[kRealtimePatternChannelCapacity][Frames]{};
        MonoBus() { CHECK_EQ(renderer.Configure(44100),VariableRateBlockStatus::Success); }
        void Render(const VariableRateVoiceCommandBatch& commands) {
            VariableRateVoiceOutputs out;
            for(unsigned channel=0;channel<kRealtimePatternChannelCapacity;++channel)
                out.channels[channel]=channels[channel];
            CHECK_EQ(renderer.Render(out,Frames,commands),VariableRateBlockStatus::Success);
        }
        std::int16_t Mix(const Pcm16ChannelGains& gains,unsigned frame) const {
            double value=0;
            for(unsigned channel=0;channel<kRealtimePatternChannelCapacity;++channel)
                value+=channels[channel][frame]*(gains.channels[channel]/65536.0);
            return static_cast<std::int16_t>(std::clamp(std::lround(value),-32768L,32767L));
        }
    };
};
}

TEST_CASE(VariableStereoBlockMatchesIndependentBusesWithClippingAndTransitions) {
    constexpr auto frames=MixedReference::Frames;
    std::array<std::int16_t,frames> center{},sample_l{},sample_r{},other_l{},other_r{};
    std::array<std::int16_t,2*frames> sample_pair{},other_pair{};
    for(unsigned i=0;i<frames;++i) {
        center[i]=32767;
        sample_l[i]=32767;
        sample_r[i]=i<8 ? -32768 : -26000;
        other_l[i]=i<32 ? 32767 : 0;
        other_r[i]=i<8 ? -1000 : -32768;
        sample_pair[2*i]=sample_l[i];sample_pair[2*i+1]=sample_r[i];
        other_pair[2*i]=other_l[i];other_pair[2*i+1]=other_r[i];
    }
    const Pcm16StereoView stereo{sample_pair.data(),frames,44100},other{other_pair.data(),frames,44100};
    const Pcm16MonoView l{sample_l.data(),frames,44100},r{sample_r.data(),frames,44100};
    const Pcm16MonoView l2{other_l.data(),frames,44100},r2{other_r.data(),frames,44100};
    MixedReference commands;
    commands.AddMono(0,0,{center.data(),frames,44100});
    commands.AddStereo(1,0,stereo,l,r);
    commands.AddStereo(3,0,other,l2,r2);
    commands.Add(2,1,VariableRateVoiceAction::TriggerSynth);
    commands.AddStereo(4,1,stereo,l,r); // Zero channel gain still advances this voice.
    commands.Add(1,13,VariableRateVoiceAction::Release);
    commands.AddStereo(1,29,other,l2,r2,65); // Retrigger during release.
    commands.AddStereo(2,41,other,l2,r2,73); // Synth to stereo.
    commands.Add(3,51,VariableRateVoiceAction::TriggerSynth); // Stereo to Synth.
    commands.AddStereo(3,67,stereo,l,r); // Retrigger during asymmetric fade.
    commands.Add(1,76,VariableRateVoiceAction::Release);
    commands.Add(0,101,VariableRateVoiceAction::Stop);
    commands.AddStereo(5,103,other,l2,r2);
    commands.Add(3,109,VariableRateVoiceAction::TriggerMidi);
    Pcm16ChannelGains gains;
    gains.channels[2]=32768; gains.channels[4]=0; gains.channels[5]=65535;

    VariableRateVoiceBlockRenderer renderer;
    CHECK_EQ(renderer.Configure(44100),VariableRateBlockStatus::Success);
    VariableRateStereoMixScratch scratch;
    std::int16_t actual_l[frames],actual_r[frames];
    CHECK_EQ(renderer.RenderStereo(actual_l,actual_r,frames,commands.mixed,gains,scratch),VariableRateBlockStatus::Success);
    MixedReference::MonoBus ref_l,ref_r;
    ref_l.Render(commands.left);
    ref_r.Render(commands.right);
    CHECK_EQ(actual_l[0],32767);
    CHECK_EQ(actual_r[0],-1001); // Unclipped right when the left bus saturates.
    for(unsigned frame=0;frame<frames;++frame) {
        CHECK_EQ(actual_l[frame],ref_l.Mix(gains,frame));
        CHECK_EQ(actual_r[frame],ref_r.Mix(gains,frame));
    }
    CHECK_EQ(renderer.GetVoice(4)->GetFramePosition(),frames-1);

    CHECK_EQ(renderer.RenderStereo(actual_l,actual_r,frames,{},gains,scratch),VariableRateBlockStatus::Success);
    ref_l.Render({});
    ref_r.Render({});
    for(unsigned frame=0;frame<frames;++frame) {
        CHECK_EQ(actual_l[frame],ref_l.Mix(gains,frame));
        CHECK_EQ(actual_r[frame],ref_r.Mix(gains,frame));
    }
    CHECK(!renderer.GetVoice(1)->IsActive());
}

TEST_CASE(VariableStereoBlockRoundsSignedTiesAndSaturatesEachBus) {
    VariableRateVoiceBlockRenderer renderer;
    CHECK_EQ(renderer.Configure(44100),VariableRateBlockStatus::Success);
    VariableRateStereoMixScratch scratch;
    const std::int16_t pcm[]{1,-1,INT16_MAX,INT16_MIN,INT16_MIN,INT16_MAX};
    const Pcm16StereoView pair{pcm,3,44100};
    VariableRateMixedVoiceCommandBatch commands;
    commands.count=1;
    commands.commands[0]={0,0,VariableRateVoiceAction::Trigger,Pcm16MixedSampleView(pair)};
    Pcm16ChannelGains gains;
    gains.channels[0]=32768;
    std::int16_t left[3],right[3];
    CHECK_EQ(renderer.RenderStereo(left,right,3,commands,gains,scratch),VariableRateBlockStatus::Success);
    CHECK_EQ(left[0],1); CHECK_EQ(right[0],-1);
    commands.count=2;
    commands.commands[1]=commands.commands[0];
    commands.commands[1].channel=1;
    gains.channels[0]=kPcm16UnityGain;
    CHECK_EQ(renderer.RenderStereo(left,right,3,commands,gains,scratch),VariableRateBlockStatus::Success);
    CHECK_EQ(left[1],INT16_MAX); CHECK_EQ(right[1],INT16_MIN);
    CHECK_EQ(left[2],INT16_MIN); CHECK_EQ(right[2],INT16_MAX);
}

TEST_CASE(VariableStereoBlockCentersMonoAndSynthAndRejectsLateErrorsTransactionally) {
    VariableRateVoiceBlockRenderer renderer;
    CHECK_EQ(renderer.Configure(44100),VariableRateBlockStatus::Success);
    VariableRateStereoMixScratch scratch;
    MixedReference commands;
    const std::int16_t mono[]{10,20,30,40,50,60,70,80};
    commands.AddMono(0,3,{mono,8,44100});
    commands.Add(1,6,VariableRateVoiceAction::TriggerSynth);
    Pcm16ChannelGains gains;
    gains.channels[0]=32768;
    std::array<std::int16_t,32> left{},right{};
    CHECK_EQ(renderer.RenderStereo(left.data(),right.data(),32,commands.mixed,gains,scratch),VariableRateBlockStatus::Success);
    CHECK(left==right);
    CHECK_EQ(left[2],0); CHECK_EQ(left[3],5);
    CHECK(std::any_of(left.begin()+12,left.end(),[](auto sample){return sample!=0;}));

    const std::int16_t interleaved[]{1,0,-1,0,0,1,0,-1};
    MixedReference stereo;
    const Pcm16StereoView pair{interleaved,4,44100};
    stereo.AddStereo(2,0,pair,{},{});
    stereo.mixed.commands[0].step_q32_32=kSourceFrameStepUnity/2;
    CHECK_EQ(renderer.RenderStereo(left.data(),right.data(),1,stereo.mixed,gains,scratch),VariableRateBlockStatus::Success);
    auto control=renderer;
    const auto before=renderer.GetVoice(2)->StreamSnapshot();
    stereo.Add(2,8,VariableRateVoiceAction::Stop); // Invalid late offset after a valid trigger.
    left.fill(123); right.fill(-123);
    CHECK_EQ(renderer.RenderStereo(left.data(),right.data(),8,stereo.mixed,gains,scratch),VariableRateBlockStatus::InvalidOffset);
    CHECK(std::all_of(left.begin(),left.end(),[](auto sample){return sample==123;}));
    CHECK(std::all_of(right.begin(),right.end(),[](auto sample){return sample==-123;}));
    CHECK_EQ(renderer.GetVoice(2)->StreamSnapshot().generation,before.generation);
    CHECK_EQ(renderer.GetVoice(2)->GetFractionalPhase(),before.phase);
    auto invalid_gain=gains;
    invalid_gain.channels[7]=65537;
    CHECK_EQ(renderer.RenderStereo(left.data(),right.data(),8,{},invalid_gain,scratch),VariableRateBlockStatus::InvalidGain);
    CHECK_EQ(renderer.RenderStereo(left.data(),left.data(),8,{},gains,scratch),VariableRateBlockStatus::InvalidDestination);
    CHECK_EQ(renderer.RenderStereo(left.data(),right.data(),129,{},gains,scratch),VariableRateBlockStatus::RangeOverflow);
    std::array<std::int16_t,32> expected_l{},expected_r{};
    CHECK_EQ(control.RenderStereo(expected_l.data(),expected_r.data(),32,{},gains,scratch),VariableRateBlockStatus::Success);
    CHECK_EQ(renderer.RenderStereo(left.data(),right.data(),32,{},gains,scratch),VariableRateBlockStatus::Success);
    CHECK(left==expected_l);
    CHECK(right==expected_r);
    CHECK_EQ(renderer.GetVoice(2)->StreamSnapshot().generation,control.GetVoice(2)->StreamSnapshot().generation);
}
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

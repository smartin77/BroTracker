#include "test_framework.h"
#include "core/audio/variable_rate_sample_voice.h"
#include "core/audio/variable_rate_voice_block_renderer.h"
#include <array>
#include <cmath>
using namespace BroTracker;
namespace {
struct Fixture {
    std::array<std::int16_t,10000> pcm{};
    std::array<std::int16_t,kStreamStartFrames> start{};
    std::array<std::int16_t,kStreamRingFrames> ring{};
    StreamedVoiceBuffer buffers[8]{};
    StreamedSample source{start.data(),10000,buffers};
    Fixture() { for(unsigned i=0;i<pcm.size();++i) pcm[i]=std::int16_t((i*137)%60001-30000);
        for(unsigned i=0;i<start.size();++i) start[i]=pcm[i];
        for(auto& b:buffers) b.ring=ring.data(); }
    void Refill(const VariableRateSampleVoice& voice,unsigned channel=0) {
        const auto state=voice.StreamSnapshot(); auto& b=buffers[channel];
        if(b.generation!=state.generation || b.sample!=state.sample) { b.sample=state.sample; b.generation=state.generation; b.begin=b.end=kStreamStartFrames; }
        if(!state.sample) return;
        const auto consumed=std::max(state.position,kStreamStartFrames);
        const auto used=b.end>consumed?b.end-consumed:0;
        if(kStreamRingFrames-used<kStreamReadFrames || b.end>=source.frames) return;
        const auto at=std::max(b.end,consumed), count=std::min(kStreamReadFrames,source.frames-at);
        for(unsigned i=0;i<count;++i) ring[(at+i)%kStreamRingFrames]=pcm[at+i];
        CHECK(PublishStreamRead(b,state,at,count));
    }
};
}
TEST_CASE(StreamedVoiceMatchesRamAcrossStartRingWrapPitchAndEof) {
    for(auto step:{kSourceFrameStepUnity/2,kSourceFrameStepUnity,kSourceFrameStepUnity*3/2,kSourceFrameStepUnity*4}) {
        Fixture f; VariableRateSampleVoice streamed,ram;
        CHECK_EQ(streamed.Configure(44100),VariableVoiceStatus::Success); CHECK_EQ(ram.Configure(44100),VariableVoiceStatus::Success);
        CHECK_EQ(streamed.Trigger({f.start.data(),f.pcm.size(),44100,&f.source},step),VariableVoiceStatus::Success);
        CHECK_EQ(ram.Trigger({f.pcm.data(),f.pcm.size(),44100},step),VariableVoiceStatus::Success);
        for(unsigned block=0;block<170;++block) {
            f.Refill(streamed); std::int16_t a[128],b[128];
            CHECK_EQ(streamed.Render(a,128),VariableVoiceStatus::Success); CHECK_EQ(ram.Render(b,128),VariableVoiceStatus::Success);
            for(unsigned i=0;i<128;++i) CHECK_EQ(a[i],b[i]);
        }
        CHECK(!streamed.IsActive()); CHECK_EQ(streamed.StreamSnapshot().underruns,0u); CHECK_EQ(streamed.StreamSnapshot().eofs,1u);
    }
}
TEST_CASE(StreamedVoiceStaleRetriggerStopAndStarvation) {
    Fixture f; VariableRateSampleVoice v; CHECK_EQ(v.Configure(44100),VariableVoiceStatus::Success);
    const Pcm16MonoView sample{f.start.data(),f.pcm.size(),44100,&f.source};
    CHECK_EQ(v.Trigger(sample,kSourceFrameStepUnity),VariableVoiceStatus::Success); f.Refill(v);
    const auto old=v.StreamSnapshot(); auto& b=f.buffers[0];
    CHECK_EQ(v.Trigger(sample,kSourceFrameStepUnity),VariableVoiceStatus::Success); f.Refill(v);
    CHECK(!PublishStreamRead(b,old,4096,2048));
    std::int16_t output[128];
    // Existing ready data belongs to the previous generation after this retrigger.
    CHECK_EQ(v.Trigger(sample,kSourceFrameStepUnity),VariableVoiceStatus::Success);
    for(unsigned i=0;i<17;++i) CHECK_EQ(v.Render(output,128),VariableVoiceStatus::Success);
    CHECK_EQ(v.StreamSnapshot().underruns,128u); CHECK_EQ(v.StreamSnapshot().eofs,0u);
    for(auto value:output) CHECK_EQ(value,0);
    CHECK_EQ(v.GetFramePosition(),2176u); // advance through silence, never replay backlog
    v.Stop(); f.Refill(v); CHECK(!PublishStreamRead(b,old,4096,2048));
    CHECK_EQ(v.Render(output,128),VariableVoiceStatus::Success); for(auto value:output) CHECK_EQ(value,0);
}
TEST_CASE(StreamedShortSourceUsesCacheAndIndependentChannelGenerations) {
    Fixture f; f.source.frames=10;
    VariableRateVoiceBlockRenderer renderer; CHECK_EQ(renderer.Configure(44100),VariableRateBlockStatus::Success);
    std::int16_t pcm[8][128]{}; VariableRateVoiceOutputs out;
    for(unsigned c=0;c<8;++c) out.channels[c]=pcm[c];
    VariableRateVoiceCommandBatch commands; commands.count=8;
    for(unsigned c=0;c<8;++c) commands.commands[c]={c,c,VariableRateVoiceAction::Trigger,{f.start.data(),10,44100,&f.source}};
    CHECK_EQ(renderer.Render(out,128,commands),VariableRateBlockStatus::Success);
    for(unsigned c=0;c<8;++c) {
        for(unsigned i=0;i<c;++i) CHECK_EQ(pcm[c][i],0);
        for(unsigned i=0;i<10;++i) CHECK_EQ(pcm[c][c+i],f.pcm[i]);
        CHECK_EQ(renderer.GetVoice(c)->StreamSnapshot().eofs,1u); CHECK_EQ(renderer.GetVoice(c)->StreamSnapshot().underruns,0u);
    }
}

TEST_CASE(StreamedRefillPriorityAccountsForPitchCapacityAndEof) {
    Fixture f; StreamedVoiceSnapshot slow{&f.source,3,kSourceFrameStepUnity,2048};
    StreamedVoiceBuffer span{f.ring.data(),3,&f.source,2048,3840};
    const auto slow_time=StreamRefillUrgency(slow,span); CHECK_EQ(slow_time,1792u);
    auto fast=slow; fast.position=1024; fast.step=4*kSourceFrameStepUnity;
    const auto fast_time=StreamRefillUrgency(fast,span); CHECK_EQ(fast_time,704u); CHECK(fast_time<slow_time);
    span.end=6144; CHECK_EQ(StreamRefillUrgency(slow,span),UINT64_MAX); // no staging-sized free span
    span.end=f.source.frames; CHECK_EQ(StreamRefillUrgency(slow,span),UINT64_MAX); // final EOF drain
    slow.sample=nullptr; CHECK_EQ(StreamRefillUrgency(slow,span),UINT64_MAX);
}
TEST_CASE(StreamSourceGeometryUsesCompleteFramesWithoutEnablingOtherFormats) {
    const StreamBufferLayout mono16{{44100,1,16}}, stereo16{{44100,2,16}}, mono8{{44100,1,8}};
    const StreamBufferLayout stereo8{{44100,2,8}}, invalid{{44100,1,12}};
    CHECK_EQ(mono16.bytes_per_frame,2u);
    CHECK_EQ(mono16.start_frames,2048u);
    CHECK_EQ(stereo16.bytes_per_frame,4u);
    CHECK_EQ(stereo16.ring_frames,2048u);
    CHECK_EQ(stereo16.read_frames,1024u);
    CHECK_EQ(mono8.ring_frames,8192u);
    CHECK_EQ(stereo8.read_frames,2048u);
    CHECK_EQ(invalid.bytes_per_frame,0u);
    CHECK_EQ(invalid.ring_frames,0u);

    Fixture f;
    VariableRateSampleVoice voice;
    CHECK_EQ(voice.Configure(44100),VariableVoiceStatus::Success);
    const StreamedSample stereo{f.start.data(),f.source.frames,f.buffers,{44100,2,16}};
    const StreamedSample lower_rate{f.start.data(),f.source.frames,f.buffers,{22050,1,16}};
    for(const auto* source:{&stereo,&lower_rate})
        CHECK_EQ(voice.Trigger({source->start,source->frames,44100,source},kSourceFrameStepUnity),
            VariableVoiceStatus::InvalidSample);
}
TEST_CASE(StreamReadinessStopsAtGapOrStaleSpan) {
    Fixture f;
    StreamedVoiceSnapshot voice{&f.source,7,kSourceFrameStepUnity,1024};
    StreamedVoiceBuffer ready{f.ring.data(),7,&f.source,2304,3072};
    CHECK_EQ(StreamRefillUrgency(voice,ready),1024u); // only the prefix is reachable
    voice.position=2048;
    CHECK_EQ(StreamRefillUrgency(voice,ready),0u);
    voice.position=2304;
    CHECK_EQ(StreamRefillUrgency(voice,ready),768u);
    --ready.generation;
    CHECK_EQ(StreamReadyEnd(f.source,ready,voice.generation,voice.position),voice.position);
    CHECK_EQ(StreamRefillUrgency(voice,ready),UINT64_MAX);
}
TEST_CASE(StreamedCommandsKeepNoteOffOffsetAndResetGeneration) {
    Fixture f; VariableRateVoiceBlockRenderer renderer; CHECK_EQ(renderer.Configure(44100),VariableRateBlockStatus::Success);
    std::int16_t pcm[8][128]{}; VariableRateVoiceOutputs out; for(unsigned c=0;c<8;++c) out.channels[c]=pcm[c];
    VariableRateVoiceCommandBatch batch; batch.count=2;
    batch.commands[0]={0,7,VariableRateVoiceAction::Trigger,{f.start.data(),f.pcm.size(),44100,&f.source}};
    batch.commands[1]={0,27,VariableRateVoiceAction::Stop,{}};
    CHECK_EQ(renderer.Render(out,128,batch),VariableRateBlockStatus::Success);
    for(unsigned i=0;i<7;++i) CHECK_EQ(pcm[0][i],0);
    for(unsigned i=7;i<27;++i) CHECK_EQ(pcm[0][i],f.pcm[i-7]);
    for(unsigned i=27;i<128;++i) CHECK_EQ(pcm[0][i],0);
    const auto previous=renderer.GetVoice(0)->StreamSnapshot().generation;
    CHECK_EQ(renderer.Configure(44100),VariableRateBlockStatus::Success);
    CHECK(renderer.GetVoice(0)->StreamSnapshot().generation>previous);
    CHECK_EQ(renderer.GetVoice(0)->StreamSnapshot().eofs,0u);
}

namespace {
struct PublicationFixture {
    std::array<std::int16_t,kStreamRingFrames> ring{};
    StreamedSample source{nullptr,32768,nullptr};
    StreamedVoiceBuffer ready{ring.data(),7,&source,kStreamStartFrames,kStreamStartFrames};
    StreamedVoiceSnapshot request{&source,7};
    static std::int16_t Pcm(std::uint32_t frame) {
        return static_cast<std::int16_t>(static_cast<int>((frame*137)%60001)-30000);
    }
    bool Valid(std::uint32_t frame) const { return frame>=ready.begin && frame<ready.end; }
    void Publish(std::uint32_t at,std::uint32_t count=kStreamReadFrames) {
        // Same copy-then-publish order and modulo addressing as the SD producer.
        for(std::uint32_t i=0;i<count;++i) ring[(at+i)%kStreamRingFrames]=Pcm(at+i);
        CHECK(PublishStreamRead(ready,request,at,count));
    }
    void CheckRange(std::uint32_t begin,std::uint32_t end) const {
        CHECK_EQ(ready.begin,begin); CHECK_EQ(ready.end,end);
        CHECK(ready.begin<=ready.end); CHECK(ready.end-ready.begin<=kStreamRingFrames);
        CHECK(!Valid(begin-1)); CHECK(!Valid(end));
        for(auto frame=begin;frame<end;++frame) {
            CHECK(Valid(frame)); CHECK_EQ(ring[frame%kStreamRingFrames],Pcm(frame));
        }
    }
};
}
TEST_CASE(StreamedPublicationEvictsOverwrittenFramesAcrossMultipleWraps) {
    PublicationFixture f;
    for(std::uint32_t at=2048;at<=22528;at+=2048) {
        f.Publish(at);
        const auto end=at+2048,begin=std::max(2048u,end>4096 ? end-4096 : 0u);
        f.CheckRange(begin,end);
        for(std::uint32_t frame=2048;frame<begin;++frame) CHECK(!f.Valid(frame));
        if(begin>2048) CHECK(f.ring[(begin-1)%kStreamRingFrames]!=PublicationFixture::Pcm(begin-1));
        if(at==6144) { CHECK_EQ(f.ready.begin,4096u); CHECK_EQ(f.ready.end,8192u); }
    }
}
TEST_CASE(StreamedPublicationPreservesStarvationGapAcrossFurtherWraps) {
    PublicationFixture f;
    f.Publish(2048); f.Publish(4096); f.CheckRange(2048,6144);
    // A gap shorter than the ring must not be bridged by end-capacity clamping.
    f.Publish(7168); f.CheckRange(7168,9216);
    for(std::uint32_t at=9216;at<=19456;at+=2048) {
        f.Publish(at); f.CheckRange(std::max(7168u,at+2048-4096),at+2048);
        for(std::uint32_t frame=6144;frame<7168;++frame) CHECK(!f.Valid(frame));
    }
}
TEST_CASE(StreamedPublicationRejectsStaleGenerationAndDifferentSourceWithoutMutation) {
    PublicationFixture f;
    f.Publish(2048); f.Publish(4096); f.Publish(6144);
    const auto saved=f.ready; const auto pcm=f.ring;
    auto stale=f.request; --stale.generation;
    StreamedSample replacement=f.source;
    auto other=f.request; other.sample=&replacement;
    for(const auto& rejected:{stale,other}) {
        CHECK(!PublishStreamRead(f.ready,rejected,saved.end+1024,2048));
        CHECK_EQ(f.ready.ring,saved.ring); CHECK_EQ(f.ready.generation,saved.generation);
        CHECK_EQ(f.ready.sample,saved.sample); CHECK_EQ(f.ready.begin,saved.begin); CHECK_EQ(f.ready.end,saved.end);
        CHECK(f.ring==pcm); f.CheckRange(4096,8192);
    }
}

namespace {
struct StereoStreamFixture {
    static constexpr std::uint32_t kFrames=7139;
    std::array<std::int16_t,2*kFrames> pcm{};
    std::array<std::int16_t,kStreamRingBytes/sizeof(std::int16_t)> ring{};
    StreamedVoiceBuffer buffers[kStreamVoiceCount]{};
    StreamedSample source{pcm.data(),kFrames,buffers,{44100,2,16}};

    StereoStreamFixture() {
        for(std::uint32_t i=0;i<kFrames;++i) {
            pcm[2*i]=(i/257)%2 ? 0 : std::int16_t(int(i*173%65536)-32768);
            pcm[2*i+1]=(i/257)%2 ? std::int16_t(32767-int(i*237%65536)) : 0;
        }
        buffers[0].ring=ring.data();
    }
    Pcm16StereoView View() const { return {pcm.data(),source.frames,44100,&source}; }
    void StartSpan(const VariableRateSampleVoice& voice) {
        const auto state=voice.StreamSnapshot();
        buffers[0]={ring.data(),state.generation,&source,source.PrefixFrames(),source.PrefixFrames()};
    }
    void Publish(const StreamedVoiceSnapshot& state,std::uint32_t at,std::uint32_t count) {
        for(std::uint32_t i=0;i<count;++i) {
            const auto slot=(at+i)%source.layout.ring_frames;
            ring[2*slot]=pcm[2*(at+i)];
            ring[2*slot+1]=pcm[2*(at+i)+1];
        }
        CHECK(PublishStreamRead(buffers[0],state,at,count));
    }
    void Refill(const VariableRateSampleVoice& voice) {
        const auto state=voice.StreamSnapshot();
        const auto& ready=buffers[0];
        if(StreamRefillUrgency(state,ready)!=UINT64_MAX) {
            const auto at=ready.end;
            Publish(state,at,std::min<std::uint32_t>(source.layout.read_frames,source.frames-at));
        }
    }
    std::int16_t Reference(double at,unsigned side) const {
        if(at>=source.frames) return 0;
        const auto i=static_cast<std::uint32_t>(at);
        const auto j=std::min(i+1,source.frames-1);
        return static_cast<std::int16_t>(std::lround(pcm[2*i+side]+(pcm[2*j+side]-pcm[2*i+side])*(at-i)));
    }
};
}

TEST_CASE(StreamedStereoMatchesIndependentInterleavedReferenceThroughEof) {
    for(const double pitch:{0.5,1.0,1.5,2.0,4.0}) {
        StereoStreamFixture f;
        VariableRateSampleVoice voice;
        CHECK_EQ(voice.Configure(44100),VariableVoiceStatus::Success);
        CHECK_EQ(voice.TriggerStereo(f.View(),std::uint64_t(pitch*kSourceFrameStepUnity)),VariableVoiceStatus::Success);
        f.StartSpan(voice);
        unsigned output_frame=0;
        std::uint32_t expected_hash=2166136261u;
        while(output_frame*pitch<f.source.frames) {
            f.Refill(voice);
            std::int16_t left[79],right[79];
            CHECK_EQ(voice.RenderStereo(left,right,79),VariableVoiceStatus::Success);
            for(unsigned i=0;i<79;++i,++output_frame) {
                const double at=output_frame*pitch;
                CHECK_EQ(left[i],f.Reference(at,0));
                CHECK_EQ(right[i],f.Reference(at,1));
                if(at<f.source.frames) for(unsigned side=0;side<2;++side) {
                    const auto word=static_cast<std::uint16_t>(f.Reference(at,side));
                    expected_hash=(expected_hash^(word&255u))*16777619u;
                    expected_hash=(expected_hash^(word>>8))*16777619u;
                }
            }
        }
        CHECK(!voice.IsActive());
        CHECK_EQ(voice.StreamSnapshot().eofs,1u);
        CHECK_EQ(voice.StreamSnapshot().underruns,0u);
        CHECK_EQ(voice.StreamSnapshot().hash,expected_hash);
    }
}

TEST_CASE(StreamedStereoRequiresBothEndpointsAndFencesPreviousGenerations) {
    StereoStreamFixture f;
    VariableRateSampleVoice voice;
    CHECK_EQ(voice.Configure(44100),VariableVoiceStatus::Success);
    const auto step=kSourceFrameStepUnity/2;
    CHECK_EQ(voice.TriggerStereo(f.View(),step),VariableVoiceStatus::Success);
    f.StartSpan(voice);
    const auto old=voice.StreamSnapshot();
    f.Publish(old,1024,1024);
    CHECK_EQ(voice.TriggerStereo(f.View(),step),VariableVoiceStatus::Success);
    std::array<std::int16_t,2049> left{},right{};
    CHECK_EQ(voice.RenderStereo(left.data(),right.data(),left.size()),VariableVoiceStatus::Success);
    CHECK_EQ(left[2046],f.Reference(1023,0));
    CHECK_EQ(right[2046],f.Reference(1023,1));
    // A fractional read at 1023.5 cannot use just the valid prefix endpoint.
    for(unsigned i:{2047u,2048u}) { CHECK_EQ(left[i],0); CHECK_EQ(right[i],0); }
    CHECK_EQ(voice.StreamSnapshot().underruns,2u);
    CHECK_EQ(voice.GetFramePosition(),1024u);
    CHECK_EQ(voice.GetFractionalPhase(),UINT32_C(0x80000000));
    f.StartSpan(voice);
    CHECK(!PublishStreamRead(f.buffers[0],old,1024,1024));
    f.Publish(voice.StreamSnapshot(),1025,1024); // Gap at 1024, straddling the ring wrap.
    CHECK_EQ(voice.RenderStereo(left.data(),right.data(),3),VariableVoiceStatus::Success);
    CHECK_EQ(left[0],0); CHECK_EQ(right[0],0);
    for(unsigned i=1;i<3;++i) {
        CHECK_EQ(left[i],f.Reference(1024.5+i*0.5,0));
        CHECK_EQ(right[i],f.Reference(1024.5+i*0.5,1));
    }
    CHECK_EQ(voice.StreamSnapshot().underruns,3u);
}

TEST_CASE(StreamedStereoOneFrameClampsInterpolationAndRejectsMonoOutput) {
    StereoStreamFixture f;
    f.source.frames=1;
    f.pcm[0]=INT16_MIN; f.pcm[1]=INT16_MAX;
    VariableRateSampleVoice voice;
    CHECK_EQ(voice.Configure(44100),VariableVoiceStatus::Success);
    CHECK_EQ(voice.TriggerStereo(f.View(),kSourceFrameStepUnity/2),VariableVoiceStatus::Success);
    std::int16_t left[3]{17,17,17},right[3]{17,17,17};
    CHECK_EQ(voice.Render(left,3),VariableVoiceStatus::InvalidSample);
    CHECK_EQ(voice.RenderStereo(left,nullptr,3),VariableVoiceStatus::InvalidDestination);
    CHECK_EQ(left[0],17);
    CHECK_EQ(voice.GetFramePosition(),0u);
    CHECK_EQ(voice.GetFractionalPhase(),0u);
    CHECK_EQ(voice.RenderStereo(left,right,3),VariableVoiceStatus::Success);
    for(unsigned i=0;i<2;++i) { CHECK_EQ(left[i],INT16_MIN); CHECK_EQ(right[i],INT16_MAX); }
    CHECK_EQ(left[2],0); CHECK_EQ(right[2],0);
    CHECK_EQ(voice.StreamSnapshot().underruns,0u);
    CHECK_EQ(voice.StreamSnapshot().eofs,1u);
    CHECK_EQ(voice.TriggerStereo({left,3,22050},kSourceFrameStepUnity),VariableVoiceStatus::SampleRateMismatch);
    CHECK_EQ(voice.TriggerStereo({left,VariableRateSampleVoice::kMaximumFrameCount/2+1,44100},
        kSourceFrameStepUnity),VariableVoiceStatus::RangeOverflow);
}

TEST_CASE(StreamedStereoCanShareTheMixedBlockWithMono) {
    StereoStreamFixture f;
    VariableRateVoiceBlockRenderer renderer;
    CHECK_EQ(renderer.Configure(44100),VariableRateBlockStatus::Success);
    VariableRateStereoMixScratch scratch;
    const std::int16_t center[]{42,42,42,42,42,42,42,42};
    VariableRateMixedVoiceCommandBatch commands;
    commands.count=2;
    commands.commands[0]={0,1,VariableRateVoiceAction::Trigger,Pcm16MixedSampleView(f.View()),
        kSourceFrameStepUnity*3/2};
    commands.commands[1]={1,1,VariableRateVoiceAction::Trigger,Pcm16MixedSampleView(Pcm16MonoView{center,8,44100})};
    std::int16_t left[8],right[8];
    CHECK_EQ(renderer.RenderStereo(left,right,8,commands,{},scratch),VariableRateBlockStatus::Success);
    CHECK_EQ(left[0],0); CHECK_EQ(right[0],0);
    for(unsigned i=1;i<8;++i) {
        CHECK_EQ(left[i],42+f.Reference((i-1)*1.5,0));
        CHECK_EQ(right[i],42+f.Reference((i-1)*1.5,1));
    }
    const auto state=renderer.GetVoice(0)->StreamSnapshot();
    CHECK_EQ(state.sample,&f.source);
    CHECK_EQ(state.position,10u);
    CHECK_EQ(state.phase,UINT32_C(0x80000000));
    CHECK(f.buffers[0].sample==nullptr); // Render exposes a snapshot for the owner to publish.
}

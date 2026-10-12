#include "test_framework.h"
#include "core/audio/variable_rate_sample_voice.h"
#include <array>
using namespace BroTracker;
namespace {
void Same(const VariableRateSampleVoice& a,const VariableRateSampleVoice& b) {
    CHECK_EQ(a.IsActive(),b.IsActive()); CHECK_EQ(a.GetFramePosition(),b.GetFramePosition());
    CHECK_EQ(a.GetFractionalPhase(),b.GetFractionalPhase()); CHECK_EQ(a.GetStep(),b.GetStep());
    CHECK_EQ(a.GetOutputSampleRate(),b.GetOutputSampleRate());
}
}
TEST_CASE(VariableVoiceUnityMatchesNativeAndCompletion) {
    const std::int16_t pcm[]{-32768,32767,-1,0,123,-456}; Pcm16MonoView view{pcm,6,44100};
    VariableRateSampleVoice voice; RamSampleVoice native;
    std::int16_t out[10],reference[10];
    CHECK_EQ(voice.Render(out,10),VariableVoiceStatus::Success); for(auto value:out) CHECK_EQ(value,0);
    CHECK_EQ(voice.Configure(44100),VariableVoiceStatus::Success); CHECK_EQ(native.Configure(44100),RamVoiceStatus::Success);
    CHECK_EQ(voice.Trigger(view,kSourceFrameStepUnity),VariableVoiceStatus::Success); CHECK_EQ(native.Trigger(view),RamVoiceStatus::Success);
    CHECK_EQ(voice.Render(out,10),VariableVoiceStatus::Success); CHECK_EQ(native.Render(reference,10),RamVoiceStatus::Success);
    for(unsigned i=0;i<10;++i) CHECK_EQ(out[i],reference[i]);
    CHECK(!voice.IsActive()); CHECK_EQ(voice.GetFramePosition(),0u); CHECK_EQ(voice.GetStep(),0u);
    CHECK_EQ(voice.Render(out,10),VariableVoiceStatus::Success); for(auto value:out) CHECK_EQ(value,0);
}
TEST_CASE(VariableVoiceStepsSignedRoundingAndFinalEndpoint) {
    VariableRateSampleVoice voice; CHECK_EQ(voice.Configure(44100),VariableVoiceStatus::Success);
    const std::int16_t pcm[]{0,3,-3,2}; Pcm16MonoView view{pcm,4,44100}; std::int16_t out[10];
    CHECK_EQ(voice.Trigger(view,kSourceFrameStepUnity/2),VariableVoiceStatus::Success);
    CHECK_EQ(voice.Render(out,10),VariableVoiceStatus::Success);
    const std::int16_t half[]{0,2,3,0,-3,-1,2,2,0,0};
    for(unsigned i=0;i<10;++i) CHECK_EQ(out[i],half[i]);
    CHECK_EQ(voice.Trigger(view,2*kSourceFrameStepUnity),VariableVoiceStatus::Success);
    CHECK_EQ(voice.Render(out,4),VariableVoiceStatus::Success); CHECK_EQ(out[0],0); CHECK_EQ(out[1],-3); CHECK_EQ(out[2],0);
    CHECK_EQ(voice.Trigger(view,kSourceFrameStepUnity*3/4),VariableVoiceStatus::Success);
    CHECK_EQ(voice.Render(out,8),VariableVoiceStatus::Success);
    const std::int16_t fractional[]{0,2,0,-2,2,2,0,0};
    for(unsigned i=0;i<8;++i) CHECK_EQ(out[i],fractional[i]);
    const std::int16_t single=-32768;
    CHECK_EQ(voice.Trigger({&single,1,44100},kSourceFrameStepUnity/2),VariableVoiceStatus::Success);
    CHECK_EQ(voice.Render(out,3),VariableVoiceStatus::Success); CHECK_EQ(out[0],single); CHECK_EQ(out[1],single); CHECK_EQ(out[2],0);
    CHECK_EQ(voice.Trigger({&single,1,44100},UINT64_MAX),VariableVoiceStatus::Success);
    CHECK_EQ(voice.Render(out,3),VariableVoiceStatus::Success); CHECK_EQ(out[0],single); CHECK_EQ(out[1],0); CHECK(!voice.IsActive());
}
TEST_CASE(VariableVoiceRetriggerStopResetAndRejections) {
    VariableRateSampleVoice voice; const std::int16_t pcm[]{10,20,30}; Pcm16MonoView view{pcm,3,44100};
    CHECK_EQ(voice.Trigger(view,1),VariableVoiceStatus::NotConfigured);
    CHECK_EQ(voice.Configure(44100),VariableVoiceStatus::Success);
    CHECK_EQ(voice.Trigger(view,kSourceFrameStepUnity/2),VariableVoiceStatus::Success);
    std::int16_t out[2]; CHECK_EQ(voice.Render(out,1),VariableVoiceStatus::Success); const auto saved=voice;
    CHECK_EQ(voice.Configure(0),VariableVoiceStatus::InvalidSampleRate); Same(voice,saved);
    CHECK_EQ(voice.Trigger(view,0),VariableVoiceStatus::InvalidStep); Same(voice,saved);
    for(auto invalid:{Pcm16MonoView{nullptr,3,44100},Pcm16MonoView{pcm,0,44100},Pcm16MonoView{pcm,3,0}}) {
        CHECK_EQ(voice.Trigger(invalid,1),VariableVoiceStatus::InvalidSample); Same(voice,saved);
    }
    CHECK_EQ(voice.Trigger({pcm,3,48000},1),VariableVoiceStatus::SampleRateMismatch); Same(voice,saved);
    CHECK_EQ(voice.Trigger({pcm,SIZE_MAX,44100},1),VariableVoiceStatus::RangeOverflow); Same(voice,saved);
    out[0]=999; out[1]=999;
    CHECK_EQ(voice.Render(out,SIZE_MAX),VariableVoiceStatus::RangeOverflow); Same(voice,saved);
    CHECK_EQ(out[0],999); CHECK_EQ(out[1],999);
    CHECK_EQ(voice.Render(nullptr,1),VariableVoiceStatus::InvalidDestination); Same(voice,saved);
    CHECK_EQ(voice.Render(nullptr,0),VariableVoiceStatus::Success); Same(voice,saved);
    CHECK_EQ(voice.Trigger(view,kSourceFrameStepUnity),VariableVoiceStatus::Success);
    CHECK_EQ(voice.Render(out,1),VariableVoiceStatus::Success); CHECK_EQ(out[0],10);
    const std::int16_t replacement[]{-100,-200};
    CHECK_EQ(voice.Trigger({replacement,2,44100},kSourceFrameStepUnity),VariableVoiceStatus::Success);
    CHECK_EQ(voice.Render(out,1),VariableVoiceStatus::Success); CHECK_EQ(out[0],-100);
    voice.Stop(); CHECK(!voice.IsActive()); CHECK_EQ(voice.GetFractionalPhase(),0u);
    CHECK_EQ(voice.Render(out,2),VariableVoiceStatus::Success); CHECK_EQ(out[0],0); CHECK_EQ(out[1],0);
    CHECK_EQ(voice.Trigger(view,1),VariableVoiceStatus::Success); voice.Reset(); CHECK(!voice.IsActive()); CHECK_EQ(voice.GetOutputSampleRate(),44100u);
    CHECK_EQ(voice.Trigger(view,1),VariableVoiceStatus::Success); CHECK_EQ(voice.Configure(44100),VariableVoiceStatus::Success); CHECK(!voice.IsActive());
}
TEST_CASE(VariableVoiceExactPartitionIndependenceAndFractionCarry) {
    const std::int16_t pcm[]{-32768,32767,-301,456,10,9}; Pcm16MonoView view{pcm,6,44100};
    for(auto step:{std::uint64_t{1},kSourceFrameStepUnity,kSourceFrameStepUnity/2,kSourceFrameStepUnity/3,kSourceFrameStepUnity*5/4,UINT64_MAX}) {
        VariableRateSampleVoice whole,split; CHECK_EQ(whole.Configure(44100),VariableVoiceStatus::Success);
        CHECK_EQ(split.Configure(44100),VariableVoiceStatus::Success);
        CHECK_EQ(whole.Trigger(view,step),VariableVoiceStatus::Success); CHECK_EQ(split.Trigger(view,step),VariableVoiceStatus::Success);
        std::int16_t a[32],b[32]; CHECK_EQ(whole.Render(a,32),VariableVoiceStatus::Success);
        unsigned start=0; for(auto size:{1u,7u,3u,9u,12u}) { CHECK_EQ(split.Render(b+start,size),VariableVoiceStatus::Success); start+=size; }
        for(unsigned i=0;i<32;++i) { CHECK_EQ(a[i],b[i]); }
        Same(whole,split);
    }
}
namespace BroTracker {
struct VariableRateSampleVoiceTestAccess {
    static void CheckLargeArithmetic() {
        // Arithmetic-only metadata test, never dereference synthetic source extents.
        VariableRateSampleVoice voice; voice.sample_.frame_count=VariableRateSampleVoice::kMaximumFrameCount;
        if(voice.sample_.frame_count<=UINT32_MAX) return;
        voice.step_=UINT64_MAX; voice.Advance(); voice.Advance();
        CHECK(voice.position_>UINT32_MAX); CHECK_EQ(voice.phase_,UINT32_MAX-1);
        voice.position_=voice.sample_.frame_count-1; voice.Advance();
        CHECK_EQ(voice.position_,0u); CHECK_EQ(voice.phase_,0u); CHECK_EQ(voice.step_,0u);
    }
};
}
TEST_CASE(VariableVoiceFullPositionRangeWithoutPackedPhaseOverflow) {
    VariableRateSampleVoiceTestAccess::CheckLargeArithmetic();
}

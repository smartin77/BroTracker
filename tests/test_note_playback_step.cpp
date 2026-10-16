#include "test_framework.h"
#include "core/audio/note_playback_step.h"
#include <array>
using namespace BroTracker;
namespace {
// Independent fixed 512-bit integer reference, test-only. Verify that the exact
// equal-temperament root lies within half a Q32 LSB of the returned integer:
// (2*q-1)^12 < 2^(396+interval) < (2*q+1)^12. No generated-table reference,
// floating point, pow or approximation tolerance; 500 significant bits available.
struct Wide {
    std::array<std::uint32_t,16> limbs{};
    void Small(std::uint32_t value) {
        std::uint64_t carry=0;
        for(auto& limb:limbs) { const auto x=static_cast<std::uint64_t>(limb)*value+carry;
            limb=static_cast<std::uint32_t>(x); carry=x>>32; }
        CHECK_EQ(carry,0u);
    }
    void Multiply(std::uint64_t value) {
        auto upper=*this; upper.Small(static_cast<std::uint32_t>(value>>32));
        Small(static_cast<std::uint32_t>(value));
        CHECK_EQ(upper.limbs.back(),0u); std::uint64_t carry=0;
        for(unsigned i=1;i<16;++i) {
            const auto sum=static_cast<std::uint64_t>(limbs[i])+upper.limbs[i-1]+carry;
            limbs[i]=static_cast<std::uint32_t>(sum); carry=sum>>32;
        }
        CHECK_EQ(carry,0u);
    }
    bool Less(const Wide& other) const {
        for(unsigned i=16;i>0;) { --i; if(limbs[i]!=other.limbs[i]) return limbs[i]<other.limbs[i]; }
        return false;
    }
};
Wide Power12(std::uint64_t base) { Wide result; result.limbs[0]=1; for(unsigned i=0;i<12;++i) result.Multiply(base); return result; }
std::uint64_t Step(unsigned target,unsigned reference) {
    std::uint64_t value=0; CHECK_EQ(NoteToPlaybackStep(target,reference,value),NotePlaybackStepStatus::Success); return value;
}
}
TEST_CASE(NoteStepUnityOctavesDirectionAndFullRangeMonotonicity) {
    for(unsigned reference=24;reference<=127;++reference) {
        CHECK_EQ(Step(reference,reference),kSourceFrameStepUnity);
        std::uint64_t previous=0;
        for(unsigned target=24;target<=127;++target) {
            const auto value=Step(target,reference); CHECK(value>previous); previous=value;
            const int interval=static_cast<int>(target)-static_cast<int>(reference);
            if(interval%12==0) {
                const auto exact=interval>=0 ? kSourceFrameStepUnity<<(interval/12) : kSourceFrameStepUnity>>(-interval/12);
                CHECK_EQ(value,exact);
            }
        }
    }
    CHECK(Step(61,60)>kSourceFrameStepUnity); CHECK(Step(59,60)<kSourceFrameStepUnity);
    CHECK(Step(24,127)>0); CHECK(Step(127,24)<UINT64_MAX);
}
TEST_CASE(NoteStepExactHighPrecisionReferenceAcrossEveryInterval) {
    for(int interval=-103;interval<=103;++interval) {
        const unsigned reference=interval>=0?24:127;
        const auto value=Step(static_cast<unsigned>(static_cast<int>(reference)+interval),reference);
        Wide exact; const unsigned bit=static_cast<unsigned>(396+interval);
        exact.limbs[bit/32]=std::uint32_t{1}<<(bit%32);
        CHECK(Power12(2*value-1).Less(exact));
        CHECK(exact.Less(Power12(2*value+1)));
    }
}
TEST_CASE(NoteStepInvalidPitchPreservesOutput) {
    std::uint64_t output=123;
    for(auto invalid:{0u,23u,128u,254u,255u,256u,UINT32_MAX}) {
        CHECK_EQ(NoteToPlaybackStep(invalid,60,output),NotePlaybackStepStatus::InvalidTargetNote); CHECK_EQ(output,123u);
        CHECK_EQ(NoteToPlaybackStep(60,invalid,output),NotePlaybackStepStatus::InvalidReferenceNote); CHECK_EQ(output,123u);
    }
}
TEST_CASE(NoteStepOctaveVoiceCompositionAndPartitions) {
    const std::int16_t pcm[]{0,100,200,300}; const Pcm16MonoView view{pcm,4,44100};
    const std::int16_t expected[3][10]={{0,50,100,150,200,250,300,300,0,0},
        {0,100,200,300,0,0,0,0,0,0},{0,200,0,0,0,0,0,0,0,0}};
    unsigned index=0;
    for(auto target:{48u,60u,72u}) {
        VariableRateSampleVoice whole,split; const auto step=Step(target,60);
        CHECK_EQ(whole.Configure(44100),VariableVoiceStatus::Success); CHECK_EQ(split.Configure(44100),VariableVoiceStatus::Success);
        CHECK_EQ(whole.Trigger(view,step),VariableVoiceStatus::Success); CHECK_EQ(split.Trigger(view,step),VariableVoiceStatus::Success);
        std::int16_t a[10],b[10]; CHECK_EQ(whole.Render(a,10),VariableVoiceStatus::Success);
        unsigned start=0;
        for(auto count:{1u,2u,4u,3u}) { CHECK_EQ(split.Render(b+start,count),VariableVoiceStatus::Success); start+=count; }
        for(unsigned i=0;i<10;++i) { CHECK_EQ(a[i],expected[index][i]); CHECK_EQ(b[i],expected[index][i]); }
        CHECK(!whole.IsActive()); CHECK(!split.IsActive()); ++index;
    }
}

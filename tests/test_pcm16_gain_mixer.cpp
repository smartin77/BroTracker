#include "test_framework.h"
#include "core/audio/pcm16_gain_mixer.h"
#include <array>
using namespace BroTracker;
namespace {
struct GainFixture {
    std::int16_t data[8][8]{};
    Pcm16MixInputs inputs;
    GainFixture() { for(unsigned c=0;c<8;++c) inputs.channels[c]=data[c]; }
};
}
TEST_CASE(GainMixUnityEquivalenceAndPreservation) {
    GainFixture f; Pcm16ChannelGains gains;
    for(unsigned c=0;c<8;++c) {
        CHECK_EQ(gains.channels[c],kPcm16UnityGain);
        const std::int16_t values[]{-32768,32767,-1,1,100,-100,0,30000};
        for(unsigned i=0;i<8;++i) f.data[c][i]=i==7 && c>=4 ? -30000 : values[i];
    }
    const auto before=f;
    std::int16_t unity[8],actual[8];
    CHECK_EQ(MixPcm16Mono(f.inputs,unity,8),Pcm16MixStatus::Success);
    CHECK_EQ(MixPcm16MonoWithGains(f.inputs,gains,actual,8),Pcm16GainMixStatus::Success);
    for(unsigned i=0;i<8;++i) CHECK_EQ(actual[i],unity[i]);
    CHECK_EQ(actual[0],-32768); CHECK_EQ(actual[1],32767); CHECK_EQ(actual[7],0);
    for(unsigned c=0;c<8;++c) for(unsigned i=0;i<8;++i) CHECK_EQ(f.data[c][i],before.data[c][i]);
}
TEST_CASE(GainMixPartialAndSignedRoundingOnce) {
    GainFixture f; Pcm16ChannelGains gains;
    for(auto& gain:gains.channels) gain=0;
    gains.channels[0]=kPcm16UnityGain/4;
    const std::int16_t values[]{1,2,3,-1,-2,-3,32767,-32768};
    const std::int16_t expected[]{0,1,1,0,-1,-1,8192,-8192};
    for(unsigned i=0;i<8;++i) f.data[0][i]=values[i];
    std::int16_t actual[8];
    CHECK_EQ(MixPcm16MonoWithGains(f.inputs,gains,actual,8),Pcm16GainMixStatus::Success);
    for(unsigned i=0;i<8;++i) CHECK_EQ(actual[i],expected[i]);
    // Four quarter-sample contributions must accumulate before rounding.
    for(unsigned c=0;c<8;++c) {
        gains.channels[c]=c<4 ? kPcm16UnityGain/4 : 0;
        f.data[c][0]=1; f.data[c][1]=-1;
    }
    CHECK_EQ(MixPcm16MonoWithGains(f.inputs,gains,actual,2),Pcm16GainMixStatus::Success);
    CHECK_EQ(actual[0],1); CHECK_EQ(actual[1],-1);
    for(auto& gain:gains.channels) gain=0;
    CHECK_EQ(MixPcm16MonoWithGains(f.inputs,gains,actual,8),Pcm16GainMixStatus::Success);
    for(auto value:actual) CHECK_EQ(value,0);
}
TEST_CASE(GainMixCancellationOrderSaturationAndPartitions) {
    GainFixture f; Pcm16ChannelGains gains;
    for(unsigned c=0;c<8;++c) {
        gains.channels[c]=c%2 ? kPcm16UnityGain : kPcm16UnityGain/2;
        for(unsigned i=0;i<8;++i) f.data[c][i]=i==0 ? 32767 : i==1 ? -32768 :
            static_cast<std::int16_t>(c<4 ? 30001 : -30000);
    }
    std::int16_t reference[8];
    CHECK_EQ(MixPcm16MonoWithGains(f.inputs,gains,reference,8),Pcm16GainMixStatus::Success);
    CHECK_EQ(reference[0],32767); CHECK_EQ(reference[1],-32768); CHECK_EQ(reference[2],3);
    for(unsigned rotation=0;rotation<8;++rotation) {
        Pcm16MixInputs inputs; Pcm16ChannelGains reordered;
        for(unsigned c=0;c<8;++c) { inputs.channels[c]=f.data[(c+rotation)%8]; reordered.channels[c]=gains.channels[(c+rotation)%8]; }
        std::int16_t actual[8];
        CHECK_EQ(MixPcm16MonoWithGains(inputs,reordered,actual,8),Pcm16GainMixStatus::Success);
        for(unsigned i=0;i<8;++i) CHECK_EQ(actual[i],reference[i]);
    }
    for(const auto& partition: {std::array<unsigned,4>{2,2,2,2},std::array<unsigned,4>{1,3,1,3}}) {
        std::int16_t actual[8]; unsigned start=0;
        for(auto size:partition) {
            Pcm16MixInputs inputs; for(unsigned c=0;c<8;++c) inputs.channels[c]=f.data[c]+start;
            CHECK_EQ(MixPcm16MonoWithGains(inputs,gains,actual+start,size),Pcm16GainMixStatus::Success); start+=size;
        }
        for(unsigned i=0;i<8;++i) CHECK_EQ(actual[i],reference[i]);
    }
}
TEST_CASE(GainMixMetadataValidationAtomicity) {
    GainFixture f; Pcm16ChannelGains gains; std::int16_t output[]{111,222};
    const Pcm16MixInputs empty_inputs{};
    CHECK_EQ(MixPcm16MonoWithGains(empty_inputs,gains,nullptr,0),Pcm16GainMixStatus::Success);
    CHECK_EQ(MixPcm16MonoWithGains(f.inputs,gains,nullptr,2),Pcm16GainMixStatus::InvalidDestination);
    CHECK_EQ(MixPcm16MonoWithGains(f.inputs,gains,output,UINT64_MAX),Pcm16GainMixStatus::RangeOverflow);
    CHECK_EQ(MixPcm16MonoWithGains(f.inputs,gains,output,static_cast<std::uint64_t>(SIZE_MAX/2)+1),Pcm16GainMixStatus::RangeOverflow);
    for(unsigned c=0;c<8;++c) {
        auto invalid=gains; invalid.channels[c]=kPcm16UnityGain+1;
        CHECK_EQ(MixPcm16MonoWithGains(f.inputs,invalid,output,2),Pcm16GainMixStatus::InvalidGain);
        CHECK_EQ(MixPcm16MonoWithGains(empty_inputs,invalid,nullptr,0),Pcm16GainMixStatus::InvalidGain);
        auto inputs=f.inputs; inputs.channels[c]=nullptr;
        gains.channels[c]=0; // Even silent input metadata must be valid.
        CHECK_EQ(MixPcm16MonoWithGains(inputs,gains,output,2),Pcm16GainMixStatus::InvalidInput);
    }
    CHECK_EQ(output[0],111); CHECK_EQ(output[1],222);
}

#include "test_framework.h"
#include "bringup/device_facts.h"
#include "bringup/boot_identity.h"
#include "firmware/teensy/BroTracker/production_audio_output.h"
#include <string>
using namespace BroTracker;
namespace {
struct Source {
    unsigned begins=0,reads=0;
    BootIdentity::Read result=BootIdentity::Read::Ready;
    std::uint32_t word=1;
    void Begin(){++begins;}
    BootIdentity::Read ReadWords(std::uint32_t (&out)[4]) {
        ++reads;for(unsigned i=0;i<4;++i)out[i]=word;return result;
    }
};
DeviceFacts Facts(const char* boot="0123456789abcdef0123456789abcdef") {
    DeviceFacts facts;std::strcpy(facts.board,"12345670");std::strcpy(facts.boot,boot);return facts;
}
}
TEST_CASE(BootNonceIsOnceOnlyAndSimulatedRebootUsesNewEntropy) {
    Source source;BootIdentity boot;boot.Begin(source,10);boot.Poll(source,11);
    CHECK_EQ(boot.Status(),BootIdentity::State::Valid);
    CHECK(std::strcmp(boot.Text(),"00000001000000010000000100000001")==0);
    for(unsigned i=0;i<20;++i){boot.Begin(source,i);boot.Poll(source,i);}
    CHECK_EQ(source.begins,1u);CHECK_EQ(source.reads,1u);
    const auto before=Facts(boot.Text());
    CHECK_EQ(CompareDeviceBoot(before,Facts(boot.Text())),BootComparison::SameBoot);
    // New application object + distinct injected entropy, not a physical reboot.
    BootIdentity reboot;source.word=2;reboot.Begin(source,0);reboot.Poll(source,1);
    CHECK_EQ(CompareDeviceBoot(before,Facts(reboot.Text())),BootComparison::NewBoot);
    auto other=Facts(reboot.Text());std::strcpy(other.board,"12345671");
    CHECK_EQ(CompareDeviceBoot(before,other),BootComparison::WrongBoard);
    other.boot[0]=0;CHECK_EQ(CompareDeviceBoot(before,other),BootComparison::WrongBoard);
    other=before;other.boot[0]=0;CHECK_EQ(CompareDeviceBoot(before,other),BootComparison::Unknown);
}
TEST_CASE(BootNonceTimeoutErrorAndZeroEntropyNeverInventAnIdentity) {
    for(auto result:{BootIdentity::Read::Waiting,BootIdentity::Read::Failed,BootIdentity::Read::Ready}) {
        Source source;source.result=result;source.word=0;BootIdentity boot;
        boot.Begin(source,UINT32_MAX-10);boot.Poll(source,UINT32_MAX-9);
        boot.Poll(source,40);CHECK_EQ(boot.Status(),BootIdentity::State::Unavailable);
        CHECK(!*boot.Text());const auto reads=source.reads;
        boot.Begin(source,0);boot.Poll(source,0);CHECK_EQ(source.begins,1u);CHECK_EQ(source.reads,reads);
    }
}
TEST_CASE(DeviceFactsBoundedRoundTripAndStrictValidation) {
    for(auto state:{AudioInitialization::Uninitialized,AudioInitialization::Initialized,AudioInitialization::Failed}) {
        auto facts=Facts();facts.audio=state;
        facts.route=state==AudioInitialization::Uninitialized?AudioOutputChoice::Default:AudioOutputChoice::AudioShield;
        char line[kDeviceFactsLineCapacity];const auto n=FormatDeviceFacts(facts,line,sizeof(line));CHECK(n>0);
        DeviceFacts parsed;CHECK(ParseDeviceFacts(std::string_view(line,n-1),parsed));
        CHECK_EQ(parsed.audio,state);CHECK_EQ(parsed.route,facts.route);
        CHECK_EQ(CompareDeviceBoot(facts,parsed),BootComparison::SameBoot);
        char small[4];CHECK_EQ(FormatDeviceFacts(facts,small,sizeof(small)),0u);CHECK_EQ(small[0],0);
    }
    auto prior=Facts();
    for(const auto& line:{
        std::string("BTDEVICE1 FACTS 2 12345670 0123456789abcdef0123456789abcdef uninitialized default"),
        std::string("BTDEVICE1 FACTS 1 12345670 0123456789abcdef0123456789abcdef initialized default"),
        std::string("BTDEVICE1 FACTS 1 12345670 0123456789abcdef0123456789abcdef uninitialized bridge"),
        std::string("BTDEVICE1 FACTS 1 12345670 00000000000000000000000000000000 initialized bridge"),
        std::string("BTDEVICE1 FACTS 1 12345670 short failed pt8211"),
        std::string("BTDEVICE1 FACTS 1 abc unavailable uninitialized default"),
        std::string("BTDEVICE1 FACTS 1 12345670 unavailable failed unknown"),
        std::string("BTDEVICE1 FACTS 1 12345670 unavailable failed"),std::string(96,'x')}) {
        auto parsed=prior;CHECK(!ParseDeviceFacts(line,parsed));
        CHECK_EQ(CompareDeviceBoot(prior,parsed),BootComparison::SameBoot);
    }
    CHECK(ParseDeviceFacts("BTDEVICE1 FACTS 1 unavailable unavailable uninitialized default",prior));
    CHECK_EQ(CompareDeviceBoot(prior,prior),BootComparison::Unknown);
}
TEST_CASE(ActualProductionAudioFactsRemainReadOnlyAndReportStickyFailure) {
    AudioFake::state={};ProductionAudioOutput output;AudioStream source;AudioOutputUSB usb;
    for(unsigned i=0;i<8;++i) {
        CHECK_EQ(output.Initialization(),AudioInitialization::Uninitialized);
        auto facts=output.Facts(Facts());
        CHECK_EQ(facts.route,AudioOutputChoice::Default);CHECK_EQ(facts.audio,AudioInitialization::Uninitialized);
        char line[kDeviceFactsLineCapacity];CHECK(FormatDeviceFacts(facts,line,sizeof(line))>0);
    }
    CHECK_EQ(AudioFake::state.patches,0u);CHECK_EQ(AudioFake::state.memory,0u);
    CHECK(output.Select(AudioOutputChoice::Pt8211,source,usb)==nullptr);
    CHECK_EQ(output.Initialization(),AudioInitialization::Initialized);
    const auto patches=AudioFake::state.patches;
    for(unsigned i=0;i<8;++i) {
        const auto facts=output.Facts(Facts());char line[kDeviceFactsLineCapacity];
        CHECK(FormatDeviceFacts(facts,line,sizeof(line))>0);
        CHECK_EQ(facts.audio,AudioInitialization::Initialized);CHECK_EQ(facts.route,AudioOutputChoice::Pt8211);
    }
    CHECK_EQ(AudioFake::state.patches,patches);
    AudioFake::state={};AudioFake::state.enable=false;ProductionAudioOutput failed;
    CHECK(failed.Select(AudioOutputChoice::AudioShield,source,usb)!=nullptr);
    CHECK_EQ(failed.Active(),AudioOutputChoice::AudioShield);
    CHECK_EQ(failed.Initialization(),AudioInitialization::Failed);CHECK(!failed.Ready());
    const auto facts=failed.Facts(Facts());char line[kDeviceFactsLineCapacity];
    CHECK(FormatDeviceFacts(facts,line,sizeof(line))>0);
    CHECK(std::strstr(line," failed audio_shield\n"));
}

#include "test_framework.h"
#include "firmware/teensy/BroTracker/production_audio_output.h"
#include "firmware/teensy/BroTracker/production_audio_guard.h"
#include <cstring>
using namespace BroTracker;
namespace BroTracker {
// Only the ARM interrupt-register boundary is substituted. Tests compile the
// actual production graph/lifecycle and the actual codec initialization helper.
AudioActivationMask BeginAudioActivation() { AudioFake::state.guarded=true;return {}; }
void EndAudioActivation(AudioActivationMask) { AudioFake::state.guarded=false; }
}
TEST_CASE(ProductionRoutesPreserveGraphsAndNeverConstructAlternativeHardware) {
    for(auto route : {AudioOutputChoice::Bridge,AudioOutputChoice::Pt8211,AudioOutputChoice::AudioShield}) {
        AudioFake::state={};ProductionAudioOutput output;AudioStream source;AudioOutputUSB usb;
        CHECK(!output.Ready());CHECK(output.Select(route,source,usb)==nullptr);CHECK(output.Ready());
        auto& g=AudioFake::state;
        CHECK_EQ(g.pt,route==AudioOutputChoice::Pt8211?1u:0u);
        CHECK_EQ(g.mqs,route==AudioOutputChoice::Bridge?1u:0u);
        CHECK_EQ(g.i2s,route==AudioOutputChoice::AudioShield?1u:0u);
        CHECK_EQ(g.memory,8u);CHECK(!g.badGuard);
        CHECK_EQ(g.patches,route==AudioOutputChoice::Bridge?3u:2u);
        for(unsigned i=0;i<g.patches;++i) {CHECK_EQ(g.patch[i].source,AudioFake::Source);CHECK_EQ(g.patch[i].from,0);}
        if(route==AudioOutputChoice::Bridge) {
            CHECK_EQ(g.patch[0].output,AudioFake::Mqs);CHECK_EQ(g.patch[0].to,0);
            CHECK_EQ(g.patch[1].output,AudioFake::Usb);CHECK_EQ(g.patch[1].to,0);
            CHECK_EQ(g.patch[2].output,AudioFake::Usb);CHECK_EQ(g.patch[2].to,1);
            CHECK_EQ(g.enables,0u);
        } else {
            const auto kind=route==AudioOutputChoice::AudioShield?AudioFake::I2s:AudioFake::Pt;
            CHECK_EQ(g.patch[0].output,kind);CHECK_EQ(g.patch[0].to,0);
            CHECK_EQ(g.patch[1].output,kind);CHECK_EQ(g.patch[1].to,1);
            CHECK_EQ(g.enables,route==AudioOutputChoice::AudioShield?1u:0u);
        }
        if(route==AudioOutputChoice::AudioShield) {
            CHECK_EQ(g.headphone,0.80f);CHECK_EQ(g.ana,0x126u);CHECK_EQ(g.hp,0x1919u);CHECK_EQ(g.reads,7u);
        }
        const auto controls=g.controls;
        CHECK(output.Select(route,source,usb)==nullptr);
        CHECK(output.Select(AudioOutputChoice::Default,source,usb)==nullptr);
        for(auto other : {AudioOutputChoice::Bridge,AudioOutputChoice::Pt8211,AudioOutputChoice::AudioShield})
            if(other!=route) CHECK_EQ(std::strcmp(output.Select(other,source,usb),"restart-required"),0);
        CHECK_EQ(g.controls,controls);CHECK_EQ(g.pt+g.mqs+g.i2s,1u);CHECK(output.Active()==route);
    }
}
TEST_CASE(ProductionDefaultRemainsBridge) {
    AudioFake::state={};ProductionAudioOutput output;AudioStream source;AudioOutputUSB usb;
    CHECK(output.Select(AudioOutputChoice::Default,source,usb)==nullptr);
    CHECK(output.Active()==AudioOutputChoice::Bridge);CHECK_EQ(AudioFake::state.enables,0u);
}
TEST_CASE(ShieldFailuresAreStickyWithoutRetryFallbackOrOtherPinDrivers) {
    for(unsigned failure=0;failure<16;++failure) {
        AudioFake::state={};auto& g=AudioFake::state;
        if(failure==0)g.enable=false;
        else if(failure==1)g.line=false;
        else if(failure==2)g.volume=false;
        else if(failure==3)g.unmute=false;
        else if(failure<9)g.readError=int(failure-3);
        else g.mismatch=int(failure-9);
        ProductionAudioOutput output;AudioStream source;AudioOutputUSB usb;
        const auto* error=output.Select(AudioOutputChoice::AudioShield,source,usb);
        CHECK(error);CHECK(!output.Ready());CHECK(output.Active()==AudioOutputChoice::AudioShield);
        const char* expected=failure==0 ? "shield-enable-failed" : failure==1 ? "shield-line-mute-failed" :
            failure==2 ? "shield-volume-failed" : failure==3 ? "shield-unmute-failed" :
            failure<9 ? "shield-readback-failed" : "shield-config-mismatch";
        if(error) CHECK_EQ(std::strcmp(error,expected),0);
        CHECK(!g.badGuard);CHECK_EQ(g.i2s,1u);CHECK_EQ(g.pt,0u);CHECK_EQ(g.mqs,0u);
        const auto controls=g.controls,reads=g.reads;
        CHECK(output.Select(AudioOutputChoice::AudioShield,source,usb)==error);
        CHECK(output.Select(AudioOutputChoice::Default,source,usb)==error);
        CHECK_EQ(std::strcmp(output.Select(AudioOutputChoice::Bridge,source,usb),"restart-required"),0);
        CHECK_EQ(std::strcmp(output.Select(AudioOutputChoice::Pt8211,source,usb),"restart-required"),0);
        CHECK_EQ(g.controls,controls);CHECK_EQ(g.reads,reads);CHECK_EQ(g.enables,1u);
    }
}

#include "test_framework.h"
#include "ui/host_config.h"
#include "bringup/host_midi_note_delay.h"
#include "bringup/midi_note_delay_service.h"
#include "firmware/teensy/BroTracker/production_audio_output.h"
#include <string>
using namespace BroTracker;
namespace {
MidiDelayRequest Request(HostMidiNoteDelay& host,unsigned now,AudioOutputChoice route,bool stopped=true) {
    CHECK(host.Begin(now,true,route,stopped));char line[96];std::size_t size=0;
    CHECK(host.FormatLine(line,sizeof(line),size));MidiDelayRequest request;
    CHECK(ParseMidiDelayRequest(line,size-1,request));return request;
}
void Reply(HostMidiNoteDelay& host,const MidiDelayReply& reply,unsigned now) {
    char line[96];const auto size=FormatMidiDelayReply(reply,line,sizeof(line));CHECK(size>0);
    CHECK(host.Response(line,size-1,now));
}
}
TEST_CASE(MidiDelayConfigIsStrictDefaultsZeroAndPreservesOtherFields) {
    const std::string base="[btx]\nschema_version=1\nnew_pattern_length=64\n[audio]\noutput=pt8211\n";
    HostConfig config;CHECK_EQ(ParseHostConfig(base,config),HostConfigStatus::Loaded);CHECK_EQ(config.midi_delay_ms,0u);
    for(auto value:{"0","25","82","160","00082"}) {
        CHECK_EQ(ParseHostConfig(base+"[audio.bridge]\nmidi_delay_ms="+value,config),HostConfigStatus::Loaded);
        CHECK(!config.midi_delay_ms_invalid);CHECK_EQ(config.midi_delay_ms,unsigned(std::stoul(value)));
    }
    for(auto value:{"","-1","+1","1.0","82ms","82 0","161","4294967296","9999999999999999999999","0x52"}) {
        CHECK_EQ(ParseHostConfig(base+"[audio.bridge]\nmidi_delay_ms="+value,config),HostConfigStatus::Loaded);
        CHECK(config.midi_delay_ms_invalid);CHECK_EQ(config.audio_output,AudioOutputChoice::Pt8211);CHECK_EQ(config.new_pattern_length,64u);
        HostMidiNoteDelay host;host.Configure(config.midi_delay_ms,config.midi_delay_ms_invalid);
        CHECK(!host.Ready());CHECK(std::strstr(host.Status(),"invalid configuration"));
    }
    CHECK_EQ(ParseHostConfig(base+"[audio.bridge]\nmidi_delay_ms=0\nmidi_delay_ms=25",config),HostConfigStatus::Invalid);
}
TEST_CASE(MidiDelayProtocolStrictRequestsUseActualOutputAndDoNotInitializeIt) {
    for(const char* bad:{"BTMIDIDELAY1 GET 2 1","BTMIDIDELAY1 GET 1 0","BTMIDIDELAY1 GET 1 -1","BTMIDIDELAY1 SET 1 1 82.0","BTMIDIDELAY1 SET 1 1 4294967296","BTMIDIDELAY1 SET 1 1 82 extra"}) {
        MidiDelayRequest request;CHECK(!ParseMidiDelayRequest(bad,std::strlen(bad),request));
    }
    AudioFake::state={};ProductionAudioOutput output;MidiOutput midi;
    const auto query=ApplyMidiDelayRequest({1,0,false},true,true,output,midi);
    CHECK_EQ(query.condition,5u);CHECK_EQ(AudioFake::state.memory,0u);CHECK(!midi.PendingCleanup());CHECK_EQ(midi.Queued(),0u);
    CHECK_EQ(ApplyMidiDelayRequest({2,82,true},true,true,output,midi).kind,MidiDelayReply::Kind::Error);
    CHECK_EQ(midi.NoteDelayUs(),0u);CHECK_EQ(AudioFake::state.patches,0u);
}
TEST_CASE(MidiDelaySetupCorrelatesIntentAndEffectiveBridgeOrNativeValues) {
    for(auto route:{AudioOutputChoice::Bridge,AudioOutputChoice::Pt8211,AudioOutputChoice::AudioShield})for(unsigned intent:{0u,25u,82u,160u}) {
        AudioFake::state={};ProductionAudioOutput output;AudioStream source;AudioOutputUSB usb;
        CHECK(output.Select(route,source,usb)==nullptr);MidiOutput midi;HostMidiNoteDelay host;
        host.Configure(intent);host.Capability();CHECK(!host.Ready());CHECK(!host.Begin(0,false,route,true));
        auto request=Request(host,1,route);
        Reply(host,ApplyMidiDelayRequest(request,true,true,output,midi),2);
        if(route==AudioOutputChoice::Bridge && intent) {
            CHECK(!host.Ready());request=Request(host,3,route);CHECK(request.set);CHECK_EQ(request.ms,intent);
            Reply(host,ApplyMidiDelayRequest(request,true,true,output,midi),4);
        }
        CHECK(host.Confirmed());CHECK_EQ(host.EffectiveMs(),route==AudioOutputChoice::Bridge?intent:0u);
        CHECK_EQ(midi.NoteDelayUs(),host.EffectiveMs()*1000u);CHECK(std::strstr(host.Status(),"configured="));
        CHECK(std::strstr(host.Status(),"confirmed"));CHECK(!host.Begin(5,true,route,true));
    }
}
TEST_CASE(MidiDelaySetupRejectsMismatchesTimeoutUnsupportedAndRecoversOnlyByVerification) {
    HostMidiNoteDelay legacy;CHECK(legacy.Ready());CHECK(!legacy.Begin(0,true,AudioOutputChoice::Bridge,true));
    legacy.Configure(25);CHECK(!legacy.Ready());CHECK(!legacy.Begin(1,true,AudioOutputChoice::Bridge,true));
    CHECK(std::strstr(legacy.Status(),"unsupported"));
    for(unsigned mode=0;mode<4;++mode) {
        HostMidiNoteDelay host;host.Configure(82);host.Capability();auto r=Request(host,0,AudioOutputChoice::Bridge);
        const auto wrong="BTMIDIDELAY1 STATE 1 "+std::to_string(r.id+1)+" 82 0";
        CHECK(!host.Response(wrong.c_str(),wrong.size(),1));CHECK(!host.Ready());
        if(mode==0){host.Tick(6000);CHECK(!host.Ready());CHECK(std::strstr(host.Status(),"timeout"));continue;}
        Reply(host,{MidiDelayReply::Kind::State,r.id,0,0,0},2);r=Request(host,3,AudioOutputChoice::Bridge);
        if(mode==1)Reply(host,{MidiDelayReply::Kind::Selected,r.id,25,82,0},4);
        if(mode==2)Reply(host,{MidiDelayReply::Kind::Selected,r.id,82,25,0},4);
        if(mode==3)Reply(host,{MidiDelayReply::Kind::Error,r.id,0,0,1},4);
        CHECK(!host.Ready());host.StopAcknowledged(10);r=Request(host,11,AudioOutputChoice::Bridge);
        CHECK(!r.set);Reply(host,{MidiDelayReply::Kind::State,r.id,0,82,0},12);CHECK(host.Ready());
    }
}
TEST_CASE(MidiDelayPreviousNonzeroRequiresStopCleanupAndExplicitlyAppliesZero) {
    AudioFake::state={};ProductionAudioOutput output;AudioStream source;AudioOutputUSB usb;
    CHECK(output.Select(AudioOutputChoice::Bridge,source,usb)==nullptr);MidiOutput midi;
    midi.Lifecycle(true,1);midi.TakeAbortRequest();
    midi.Service(0,[](const std::uint32_t*,unsigned){return true;});midi.ConfirmTransfers();
    CHECK_EQ(midi.SetNoteDelayMs(82,output.Active(),output.Initialization(),true),MidiOutput::DelaySetting::Applied);
    HostMidiNoteDelay host;host.Configure(0);host.Capability();auto r=Request(host,0,AudioOutputChoice::Bridge,false);
    Reply(host,ApplyMidiDelayRequest(r,true,false,output,midi),1);CHECK(!host.Ready());
    CHECK(std::strstr(host.Status(),"STOP required"));CHECK(!host.Begin(100,true,AudioOutputChoice::Bridge,false));
    CHECK_EQ(midi.NoteDelayUs(),82000u);
    host.StopAcknowledged(200);midi.Reset();r=Request(host,201,AudioOutputChoice::Bridge);
    Reply(host,ApplyMidiDelayRequest(r,true,true,output,midi),202);CHECK(!host.Ready());
    midi.TakeAbortRequest();midi.Service(203,[](const std::uint32_t*,unsigned){return true;});midi.ConfirmTransfers();
    r=Request(host,453,AudioOutputChoice::Bridge);CHECK(!r.set);Reply(host,ApplyMidiDelayRequest(r,true,true,output,midi),454);
    r=Request(host,455,AudioOutputChoice::Bridge);CHECK(r.set);CHECK_EQ(r.ms,0u);
    Reply(host,ApplyMidiDelayRequest(r,true,true,output,midi),456);CHECK(host.Ready());CHECK_EQ(midi.NoteDelayUs(),0u);
    host.Disconnect();CHECK(!host.Confirmed());host.Capability();r=Request(host,500,AudioOutputChoice::Bridge);
    CHECK(!r.set);Reply(host,ApplyMidiDelayRequest(r,true,true,output,midi),501);CHECK(host.Ready());
}
TEST_CASE(MidiDelayCleanupPollingHasBoundedWaitWithoutAnyTransportAction) {
    HostMidiNoteDelay host;host.Configure(82);host.Capability();auto r=Request(host,0,AudioOutputChoice::Bridge);
    Reply(host,{MidiDelayReply::Kind::State,r.id,0,0,4},1);
    CHECK(!host.Begin(100,true,AudioOutputChoice::Bridge,true));
    CHECK(!host.Begin(6001,true,AudioOutputChoice::Bridge,true));CHECK(!host.Ready());
    CHECK(std::strstr(host.Status(),"cleanup not ready"));
}
TEST_CASE(MidiDelayFirmwareRejectsUnsafeChangesAndReportsFailedInitialization) {
    AudioFake::state={};ProductionAudioOutput output;AudioStream source;AudioOutputUSB usb;
    CHECK(output.Select(AudioOutputChoice::Bridge,source,usb)==nullptr);MidiOutput midi;
    CHECK_EQ(ApplyMidiDelayRequest({1,82,true},true,false,output,midi).condition,2u);
    CHECK_EQ(ApplyMidiDelayRequest({2,161,true},true,true,output,midi).condition,1u);
    midi.Reset();
    CHECK_EQ(ApplyMidiDelayRequest({3,82,true},true,true,output,midi).condition,4u);
    CHECK_EQ(midi.NoteDelayUs(),0u);CHECK(midi.PendingCleanup());
    HostMidiNoteDelay host;host.Capability();auto r=Request(host,0,AudioOutputChoice::Bridge);
    Reply(host,{MidiDelayReply::Kind::State,r.id,0,0,5},1);
    CHECK(!host.Ready());CHECK(std::strstr(host.Status(),"audio initialization not ready"));
}
TEST_CASE(MidiDelayChangeAfterConfirmedFaultCleanupDoesNotRecoverPlayback) {
    AudioFake::state={};ProductionAudioOutput output;AudioStream source;AudioOutputUSB usb;
    CHECK(output.Select(AudioOutputChoice::Bridge,source,usb)==nullptr);MidiOutput midi;
    midi.Lifecycle(true,1);midi.TimerFailure();CHECK(midi.Faulted());
    CHECK_EQ(ApplyMidiDelayRequest({1,82,true},true,true,output,midi).condition,4u);
    midi.TakeAbortRequest();midi.Service(0,[](const std::uint32_t*,unsigned){return true;});midi.ConfirmTransfers();
    CHECK(!midi.PendingCleanup());CHECK(midi.Faulted());
    CHECK_EQ(ApplyMidiDelayRequest({2,82,true},true,true,output,midi).kind,MidiDelayReply::Kind::Selected);
    CHECK_EQ(midi.NoteDelayUs(),82000u);CHECK(midi.Faulted());CHECK_EQ(midi.Queued(),0u);
    CHECK(midi.Recover(true));CHECK(!midi.Faulted()); // Existing explicit START recovery remains separate.
}

#include "test_framework.h"
#include "core/audio/master_volume.h"
#include "bringup/master_volume_protocol.h"
#include "bringup/host_master_volume.h"
#include "bringup/pattern_control.h"
#include "bringup/variable_rate_player_adapter.h"
#include "bringup/pattern_fixture.h"
#include "ui/volume_repeat.h"
#include <array>
#include <cstring>
using namespace BroTracker;
TEST_CASE(MasterVolumeUnitySilenceSmoothingRetargetAndPartitions) {
    MasterVolume unity;std::int16_t edge[]={-32768,-1,0,1,32767};const auto saved=std::array<std::int16_t,5>{-32768,-1,0,1,32767};
    unity.Process(edge,5);for(unsigned i=0;i<5;++i)CHECK_EQ(edge[i],saved[i]);CHECK(!unity.Set(101));CHECK_EQ(unity.Percent(),100u);
    MasterVolume a,b;a.Configure(44100);b.Configure(44100);CHECK(a.Set(0));CHECK(b.Set(0));
    std::array<std::int16_t,600> x,y;x.fill(10000);y=x;
    a.Process(x.data(),600);unsigned at=0;for(unsigned n:{1u,127u,3u,128u,41u,128u,128u,44u}){b.Process(y.data()+at,n);at+=n;}CHECK_EQ(at,600u);CHECK(x==y);
    CHECK(x[0]<10000 && x[0]>9900);for(unsigned i=1;i<441;++i){CHECK(x[i]<=x[i-1]);CHECK(x[i-1]-x[i]<=24);}CHECK_EQ(x[440],0);CHECK_EQ(x[599],0);CHECK_EQ(a.Current(),0u);
    CHECK(a.Set(100));std::int16_t pcm[128];for(auto& p:pcm)p=10000;a.Process(pcm,128);const auto before=a.Current();CHECK(a.Set(50));for(auto& p:pcm)p=10000;a.Process(pcm,1);CHECK(a.Current()>=before);CHECK(a.Current()-before<150);
    MasterVolume c;c.Configure(48000);CHECK(c.Set(0));std::array<std::int16_t,480> neg;neg.fill(-10000);c.Process(neg.data(),480);CHECK_EQ(neg.back(),0);CHECK_EQ(c.Current(),0u);
}
TEST_CASE(MasterVolumeCombinedSampleSynthOnceAndTransportRetention) {
    using Control=BasicPatternBringUpControl<VariableRateBringUpPlayer,VariableRateSampleBindings>;
    std::array<std::int16_t,4096> sample;sample.fill(9000);const auto bindings=SelectDevelopmentFixture({sample.data(),sample.size(),44100}).bindings;
    for(unsigned mode=0;mode<3;++mode) {
        RealtimePattern pattern;pattern.active_channels=2;if(mode!=1)pattern.cells[0][0]={60,0};if(mode!=0)pattern.cells[0][1]={69,2};
        Control a,b;CHECK_EQ(a.InitializeAudio(15500,44100,pattern,bindings),PatternPlayerStatus::Success);CHECK_EQ(b.InitializeAudio(15500,44100,pattern,bindings),PatternPlayerStatus::Success);
        CHECK_EQ(b.SubmitVolume(50,1),PatternRequestStatus::Accepted);
        std::int16_t x[128],y[128];PatternAppliedRequest ack;
        for(unsigned i=0;i<4;++i){a.AudioBlock(x,128,true);b.AudioBlock(y,128,true);while(b.TakeApplied(ack)){};}
        CHECK_EQ(b.Snapshot().volume_current,32768u);CHECK_EQ(b.Snapshot().volume_percent,50u);
        CHECK_EQ(a.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);CHECK_EQ(b.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
        for(unsigned block=0;block<8;++block){a.AudioBlock(x,128,true);b.AudioBlock(y,128,true);for(unsigned i=0;i<128;++i)CHECK_EQ(y[i],(int(x[i])+(x[i]>=0?1:-1))/2);while(a.TakeApplied(ack)){}while(b.TakeApplied(ack)){};}
        CHECK_EQ(a.Snapshot().next_sample,b.Snapshot().next_sample);
        for(auto action:{PatternRequest::Pause,PatternRequest::Continue,PatternRequest::Stop}){CHECK_EQ(b.Submit(action),PatternRequestStatus::Accepted);b.AudioBlock(y,128,true);while(b.TakeApplied(ack)){}CHECK_EQ(b.Snapshot().volume_percent,50u);}
        CHECK_EQ(b.SubmitVolume(101,2),PatternRequestStatus::InvalidGain);CHECK_EQ(b.SubmitVolume(0,1),PatternRequestStatus::Identity);
        CHECK_EQ(b.SubmitPattern(pattern,2,15500),PatternRequestStatus::Accepted);b.AudioBlock(y,128,true);while(b.TakeApplied(ack)){}CHECK_EQ(b.Snapshot().volume_percent,50u);
    }
}
TEST_CASE(MasterVolumeProtocolBoundsAndHostAcknowledgmentCoalescingReconnect) {
    MasterVolumeMessage message;for(const auto* line:{"BTVOL1 SET 1 1 0","BTVOL1 SET 1 4294967295 100"})CHECK(ParseMasterVolume(line,std::strlen(line),message));const auto saved=message;
    for(const auto* line:{"BTVOL1 SET 1 0 50","BTVOL1 SET 1 1 101","BTVOL1 SET 1 1 -1","BTVOL1 SET 2 1 50","BTVOL1 SET 1 1 50 ","BTVOL1 SET 1 1 50 1","BTVOL1 SET 1 4294967296 50"}){CHECK(!ParseMasterVolume(line,std::strlen(line),message));CHECK_EQ(message.id,saved.id);}
    HostMasterVolume host;auto response=[&](const char* s){return host.Response(s,std::strlen(s));};CHECK(!host.Step(-1));CHECK(response("BTVOL1 CAP 1"));CHECK(response("BTVOL1 STATE 1 0 1 75 49152"));CHECK_EQ(host.Percent(),75u);
    CHECK(host.Step(-1));CHECK(host.Begin());char line[96];std::size_t n;CHECK(host.FormatLine(line,sizeof(line),n));CHECK(!std::strcmp(line,"BTVOL1 SET 1 1 70\n"));host.BeforeWrite(10);host.LineComplete();CHECK_EQ(host.Percent(),75u);
    CHECK(host.Step(-1));CHECK(response("BTVOL1 APPLIED 1 1"));CHECK(response("BTVOL1 STATE 1 1 2 70 47000"));CHECK_EQ(host.Percent(),70u);CHECK_EQ(host.Desired(),65u);CHECK(host.Begin());host.BeforeWrite(20);host.LineComplete();host.Tick(5020);CHECK(!host.Available());CHECK(!host.Step(-1));CHECK(!host.Begin());CHECK(host.NeedsRead(5020));
    CHECK(response("BTVOL1 STATE 1 2 3 65 42598"));CHECK(host.Available());host.Disconnect();CHECK(!host.Step(-1));CHECK(response("BTVOL1 CAP 1"));CHECK(response("BTVOL1 STATE 1 2 3 65 42598"));CHECK_EQ(host.Desired(),65u);CHECK(!host.Begin());
    CHECK(!response("BTVOL1 STATE 1 2 3 101 1"));CHECK(!response("BTVOL1 STATE 1 2 2 65 42598"));CHECK_EQ(host.Percent(),65u);
    for(unsigned i=0;i<40;++i)CHECK(host.Step(-1));CHECK_EQ(host.Desired(),0u);for(unsigned i=0;i<40;++i)CHECK(host.Step(1));CHECK_EQ(host.Desired(),100u);
}
TEST_CASE(MasterVolumeRepeatIgnoresStallsOppositionAndCancellation) {
    VolumeRepeat input;CHECK_EQ(input.Edge(false,true,0),-1);CHECK_EQ(input.Tick(399),0);CHECK_EQ(input.Tick(400),-1);CHECK_EQ(input.Tick(10000),-1);CHECK_EQ(input.Tick(10000),0);
    input.Cancel();CHECK_EQ(input.Tick(11000),0);CHECK_EQ(input.Edge(false,true,11001),0);CHECK_EQ(input.Edge(false,false,11002),0);CHECK_EQ(input.Edge(true,false,11003),1);
    CHECK_EQ(input.Edge(true,true,11004),0);CHECK_EQ(input.Edge(true,false,11005),0);CHECK_EQ(input.Edge(false,false,11006),0);CHECK_EQ(input.Edge(true,false,11007),1);input.Cancel();CHECK_EQ(input.Tick(12000),0);
}

TEST_CASE(MasterVolumeLeavesMidiCommandsAndTimelineUnchanged) {
    using Control=BasicPatternBringUpControl<VariableRateBringUpPlayer,VariableRateSampleBindings>;
    RealtimePattern pattern;pattern.active_channels=1;pattern.cells[0][0]={69,1};pattern.cells[1][0]={NOTE_OFF,255};
    const auto bindings=SelectDevelopmentFixture({}).bindings;Control a,b;
    CHECK_EQ(a.InitializeAudio(15500,44100,pattern,bindings),PatternPlayerStatus::Success);CHECK_EQ(b.InitializeAudio(15500,44100,pattern,bindings),PatternPlayerStatus::Success);
    CHECK_EQ(b.SubmitVolume(0,1),PatternRequestStatus::Accepted);
    CHECK_EQ(a.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);CHECK_EQ(b.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    unsigned commands=0;std::int16_t x[128],y[128];PatternAppliedRequest ack;
    for(unsigned block=0;block<40;++block) {
        a.AudioBlock(x,128,true);b.AudioBlock(y,128,true);auto left=a.OutputPlayer().MidiPlayer().TakeMidiCommands();auto right=b.OutputPlayer().MidiPlayer().TakeMidiCommands();
        CHECK_EQ(left.count,right.count);commands+=left.count;
        for(unsigned i=0;i<left.count;++i){CHECK_EQ(left.commands[i].sample_offset,right.commands[i].sample_offset);CHECK_EQ(left.commands[i].channel,right.commands[i].channel);CHECK_EQ(left.commands[i].action,right.commands[i].action);CHECK_EQ(left.commands[i].synth_note,right.commands[i].synth_note);}
        CHECK_EQ(a.Snapshot().next_sample,b.Snapshot().next_sample);CHECK_EQ(b.Snapshot().tempo,15500u);while(a.TakeApplied(ack)){}while(b.TakeApplied(ack)){}
    }
    CHECK(commands>=2);CHECK_EQ(b.Snapshot().volume_current,0u);
}
TEST_CASE(MasterVolumeQueueCapacityAndRejectedIdsAreBounded) {
    using Control=BasicPatternBringUpControl<VariableRateBringUpPlayer,VariableRateSampleBindings>;
    Control control;const auto pattern=MakeDevelopmentPattern();const auto bindings=SelectDevelopmentFixture({}).bindings;
    CHECK_EQ(control.InitializeAudio(15500,44100,pattern,bindings),PatternPlayerStatus::Success);
    for(unsigned id=1;id<=kPatternRequestCapacity;++id)CHECK_EQ(control.SubmitVolume(id*5,id),PatternRequestStatus::Accepted);
    CHECK_EQ(control.SubmitVolume(0,9),PatternRequestStatus::Full);CHECK_EQ(control.Snapshot().next_volume_id,9u);
    std::int16_t pcm[128];control.AudioBlock(pcm,128,true);PatternAppliedRequest ack;unsigned count=0;
    while(control.TakeApplied(ack)){CHECK_EQ(ack.request,PatternRequest::MasterVolume);++count;}CHECK_EQ(count,8u);CHECK_EQ(control.Snapshot().volume_percent,40u);
    CHECK_EQ(control.SubmitVolume(0,9),PatternRequestStatus::Accepted);control.AudioBlock(pcm,128,true);while(control.TakeApplied(ack)){}CHECK_EQ(control.Snapshot().applied_volume_id,9u);
}

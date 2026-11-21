#include "test_framework.h"
#include "ui/sample_waveform.h"
#include "bringup/pattern_control.h"
#include "bringup/variable_rate_player_adapter.h"
#include "bringup/pattern_fixture.h"

using namespace BroTracker;
namespace {
using State=SampleWaveformCache::State;
std::string Command(SampleWaveformTransfer& host,std::uint32_t now=10) {
    char line[kPathProtocolCapacity];
    const auto n=host.Command(line,sizeof(line),now);
    return n?std::string(line,n):std::string{};
}
void Data(SampleWaveformTransfer& host,unsigned id,unsigned offset,
    std::initializer_list<std::uint8_t> pcm) {
    char line[kWaveformLineCapacity];
    const auto n=FormatWaveformData(line,id,offset,pcm.begin(),static_cast<unsigned>(pcm.size()));
    CHECK(n>0);
    if(n)host.Response({line,n-1});
}
}

TEST_CASE(WaveformPullBoundsAndTransportTransitions) {
    const WaveformSource source{9,259,44100,2,16};
    WaveformPull pull;
    pull.Begin(5,7,source.token);
    CHECK(!pull.Next(5,0));
    pull.Sent();
    CHECK(!pull.Next(4,0));
    CHECK(pull.Next(5,0));
    CHECK(!pull.Next(5,0));
    BasicPatternBringUpControl<VariableRateBringUpPlayer,VariableRateSampleBindings> control;
    CHECK_EQ(control.InitializeAudio(15500,44100,MakeDevelopmentPattern(),
        SelectDevelopmentFixture({}).bindings),PatternPlayerStatus::Success);
    auto budget=[&](unsigned elapsed) {
        if(control.ForegroundControlPending())return 0u;
        return WaveformByteBudget(control.Snapshot().transport==PatternTransportState::Playing,elapsed);
    };
    auto transition=[&](PatternRequest request) {
        CHECK_EQ(control.Submit(request),PatternRequestStatus::Accepted);
        CHECK_EQ(budget(100),0u);
        std::int16_t audio[128]{};
        control.AudioBlock(audio,128,true);
        CHECK_EQ(budget(100),0u);
        PatternAppliedRequest applied;
        CHECK(control.TakeApplied(applied));
        CHECK_EQ(applied.request,request);
        CHECK_EQ(applied.result,PatternPlayerStatus::Success);
    };
    CHECK_EQ(WaveformReadBytes(source,0,budget(0)),kWaveformIdleBytes);
    pull.Sent(kWaveformIdleBytes); // One stopped reply already in flight when START arrives.
    transition(PatternRequest::Start);
    CHECK(pull.Next(5,kWaveformIdleBytes));
    CHECK_EQ(WaveformReadBytes(source,pull.cursor,budget(24)),0u);
    CHECK_EQ(WaveformReadBytes(source,pull.cursor,budget(25)),64u);
    pull.Sent(64);
    transition(PatternRequest::Pause);
    CHECK_EQ(control.Snapshot().transport,PatternTransportState::Paused);
    CHECK_EQ(WaveformReadBytes(source,pull.cursor,budget(0)),204u);
    transition(PatternRequest::Continue);
    CHECK_EQ(WaveformReadBytes(source,pull.cursor,budget(25)),64u);
    transition(PatternRequest::Stop);
    CHECK_EQ(budget(0),kWaveformIdleBytes);

    pull.Begin(6,2,10);
    pull.Cancel(5);
    CHECK_EQ(pull.id,6u);
    CHECK(!pull.Next(5,320));
    pull.Cancel(6);
    CHECK_EQ(pull.pending,WaveformPull::Reply::None);
}

TEST_CASE(WaveformTransferChecksOffsetsAndSupersedesObsoleteChunks) {
    SampleWaveformTransfer host;
    host.Select(1,"Samples/Old source.wav",true);
    const auto line=Command(host);
    std::uint32_t id=0,slot=99;
    char path[kSamplePathMaximum+1];
    CHECK(ParseWaveformGet(std::string_view(line).substr(0,line.size()-1),id,slot,path));
    CHECK_EQ(id,1u);
    CHECK_EQ(slot,0u);
    CHECK_EQ(std::string(path),"Samples/Old source.wav");
    CHECK(Command(host).empty());
    host.Response("BTWAVE1 META 1 1 7 80 8363 2 16");
    CHECK_EQ(Command(host),"BTWAVE1 NEXT 1 1 0\n");
    CHECK(Command(host).empty());
    host.Select(2,"Samples/New.wav",true);
    CHECK_EQ(Command(host),"BTWAVE1 CANCEL 1 1\n");
    CHECK(!Command(host).empty());
    host.Response("BTWAVE1 META 1 1 7 80 8363 2 16");
    Data(host,1,0,{0,128,255,127});
    CHECK(Command(host).empty());
    host.Response("BTWAVE1 META 1 2 8 2 8000 2 8");
    CHECK_EQ(Command(host),"BTWAVE1 NEXT 1 2 0\n");
    Data(host,1,0,{0,0,0,0});
    Data(host,2,0,{0,255});
    CHECK_EQ(host.View().Status(),State::Loading);
    CHECK_EQ(Command(host),"BTWAVE1 NEXT 1 2 2\n");
    Data(host,2,2,{128,129});
    CHECK_EQ(host.View().Status(),State::Ready);
    CHECK_EQ(host.View().Column(0).minimum,-32768);
    CHECK_EQ(host.View().Column(0).maximum,32512);
    CHECK_EQ(host.View().Column(1).minimum,0);
    CHECK_EQ(host.View().Column(1).maximum,256);
}

TEST_CASE(WaveformCacheRevalidatesAliasesReplacementRollbackAndReconnect) {
    SampleWaveformTransfer host;
    host.Select(1,"Samples/A.wav",true);
    CHECK(!Command(host).empty());
    host.Response("BTWAVE1 META 1 1 9 1 44100 1 16");
    CHECK_EQ(Command(host),"BTWAVE1 NEXT 1 1 0\n");
    Data(host,1,0,{10,0});
    CHECK_EQ(host.View().Status(),State::Ready);
    auto select=[&](unsigned slot,const char* path,const char* meta) {
        host.Select(slot,path,true);
        CHECK(Command(host).find("CANCEL")!=std::string::npos);
        CHECK(Command(host).find("GET")!=std::string::npos);
        CHECK_EQ(host.View().Status(),State::Loading);
        host.Response(meta);
    };
    select(2,"Samples/A.wav","BTWAVE1 META 1 2 9 1 44100 1 16");
    CHECK_EQ(host.View().Column(0).maximum,10);
    CHECK(Command(host).empty());
    select(1,"Samples/B.wav","BTWAVE1 META 1 3 10 1 44100 1 16");
    CHECK_EQ(host.View().Status(),State::Loading);
    CHECK_EQ(Command(host),"BTWAVE1 NEXT 1 3 0\n");
    Data(host,3,0,{20,0});
    CHECK_EQ(host.View().Column(0).maximum,20);
    select(1,"Samples/A.wav","BTWAVE1 META 1 4 9 1 44100 1 16");
    CHECK_EQ(host.View().Column(0).maximum,10);
    host.Select(0,{},true);
    CHECK_EQ(Command(host),"BTWAVE1 CANCEL 1 4\n");
    host.Select(1,"Samples/A.wav",true);
    CHECK(Command(host).find("GET")!=std::string::npos);
    host.Response("BTWAVE1 META 1 5 11 1 44100 1 16"); // Same path, newly prepared.
    CHECK_EQ(host.View().Status(),State::Loading);
    CHECK_EQ(Command(host),"BTWAVE1 NEXT 1 5 0\n");
    host.Disconnect();
    host.Select(1,"Samples/A.wav",true);
    CHECK(Command(host).find("GET")!=std::string::npos);
    host.Response("BTWAVE1 META 1 6 9 1 44100 1 16"); // Device can reuse a token after reboot.
    CHECK_EQ(host.View().Status(),State::Loading);
    CHECK_EQ(Command(host),"BTWAVE1 NEXT 1 6 0\n");
}

TEST_CASE(WaveformTransferFailsWithoutPublishingPartialData) {
    for(unsigned scenario=0;scenario<5;++scenario) {
        SampleWaveformTransfer host;
        host.Select(1,"Samples/A.wav",true);
        CHECK(!Command(host).empty());
        if(scenario==0)host.Response("BTWAVE1 ERROR 1 1");
        if(scenario==1)host.Response("BTWAVE1 META 1 1 9 3 96000 1 16");
        if(scenario>=2) {
            host.Response("BTWAVE1 META 1 1 9 3 44100 1 16");
            CHECK_EQ(Command(host),"BTWAVE1 NEXT 1 1 0\n");
            if(scenario==2)Data(host,1,2,{1,0});
            if(scenario==3)Data(host,1,0,{1});
            if(scenario==4) {
                host.Response("BTWAVE1 DATA 1 1 0 invalid");
                CHECK_EQ(host.View().Status(),State::Loading);
                host.Tick(12010,false);
            }
        }
        CHECK_EQ(host.View().Status(),State::Unavailable);
    }
}

TEST_CASE(WaveformPlayingAllowancePersistsAcrossPauseStopAndScreenVisits) {
    SampleWaveformTransfer host;
    host.Select(1,"Samples/A.wav",true);
    host.Tick(0,true);
    CHECK_EQ(host.View().Percent(),0u);
    CHECK(!Command(host,0).empty());
    host.Response("BTWAVE1 META 1 1 9 8 44100 1 16");
    CHECK_EQ(Command(host,0),"BTWAVE1 NEXT 1 1 0\n");
    Data(host,1,0,{1,0,2,0});
    CHECK_EQ(host.View().Percent(),25u);
    host.Tick(3500,false);
    CHECK(!host.Deferred());
    CHECK_EQ(Command(host,3500),"BTWAVE1 NEXT 1 1 4\n");
    Data(host,1,4,{3,0,4,0});
    CHECK_EQ(host.View().Percent(),50u);
    host.Tick(8000,true);
    host.Tick(10499,true);
    CHECK(!host.Deferred());
    CHECK_EQ(Command(host,10499),"BTWAVE1 NEXT 1 1 8\n");
    host.Tick(10500,true);
    CHECK(host.Deferred());
    host.Tick(28000,true);
    CHECK(host.Deferred()); // Reserved response timed out only if active, not while deferred.
    Data(host,1,8,{5,0,6,0});
    CHECK_EQ(host.View().Percent(),75u);
    CHECK(host.Deferred());
    CHECK(Command(host,28000).empty());

    host.Select(0,{},true);
    CHECK_EQ(Command(host,28000),"BTWAVE1 CANCEL 1 1\n");
    host.Select(2,"Samples/A.wav",true);
    CHECK(!Command(host,28001).empty());
    host.Response("BTWAVE1 META 1 2 9 8 44100 1 16");
    CHECK_EQ(host.View().Percent(),75u);
    CHECK(host.Deferred());
    CHECK(Command(host,28001).empty());
    Data(host,1,12,{99,0,99,0});
    CHECK_EQ(host.View().Percent(),75u);

    host.Tick(28002,false);
    CHECK(!host.Deferred());
    CHECK_EQ(Command(host,28002),"BTWAVE1 RESUME 1 2 12\n");
    host.Tick(28003,true);
    CHECK(host.Deferred());
    host.Tick(42003,true);
    CHECK(host.Deferred());
    host.Tick(42004,false);
    CHECK(!host.Deferred());
    CHECK(Command(host,42004).empty()); // The original resume credit is still outstanding.
    Data(host,2,12,{7,0,8,0});
    CHECK_EQ(host.View().Percent(),100u);
    CHECK_EQ(host.View().Status(),State::Ready);

    host.Tick(42005,true);
    host.Select(1,"Samples/A.wav",true);
    CHECK_EQ(Command(host,42005),"BTWAVE1 CANCEL 1 2\n");
    CHECK(!Command(host,42006).empty());
    host.Response("BTWAVE1 META 1 3 9 8 44100 1 16");
    CHECK(!host.Deferred());
    CHECK_EQ(host.View().Percent(),100u);
    CHECK_EQ(host.View().Column(7).maximum,8);
    CHECK(Command(host,42007).empty());
}

TEST_CASE(WaveformResumeCreditIsCorrelatedAndAllowedOnlyAfterMetadata) {
    WaveformPull pull;
    pull.Begin(11,2,4);
    CHECK(!pull.Next(11,256,true));
    pull.Sent();
    CHECK(!pull.Next(10,256,true));
    CHECK(pull.Next(11,256,true));
    CHECK(!pull.Next(11,512,true));
    pull.Sent(64);
    CHECK(!pull.Next(11,512,true));
    CHECK(!pull.Next(11,256));
    CHECK(pull.Next(11,320));
    pull.Cancel(11);
    CHECK_EQ(pull.pending,WaveformPull::Reply::None);
}

TEST_CASE(WaveformDeferralStillHonorsDeviceErrorsAndResourceInvalidation) {
    SampleWaveformTransfer host;
    host.Tick(0,true);
    host.Select(1,"Samples/A.wav",true);
    CHECK(!Command(host,0).empty());
    host.Response("BTWAVE1 META 1 1 5 100 44100 1 16");
    CHECK_EQ(Command(host,0),"BTWAVE1 NEXT 1 1 0\n");
    host.Tick(6000,true);
    CHECK(host.Deferred());
    host.Response("BTWAVE1 ERROR 1 1");
    CHECK_EQ(host.View().Status(),State::Unavailable);
    CHECK(!host.Deferred());
    host.Select(0,{},true);
    CHECK_EQ(Command(host,6000),"BTWAVE1 CANCEL 1 1\n");
    host.Select(1,"Samples/A.wav",true);
    CHECK(!Command(host,6001).empty());
    host.Response("BTWAVE1 META 1 2 6 100 44100 1 16");
    CHECK(!host.Deferred()); // A new device generation starts a new acquisition.
    CHECK_EQ(host.View().Percent(),0u);
    CHECK_EQ(Command(host,6001),"BTWAVE1 NEXT 1 2 0\n");
}

TEST_CASE(WaveformDeferredMetadataIgnoresSupersededRepliesAndAcceptsCachedSource) {
    SampleWaveformTransfer host;
    host.Tick(0,true);
    host.Select(1,"Samples/A.wav",true);
    CHECK(!Command(host,0).empty());
    host.Tick(6000,true);
    CHECK(host.Deferred());

    host.Select(2,"Samples/B.wav",true);
    CHECK_EQ(Command(host,6000),"BTWAVE1 CANCEL 1 1\n");
    CHECK(!Command(host,6000).empty());
    host.Tick(20000,true);
    CHECK(host.Deferred());
    host.Response("BTWAVE1 META 1 1 31 8 44100 1 16");
    host.Response("BTWAVE1 ERROR 1 1");
    CHECK(host.Deferred());
    CHECK_EQ(host.View().Metadata().token,0u);
    CHECK(Command(host,20000).empty());
    host.Response("BTWAVE1 META 1 2 32 1 44100 1 16");
    CHECK(host.Deferred()); // The metadata wait consumed the PCM allowance.
    CHECK_EQ(host.View().Metadata().token,32u);
    CHECK(Command(host,20000).empty());
    host.Tick(20001,false);
    CHECK_EQ(Command(host,20001),"BTWAVE1 NEXT 1 2 0\n");
    Data(host,2,0,{44,0});
    CHECK_EQ(host.View().Status(),State::Ready);

    host.Tick(20002,true);
    host.Select(3,"Samples/B.wav",true);
    CHECK_EQ(Command(host,20002),"BTWAVE1 CANCEL 1 2\n");
    CHECK(!Command(host,20002).empty());
    host.Tick(36000,true);
    CHECK(host.Deferred());
    host.Response("BTWAVE1 META 1 3 32 1 44100 1 16");
    CHECK(!host.Deferred());
    CHECK_EQ(host.View().Status(),State::Ready);
    CHECK_EQ(host.View().Column(0).maximum,44);
    CHECK(Command(host,36000).empty());

    host.Select(1,"Samples/A.wav",true);
    CHECK_EQ(Command(host,36000),"BTWAVE1 CANCEL 1 3\n");
    CHECK(!Command(host,36000).empty());
    host.Tick(50000,true);
    CHECK(host.Deferred());
    host.Response("BTWAVE1 ERROR 1 4");
    CHECK(!host.Deferred());
    CHECK_EQ(host.View().Status(),State::Unavailable);
}

TEST_CASE(WaveformEstimatesPlayingBeforeFirstCreditAndReevaluatesOnContinue) {
    SampleWaveformTransfer host;
    host.Tick(0,true);
    host.Select(1,"Samples/Long.wav",true);
    CHECK(!Command(host,0).empty());
    host.Response("BTWAVE1 META 1 1 78 68284 44100 1 16");
    CHECK(host.Deferred());
    CHECK(Command(host,0).empty());
    host.Tick(13000,true);
    CHECK_EQ(host.View().Status(),State::Loading);
    host.Tick(13001,false);
    CHECK_EQ(Command(host,13001),"BTWAVE1 NEXT 1 1 0\n");
    host.Tick(13002,true);
    CHECK(host.Deferred());
    host.Tick(30000,true);
    Data(host,1,0,{1,0,2,0});
    CHECK_EQ(host.View().BytesLoaded(),4u);
    CHECK(Command(host,30000).empty());

    host.Select(2,"Samples/Short.wav",true);
    CHECK_EQ(Command(host,30000),"BTWAVE1 CANCEL 1 1\n");
    CHECK(!Command(host,30000).empty());
    host.Response("BTWAVE1 META 1 2 79 2 44100 1 16");
    CHECK(!host.Deferred());
    CHECK_EQ(Command(host,30000),"BTWAVE1 NEXT 1 2 0\n");
    Data(host,2,0,{5,0,6,0});
    CHECK_EQ(host.View().Status(),State::Ready);
    CHECK(WaveformPlayingEstimateMs(68284*2)>6000);
    CHECK(WaveformPlayingEstimateMs(3000)<6000);
}

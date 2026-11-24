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
    CHECK_EQ(host.View().Column(0).minimum,-128);
    CHECK_EQ(host.View().Column(0).maximum,-128);
    CHECK_EQ(host.View().Column(1).minimum,128);
    CHECK_EQ(host.View().Column(1).maximum,128);
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

TEST_CASE(WaveformAdaptiveOverviewReusesSparseRangesAndFillsGaps) {
    SampleWaveformTransfer host;
    const WaveformSource source{78,68284,44100,1,16};
    WaveformPull device;
    host.Response(kWaveformRangeCapability);
    unsigned refinement_requests=0;
    std::uint32_t now=0;
    const auto tick=[&](bool playing) { host.Tick(now,playing); };
    const auto step=[&](bool playing) {
        const auto command=Command(host,now);
        if(command.empty())return false;
        std::uint32_t id=0,slot=0;
        char path[kSamplePathMaximum+1];
        if(ParseWaveformGet(std::string_view(command).substr(0,command.size()-1),id,slot,path)) {
            device.Begin(id,slot,source.token);
            device.Sent();
            host.Response("BTWAVE1 META 1 "+std::to_string(id)+" 78 68284 44100 1 16");
        } else {
            std::uint32_t offset=0;
            unsigned limit=0;
            const bool range=std::sscanf(command.c_str(),"BTWAVE1 RANGE 1 %u %u %u",&id,&offset,&limit)==3;
            const bool resume=std::sscanf(command.c_str(),"BTWAVE1 RESUME 1 %u %u",&id,&offset)==2;
            if(!range && !resume)CHECK(std::sscanf(command.c_str(),"BTWAVE1 NEXT 1 %u %u",&id,&offset)==2);
            if(range) {
                ++refinement_requests;
                const auto stride=source.Stride();
                const auto expected=host.View().RefinementWindow(playing?64:768);
                CHECK_EQ(offset,expected.first*stride);
                CHECK_EQ(limit,(expected.end-expected.first)*stride);
                CHECK(!device.Range(id,offset,limit+1,source));
                CHECK(device.Range(id,offset,limit,source));
                for(const auto covered:host.View().Coverage())
                    CHECK(offset/stride>=covered.end || (offset+limit)/stride<=covered.first);
            } else CHECK(device.Next(id,offset,resume));
            const auto size=WaveformReadBytes(source,offset,std::min(device.limit,playing?64u:768u));
            std::uint8_t pcm[kWaveformIdleBytes]{1};
            char line[kWaveformLineCapacity];
            const auto length=FormatWaveformData(line,id,offset,pcm,size);
            CHECK(length>0);
            if(length)host.Response({line,length-1});
            device.Sent(size);
        }
        now+=playing?40:10;
        tick(playing);
        return true;
    };

    host.Select(1,"Samples/Test3.wav",true);
    tick(true);
    for(unsigned i=0;i<140 && !host.View().OverviewReady();++i)if(!step(true))break;
    CHECK(host.View().OverviewReady());
    CHECK_EQ(host.View().Status(),State::Loading);
    const auto bytes=host.View().BytesLoaded();
    CHECK(bytes>1000u);
    CHECK(bytes<4000u);
    CHECK(host.View().Coverage().size()>10u);
    CHECK_EQ(host.View().Coverage().front().first,0u);
    CHECK_EQ(host.View().Coverage().back().end,source.frames);
    CHECK(host.View().Column(0).frames>0u);
    CHECK(!host.View().ColumnExact(0));
    std::uint8_t frame[4]{};
    CHECK(host.View().ReadFrame(0,frame));
    CHECK_EQ(frame[0],1);
    CHECK(!host.View().ReadFrame(host.View().Coverage().front().end,frame));
    CHECK(!host.Deferred());
    CHECK(step(true));
    CHECK_EQ(refinement_requests,1u);
    CHECK_EQ(host.View().BytesLoaded(),bytes+64u);
    for(unsigned i=0;i<180 && !host.Deferred();++i)if(!step(true))break;
    CHECK(host.Deferred());
    CHECK(now>=5966u && now<=6040u);
    const auto resumed_bytes=host.View().BytesLoaded();
    CHECK(Command(host,now).empty());

    host.Select(0,{},true);
    CHECK(Command(host,now).find("CANCEL")!=std::string::npos);
    host.Select(1,"Samples/Test3.wav",true);
    CHECK(step(true));
    CHECK_EQ(host.View().BytesLoaded(),resumed_bytes);
    CHECK(host.View().OverviewReady());
    CHECK(Command(host,now).empty());

    tick(false);
    CHECK(step(false));
    CHECK(host.View().BytesLoaded()>resumed_bytes);
    tick(true);
    CHECK(host.Deferred());
    CHECK(Command(host,now).empty());
    tick(false);
    for(unsigned i=0;i<370 && host.View().Status()!=State::Ready;++i)if(!step(false))break;
    CHECK_EQ(host.View().Status(),State::Ready);
    CHECK_EQ(host.View().BytesLoaded(),source.Bytes());
    CHECK_EQ(host.View().Coverage().size(),1u);
    CHECK(host.View().ColumnExact(0));
    CHECK(host.View().ReadFrame(source.frames-1,frame));

    SampleWaveformCache longer;
    CHECK(longer.Begin({79,306970,44100,1,16}));
    CHECK(longer.OverviewPlayingEstimateMs()<3200u);
    SampleWaveformCache small;
    CHECK(small.Begin({80,4096,44100,1,16}));
    CHECK_EQ(small.OverviewPlayingEstimateMs(),WaveformPlayingEstimateMs(8192));
}

TEST_CASE(WaveformStereoDefersNewPcmButReusesCompletedDownmix) {
    SampleWaveformTransfer host;
    host.Tick(0,true);
    host.Select(1,"Samples/Stereo.wav",true);
    CHECK(!Command(host,0).empty());
    host.Response("BTWAVE1 META 1 1 8 2 8000 2 8");
    CHECK(host.Deferred());
    CHECK(Command(host,0).empty());
    host.Tick(1,false);
    CHECK_EQ(Command(host,1),"BTWAVE1 NEXT 1 1 0\n");
    Data(host,1,0,{0,255,128,129});
    CHECK(host.View().OverviewReady());
    CHECK_EQ(host.View().Status(),State::Ready);
    CHECK_EQ(host.View().Column(0).maximum,-128);
    CHECK_EQ(host.View().Column(1).maximum,128);
    host.Tick(2,true);
    host.Select(2,"Samples/Stereo.wav",true);
    CHECK(!Command(host,2).empty());
    CHECK(!Command(host,2).empty());
    host.Response("BTWAVE1 META 1 2 8 2 8000 2 8");
    CHECK(!host.Deferred());
    CHECK_EQ(host.View().Status(),State::Ready);
    CHECK(Command(host,2).empty());
}


TEST_CASE(WaveformRefinementCentersTheLargestGapAndFinishesShortRanges) {
    SampleWaveformCache cache;
    const WaveformSource source{1,20000,8000,1,8};
    CHECK(cache.Begin(source));
    std::uint8_t pcm[kWaveformIdleBytes]{};
    while(!cache.OverviewReady())
        CHECK(cache.Append(cache.NextOverviewFrame(),pcm,kWaveformPlayingBytes));
    // Compute the largest gap independently from cached endpoints.
    SampleWaveformCache::Range largest{};
    std::uint32_t previous=0;
    for(const auto covered:cache.Coverage()) {
        if(covered.first-previous>largest.end-largest.first)largest={previous,covered.first};
        previous=covered.end;
    }
    const auto window=cache.RefinementWindow(768);
    CHECK_EQ(window.end-window.first,768u);
    CHECK_EQ(window.first,largest.first+(largest.end-largest.first-768)/2);
    while(cache.Status()!=State::Ready) {
        const auto next=cache.RefinementWindow(768);
        CHECK(next.end>next.first);
        if(next.end<=next.first)break;
        const auto old=cache.BytesLoaded();
        CHECK(cache.Append(next.first,pcm,next.end-next.first));
        CHECK_EQ(cache.BytesLoaded()-old,next.end-next.first);
    }
    CHECK_EQ(cache.Coverage().size(),1u);
    CHECK_EQ(cache.BytesLoaded(),source.Bytes());
}

TEST_CASE(WaveformLegacyRefinementReservesMetadataBeforeSendingGet) {
    SampleWaveformTransfer host;
    host.Select(1,"Samples/Gap.wav",true);
    host.Tick(0,true);
    char line[kWaveformLineCapacity];
    const std::uint8_t pcm[kWaveformPlayingBytes]{};
    unsigned id=0;
    for(unsigned pass=0;pass<12 && !host.View().OverviewReady();++pass) {
        const auto command=Command(host,0);
        unsigned offset=0;
        if(std::sscanf(command.c_str(),"BTWAVE1 GET 1 %u",&id)==1) {
            host.Response("BTWAVE1 META 1 "+std::to_string(id)+" 10 5000 8000 1 8");
            continue;
        }
        CHECK(std::sscanf(command.c_str(),"BTWAVE1 NEXT 1 %u %u",&id,&offset)==2 ||
              std::sscanf(command.c_str(),"BTWAVE1 RESUME 1 %u %u",&id,&offset)==2);
        const auto count=FormatWaveformData(line,id,offset,pcm,sizeof(pcm));
        host.Response({line,count-1});
    }
    CHECK(host.View().OverviewReady());
    host.Tick(5900,true);
    CHECK(host.Deferred()); // A credit fits in 100 ms, but GET plus credit does not.
    CHECK(Command(host,5900).empty());
    host.Tick(5901,false);
    CHECK(Command(host,5901).find("BTWAVE1 GET ")==0);
}

TEST_CASE(WaveformFingerprintRequiresCurrentNegotiatedMetadataRequest) {
    SampleWaveformTransfer host;
    host.Response(kWaveformFingerprintCapability);
    host.Select(1,"Samples/First.wav",true);
    CHECK(Command(host).find("BTWAVE1 GET 2 1 0 ")==0);
    host.Response("BTWAVE1 FP 1 1 17 0 60 44 16 4 5");
    CHECK(!host.Fingerprint());
    host.Response("BTWAVE1 META 1 1 17 8 44100 1 16");
    host.Response("BTWAVE1 FP 1 1 18 0 60 44 16 4 5");
    CHECK(!host.Fingerprint());
    host.Response("BTWAVE1 FP 1 1 17 0 60 44 16 4 5");
    CHECK(host.Fingerprint());
    if(host.Fingerprint())CHECK_EQ(host.Fingerprint()->prefix_digest,(std::uint64_t(4)<<32)|5u);
    host.Select(2,"Samples/Replacement.wav",true);
    Command(host);
    CHECK(Command(host).find("BTWAVE1 GET 2 2 1 ")==0);
    host.Response("BTWAVE1 META 1 2 18 8 44100 1 16");
    host.Response("BTWAVE1 FP 1 1 17 0 60 44 16 4 5");
    CHECK(!host.Fingerprint());
    host.Disconnect();
    host.Select(1,"Samples/First.wav",true);
    CHECK(Command(host).find("BTWAVE1 GET 1 ")==0);
}

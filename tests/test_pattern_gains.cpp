#include "test_framework.h"
#include "bringup/pattern_gains.h"
#include "bringup/pattern_control.h"
#include "bringup/pattern_publication.h"
#include <cstring>
#include <string>
using namespace BroTracker;
TEST_CASE(PatternGainsStrictParsingAndFormatting) {
    PatternGainMessage message;
    const char* valid="BTGAIN1 SET 1 4294967295 0 65536 1 32768 65535 0 65536 42";
    CHECK(ParsePatternGains(valid,std::strlen(valid),message)); CHECK_EQ(message.id,UINT32_MAX);
    CHECK_EQ(message.gains.channels[1],65536u);
    const auto saved=message;
    const char* malformed[]={"BTGAIN1 SET 2 1 0 0 0 0 0 0 0 0","BTGAIN1 SET 1 0 0 0 0 0 0 0 0 0",
        "BTGAIN1 SET 1 4294967296 0 0 0 0 0 0 0 0","BTGAIN1 SET 1 1 0 0 0 0 0 0 0 65537",
        "BTGAIN1 SET 1 1 0 0 0 0 0 0 0 -1","BTGAIN1 SET 1 1 0 0 0 0 0 0 0 +1",
        "BTGAIN1 SET 1 1 0 0 0 0 0 0 0","BTGAIN1 SET 1 1 0 0 0 0 0 0 0 0 0",
        "BTGAIN1 SET 1 1 0 0 0 0 0 0 0 0 ","BTGAIN1 SET 1 1  0 0 0 0 0 0 0 0"};
    for(auto line:malformed) {
        CHECK(!ParsePatternGains(line,std::strlen(line),message)); CHECK_EQ(message.id,saved.id);
        for(unsigned c=0;c<8;++c) CHECK_EQ(message.gains.channels[c],saved.gains.channels[c]);
    }
    char line[kPatternGainLineCapacity]; Pcm16ChannelGains unity;
    CHECK(FormatPatternGainState(unity,UINT32_MAX,UINT32_MAX,line,sizeof(line)));
    CHECK(std::strlen(line)<96); CHECK(std::strstr(line,"4294967295 4294967295"));
    CHECK(!FormatPatternGainState(unity,UINT32_MAX,0,line,10));
    CHECK(!FormatPatternGainState(unity,0,1,nullptr,0));
    CHECK_EQ(kPatternDiscoveryReplyCount,12u); CHECK(kPatternDiscoveryReplyCount<=16);
}
TEST_CASE(PatternGainsCopiedAtomicOwnerApplicationAndLifecycle) {
    const std::int16_t pcm[]={100,100,100,100,100,100,100,100};
    NativeRateSampleBindings bindings; bindings.count=1; bindings.bindings[0]={7,60,{pcm,8,44100}};
    RealtimePattern pattern; pattern.active_channels=1; pattern.cells[0][0]={60,7};
    PatternBringUpControl control;
    CHECK_EQ(control.InitializeAudio(12753,44100,pattern,bindings),PatternPlayerStatus::Success);
    auto gains=control.Snapshot().gains; gains.channels[0]=32768;
    CHECK_EQ(control.SubmitGains(gains,1),PatternRequestStatus::Accepted);
    gains.channels[0]=0; // caller may mutate/release immediately
    CHECK_EQ(control.Snapshot().gains.channels[0],65536u); CHECK_EQ(control.Snapshot().applied_gain_id,0u);
    CHECK_EQ(control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    std::int16_t out; control.AudioBlock(&out,1,true); CHECK_EQ(out,50);
    PatternAppliedRequest ack; CHECK(control.TakeApplied(ack)); CHECK_EQ(ack.request,PatternRequest::Gains);
    CHECK_EQ(ack.publication_id,1u); CHECK_EQ(ack.result,PatternPlayerStatus::Success);
    CHECK(control.TakeApplied(ack)); CHECK_EQ(ack.request,PatternRequest::Start);
    CHECK_EQ(control.Snapshot().applied_gain_id,1u); CHECK_EQ(control.Snapshot().next_gain_id,2u);
    const auto before=control.Snapshot();
    CHECK_EQ(control.SubmitGains(gains,1),PatternRequestStatus::Identity);
    auto invalid=gains; invalid.channels[7]=65537;
    CHECK_EQ(control.SubmitGains(invalid,2),PatternRequestStatus::InvalidGain);
    CHECK_EQ(control.Snapshot().next_sample,before.next_sample); CHECK_EQ(control.Snapshot().gains.channels[0],32768u);
    CHECK_EQ(control.Submit(PatternRequest::Pause),PatternRequestStatus::Accepted);
    CHECK_EQ(control.SubmitGains(gains,2),PatternRequestStatus::Accepted);
    control.AudioBlock(&out,1,true); CHECK_EQ(out,0);
    while(control.TakeApplied(ack)) {}
    CHECK_EQ(control.Snapshot().transport,PatternTransportState::Paused);
    CHECK_EQ(control.Snapshot().position.absolute_tick,before.position.absolute_tick);
    CHECK_EQ(control.SubmitPreview(pattern,1),PatternRequestStatus::Accepted);
    control.AudioBlock(&out,1,true); while(control.TakeApplied(ack)) {}
    CHECK_EQ(control.SubmitPattern(pattern,1),PatternRequestStatus::Accepted);
    control.AudioBlock(&out,1,true); while(control.TakeApplied(ack)) {}
    CHECK_EQ(control.Snapshot().gains.channels[0],0u); CHECK_EQ(control.Snapshot().applied_gain_id,2u);
    CHECK_EQ(control.Submit(PatternRequest::Continue),PatternRequestStatus::Accepted);
    control.AudioBlock(&out,1,true); while(control.TakeApplied(ack)) {} CHECK_EQ(out,0);
    CHECK_EQ(control.Submit(PatternRequest::Stop),PatternRequestStatus::Accepted);
    control.AudioBlock(&out,1,true); while(control.TakeApplied(ack)) {}
    // Main-loop reconnect/discovery only snapshots; never initializes or resubmits.
    char readback[96]; const auto reconnect=control.Snapshot();
    CHECK(FormatPatternGainState(reconnect.gains,reconnect.applied_gain_id,reconnect.next_gain_id,readback,sizeof(readback)));
    CHECK(std::strstr(readback,"STATE 1 2 3 0 "));
    CHECK_EQ(control.InitializeAudio(12753,44100,pattern,bindings),PatternPlayerStatus::Success);
    CHECK_EQ(control.Snapshot().gains.channels[0],65536u); CHECK_EQ(control.Snapshot().applied_gain_id,0u);
    CHECK_EQ(control.Snapshot().next_gain_id,3u); // configuration does not permit identity replay
}
TEST_CASE(PatternGainsQueueBackpressureAndIdentityExhaustion) {
    PatternBringUpControl control; Pcm16ChannelGains gains;
    for(unsigned i=1;i<=8;++i) CHECK_EQ(control.SubmitGains(gains,i),PatternRequestStatus::Accepted);
    CHECK_EQ(control.SubmitGains(gains,9),PatternRequestStatus::Full); CHECK_EQ(control.Snapshot().next_gain_id,9u);
    std::int16_t out; control.AudioBlock(&out,1,true); // unconfigured render faults; gains still apply
    CHECK_EQ(control.Snapshot().applied_gain_id,8u);
    CHECK_EQ(control.SubmitGains(gains,9),PatternRequestStatus::Accepted);
    control.AudioBlock(&out,1,true); CHECK_EQ(control.Snapshot().applied_gain_id,8u); // full ACK FIFO
    PatternAppliedRequest ack; unsigned count=0; while(control.TakeApplied(ack)) { ++count; CHECK_EQ(ack.publication_id,count); }
    CHECK_EQ(count,8u); control.AudioBlock(&out,1,true); CHECK(control.TakeApplied(ack)); CHECK_EQ(ack.publication_id,9u);
    CHECK_EQ(control.SubmitGains(gains,UINT32_MAX),PatternRequestStatus::Accepted);
    CHECK_EQ(control.Snapshot().next_gain_id,0u);
    CHECK_EQ(control.SubmitGains(gains,UINT32_MAX),PatternRequestStatus::Identity);
    CHECK_EQ(control.SubmitGains(gains,0),PatternRequestStatus::Identity);
    control.AudioBlock(&out,1,true); CHECK(control.TakeApplied(ack)); CHECK_EQ(ack.publication_id,UINT32_MAX);
}

TEST_CASE(PatternGainReadbackPartialWritesPreserveFramingAndAckPriority) {
    RealtimePattern pattern; pattern.active_rows=1; pattern.active_channels=1;
    PatternTelemetrySender telemetry; PatternSnapshotSender snapshot(pattern,12753);
    PatternOutputMux mux(telemetry,snapshot);
    CHECK(snapshot.Begin(1,0)); telemetry.Offer({false,false,0,0,0,1,1});
    // An already-started telemetry line must finish before a gain reply.
    std::size_t remaining=0; const char* bytes=telemetry.Data(remaining);
    std::string wire(bytes,3); telemetry.Consume(3);
    char state[96]; Pcm16ChannelGains gains;
    CHECK(FormatPatternGainState(gains,UINT32_MAX,0,state,sizeof(state)));
    const std::string replies=std::string("BTGAIN1 APPLIED 1 4294967295\n")+state;
    std::size_t offset=0;
    for(unsigned step=0;step<500 && (offset<replies.size() || snapshot.HasData() || telemetry.HasData());++step) {
        const bool pending=offset<replies.size();
        const auto line_begin=offset==0 ? 0 : replies.rfind('\n',offset-1)+1;
        const auto lane=mux.Select(pending,pending && offset>line_begin);
        if(lane==PatternOutputLane::None) break;
        if(pending && !snapshot.Partial() && !telemetry.Partial()) CHECK_EQ(lane,PatternOutputLane::Command);
        if(lane==PatternOutputLane::Command) {
            bytes=replies.data()+offset;
            const auto end=replies.find('\n',offset); remaining=end+1-offset;
        } else bytes=lane==PatternOutputLane::Position ? telemetry.Data(remaining) : snapshot.Data(remaining);
        const auto written=step%4==0 ? 0 : std::min<std::size_t>(3,remaining);
        wire.append(bytes,written);
        if(lane==PatternOutputLane::Command) offset+=written; else mux.Consume(lane,written);
    }
    CHECK_EQ(offset,replies.size());
    CHECK(wire.find(replies)!=std::string::npos);
    CHECK(wire.find("BTPATTERN1 BEGIN 1 1")!=std::string::npos);
}

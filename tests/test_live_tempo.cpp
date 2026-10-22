#include "test_framework.h"
#include "core/playback/variable_rate_pattern_player.h"
#include "bringup/pattern_control.h"
#include "bringup/variable_rate_player_adapter.h"
#include "bringup/host_tempo.h"
#include "bringup/host_pattern_publication.h"
#include "ui/tempo_repeat.h"
#include "ui/bringup_serial.h"
#include <vector>
#include <algorithm>
#include <string>
using namespace BroTracker;
TEST_CASE(TempoIntegerStepsRoundingClampingAndRepeatThresholds) {
    CHECK_EQ(StepTempo(12500,5),12505); CHECK_EQ(StepTempo(12505,5),12510);
    CHECK_EQ(TempoTenths(12504),1250); CHECK_EQ(TempoTenths(12505),1251);
    CHECK_EQ(StepTempo(3200,-100),3200); CHECK_EQ(StepTempo(30000,100),30000);
    CHECK_EQ(StepTempo(UINT32_MAX,INT32_MAX),30000); CHECK_EQ(StepTempo(0,INT32_MIN),3200);
    TempoRepeat r; CHECK_EQ(r.Begin(1,0),5); CHECK_EQ(r.Tick(399),0);
    CHECK_EQ(r.Tick(400),5); CHECK_EQ(r.Tick(499),0); CHECK_EQ(r.Tick(500),5);
    CHECK_EQ(r.Tick(1400),5); CHECK_EQ(r.Tick(1500),25); CHECK_EQ(r.Tick(2900),25); CHECK_EQ(r.Tick(3000),100);
    CHECK_EQ(r.Tick(10000),100); CHECK_EQ(r.Tick(10000),0); // one repeat after a stall
    r.Cancel(); CHECK_EQ(r.Tick(20000),0); CHECK_EQ(r.Begin(-1,20000),-5); CHECK_EQ(r.Tick(20400),-5);
    CHECK_EQ(r.Begin(1,UINT32_MAX-100),5); CHECK_EQ(r.Tick(298),0); CHECK_EQ(r.Tick(299),5);
}
TEST_CASE(TempoSegmentsPreserveEveryTickAndExactBoundaryAcrossPartitions) {
    auto run=[](unsigned frames) {
        MusicalTickCursor cursor; CHECK_EQ(cursor.Configure(12753,44100),TickCursorStatus::Success);
        std::vector<std::uint64_t> samples; std::uint64_t start=0;
        while(samples.size()<12000) {
            CHECK_EQ(cursor.BeginBlock(start,frames),TickCursorStatus::Success);
            MusicalTick tick;
            while(cursor.Pull(tick)==TickCursorStatus::Tick) {
                CHECK_EQ(tick.tick_index,samples.size()); CHECK_EQ(tick.sample_position,start+tick.sample_offset);
                samples.push_back(tick.sample_position);
                if(tick.tick_index && tick.tick_index%kTicksPerRow==0)
                    CHECK_EQ(cursor.SetTempoAtTick(tick.tick_index,(tick.tick_index/kTicksPerRow)%2?3200:30000),TickCursorStatus::Success);
            }
            start+=frames;
        }
        samples.resize(12000); return samples;
    };
    const auto expected=run(1); CHECK(expected==run(128)); CHECK(expected==run(5292));
    std::uint64_t base=0; unsigned tempo=12753;
    for(unsigned tick=0;tick<expected.size();++tick) {
        const auto row=tick/kTicksPerRow;
        const auto offset=tick%kTicksPerRow;
        if(!offset && tick) { std::uint64_t length=0; CHECK_EQ(TickToSamplePosition(kTicksPerRow,tempo,44100,length),TickToSampleStatus::Success); base+=length; tempo=row%2?3200:30000; }
        std::uint64_t delta=0; CHECK_EQ(TickToSamplePosition(offset,tempo,44100,delta),TickToSampleStatus::Success);
        CHECK_EQ(expected[tick],base+delta);
    }
}
namespace {
struct Fixture {
    RealtimePattern pattern; VariableRateSampleBindings bindings;
    std::vector<std::int16_t> sample;
    Fixture():sample(60000,1000) {
        pattern.active_rows=4; pattern.active_channels=8;
        bindings.count=2; bindings.bindings[0]={0,60,{sample.data(),sample.size(),44100}};
        bindings.bindings[1]={2,0,{},InstrumentType::Synth};
        pattern.cells[0][0]={60,0};
    }
};
std::vector<std::int16_t> Render(VariableRatePatternPlayer& p,unsigned count,unsigned partition=128) {
    std::vector<std::int16_t> result; std::int16_t buffer[128];
    for(unsigned n=0;n<count;) { auto frames=std::min(partition,count-n); CHECK_EQ(p.Render(buffer,frames),VariableRatePatternPlayerStatus::Success); result.insert(result.end(),buffer,buffer+frames);n+=frames; }
    return result;
}
}
TEST_CASE(TempoLiveBoundaryOffDeadlinesAndSampleSynthContinuity) {
    Fixture f; f.pattern.active_rows=64; f.pattern.cells[0][1]={69,2};
    VariableRatePatternPlayer changed,reference;
    for(auto* p:{&changed,&reference}) { CHECK_EQ(p->Configure(12500,44100,f.pattern,f.bindings),VariableRatePatternPlayerStatus::Success); CHECK_EQ(p->Start(),VariableRatePatternPlayerStatus::Success); }
    CHECK(Render(changed,777)==Render(reference,777));
    const auto position=changed.GetPlaybackPosition(); const auto next=changed.GetNextSamplePosition();
    CHECK_EQ(changed.RequestTempo(15000),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(changed.GetTempo(),12500); CHECK_EQ(changed.GetNextSamplePosition(),next);
    CHECK_EQ(changed.GetPlaybackPosition().absolute_tick,position.absolute_tick);
    CHECK(Render(changed,6000)==Render(reference,6000)); CHECK_EQ(changed.GetTempo(),15000);
    CHECK(Render(changed,40000)==Render(reference,40000)); // pitch, phase and one-second Synth envelope unchanged
    f.pattern.cells[0][1]={NOTE_EMPTY,kNoInstrumentUpdate}; f.pattern.cells[1][0]={NOTE_OFF,kNoInstrumentUpdate};
    f.pattern.note_off_timing[1][0]=NoteOffTiming::EndOfPosition;
    VariableRatePatternPlayer p; CHECK_EQ(p.Configure(12500,44100,f.pattern,f.bindings),VariableRatePatternPlayerStatus::Success); CHECK_EQ(p.Start(),VariableRatePatternPlayerStatus::Success);
    (void)Render(p,1000); CHECK_EQ(p.RequestTempo(15000),VariableRatePatternPlayerStatus::Success);
    const auto audio=Render(p,10000,97);
    CHECK_EQ(audio[9701-1000],1000); CHECK_EQ(audio[9702-1000],0);
}
TEST_CASE(TempoPlayingAndBlockBoundaryChangesLoopsAndPartitionIndependence) {
    Fixture f; f.sample.resize(1); f.sample[0]=777; f.bindings.bindings[0].sample={f.sample.data(),1,44100};
    for(unsigned row=0;row<4;++row) f.pattern.cells[row][0]={60,0};
    auto run=[&](unsigned partition) {
        VariableRatePatternPlayer p; CHECK_EQ(p.Configure(12500,44100,f.pattern,f.bindings),VariableRatePatternPlayerStatus::Success); CHECK_EQ(p.Start(),VariableRatePatternPlayerStatus::Success);
        std::vector<std::int16_t> result;
        for(const auto change:{std::pair<unsigned,unsigned>{1000,15000},{5292,10000},{20000,12753},{40000,30000}}) {
            auto part=Render(p,change.first-unsigned(result.size()),partition); result.insert(result.end(),part.begin(),part.end());
            CHECK_EQ(p.RequestTempo(change.second),VariableRatePatternPlayerStatus::Success);
        }
        auto part=Render(p,90000-unsigned(result.size()),partition); result.insert(result.end(),part.begin(),part.end());
        CHECK(p.GetPlaybackPosition().loop_index>0); return result;
    };
    const auto expected=run(1); CHECK(expected==run(128)); CHECK(expected==run(97));
    CHECK_EQ(expected[0],777); CHECK_EQ(expected[5292],777);
    std::vector<std::size_t> impulses,oracle;
    for(std::size_t i=0;i<expected.size();++i) if(expected[i]) impulses.push_back(i);
    const std::pair<unsigned,unsigned> changes[]={{1000,15000},{5292,10000},{20000,12753},{40000,30000}};
    std::uint64_t anchor_tick=0,anchor_sample=0; unsigned tempo=12500,next=0;
    for(std::uint64_t row=0;;++row) {
        std::uint64_t relative=0;CHECK_EQ(TickToSamplePosition(row*kTicksPerRow-anchor_tick,tempo,44100,relative),TickToSampleStatus::Success);
        const auto sample=anchor_sample+relative;if(sample>=expected.size()) break;oracle.push_back(sample);
        unsigned value=0;
        while(next<4 && changes[next].first<=sample) value=changes[next++].second;
        if(value) {anchor_tick=row*kTicksPerRow;anchor_sample=sample;tempo=value;}
    }
    CHECK(impulses==oracle);

}
TEST_CASE(TempoStoppedPausedContinueStopCancelsAndPublicationOrdering) {
    Fixture f; using Control=BasicPatternBringUpControl<VariableRateBringUpPlayer,VariableRateSampleBindings>;
    Control control; CHECK_EQ(control.InitializeAudio(12500,44100,f.pattern,f.bindings),PatternPlayerStatus::Success);
    std::int16_t audio[128]; PatternAppliedRequest ack;
    CHECK_EQ(control.SubmitTempo(13000,1),PatternRequestStatus::Accepted); control.AudioBlock(audio,0,true);
    CHECK_EQ(control.Snapshot().tempo,13000); CHECK_EQ(control.Snapshot().applied_tempo_id,1); CHECK_EQ(control.Snapshot().next_sample,0);
    CHECK_EQ(control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted); control.AudioBlock(audio,128,true); while(control.TakeApplied(ack)) {}
    CHECK_EQ(control.SubmitTempo(14000,2),PatternRequestStatus::Accepted); control.AudioBlock(audio,128,true); CHECK_EQ(control.Snapshot().tempo,13000);
    CHECK_EQ(control.Submit(PatternRequest::Stop),PatternRequestStatus::Accepted); control.AudioBlock(audio,128,true); while(control.TakeApplied(ack)) {}
    CHECK_EQ(control.Snapshot().tempo,13000); CHECK_EQ(control.Snapshot().settled_tempo_id,2); CHECK_EQ(control.Snapshot().pending_tempo_id,0);
    CHECK_EQ(control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted); control.AudioBlock(audio,128,true); while(control.TakeApplied(ack)) {}
    CHECK_EQ(control.Submit(PatternRequest::Pause),PatternRequestStatus::Accepted); control.AudioBlock(audio,128,true); while(control.TakeApplied(ack)) {}
    const auto paused=control.Snapshot(); CHECK_EQ(control.SubmitTempo(14500,3),PatternRequestStatus::Accepted); control.AudioBlock(audio,128,true);
    CHECK_EQ(control.Snapshot().tempo,14500); CHECK_EQ(control.Snapshot().next_sample,paused.next_sample); CHECK_EQ(control.Snapshot().position.absolute_tick,paused.position.absolute_tick);
    CHECK_EQ(control.SubmitPattern(f.pattern,1,15500),PatternRequestStatus::Accepted); control.AudioBlock(audio,0,true); while(control.TakeApplied(ack)) {}
    CHECK_EQ(control.Snapshot().published_tempo,15500); CHECK_EQ(control.Snapshot().tempo,15500);
    CHECK_EQ(control.Submit(PatternRequest::Continue),PatternRequestStatus::Accepted); control.AudioBlock(audio,128,true); while(control.TakeApplied(ack)) {}
    CHECK_EQ(control.Snapshot().tempo,15500); CHECK(control.Snapshot().running);
    CHECK_EQ(control.SubmitTempo(3199,4),PatternRequestStatus::InvalidTempo); CHECK_EQ(control.SubmitTempo(UINT32_MAX,4),PatternRequestStatus::InvalidTempo);
}
TEST_CASE(TempoHostCoalescingReadbackRejectionTimeoutAndReconnect) {
    HostTempo h; auto rx=[&](const char* s){return h.Response(s,std::strlen(s));};
    CHECK(!h.Step(5)); CHECK(rx("BTTEMPO1 CAP 1")); CHECK(rx("BTTEMPO1 STATE 1 0 0 1 0 12500 12500"));
    CHECK(h.Step(5)); CHECK(h.Step(5)); CHECK_EQ(h.Desired(),12510); CHECK(h.Begin());
    char line[96];std::size_t length=0;CHECK(h.FormatLine(line,sizeof(line),length));CHECK_EQ(std::string(line),"BTTEMPO1 SET 1 1 12510\n");
    h.BeforeWrite(0);h.LineComplete();CHECK(h.Step(5));CHECK(h.Step(5));CHECK_EQ(h.Desired(),12520);
    CHECK(rx("BTTEMPO1 APPLIED 1 1"));CHECK(rx("BTTEMPO1 STATE 1 1 1 2 0 12510 12500"));CHECK_EQ(h.Desired(),12520);CHECK(h.Begin());
    h.BeforeWrite(100);h.LineComplete();h.Tick(5100);CHECK_EQ(h.Phase(),HostTempoPhase::Uncertain);CHECK(!h.Step(5));CHECK(h.NeedsRead(5100));
    CHECK(rx("BTTEMPO1 STATE 1 2 2 3 0 12520 12500"));CHECK(h.Synchronized());CHECK(!h.Begin());
    h.Disconnect();CHECK(!h.Supported());CHECK(rx("BTTEMPO1 CAP 1"));CHECK(rx("BTTEMPO1 STATE 1 2 2 3 0 12520 12500"));CHECK_EQ(h.Desired(),12520);CHECK(!h.Begin());
    CHECK(h.Step(5));CHECK(h.Begin());h.BeforeWrite(6000);h.LineComplete();CHECK(rx("BTTEMPO1 REJECTED 1 3"));CHECK(rx("BTTEMPO1 STATE 1 2 2 3 0 12520 12500"));
    CHECK(!h.Begin()); CHECK(h.Synchronized()); CHECK(!rx("BTTEMPO1 STATE 1 2 2 3 0 0 12500"));
    h.Disconnect();CHECK(rx("BTTEMPO1 CAP 1"));CHECK(rx("BTTEMPO1 STATE 1 2 2 4 3 12520 12500"));
    CHECK_EQ(h.Phase(),HostTempoPhase::Uncertain);CHECK(!h.Step(5));
    CHECK(rx("BTTEMPO1 STATE 1 3 3 4 0 13000 12500"));CHECK(h.Synchronized());CHECK_EQ(h.Desired(),13000);CHECK(!h.Begin());
}
TEST_CASE(TempoPublicationVersionTwoFreezesPatternTempoAndChecksums) {
    DevicePatternSnapshot baseline;baseline.version=2;baseline.transfer_id=1;baseline.tempo_hundredths=12500;
    baseline.pattern.active_rows=1;baseline.pattern.active_channels=1;baseline.pattern.cells[0][0]={69,2};
    HostPatternPublication host;CHECK(host.Begin(1,baseline,baseline.pattern,0,13005));
    PatternPublication device; PublicationStatus result=PublicationStatus::Malformed;
    while(host.Phase()==HostPublicationPhase::Sending) {
        char line[96];std::size_t size=0;CHECK(host.FormatLine(line,sizeof(line),size));
        PublicationMessage message;CHECK(ParsePublication(line,size-1,message));result=device.Accept(message,0);
        host.BeforeWrite();host.LineComplete();
    }
    CHECK_EQ(result,PublicationStatus::Ready);CHECK(device.Freeze());CHECK_EQ(device.Tempo(),13005);
    CHECK(host.Response("BTPUBLISH1 SUBMITTED 1 1",std::strlen("BTPUBLISH1 SUBMITTED 1 1"),0));CHECK(host.Response("BTPUBLISH1 APPLIED 1 1",std::strlen("BTPUBLISH1 APPLIED 1 1"),0));
    baseline.tempo_hundredths=13005;CHECK(host.Verify(baseline));CHECK(host.TakeConfirmed());
}

TEST_CASE(TempoStopCancelsAllQueuedTempoAndPreviewOrderingRemainsDeterministic) {
    Fixture f; using Control=BasicPatternBringUpControl<VariableRateBringUpPlayer,VariableRateSampleBindings>;
    Control control; CHECK_EQ(control.InitializeAudio(12500,44100,f.pattern,f.bindings),PatternPlayerStatus::Success);
    std::int16_t audio[128]; PatternAppliedRequest ack;
    CHECK_EQ(control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);control.AudioBlock(audio,128,true);while(control.TakeApplied(ack)) {}
    CHECK_EQ(control.SubmitTempo(13500,1),PatternRequestStatus::Accepted);
    CHECK_EQ(control.SubmitTempo(14500,2),PatternRequestStatus::Accepted);
    CHECK_EQ(control.Submit(PatternRequest::Stop),PatternRequestStatus::Accepted);
    CHECK_EQ(control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    control.AudioBlock(audio,128,true);while(control.TakeApplied(ack)) {}
    CHECK_EQ(control.Snapshot().settled_tempo_id,2);CHECK_EQ(control.Snapshot().pending_tempo_id,0);CHECK_EQ(control.Snapshot().tempo,12500);
    auto preview=f.pattern;preview.cells[1][0]={60,0};
    CHECK_EQ(control.SubmitPreview(preview,1),PatternRequestStatus::Accepted);
    CHECK_EQ(control.SubmitTempo(15000,3),PatternRequestStatus::Accepted);
    for(unsigned i=0;i<50;++i) {control.AudioBlock(audio,128,true);while(control.TakeApplied(ack)) {}}
    CHECK_EQ(control.Snapshot().tempo,15000);CHECK_EQ(control.Snapshot().working_id,1);CHECK_EQ(control.Snapshot().position.loop_index,0);
    CHECK_EQ(control.SubmitTempo(16000,4),PatternRequestStatus::Accepted);
    control.AudioBlock(nullptr,128,false);
    CHECK_EQ(control.Snapshot().applied_tempo_id,3);CHECK_EQ(control.Snapshot().settled_tempo_id,4);
}

TEST_CASE(TempoRestoreGoalCoalescesWithAnInflightAdjustment) {
    HostTempo host; auto reply=[&](const char* line) {return host.Response(line,std::strlen(line));};
    CHECK(reply("BTTEMPO1 CAP 1"));CHECK(reply("BTTEMPO1 STATE 1 0 0 1 0 12500 12500"));
    CHECK(host.Request(13005));CHECK(host.Begin());host.BeforeWrite(0);host.LineComplete();
    CHECK(host.Request(12500)); // Restore uses the frozen published snapshot baseline.
    CHECK(reply("BTTEMPO1 APPLIED 1 1"));CHECK(reply("BTTEMPO1 STATE 1 1 1 2 0 13005 12500"));
    CHECK_EQ(host.Desired(),12500);CHECK(host.Begin());char line[96];std::size_t length=0;
    CHECK(host.FormatLine(line,sizeof(line),length));CHECK_EQ(std::string(line),"BTTEMPO1 SET 1 2 12500\n");
}

namespace {
struct TempoSerialFake : SerialTransport {
    std::string rx,tx; bool healthy=true;
    bool Open(FILE*,const char*) override {healthy=true;return true;}
    void Close() override {}
    bool Healthy() override {return healthy;}
    int Read(char* out,unsigned size) override {size=static_cast<unsigned>(std::min<std::size_t>(size,rx.size()));std::memcpy(out,rx.data(),size);rx.erase(0,size);return size;}
    int Write(const char* text,unsigned size) override {tx.append(text,size);return size;}
    const char* Error() const override {return "test";}
};
}
TEST_CASE(TempoSerialCapabilityCoalescingStopPriorityAndHybridPublicationRejection) {
    auto fake=std::make_unique<TempoSerialFake>();auto* wire=fake.get();BringUpSerial serial(nullptr,std::move(fake));
    serial.Tick(0);wire->rx="BTTEST1 STATE IDLE\nBTTEMPO1 CAP 1\nBTTEMPO1 STATE 1 0 0 1 0 12500 12500\nBTPUBLISH1 CAP 1\n";serial.Tick(1);
    CHECK(serial.Connected());CHECK(serial.TempoAvailable());
    LocalPatternEditor editor;CHECK(!serial.Publish(editor,1));CHECK(std::strstr(serial.PublicationStatus(),"Tempo-aware publication unsupported"));
    CHECK(serial.StepTempo(5));CHECK(serial.StepTempo(5));serial.Tick(2);
    CHECK(wire->tx.find("BTTEMPO1 SET 1 1 12510\n")!=std::string::npos);
    CHECK(serial.StepTempo(5));CHECK_EQ(serial.Tempo().Desired(),12515);
    wire->rx="BTTEMPO1 APPLIED 1 1\nBTTEMPO1 STATE 1 1 1 2 0 12510 12500\n";serial.Tick(3);
    CHECK(wire->tx.find("BTTEMPO1 SET 1 2 12515\n")!=std::string::npos);
    const auto prior=wire->tx.size();CHECK(serial.Stop());serial.Tick(4);
    CHECK(wire->tx.substr(prior).find("BTTEST1 STOP\n")==0);
    wire->rx="BTTEST1 STOPPED\nBTTEMPO1 CANCELLED 1 2\nBTTEMPO1 STATE 1 2 1 3 0 12510 12500\n";serial.Tick(5);
    CHECK_EQ(serial.Tempo().State().tempo,12510);CHECK(wire->tx.find("BTTEMPO1 SET 1 3")==std::string::npos);
    wire->healthy=false;serial.Tick(6);CHECK(!serial.Connected());CHECK(!serial.StepTempo(5));
}

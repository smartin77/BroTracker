#include "test_framework.h"
#include "bringup/pattern_publication.h"
#include "bringup/pattern_control.h"
#include <string>
#include <vector>
#include <cstring>

using namespace BroTracker;
namespace {
    const std::int16_t pcm[] = {10,20,30,40,50,60,70,80};
    NativeRateSampleBindings Bindings() {
        NativeRateSampleBindings b{}; b.count=1; b.bindings[0]={7,60,{pcm,8,100}}; return b;
    }
    RealtimePattern Pattern() {
        RealtimePattern p; p.active_rows=2; p.active_channels=1;
        p.cells[0][0]={60,7}; p.cells[1][0]={NOTE_OFF,255};
        p.note_off_timing[1][0]=NoteOffTiming::EndOfPosition; return p;
    }
    std::vector<std::string> Transfer(const RealtimePattern& p, unsigned id) {
        std::vector<std::string> lines;
        lines.push_back("BTPUBLISH1 BEGIN 1 "+std::to_string(id)+" "+std::to_string(p.active_rows)+" "+
            std::to_string(p.active_channels)+" "+std::to_string(p.active_rows*p.active_channels));
        auto hash=PublicationHeaderHash(p.active_rows,p.active_channels);
        unsigned index=0;
        for(unsigned r=0;r<p.active_rows;++r) for(unsigned c=0;c<p.active_channels;++c) {
            const auto cell=p.cells[r][c]; const auto timing=static_cast<unsigned>(p.note_off_timing[r][c]);
            lines.push_back("BTPUBLISH1 CELL 1 "+std::to_string(id)+" "+std::to_string(index++)+" "+
                std::to_string(cell.note)+" "+std::to_string(cell.instrument)+" "+std::to_string(timing));
            hash=SnapshotHashByte(SnapshotHashByte(SnapshotHashByte(hash,cell.note),cell.instrument),
                static_cast<std::uint8_t>(timing));
        }
        lines.push_back("BTPUBLISH1 COMMIT 1 "+std::to_string(id)+" "+std::to_string(index)+" "+std::to_string(hash));
        return lines;
    }
    PublicationStatus Feed(PatternPublication& p, const std::string& line, unsigned now=0) {
        PublicationMessage message;
        if (!ParsePublication(line.data(),line.size(),message)) { p.Cancel(); return PublicationStatus::Malformed; }
        return p.Accept(message,now);
    }
    void Stage(PatternPublication& p, const RealtimePattern& pattern, unsigned id) {
        const auto lines=Transfer(pattern,id);
        for(unsigned i=0;i<lines.size();++i)
            CHECK_EQ(Feed(p,lines[i]),i+1==lines.size() ? PublicationStatus::Ready : PublicationStatus::Receiving);
        CHECK(p.Freeze()); CHECK(p.Frozen());
    }
    void Same(const RealtimePattern& a, const RealtimePattern& b) {
        CHECK_EQ(a.active_rows,b.active_rows); CHECK_EQ(a.active_channels,b.active_channels);
        for(unsigned r=0;r<kRealtimePatternRowCapacity;++r) for(unsigned c=0;c<kRealtimePatternChannelCapacity;++c) {
            CHECK_EQ(a.cells[r][c].note,b.cells[r][c].note);
            CHECK_EQ(a.cells[r][c].instrument,b.cells[r][c].instrument);
            CHECK_EQ(a.note_off_timing[r][c],b.note_off_timing[r][c]);
        }
    }
}

TEST_CASE(PublicationAudioApplyReadbackAndImmutableHandoff) {
    auto active=Pattern(); auto next=active; next.active_rows=3; next.cells[0][0]={NOTE_EMPTY,7};
    next.cells[1][0]={60,255}; next.note_off_timing[1][0]=NoteOffTiming::Arrival;
    next.cells[2][0]={NOTE_OFF,255}; next.note_off_timing[2][0]=NoteOffTiming::EndOfPosition;
    PatternPublication publication; PatternBringUpControl control;
    CHECK_EQ(control.InitializeAudio(12753,100,active,Bindings()),PatternPlayerStatus::Success);
    Stage(publication,next,1);
    CHECK_EQ(control.SubmitPattern(*publication.Frozen(),1),PatternRequestStatus::Accepted);
    CHECK_EQ(control.SubmitPattern(*publication.Frozen(),1),PatternRequestStatus::Full);
    const auto frozen=*publication.Frozen();
    CHECK_EQ(Feed(publication,Transfer(active,2)[0]),PublicationStatus::Busy);
    CHECK_EQ(Feed(publication,Transfer(next,1).back()),PublicationStatus::Busy);
    publication.NewSession(); publication.Cancel(); CHECK(!publication.Tick(6000)); Same(*publication.Frozen(),frozen);
    CHECK(!publication.Complete(2,true,active)); Same(active,Pattern());
    std::int16_t out[12]; control.AudioBlock(out,1,true);
    CHECK_EQ(control.Snapshot().transport,PatternTransportState::Stopped);
    CHECK_EQ(control.Snapshot().next_sample,0); CHECK(!control.Snapshot().position.valid);
    PatternAppliedRequest applied; CHECK(control.TakeApplied(applied));
    CHECK_EQ(applied.request,PatternRequest::Replace); CHECK_EQ(applied.publication_id,1);
    CHECK_EQ(applied.result,PatternPlayerStatus::Success);
    Same(active,Pattern()); // Readback changes only after acknowledgement synchronization.
    CHECK(publication.Complete(applied.publication_id,true,active)); Same(active,next);
    // Exact v2 snapshot readback and OFF metadata from the applied source.
    PatternSnapshotSender sender(active,12753); PatternSnapshotAssembler assembly;
    CHECK(sender.Begin(1,0,2)); CHECK(assembly.Request(1,0,2));
    while(sender.HasData()) {
        std::size_t count; const auto* data=sender.Data(count);
        (void)assembly.Accept(data,count-1,1); sender.Consume(count);
    }
    CHECK(assembly.Snapshot()); Same(assembly.Snapshot()->pattern,next);
    CHECK_EQ(Feed(publication,Transfer(next,1)[0]),PublicationStatus::Identity); // Lost ACK is not retried.
    CHECK_EQ(control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    control.AudioBlock(out,12,true); CHECK_EQ(out[11],10); // Retained tempo/rate/binding.
}

TEST_CASE(PublicationRejectsMalformedOrderCountChecksumAndTimeout) {
    const auto pattern=Pattern(); const auto good=Transfer(pattern,1);
    for (const auto& bad : {std::string("BTPUBLISH1 BEGIN 2 1 2 1 2"),
        std::string("BTPUBLISH1 BEGIN 1 0 2 1 2"),std::string("BTPUBLISH1 BEGIN 1 4294967296 2 1 2"),
        std::string("BTPUBLISH1 CELL 1 1 0 254 255 2 extra"),std::string(96,'x')}) {
        PublicationMessage untouched{PublicationKind::Commit,{99,0,0,0,0}}, output=untouched;
        CHECK(!ParsePublication(bad.data(),bad.size(),output)); CHECK_EQ(output.fields[0],99);
    }
    for(unsigned failure=0;failure<8;++failure) {
        PatternPublication p; CHECK_EQ(Feed(p,good[0]),PublicationStatus::Receiving);
        PublicationStatus result;
        switch(failure) {
        case 0: result=Feed(p,"BTPUBLISH1 CELL 1 1 1 60 7 0"); break; // Out of order.
        case 1: result=Feed(p,"BTPUBLISH1 CELL 1 2 0 60 7 0"); break; // Identity.
        case 2: result=Feed(p,"BTPUBLISH1 CELL 1 1 0 23 7 0"); break;
        case 3: result=Feed(p,"BTPUBLISH1 CELL 1 1 0 60 256 0"); break;
        case 4: result=Feed(p,"BTPUBLISH1 CELL 1 1 0 60 7 2"); break;
        case 5: result=Feed(p,good.back()); break; // Incomplete.
        case 6: Feed(p,good[1]); result=Feed(p,good[1]); break; // Duplicate.
        default: Feed(p,good[1]); Feed(p,good[2]); result=Feed(p,"BTPUBLISH1 COMMIT 1 1 2 0"); break;
        }
        CHECK(result != PublicationStatus::Receiving && result != PublicationStatus::Ready);
        CHECK(!p.Busy()); CHECK(!p.Frozen());
    }
    PatternPublication p;
    CHECK_EQ(Feed(p,"BTPUBLISH1 BEGIN 1 1 17 1 17"),PublicationStatus::Dimensions);
    CHECK_EQ(Feed(p,"BTPUBLISH1 BEGIN 1 1 2 1 3"),PublicationStatus::Dimensions);
    Feed(p,good[0]); CHECK(!p.Tick(4999)); CHECK(p.Tick(5000)); CHECK(!p.Freeze());
    CHECK_EQ(Feed(p,good.back()),PublicationStatus::Identity);
    Feed(p,Transfer(pattern,2)[0]); p.NewSession(); CHECK(!p.Busy());
    CHECK_EQ(Feed(p,Transfer(pattern,2)[0]),PublicationStatus::Identity);
    Stage(p,pattern,3); p.NewSession(); CHECK(p.Submitted());
}

TEST_CASE(PublicationAudioStateAndResolutionRejectionPreserveOldPattern) {
    for(unsigned mode=0;mode<5;++mode) {
        auto active=Pattern(); PatternPublication p; PatternBringUpControl c;
        CHECK_EQ(c.InitializeAudio(12753,100,active,Bindings()),PatternPlayerStatus::Success);
        std::int16_t out[1]; PatternAppliedRequest ack;
        if (mode<2) {
            CHECK_EQ(c.Submit(PatternRequest::Start),PatternRequestStatus::Accepted); c.AudioBlock(out,1,true); CHECK(c.TakeApplied(ack));
            if(mode==1) { CHECK_EQ(c.Submit(PatternRequest::Pause),PatternRequestStatus::Accepted); c.AudioBlock(out,1,true); CHECK(c.TakeApplied(ack)); }
        }
        auto candidate=active;
        if(mode==2) candidate.cells[0][0].instrument=255;
        if(mode==3) candidate.cells[0][0].instrument=99;
        if(mode==4) candidate.cells[0][0].note=61;
        Stage(p,candidate,1); CHECK_EQ(c.SubmitPattern(*p.Frozen(),1),PatternRequestStatus::Accepted);
        const auto state=c.Snapshot(); c.AudioBlock(out,1,true); CHECK(c.TakeApplied(ack));
        CHECK_EQ(ack.result,mode<2 ? PatternPlayerStatus::InvalidTransportState :
            mode==2 ? PatternPlayerStatus::MissingInstrument : mode==3 ? PatternPlayerStatus::UnknownInstrument : PatternPlayerStatus::UnsupportedPitch);
        CHECK_EQ(c.Snapshot().transport,state.transport);
        CHECK(p.Complete(ack.publication_id,false,active)); Same(active,Pattern());
        CHECK_EQ(c.Submit(PatternRequest::Stop),PatternRequestStatus::Accepted); c.AudioBlock(out,1,true); CHECK(c.TakeApplied(ack));
        CHECK_EQ(c.Submit(PatternRequest::Start),PatternRequestStatus::Accepted); c.AudioBlock(out,1,true); CHECK_EQ(out[0],10);
    }
}

TEST_CASE(PublicationSharesBoundedAckFifoAndFairReadbackMux) {
    auto active=Pattern(); PatternPublication p; PatternBringUpControl c;
    CHECK_EQ(c.InitializeAudio(12753,100,active,Bindings()),PatternPlayerStatus::Success);
    for(unsigned i=0;i<kPatternRequestCapacity;++i) CHECK_EQ(c.Submit(PatternRequest::Stop),PatternRequestStatus::Accepted);
    Stage(p,active,1); CHECK_EQ(c.SubmitPattern(*p.Frozen(),1),PatternRequestStatus::Full);
    p.Finish(); // Definitely unsubmitted: rejection allows release, not a host timeout.
    std::int16_t out; c.AudioBlock(&out,1,true); // Fill ACK FIFO.
    Stage(p,active,2); CHECK_EQ(c.SubmitPattern(*p.Frozen(),2),PatternRequestStatus::Accepted);
    c.AudioBlock(&out,1,true); CHECK(p.Submitted());
    PatternAppliedRequest ack; for(unsigned i=0;i<kPatternRequestCapacity;++i) { CHECK(c.TakeApplied(ack)); CHECK_EQ(ack.request,PatternRequest::Stop); }
    c.AudioBlock(&out,1,true); CHECK(c.TakeApplied(ack)); CHECK_EQ(ack.publication_id,2);
    CHECK(p.Complete(2,true,active));
    PatternSnapshotSender snapshot(active,12753); PatternTelemetrySender position;
    PatternOutputMux mux(position,snapshot); CHECK(snapshot.Begin(1,0,2));
    position.Offer({false,false,0,0,0,2,1});
    CHECK_EQ(mux.Select(true,false),PatternOutputLane::Command); // APPLIED first.
    std::size_t count; (void)position.Data(count); mux.Consume(PatternOutputLane::Position,2);
    CHECK_EQ(mux.Select(true,false),PatternOutputLane::Position); // Finish partial line.
    (void)position.Data(count); mux.Consume(PatternOutputLane::Position,count);
    CHECK_EQ(mux.Select(true,false),PatternOutputLane::Command);
    CHECK_EQ(mux.Select(false,false),PatternOutputLane::Snapshot);
    (void)snapshot.Data(count); mux.Consume(PatternOutputLane::Snapshot,count);
    position.Offer({false,false,0,0,0,2,1}); CHECK_EQ(mux.Select(false,false),PatternOutputLane::Position);
}

TEST_CASE(PublicationAndSnapshotsCannotMixRevisions) {
    auto active=Pattern(); PatternPublication p; PatternSnapshotSender snapshot(active,12753);
    PublicationMessage begin; const auto line=Transfer(active,1)[0];
    CHECK(ParsePublication(line.data(),line.size(),begin));
    CHECK(snapshot.Begin(1,0,2,p.Busy()));
    CHECK_EQ(p.Accept(begin,0,snapshot.HasData()),PublicationStatus::SnapshotBusy); CHECK(!p.Busy());
    snapshot.Clear(); CHECK_EQ(p.Accept(begin,0,snapshot.HasData()),PublicationStatus::Receiving);
    CHECK(!snapshot.Begin(2,0,2,p.Busy()));
    p.NewSession(); CHECK(snapshot.Begin(2,0,2,p.Busy())); snapshot.Clear();
    Stage(p,active,2); CHECK(!snapshot.Begin(3,0,2,p.Busy()));
    p.NewSession(); CHECK(!snapshot.Begin(3,0,2,p.Busy()));
    CHECK(p.Complete(2,true,active)); CHECK(snapshot.Begin(3,0,2,p.Busy()));
}

TEST_CASE(PublicationChecksAudioStateInTransportFifoOrderAndFullCapacity) {
    auto active=Pattern(); PatternPublication p; PatternBringUpControl control;
    CHECK_EQ(control.InitializeAudio(12753,100,active,Bindings()),PatternPlayerStatus::Success);
    Stage(p,active,1);
    CHECK_EQ(control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    CHECK_EQ(control.SubmitPattern(*p.Frozen(),1),PatternRequestStatus::Accepted);
    CHECK_EQ(control.Snapshot().transport,PatternTransportState::Stopped); // Not the application authority.
    CHECK_EQ(control.Submit(PatternRequest::Stop),PatternRequestStatus::Accepted);
    std::int16_t out; control.AudioBlock(&out,1,true); PatternAppliedRequest ack;
    CHECK(control.TakeApplied(ack)); CHECK_EQ(ack.request,PatternRequest::Start);
    CHECK(control.TakeApplied(ack)); CHECK_EQ(ack.result,PatternPlayerStatus::InvalidTransportState);
    CHECK(p.Complete(1,false,active)); CHECK(control.TakeApplied(ack)); CHECK_EQ(ack.request,PatternRequest::Stop);
    CHECK_EQ(control.Snapshot().transport,PatternTransportState::Stopped); CHECK_EQ(out,0);
    RealtimePattern maximum;
    Stage(p,maximum,2); CHECK_EQ(p.Frozen()->active_rows,16); CHECK_EQ(p.Frozen()->active_channels,8);
    CHECK_EQ(control.SubmitPattern(*p.Frozen(),2),PatternRequestStatus::Accepted);
    control.AudioBlock(&out,1,true); CHECK(control.TakeApplied(ack)); CHECK_EQ(ack.result,PatternPlayerStatus::Success);
    CHECK(p.Complete(2,true,active)); Same(active,maximum);
    CHECK_EQ(control.Snapshot().next_sample,0);
}
TEST_CASE(PublicationChecksumIncludesDimensionsAndOffTiming) {
    const auto pattern=Pattern(); auto lines=Transfer(pattern,1);
    for(unsigned kind=0;kind<2;++kind) {
        PatternPublication p;
        if(kind==0) lines[0]="BTPUBLISH1 BEGIN 1 1 1 2 2"; // Same count, changed dimensions.
        else { lines=Transfer(pattern,1); lines[2].back()='0'; } // Valid but tampered OFF mode.
        Feed(p,lines[0]); Feed(p,lines[1]); Feed(p,lines[2]);
        CHECK_EQ(Feed(p,lines[3]),PublicationStatus::Completion); CHECK(!p.Freeze());
    }
}

TEST_CASE(PublicationNextIdentityDiscoverySurvivesSessionsAndExhaustion) {
    PatternPublication p; char line[64];
    const auto discover = [&](std::uint32_t expected) {
        CHECK_EQ(p.NextIdentity(),expected); CHECK(p.FormatDiscovery(line,sizeof(line)));
        CHECK_EQ(std::string(line),"BTPUBLISH1 NEXT 1 "+std::to_string(expected)+"\n");
    };
    discover(1);
    auto active=Pattern(); PatternBringUpControl control;
    CHECK_EQ(control.InitializeAudio(12753,100,active,Bindings()),PatternPlayerStatus::Success);
    Stage(p,active,1); discover(2);
    const auto frozen=*p.Frozen(); p.NewSession(); discover(2); Same(*p.Frozen(),frozen);
    CHECK_EQ(control.SubmitPattern(*p.Frozen(),1),PatternRequestStatus::Accepted);
    std::int16_t out; control.AudioBlock(&out,1,true); PatternAppliedRequest ack; CHECK(control.TakeApplied(ack));
    CHECK(p.Complete(1,true,active)); discover(2);
    Feed(p,Transfer(active,2)[0]); p.NewSession(); discover(3); // HELLO/disconnect cancellation.
    Feed(p,Transfer(active,3)[0]); CHECK(p.Tick(5000)); discover(4);
    Feed(p,Transfer(active,4)[0]); CHECK_EQ(Feed(p,"BTPUBLISH1 CELL 1 4 0 60 7 9"),PublicationStatus::Cell); discover(5);
    Stage(p,active,5); CHECK(p.Complete(5,false,active)); discover(6); // Rejected after staging.
    CHECK_EQ(Feed(p,"BTPUBLISH1 BEGIN 1 99 17 1 17"),PublicationStatus::Dimensions); discover(6);
    const auto before=control.Snapshot();
    for(unsigned i=0;i<3;++i) { p.NewSession(); discover(6); }
    CHECK_EQ(control.Snapshot().transport,before.transport); CHECK_EQ(control.Snapshot().next_sample,before.next_sample);
    CHECK_EQ(control.Snapshot().position.valid,before.position.valid); Same(active,Pattern());
    Stage(p,active,UINT32_MAX); discover(0); p.NewSession(); discover(0);
    CHECK(p.Complete(UINT32_MAX,false,active)); discover(0);
    CHECK_EQ(Feed(p,Transfer(active,UINT32_MAX)[0]),PublicationStatus::Identity);
    CHECK_EQ(Feed(p,Transfer(active,1)[0]),PublicationStatus::Identity); discover(0);
}

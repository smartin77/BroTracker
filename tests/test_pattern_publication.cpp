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
    CHECK_EQ(Feed(p,"BTPUBLISH1 BEGIN 1 1 65 1 65"),PublicationStatus::Dimensions);
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
        if(mode==1) candidate.active_rows=1;
        if(mode==2) candidate.cells[0][0].instrument=255;
        if(mode==3) candidate.cells[0][0].instrument=99;
        if(mode==4) candidate.cells[0][0].note=61;
        Stage(p,candidate,1); CHECK_EQ(c.SubmitPattern(*p.Frozen(),1),PatternRequestStatus::Accepted);
        const auto state=c.Snapshot(); c.AudioBlock(out,1,true); CHECK(c.TakeApplied(ack));
        CHECK_EQ(ack.result,mode==0 ? PatternPlayerStatus::InvalidTransportState : mode==1 ? PatternPlayerStatus::InvalidDimensions :
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
    CHECK_EQ(Feed(p,"BTPUBLISH1 BEGIN 1 99 65 1 65"),PublicationStatus::Dimensions); discover(6);
    const auto before=control.Snapshot();
    for(unsigned i=0;i<3;++i) { p.NewSession(); discover(6); }
    CHECK_EQ(control.Snapshot().transport,before.transport); CHECK_EQ(control.Snapshot().next_sample,before.next_sample);
    CHECK_EQ(control.Snapshot().position.valid,before.position.valid); Same(active,Pattern());
    Stage(p,active,UINT32_MAX); discover(0); p.NewSession(); discover(0);
    CHECK(p.Complete(UINT32_MAX,false,active)); discover(0);
    CHECK_EQ(Feed(p,Transfer(active,UINT32_MAX)[0]),PublicationStatus::Identity);
    CHECK_EQ(Feed(p,Transfer(active,1)[0]),PublicationStatus::Identity); discover(0);
}

TEST_CASE(PausedPublicationSynchronizesReadbackWithoutRestartAndDiscoveryFits) {
    CHECK_EQ(kPatternDiscoveryReplyCount,13);
    const char* discovery[]={"BTTEST1 STATE PAUSED","BTPATTERN1 SNAPCAP 2","BTPATTERN1 SNAPCAP 1",
        "BTPATTERN1 TRANSPORTCAP 1",kPatternPublicationCapability,kPausedPatternPublicationCapability,"BTPUBLISH1 NEXT 1 1",
        "BTPREVIEW1 CAP 1",kSparsePreviewCapability,"BTPREVIEW1 NEXT 1 1","BTPREVIEW1 STATE 1 0 0 0","BTGAIN1 CAP 1",
        "BTGAIN1 STATE 1 0 1 65536 65536 65536 65536 65536 65536 65536 65536"};
    CHECK_EQ(sizeof(discovery)/sizeof(discovery[0]),kPatternDiscoveryReplyCount);
    for(const auto* line:discovery) CHECK(std::strlen(line)<kPatternSnapshotLineCapacity);
    auto active=Pattern(); PatternPublication p; PatternBringUpControl control;
    CHECK_EQ(control.InitializeAudio(12753,100,active,Bindings()),PatternPlayerStatus::Success);
    CHECK_EQ(control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    std::int16_t out[32]{}; control.AudioBlock(out,24,true); PatternAppliedRequest ack; CHECK(control.TakeApplied(ack));
    CHECK_EQ(control.Submit(PatternRequest::Pause),PatternRequestStatus::Accepted);
    control.AudioBlock(out,1,true); CHECK(control.TakeApplied(ack)); const auto before=control.Snapshot();
    auto next=active; next.cells[before.position.pattern_row][0]={NOTE_OFF,255};
    next.note_off_timing[before.position.pattern_row][0]=NoteOffTiming::EndOfPosition;
    Stage(p,next,1); CHECK_EQ(control.SubmitPattern(*p.Frozen(),1),PatternRequestStatus::Accepted);
    control.AudioBlock(out,1,true); CHECK(control.TakeApplied(ack)); CHECK_EQ(ack.result,PatternPlayerStatus::Success);
    CHECK_EQ(control.Snapshot().transport,PatternTransportState::Paused); CHECK_EQ(out[0],0);
    CHECK_EQ(control.Snapshot().position.absolute_tick,before.position.absolute_tick);
    CHECK_EQ(control.Snapshot().position.loop_index,before.position.loop_index);
    CHECK_EQ(control.Snapshot().next_sample,before.next_sample); Same(active,Pattern());
    CHECK(p.Complete(1,true,active)); Same(active,next);
    CHECK_EQ(control.Submit(PatternRequest::Continue),PatternRequestStatus::Accepted);
    control.AudioBlock(out,1,true); CHECK(control.TakeApplied(ack)); CHECK_EQ(out[0],0);
    CHECK_EQ(control.Snapshot().position.pattern_row,before.position.pattern_row);
}


#include "bringup/pattern_preview.h"
namespace {
    PublicationStatus PreviewFeed(PatternPublication& p,std::string line,unsigned now=0,bool busy=false) {
        line.replace(0,10,"BTPREVIEW1"); PublicationMessage message;
        if (!ParsePublication(line.data(),line.size(),message,true)) { p.Cancel(); return PublicationStatus::Malformed; }
        return p.Accept(message,now,busy);
    }
    void PreviewStage(PatternPublication& p,const RealtimePattern& pattern,unsigned id) {
        const auto lines=Transfer(pattern,id);
        for(unsigned i=0;i<lines.size();++i)
            CHECK_EQ(PreviewFeed(p,lines[i]),i+1==lines.size()?PublicationStatus::Ready:PublicationStatus::Receiving);
        CHECK(p.Freeze());
    }
    void PreviewDrain(PatternBringUpControl& c,PatternPublication& transfer,PatternPreviewReadback& state,
        std::vector<PatternAppliedRequest>& log) {
        PatternAppliedRequest ack;
        while(c.TakeApplied(ack)) {
            log.push_back(ack);
            if(ack.request==PatternRequest::Preview) CHECK(state.Accepted(transfer,ack.publication_id,ack.result==PatternPlayerStatus::Success));
            if(ack.request==PatternRequest::PreviewApplied) CHECK(state.Applied(ack.publication_id));
            if(ack.request==PatternRequest::PreviewSuperseded) state.Superseded(ack.publication_id);
        }
    }
}
TEST_CASE(PreviewTransferStrictValidationIdentityTimeoutAndFrozenSession) {
    const auto pattern=Pattern(); PatternPublication p; char discovery[96];
    CHECK(FormatPreviewDiscovery(p,discovery,sizeof(discovery))); CHECK_EQ(std::string(discovery),"BTPREVIEW1 NEXT 1 1\n");
    auto lines=Transfer(pattern,1);
    CHECK_EQ(PreviewFeed(p,lines[0],0,true),PublicationStatus::SnapshotBusy);
    CHECK_EQ(p.NextIdentity(),1);
    CHECK_EQ(PreviewFeed(p,lines[0]),PublicationStatus::Receiving);
    CHECK_EQ(PreviewFeed(p,"BTPUBLISH1 CELL 1 1 1 60 7 0"),PublicationStatus::Cell);
    CHECK(!p.Busy()); CHECK_EQ(p.NextIdentity(),2); // Rejected staging still consumes ID.
    CHECK_EQ(PreviewFeed(p,lines[0]),PublicationStatus::Identity);
    lines=Transfer(pattern,2); CHECK_EQ(PreviewFeed(p,lines[0]),PublicationStatus::Receiving);
    CHECK_EQ(PreviewFeed(p,"BTPUBLISH1 CELL 1 2 0 60 7 2"),PublicationStatus::Cell);
    CHECK_EQ(PreviewFeed(p,"BTPUBLISH1 BEGIN 1 4294967296 2 1 2"),PublicationStatus::Malformed);
    lines=Transfer(pattern,3); CHECK_EQ(PreviewFeed(p,lines[0]),PublicationStatus::Receiving);
    CHECK(p.Tick(5000)); CHECK_EQ(p.NextIdentity(),4); p.NewSession(); CHECK_EQ(p.NextIdentity(),4);
    lines=Transfer(pattern,4);
    for(unsigned i=0;i+1<lines.size();++i) CHECK_EQ(PreviewFeed(p,lines[i]),PublicationStatus::Receiving);
    CHECK_EQ(PreviewFeed(p,"BTPUBLISH1 COMMIT 1 4 2 0"),PublicationStatus::Completion);
    PreviewStage(p,pattern,5); const auto frozen=*p.Frozen(); p.NewSession(); p.Cancel();
    CHECK(!p.Tick(UINT32_MAX)); Same(*p.Frozen(),frozen);
    CHECK_EQ(PreviewFeed(p,Transfer(pattern,5).back()),PublicationStatus::Busy);
    RealtimePattern ignored; CHECK(p.Complete(5,false,ignored));
    CHECK_EQ(PreviewFeed(p,Transfer(pattern,5).back()),PublicationStatus::Identity);
    PreviewStage(p,pattern,UINT32_MAX); CHECK(p.Complete(UINT32_MAX,false,ignored));
    CHECK(FormatPreviewDiscovery(p,discovery,sizeof(discovery))); CHECK_EQ(std::string(discovery),"BTPREVIEW1 NEXT 1 0\n");
    CHECK(!FormatPreviewState(UINT32_MAX,UINT32_MAX,UINT32_MAX,discovery,8));
    CHECK(FormatPreviewState(UINT32_MAX,UINT32_MAX,UINT32_MAX,discovery,sizeof(discovery)));
}
TEST_CASE(PreviewAudioAcceptanceApplicationSupersessionAndSeparateBaseline) {
    auto baseline=Pattern(); baseline.cells[1][0]={60,7};
    PatternPreviewReadback metadata; metadata.working=baseline;
    PatternPublication preview; PatternBringUpControl control;
    CHECK_EQ(control.InitializeAudio(12753,100,baseline,Bindings()),PatternPlayerStatus::Success);
    CHECK_EQ(control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    std::int16_t out[128]{}; control.AudioBlock(out,5,true); PatternAppliedRequest ack; CHECK(control.TakeApplied(ack));
    auto next=baseline; next.cells[1][0]={NOTE_OFF,255};
    PreviewStage(preview,next,1); CHECK_EQ(control.SubmitPreview(*preview.Frozen(),1),PatternRequestStatus::Accepted);
    CHECK_EQ(control.SubmitPattern(baseline,1),PatternRequestStatus::Full); // Frozen conflict.
    control.AudioBlock(out,1,true); CHECK_EQ(out[0],60);
    std::vector<PatternAppliedRequest> events; PreviewDrain(control,preview,metadata,events);
    CHECK_EQ(events.size(),1); CHECK_EQ(events[0].request,PatternRequest::Preview);
    CHECK_EQ(metadata.pending_id,1); CHECK_EQ(metadata.working_id,0); Same(metadata.working,baseline);
    CHECK_EQ(control.Snapshot().pending_preview_id,1);
    // Reconnect does not clear accepted candidate or playback; cancelled staging does not apply.
    preview.NewSession(); CHECK_EQ(preview.NextIdentity(),2);
    auto newest=next; newest.note_off_timing[1][0]=NoteOffTiming::EndOfPosition;
    PreviewStage(preview,newest,2); CHECK_EQ(control.SubmitPreview(*preview.Frozen(),2),PatternRequestStatus::Accepted);
    control.AudioBlock(out,1,true); PreviewDrain(control,preview,metadata,events);
    CHECK_EQ(events[1].request,PatternRequest::PreviewSuperseded); CHECK_EQ(events[1].publication_id,1);
    CHECK_EQ(metadata.pending_id,2);
    CHECK_EQ(control.Submit(PatternRequest::Pause),PatternRequestStatus::Accepted);
    control.AudioBlock(out,1,true); PreviewDrain(control,preview,metadata,events);
    CHECK_EQ(metadata.pending_id,2); CHECK_EQ(control.Snapshot().transport,PatternTransportState::Paused);
    CHECK_EQ(control.Submit(PatternRequest::Continue),PatternRequestStatus::Accepted);
    control.AudioBlock(out,1,true); PreviewDrain(control,preview,metadata,events);
    CHECK_EQ(metadata.working_id,2); CHECK_EQ(metadata.pending_id,0); CHECK_EQ(out[0],10);
    Same(metadata.working,newest); CHECK_EQ(baseline.cells[1][0].note,60);
    // Playing publication still rejects, preview rejection preserves applied metadata.
    PatternPublication publish; Stage(publish,baseline,1);
    CHECK_EQ(control.SubmitPattern(*publish.Frozen(),1),PatternRequestStatus::Accepted);
    control.AudioBlock(out,0,true); CHECK(control.TakeApplied(ack)); CHECK_EQ(ack.result,PatternPlayerStatus::InvalidTransportState);
    CHECK(publish.Complete(1,false,baseline)); CHECK_EQ(metadata.working_id,2);
    auto bad=newest; bad.cells[0][0]={61,7}; PreviewStage(preview,bad,3);
    CHECK_EQ(control.SubmitPreview(*preview.Frozen(),3),PatternRequestStatus::Accepted);
    control.AudioBlock(out,0,true); PreviewDrain(control,preview,metadata,events);
    CHECK_EQ(events.back().result,PatternPlayerStatus::UnsupportedPitch); CHECK_EQ(metadata.working_id,2);
    CHECK_EQ(control.Submit(PatternRequest::Stop),PatternRequestStatus::Accepted);
    control.AudioBlock(out,1,true); CHECK(control.TakeApplied(ack)); CHECK_EQ(out[0],0);
    Stage(publish,baseline,2); CHECK_EQ(control.SubmitPattern(*publish.Frozen(),2),PatternRequestStatus::Accepted);
    control.AudioBlock(out,1,true); CHECK(control.TakeApplied(ack)); CHECK_EQ(ack.result,PatternPlayerStatus::Success);
    CHECK(publish.Complete(2,true,baseline)); metadata.Published(baseline,2);
    CHECK_EQ(metadata.working_id,0); CHECK_EQ(metadata.published_id,2); Same(metadata.working,baseline);
}
TEST_CASE(PreviewActualBoundaryReadbackPartialLinesAndBackpressure) {
    auto baseline=Pattern(); baseline.cells[1][0]={60,7}; auto next=baseline; next.cells[1][0]={NOTE_OFF,255};
    PatternBringUpControl control; PatternPublication transfer; PatternPreviewReadback state; state.working=baseline;
    CHECK_EQ(control.InitializeAudio(12753,100,baseline,Bindings()),PatternPlayerStatus::Success);
    CHECK_EQ(control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    std::int16_t out[128]{}; control.AudioBlock(out,10,true); PatternAppliedRequest ack; CHECK(control.TakeApplied(ack));
    PreviewStage(transfer,next,1); CHECK_EQ(control.SubmitPreview(*transfer.Frozen(),1),PatternRequestStatus::Accepted);
    control.AudioBlock(out,1,true); CHECK_EQ(control.Snapshot().working_id,0); // [10,11), boundary at 11 excluded.
    std::vector<PatternAppliedRequest> log; PreviewDrain(control,transfer,state,log);
    control.AudioBlock(out,1,true); CHECK_EQ(out[0],0); PreviewDrain(control,transfer,state,log);
    CHECK_EQ(log.back().request,PatternRequest::PreviewApplied); CHECK_EQ(state.working_id,1); Same(state.working,next);
    const auto frozen_readback=state.working;
    PatternSnapshotSender sender(frozen_readback,12753); CHECK(sender.SetWorkingNamespace(true)); CHECK(sender.Begin(9,0,2));
    PatternTelemetrySender telemetry; telemetry.Offer({true,true,96,1,0,2,1}); PatternOutputMux mux(telemetry,sender);
    std::string result; unsigned steps=0;
    while(sender.HasData() || telemetry.HasData()) {
        CHECK(++steps<2000); if(steps>=2000) break;
        const auto lane=mux.Select(false,false); std::size_t count=0;
        const auto* bytes=lane==PatternOutputLane::Position?telemetry.Data(count):sender.Data(count);
        const auto n=std::min<std::size_t>(3,count); result.append(bytes,n); mux.Consume(lane,n);
    }
    CHECK(result.find("BTPREVIEW1 BEGIN 2 9 2 1 12753 2\n")!=std::string::npos);
    CHECK(result.find("BTPREVIEW1 CELL 2 9 1 254 255 1\n")!=std::string::npos);
    CHECK(result.find("BTPREVIEW1 END 2 9 2 ")!=std::string::npos);
    CHECK(result.find("BTPATTERN1 POS ")!=std::string::npos);
    // Publication source remains baseline, and discovery does not mutate transport.
    const auto before=control.Snapshot(); char line[96]; CHECK(FormatPreviewDiscovery(transfer,line,sizeof(line)));
    CHECK_EQ(control.Snapshot().next_sample,before.next_sample);
}

TEST_CASE(PreviewStopStartAndPublicationSupersedePendingWithoutBaselineMutation) {
    auto baseline=Pattern(); auto next=baseline; next.cells[1][0]={60,7};
    PatternBringUpControl c; PatternPublication transfer; PatternPreviewReadback state; state.working=baseline;
    CHECK_EQ(c.InitializeAudio(12753,100,baseline,Bindings()),PatternPlayerStatus::Success);
    CHECK_EQ(c.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    std::int16_t out[32]{}; c.AudioBlock(out,5,true); PatternAppliedRequest ack; CHECK(c.TakeApplied(ack));
    PreviewStage(transfer,next,1); CHECK_EQ(c.SubmitPreview(*transfer.Frozen(),1),PatternRequestStatus::Accepted);
    std::vector<PatternAppliedRequest> events;
    c.AudioBlock(out,0,true); PreviewDrain(c,transfer,state,events); CHECK_EQ(state.pending_id,1);
    CHECK_EQ(c.Submit(PatternRequest::Stop),PatternRequestStatus::Accepted);
    c.AudioBlock(out,1,true); PreviewDrain(c,transfer,state,events);
    CHECK_EQ(events[1].request,PatternRequest::Stop); CHECK_EQ(events[2].request,PatternRequest::PreviewApplied);
    CHECK_EQ(state.working_id,1); CHECK_EQ(c.Snapshot().next_sample,0); CHECK_EQ(out[0],0);
    CHECK_EQ(c.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    c.AudioBlock(out,5,true); PreviewDrain(c,transfer,state,events);
    PreviewStage(transfer,baseline,2); CHECK_EQ(c.SubmitPreview(*transfer.Frozen(),2),PatternRequestStatus::Accepted);
    c.AudioBlock(out,0,true); PreviewDrain(c,transfer,state,events); CHECK_EQ(state.pending_id,2);
    CHECK_EQ(c.Submit(PatternRequest::Pause),PatternRequestStatus::Accepted);
    c.AudioBlock(out,0,true); PreviewDrain(c,transfer,state,events);
    const auto before=c.Snapshot(); PatternPublication publication; Stage(publication,next,3);
    CHECK_EQ(c.SubmitPattern(*publication.Frozen(),3),PatternRequestStatus::Accepted);
    c.AudioBlock(out,1,true);
    CHECK(c.TakeApplied(ack)); CHECK_EQ(ack.request,PatternRequest::PreviewSuperseded); CHECK_EQ(ack.publication_id,2);
    state.Superseded(2); CHECK(c.TakeApplied(ack)); CHECK_EQ(ack.request,PatternRequest::Replace);
    CHECK_EQ(ack.result,PatternPlayerStatus::Success); CHECK(publication.Complete(3,true,baseline)); state.Published(baseline,3);
    CHECK_EQ(state.pending_id,0); CHECK_EQ(state.working_id,0); CHECK_EQ(c.Snapshot().published_id,3);
    CHECK_EQ(c.Snapshot().transport,PatternTransportState::Paused); CHECK_EQ(c.Snapshot().position.absolute_tick,before.position.absolute_tick);
    Same(state.working,next);
}

TEST_CASE(PreviewImmediateStoppedPausedAndAckBackpressure) {
    const auto baseline=Pattern(); auto next=baseline; next.cells[1][0]={60,7};
    for(bool paused:{false,true}) {
        PatternBringUpControl c; PatternPublication p; PatternPreviewReadback state; state.working=baseline;
        CHECK_EQ(c.InitializeAudio(12753,100,baseline,Bindings()),PatternPlayerStatus::Success);
        std::int16_t out[32]{}; PatternAppliedRequest ack;
        if(paused) {
            CHECK_EQ(c.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
            c.AudioBlock(out,5,true); CHECK(c.TakeApplied(ack));
            CHECK_EQ(c.Submit(PatternRequest::Pause),PatternRequestStatus::Accepted);
            c.AudioBlock(out,1,true); CHECK(c.TakeApplied(ack));
        }
        const auto before=c.Snapshot(); PreviewStage(p,next,1);
        CHECK_EQ(c.SubmitPreview(*p.Frozen(),1),PatternRequestStatus::Accepted);
        p.NewSession(); CHECK(p.Submitted()); // Frozen storage is NOT cancelled by reconnect.
        c.AudioBlock(out,1,true); std::vector<PatternAppliedRequest> events; PreviewDrain(c,p,state,events);
        CHECK_EQ(events.size(),2); CHECK_EQ(events[0].request,PatternRequest::Preview); CHECK_EQ(events[1].request,PatternRequest::PreviewApplied);
        CHECK_EQ(out[0],0); CHECK_EQ(c.Snapshot().transport,before.transport); CHECK_EQ(c.Snapshot().next_sample,before.next_sample);
        CHECK_EQ(state.working_id,1); Same(state.working,next);
    }
    PatternBringUpControl c; PatternPublication transfer; PatternPreviewReadback state; state.working=baseline;
    CHECK_EQ(c.InitializeAudio(12753,100,baseline,Bindings()),PatternPlayerStatus::Success);
    for(unsigned i=0;i<6;++i) CHECK_EQ(c.Submit(PatternRequest::Stop),PatternRequestStatus::Accepted);
    std::int16_t out[32]{}; c.AudioBlock(out,1,true);
    PreviewStage(transfer,next,1); CHECK_EQ(c.SubmitPreview(*transfer.Frozen(),1),PatternRequestStatus::Accepted);
    c.AudioBlock(out,1,true); CHECK_EQ(c.Snapshot().working_id,0); CHECK(transfer.Submitted());
    std::vector<PatternAppliedRequest> events; PreviewDrain(c,transfer,state,events); CHECK_EQ(events.size(),6);
    c.AudioBlock(out,1,true); PreviewDrain(c,transfer,state,events);
    CHECK_EQ(events.size(),8); CHECK_EQ(state.working_id,1); CHECK(!transfer.Busy());
}

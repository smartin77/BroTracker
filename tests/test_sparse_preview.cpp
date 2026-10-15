#include "test_framework.h"
#include "bringup/host_pattern_preview.h"
#include "bringup/pattern_preview.h"
#include "bringup/pattern_control.h"
#include "ui/local_pattern_editor.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
using namespace BroTracker;
namespace {
struct SparseFixture {
    DevicePatternSnapshot baseline;
    HostPatternPreview host;
    PatternPublication device;
    PatternPreviewReadback metadata;
    PatternBringUpControl owner;
    std::int16_t quiet[256]{},loud[256]{};
    unsigned now=0;
    std::vector<std::string> sent;
    SparseFixture(unsigned rows=32,bool capability=true) {
        std::fill_n(quiet,256,10); std::fill_n(loud,256,90);
        baseline.version=2;baseline.transfer_id=1;baseline.tempo_hundredths=12753;
        baseline.pattern.active_rows=static_cast<std::uint8_t>(rows);baseline.pattern.active_channels=8;
        baseline.pattern.cells[0][0]={60,7}; metadata.working=baseline.pattern;
        NativeRateSampleBindings bindings; bindings.count=2;
        bindings.bindings[0]={7,60,{quiet,256,100}};bindings.bindings[1]={8,60,{loud,256,100}};
        CHECK_EQ(owner.InitializeAudio(12753,100,baseline.pattern,bindings),PatternPlayerStatus::Success);
        host.SetBaseline(baseline); Discover(capability); Read();host.RecoveredInstalled();
    }
    bool Reply(const std::string& text) {return host.Response(text.data(),text.size(),now);}
    void Discover(bool capability=true) {
        host.DiscoveryRequested();CHECK(Reply("BTPREVIEW1 CAP 1"));
        if(capability) CHECK(Reply(kSparsePreviewCapability));
        CHECK(Reply("BTPREVIEW1 NEXT 1 "+std::to_string(device.NextIdentity())));
        CHECK(Reply("BTPREVIEW1 STATE 1 0 "+std::to_string(metadata.working_id)+" "+std::to_string(metadata.pending_id)));
    }
    void Read() {
        char text[96];std::size_t length=0;CHECK(host.BeginRead(1,now,text,sizeof(text),length));
        const unsigned selector=std::string(text,length).find(" 1\n")!=std::string::npos?1:0;
        CHECK(Reply("BTPREVIEW1 READ 1 1 "+std::to_string(selector)+" 0 "+std::to_string(selector?metadata.pending_id:metadata.working_id)));
        PatternSnapshotSender snapshot(selector?metadata.pending:metadata.working,12753);
        CHECK(snapshot.SetWorkingNamespace(true));CHECK(snapshot.Begin(1,now,2));
        for(unsigned budget=0;snapshot.HasData() && budget<514;++budget) {
            const char* data=snapshot.Data(length);CHECK(Reply(std::string(data,length-1)));snapshot.Consume(length);
        }
        CHECK(!snapshot.HasData());
    }
    PublicationStatus Receive(const std::string& text,bool reply=true) {
        SparsePreviewMessage message{};CHECK(ParseSparsePreview(text.data(),text.size(),message));
        const auto base_id=metadata.pending_id?metadata.pending_id:metadata.working_id;
        const auto result=device.AcceptSparse(message,now,metadata.pending_id?metadata.pending:metadata.working,base_id);
        if(reply && message.kind==PublicationKind::Begin && result==PublicationStatus::Receiving)
            CHECK(Reply("BTPREVIEW1 RECEIVING 1 "+std::to_string(message.fields[0])));
        if(reply && result==PublicationStatus::Ready) {
            CHECK(device.Freeze());CHECK_EQ(owner.SubmitPreview(*device.Frozen(),device.Identity()),PatternRequestStatus::Accepted);
            CHECK(Reply("BTPREVIEW1 SUBMITTED 1 "+std::to_string(message.fields[0])));
        }
        return result;
    }
    // Full lines are frozen by the asynchronous owner even when completion is zero.
    std::size_t Transfer(unsigned fragment=96) {
        std::size_t bytes=0;
        for(unsigned budget=0;host.Sending() && budget<514;++budget) {
            char line[96];std::size_t length=0;CHECK(host.FormatLine(line,sizeof(line),length));
            host.BeforeWrite();const std::string frozen(line,length);std::string assembled;
            for(std::size_t offset=0;offset<length;) {
                char check[96];std::size_t count=0;CHECK(host.FormatLine(check,sizeof(check),count));
                CHECK_EQ(std::string(check,count),frozen);
                const auto n=std::min<std::size_t>(fragment,length-offset);assembled.append(line+offset,n);offset+=n;
            }
            host.LineComplete();sent.push_back(assembled);bytes+=length;
            CHECK(Receive(assembled.substr(0,length-1))== (host.Sending()?PublicationStatus::Receiving:PublicationStatus::Ready));
            ++now;
        }
        CHECK(!host.Sending());return bytes;
    }
    std::vector<std::int16_t> Audio(unsigned frames,bool require_reply=true) {
        std::int16_t pcm[128]{};owner.AudioBlock(pcm,frames,true);
        PatternAppliedRequest ack;
        for(unsigned budget=0;owner.TakeApplied(ack) && budget<16;++budget) {
            if(ack.request==PatternRequest::Preview) {
                const bool ok=ack.result==PatternPlayerStatus::Success;
                const char* reason=ack.result==PatternPlayerStatus::MissingInstrument?" missing-instrument":" unknown-instrument";
                CHECK(metadata.Accepted(device,ack.publication_id,ok));
                CHECK(!require_reply || Reply("BTPREVIEW1 "+std::string(ok?"ACCEPTED":"REJECTED")+" 1 "+std::to_string(ack.publication_id)+
                    (ok?"":reason)));
            } else if(ack.request==PatternRequest::PreviewApplied) {
                CHECK(metadata.Applied(ack.publication_id));CHECK(!require_reply || Reply("BTPREVIEW1 APPLIED 1 "+std::to_string(ack.publication_id)));
            } else if(ack.request==PatternRequest::PreviewSuperseded) {
                metadata.Superseded(ack.publication_id);CHECK(!require_reply || Reply("BTPREVIEW1 SUPERSEDED 1 "+std::to_string(ack.publication_id)));
            }
        }
        return {pcm,pcm+frames};
    }
};
}
TEST_CASE(SparsePreviewOneCellWireSizeIndependentOfPatternSizeAndExactPcm) {
    std::size_t reference=0;
    for(unsigned rows:{16u,32u,64u}) {
        SparseFixture f(rows);auto edit=f.baseline.pattern;edit.cells[1][0]={60,8};
        CHECK_EQ(f.owner.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
        const auto prefix=f.Audio(5);for(auto x:prefix) CHECK_EQ(x,10);
        f.now=10;f.host.Observe(edit,f.now);f.now=85;CHECK(f.host.TryBegin(f.baseline,f.now));
        const auto bytes=f.Transfer(1);CHECK_EQ(f.sent.size(),3);CHECK(f.sent[0].find("DBEGIN 1 1 0 1")!=std::string::npos);
        if(!reference)reference=bytes;CHECK_EQ(bytes,reference);CHECK(bytes<128);
        f.now=89;f.Audio(1);CHECK_EQ(f.host.Phase(),HostPreviewPhase::Accepted);
        f.now=95;const auto tail=f.Audio(10); // samples 6..15, absolute floor boundary at sample 11
        for(unsigned i=0;i<10;++i) CHECK_EQ(tail[i],i<5?10:90);
        CHECK_EQ(f.host.Phase(),HostPreviewPhase::Synchronized);CHECK(!f.host.Recovering());
        CHECK_EQ(f.metadata.working_id,1);CHECK_EQ(f.metadata.working.cells[1][0].instrument,8);
        CHECK_EQ(f.baseline.pattern.cells[1][0].note,NOTE_EMPTY); // published baseline stays independent
        std::printf("Sparse modeled timing rows=%u bytes=%zu edit=10 TX=85 ACCEPTED=89 APPLIED=95 ms (injected, not hardware)\n",rows,bytes);
    }
}
TEST_CASE(SparsePreviewCoalescesDifferentCellsPendingSupersessionAndRestore) {
    SparseFixture f;CHECK_EQ(f.owner.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);f.Audio(5);
    auto edit=f.baseline.pattern;edit.cells[1][0]={60,8};f.host.Observe(edit,10);f.now=85;CHECK(f.host.TryBegin(f.baseline,85));
    // Rapid pitches and navigation affect latest slots, never the in-flight revision.
    for(Note note:{Note(61),Note(62),Note(60)}) {edit.cells[1][0].note=note;f.host.Observe(edit,90);}
    edit.cells[1][7]={60,8};f.host.Observe(edit,90);f.Transfer(2);f.Audio(0);
    CHECK_EQ(f.metadata.pending_id,1);f.Discover();f.now=165;CHECK(f.host.TryBegin(f.baseline,165));
    f.sent.clear();f.Transfer(3);CHECK_EQ(f.sent.size(),3); // only the newly changed CH8 cell
    CHECK(f.sent[1].find(" 1 7 60 8 0")!=std::string::npos);f.Audio(0);CHECK_EQ(f.metadata.pending_id,2);
    f.Audio(8);CHECK_EQ(f.metadata.working_id,2);CHECK_EQ(f.host.Phase(),HostPreviewPhase::Synchronized);
    f.Discover();f.host.Observe(f.baseline.pattern,200);f.now=275;CHECK(f.host.TryBegin(f.baseline,275));
    f.sent.clear();f.Transfer();CHECK_EQ(f.sent.size(),4);f.Audio(16);
    CHECK(SamePattern(f.metadata.working,f.baseline.pattern));CHECK_EQ(f.host.Phase(),HostPreviewPhase::Synchronized);
    CHECK_EQ(f.owner.Submit(PatternRequest::Stop),PatternRequestStatus::Accepted);for(auto x:f.Audio(8)) CHECK_EQ(x,0);
}
TEST_CASE(SparsePreviewStaleDuplicatesInvalidValuesAndTemporaryMissingInstrumentAreAtomic) {
    SparseFixture f;auto before=f.baseline.pattern;
    CHECK_EQ(f.Receive("BTPREVIEW1 DBEGIN 1 1 9 1",false),PublicationStatus::StaleBase);CHECK_EQ(f.device.NextIdentity(),1);
    CHECK_EQ(f.Receive("BTPREVIEW1 DBEGIN 1 1 0 1",false),PublicationStatus::Receiving);
    CHECK_EQ(f.Receive("BTPREVIEW1 DCELL 1 1 0 64 0 60 8 0",false),PublicationStatus::Cell);
    CHECK_EQ(f.Receive("BTPREVIEW1 DBEGIN 1 1 0 1",false),PublicationStatus::Identity);
    CHECK(SamePattern(f.metadata.working,before));
    auto edit=before;edit.cells[1][0]={60,99};f.Discover();f.host.Observe(edit,10);f.now=85;CHECK(f.host.TryBegin(f.baseline,85));
    f.Transfer();f.Audio(0);CHECK_EQ(f.host.Phase(),HostPreviewPhase::Rejected);CHECK(SamePattern(f.metadata.working,before));
    edit.cells[1][0].instrument=8;f.Discover();f.host.Observe(edit,100);f.now=175;CHECK(f.host.TryBegin(f.baseline,175));
    f.Transfer();f.Audio(0);CHECK_EQ(f.host.Phase(),HostPreviewPhase::Synchronized);CHECK_EQ(f.metadata.working.cells[1][0].instrument,8);
}
TEST_CASE(SparsePreviewMaximumRestoreTimeoutReconnectAndLegacyFallback) {
    SparseFixture f(64);auto edit=f.baseline.pattern;
    for(auto& row:edit.cells)for(auto& cell:row)cell={60,8};
    f.host.Observe(edit,1);f.now=76;CHECK(f.host.TryBegin(f.baseline,76));CHECK(f.Transfer()<40000);CHECK_EQ(f.sent.size(),514);
    f.Audio(0);CHECK_EQ(f.host.Phase(),HostPreviewPhase::Synchronized);CHECK(SamePattern(f.metadata.working,edit));
    f.Discover();f.host.Observe(f.baseline.pattern,100);f.now=175;CHECK(f.host.TryBegin(f.baseline,175));f.Transfer(7);
    f.host.Tick(7000);CHECK(f.host.Recovering());f.Audio(0,false); // ACK can be late: reconcile from authoritative STATE/readback
    f.host.RetainLocal(true);f.host.Disconnect();f.Discover();f.Read();
    CHECK(SamePattern(f.metadata.working,f.baseline.pattern));
    SparseFixture legacy(16,false);auto old=legacy.baseline.pattern;old.cells[1][0]={60,8};legacy.host.Observe(old,1);
    CHECK(legacy.host.TryBegin(legacy.baseline,76));char text[96];std::size_t n=0;
    CHECK(legacy.host.FormatLine(text,sizeof(text),n));CHECK(std::string(text,n).find("BTPREVIEW1 BEGIN 1")==0);
}
TEST_CASE(SparseEditorCapturesCompleteCellValuesIndependentOfCursor) {
    LocalPatternEditor editor;std::optional<LivePlaybackView> live=LivePlaybackView{};
    live->telemetry_available=true;live->position.rows=32;live->position.channels=8;
    DevicePatternSnapshot baseline;baseline.version=2;baseline.transfer_id=1;baseline.tempo_hundredths=12753;
    baseline.pattern.active_rows=32;baseline.pattern.active_channels=8;baseline.pattern.cells[0][0]={60,7};
    live->device_pattern=baseline;editor.Sync(live);CHECK(editor.Apply(EditorAction::Increment));CHECK(editor.Apply(EditorAction::Increment));
    CHECK(editor.Apply(EditorAction::NextChannel));CHECK(editor.Apply(EditorAction::NoteOff));
    CHECK(editor.Apply(EditorAction::Right));CHECK(editor.Apply(EditorAction::Increment));
    SparsePreviewCell first,second;CHECK(editor.TakeCapturedCell(0,first));CHECK(editor.TakeCapturedCell(1,second));
    CHECK_EQ(first.row,0);CHECK_EQ(first.channel,0);CHECK_EQ(first.note,62);CHECK_EQ(first.instrument,7);
    CHECK_EQ(second.channel,1);CHECK_EQ(second.note,NOTE_OFF);CHECK_EQ(second.instrument,255);CHECK_EQ(second.timing,NoteOffTiming::EndOfPosition);
    CHECK(!editor.TakeCapturedCell(0,first));CHECK(editor.Apply(EditorAction::Restore));
    CHECK(editor.TakeCapturedCell(0,first));CHECK_EQ(first.note,60);CHECK(editor.TakeCapturedCell(1,second));CHECK_EQ(second.note,NOTE_EMPTY);
    editor.Sync(std::nullopt);CHECK(!editor.TakeCapturedCell(0,first));
}

TEST_CASE(SparsePreviewStrictOrderingChecksumBaseAndFrozenOwnership) {
    const auto parse=[](const char* text) {SparsePreviewMessage m{};CHECK(ParseSparsePreview(text,std::strlen(text),m));return m;};
    SparsePreviewMessage sentinel{PublicationKind::Commit,{99}};
    for(const char* bad:{"BTPREVIEW1 DBEGIN 1 1 0 4294967296","BTPREVIEW1 DBEGIN 2 1 0 1",
        "BTPREVIEW1 DCELL 1 1 0 0 0 60 7","BTPREVIEW1 DCELL 1 1 0 0 0 60 7 -1",
        "BTPREVIEW1 DCOMMIT 1 1 1 0 extra"}) {
        CHECK(!ParseSparsePreview(bad,std::strlen(bad),sentinel));CHECK_EQ(sentinel.fields[0],99);
    }
    for(const char* bad:{"BTPREVIEW1 DCELL 1 1 0 0 0 23 7 0","BTPREVIEW1 DCELL 1 1 0 0 0 60 256 0",
        "BTPREVIEW1 DCELL 1 1 0 0 0 60 7 2","BTPREVIEW1 DCELL 1 1 0 0 8 60 7 0"}) {
        SparseFixture f;CHECK_EQ(f.Receive("BTPREVIEW1 DBEGIN 1 1 0 1",false),PublicationStatus::Receiving);
        CHECK_EQ(f.Receive(bad,false),PublicationStatus::Cell);CHECK_EQ(f.metadata.working_id,0);
    }
    SparseFixture f;auto& d=f.device;const auto& base=f.baseline.pattern;
    CHECK_EQ(d.AcceptSparse(parse("BTPREVIEW1 DBEGIN 1 1 0 2"),0,base,0),PublicationStatus::Receiving);
    CHECK_EQ(d.AcceptSparse(parse("BTPREVIEW1 DCELL 1 1 0 1 0 60 8 0"),1,base,0),PublicationStatus::Receiving);
    CHECK_EQ(d.AcceptSparse(parse("BTPREVIEW1 DCELL 1 1 1 1 0 60 8 0"),2,base,0),PublicationStatus::Cell); // duplicate coordinate
    CHECK_EQ(d.AcceptSparse(parse("BTPREVIEW1 DBEGIN 1 2 0 1"),3,base,0),PublicationStatus::Receiving);
    CHECK_EQ(d.AcceptSparse(parse("BTPREVIEW1 DCELL 1 2 0 1 0 60 8 0"),4,base,0),PublicationStatus::Receiving);
    CHECK_EQ(d.AcceptSparse(parse("BTPREVIEW1 DCOMMIT 1 2 1 0"),5,base,0),PublicationStatus::Completion);
    CHECK_EQ(d.AcceptSparse(parse("BTPREVIEW1 DBEGIN 1 3 0 1"),6,base,0),PublicationStatus::Receiving);
    CHECK_EQ(d.AcceptSparse(parse("BTPREVIEW1 DCELL 1 3 0 1 0 60 8 0"),7,base,1),PublicationStatus::StaleBase);
    CHECK_EQ(d.AcceptSparse(parse("BTPREVIEW1 DBEGIN 1 4 0 1"),8,base,0),PublicationStatus::Receiving);
    CHECK(d.Tick(5008));CHECK_EQ(d.NextIdentity(),5);
    const SparsePreviewCell cell{1,0,60,8,NoteOffTiming::EndOfPosition};
    CHECK_EQ(d.AcceptSparse(parse("BTPREVIEW1 DBEGIN 1 5 0 1"),5010,base,0),PublicationStatus::Receiving);
    CHECK_EQ(d.AcceptSparse(parse("BTPREVIEW1 DCELL 1 5 0 1 0 60 8 1"),5011,base,0),PublicationStatus::Receiving);
    const auto commit="BTPREVIEW1 DCOMMIT 1 5 1 "+std::to_string(SparsePreviewHashCell(SparsePreviewHeaderHash(5,0,1),cell));
    CHECK_EQ(d.AcceptSparse(parse(commit.c_str()),5012,base,0),PublicationStatus::Ready);CHECK(d.Freeze());
    const auto frozen=*d.Frozen();d.NewSession();d.Cancel();CHECK(!d.Tick(20000));CHECK(SamePattern(frozen,*d.Frozen()));
    CHECK_EQ(d.AcceptSparse(parse(commit.c_str()),5013,base,0),PublicationStatus::Busy);
    d.Finish();CHECK_EQ(d.AcceptSparse(parse(commit.c_str()),5014,base,0),PublicationStatus::Identity);
    CHECK_EQ(f.metadata.working_id,0);CHECK_EQ(f.metadata.working.cells[1][0].note,NOTE_EMPTY);
}

TEST_CASE(SparsePreviewMissingInstrumentDraftCanBeCompletedWithoutResendingRejectedRevision) {
    SparseFixture f;auto draft=f.baseline.pattern;draft.cells[2][7]={60,255};
    f.host.Observe(draft,1);f.now=76;CHECK(f.host.TryBegin(f.baseline,76));f.Transfer();f.Audio(0);
    CHECK_EQ(f.host.Phase(),HostPreviewPhase::Rejected);CHECK(std::strstr(f.host.Status(),"missing-instrument"));
    CHECK_EQ(f.metadata.working.cells[2][7].note,NOTE_EMPTY);f.Discover();CHECK(!f.host.TryBegin(f.baseline,1000));
    draft.cells[2][7].instrument=8;f.host.Observe(draft,1001);f.now=1076;CHECK(f.host.TryBegin(f.baseline,1076));
    f.Transfer();f.Audio(0);CHECK_EQ(f.host.Phase(),HostPreviewPhase::Synchronized);
    CHECK_EQ(f.metadata.working.cells[2][7].instrument,8);CHECK_EQ(f.metadata.working.cells[0][0].instrument,7);
}

TEST_CASE(SparseReviewUnexpectedStateCannotRelabelCachedCellsAsANewBase) {
    SparseFixture f;auto bad=f.baseline.pattern;bad.cells[1][0]={60,99};
    f.host.Observe(bad,1);f.now=76;CHECK(f.host.TryBegin(f.baseline,76));f.Transfer();f.Audio(0);
    CHECK_EQ(f.host.Phase(),HostPreviewPhase::Rejected);
    // An authoritative revision change must invalidate cached cells in every phase,
    // including rejection. It cannot relabel the old cache with identity 20.
    CHECK_EQ(f.Receive("BTPREVIEW1 DBEGIN 1 20 0 1",false),PublicationStatus::Receiving);
    CHECK_EQ(f.Receive("BTPREVIEW1 DCELL 1 20 0 3 7 60 8 0",false),PublicationStatus::Receiving);
    CHECK_EQ(f.Receive("BTPREVIEW1 DCOMMIT 1 20 1 1432349850",false),PublicationStatus::Ready);
    CHECK(f.device.Freeze());CHECK_EQ(f.owner.SubmitPreview(*f.device.Frozen(),20),PatternRequestStatus::Accepted);
    f.Audio(0,false); // actually applied through the owner, but its ACKs were not observed
    f.Discover();CHECK(f.host.Recovering());
    if(!f.host.Recovering()) return;
    auto latest=f.baseline.pattern;latest.cells[2][0]={60,8};f.host.Observe(latest,100);
    CHECK(!f.host.TryBegin(f.baseline,175)); // no patch against unread revision 20
    f.Read();f.host.RecoveredInstalled();CHECK(f.host.TryBegin(f.baseline,175));
    char line[96];std::size_t n=0;CHECK(f.host.FormatLine(line,sizeof(line),n));
    CHECK(std::string(line,n).find(" 20 2\n")!=std::string::npos); // add desired cell AND remove unexpected remote cell
    f.Transfer();f.Audio(0);CHECK(SamePattern(f.metadata.working,latest));
    CHECK_EQ(f.owner.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    const auto pcm=f.Audio(40); // exact floor row-2 boundary: floor(1200000/51012) = 23
    for(unsigned i=0;i<40;++i)CHECK_EQ(pcm[i],i<23?10:90); // no unexpected CH8 note at row 3
}
TEST_CASE(SparseReviewRestoreToVerifiedWorkingNeedsNoTransferAndClearsRejection) {
    SparseFixture f;auto bad=f.baseline.pattern;bad.cells[1][0]={60,99};
    f.host.Observe(bad,1);f.now=76;CHECK(f.host.TryBegin(f.baseline,76));f.Transfer();f.Audio(0);
    f.Discover();f.host.Observe(f.baseline.pattern,100);CHECK(!f.host.TryBegin(f.baseline,175));
    CHECK_EQ(f.host.Phase(),HostPreviewPhase::Synchronized);
    CHECK(std::strstr(f.host.Status(),"synchronized"));
}
TEST_CASE(SparseReviewRejectedNewerDraftThenRestoreAndOlderApplicationConverge) {
    SparseFixture f;CHECK_EQ(f.owner.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);f.Audio(5);
    auto first=f.baseline.pattern;first.cells[1][0]={60,8};f.host.Observe(first,1);f.now=76;
    CHECK(f.host.TryBegin(f.baseline,76));f.Transfer();f.Audio(0);CHECK_EQ(f.metadata.pending_id,1);
    f.Discover();auto bad=first;bad.cells[2][0]={60,99};f.host.Observe(bad,100);f.now=175;
    CHECK(f.host.TryBegin(f.baseline,175));f.Transfer();f.Audio(0);CHECK_EQ(f.host.Phase(),HostPreviewPhase::Rejected);
    f.Discover();f.host.Observe(first,200);CHECK(!f.host.TryBegin(f.baseline,275));
    CHECK_EQ(f.host.Phase(),HostPreviewPhase::Accepted);f.Audio(8);
    CHECK_EQ(f.host.Phase(),HostPreviewPhase::Synchronized);CHECK(SamePattern(f.metadata.working,first));
}
TEST_CASE(SparseReviewRecoveryInvalidatesUninstalledReadbackAndContextResetClearsEdits) {
    SparseFixture f;f.host.ReadFailure();f.Discover();f.Read();CHECK(f.host.RecoveredDraft());
    CHECK(f.Reply("BTPREVIEW1 STATE 1 0 21 0"));CHECK(f.host.Recovering());CHECK(!f.host.RecoveredDraft());
    LocalPatternEditor editor;std::optional<LivePlaybackView> live=LivePlaybackView{};
    live->telemetry_available=true;live->position.rows=32;live->position.channels=8;live->device_pattern=f.baseline;
    editor.Sync(live);CHECK(editor.Apply(EditorAction::Increment));editor.ResetContext();
    SparsePreviewCell value;CHECK(!editor.TakeCapturedCell(0,value));
}

TEST_CASE(SparseReviewMetadataBoundsAndExhaustionNeverTruncateOrConsumeAnotherIdentity) {
    SparseFixture f(64);auto invalid=f.baseline;invalid.pattern.active_rows=255;f.host.SetBaseline(invalid);
    invalid=f.baseline;invalid.pattern.active_channels=255;f.host.SetBaseline(invalid);
    invalid=f.baseline;invalid.tempo_hundredths=0;f.host.SetBaseline(invalid);
    auto draft=f.baseline.pattern;draft.cells[63][7]={60,8};f.host.Observe(draft,1);f.now=76;
    CHECK(f.host.TryBegin(f.baseline,76));f.Transfer();f.Audio(0);
    CHECK_EQ(f.metadata.working.cells[63][7].instrument,8);CHECK_EQ(f.metadata.working.active_rows,64);
    f.Discover();draft.active_rows=255;draft.active_channels=255;f.host.Observe(draft,99);
    f.host.ObserveCell({200,200,60,8,NoteOffTiming::Arrival},100); // absolute storage bounds, not just caller dimensions
    CHECK(!f.host.TryBegin(f.baseline,175));
    draft.active_rows=32;draft.active_channels=8;f.host.Observe(draft,100);CHECK(!f.host.TryBegin(f.baseline,175));
    CHECK_EQ(f.host.Phase(),HostPreviewPhase::Rejected);CHECK_EQ(f.metadata.working.active_rows,64);
    SparsePreviewMessage maximum{PublicationKind::Begin,{UINT32_MAX,1,1}};
    CHECK_EQ(f.device.AcceptSparse(maximum,200,f.metadata.working,1),PublicationStatus::Receiving);
    f.device.NewSession();CHECK_EQ(f.device.NextIdentity(),0);f.Discover();
    draft=f.metadata.working;draft.cells[62][7]={60,8};f.host.Observe(draft,201);
    CHECK(!f.host.TryBegin(f.baseline,276));CHECK(std::strstr(f.host.Status(),"exhausted"));
    f.host.Observe(f.metadata.working,277);CHECK(!f.host.TryBegin(f.baseline,352));
    CHECK_EQ(f.host.Phase(),HostPreviewPhase::Synchronized); // no ID needed to restore an already applied revision
}
TEST_CASE(SparseReviewUpperRowCaptureAndRestoreStayInTheirOriginalCoordinates) {
    SparseFixture f(64);std::optional<LivePlaybackView> live=LivePlaybackView{};
    live->telemetry_available=true;live->position.rows=64;live->position.channels=8;live->device_pattern=f.baseline;
    LocalPatternEditor editor;editor.Sync(live);
    for(unsigned row=0;row<63;++row)CHECK(editor.Apply(EditorAction::Down));
    for(unsigned channel=0;channel<7;++channel)CHECK(editor.Apply(EditorAction::NextChannel));
    CHECK(editor.Apply(EditorAction::SetValue,60));CHECK(editor.Apply(EditorAction::Right));
    CHECK(editor.Apply(EditorAction::SetValue,8));CHECK(editor.Apply(EditorAction::Up));
    SparsePreviewCell cell;CHECK(editor.TakeCapturedCell(511,cell));CHECK_EQ(cell.row,63);CHECK_EQ(cell.channel,7);
    CHECK_EQ(cell.note,60);CHECK_EQ(cell.instrument,8);CHECK(editor.Apply(EditorAction::Restore));
    CHECK(editor.TakeCapturedCell(511,cell));CHECK_EQ(cell.note,NOTE_EMPTY);CHECK_EQ(cell.instrument,255);
    CHECK_EQ(cell.timing,NoteOffTiming::Arrival);CHECK(!editor.Dirty());
}

TEST_CASE(SparseReviewUnexpectedStateInReadyPhaseInvalidatesCachedBase) {
    SparseFixture f;auto first=f.baseline.pattern;first.cells[1][0]={60,8};
    f.host.Observe(first,1);f.now=76;CHECK(f.host.TryBegin(f.baseline,76));f.Transfer();
    auto latest=first;latest.cells[2][7]={60,8};f.host.Observe(latest,100);f.Audio(0);
    CHECK_EQ(f.host.Phase(),HostPreviewPhase::Ready);
    CHECK(f.Reply("BTPREVIEW1 STATE 1 0 21 0"));CHECK(f.host.Recovering());
    CHECK(!f.host.TryBegin(f.baseline,175));CHECK(!f.host.RecoveredDraft());
}

TEST_CASE(SparseReviewUnconsumedBeginIdentityCanRecoverWithoutBlindReuseOrStatusLoop) {
    for(unsigned error:{3u,9u,12u}) {
        SparseFixture f;auto draft=f.baseline.pattern;draft.cells[1][0]={60,8};
        f.host.Observe(draft,1);f.now=76;CHECK(f.host.TryBegin(f.baseline,76));
        // Snapshot/busy/stale-base rejection may occur before BEGIN consumes ID 1.
        CHECK(f.Reply("BTPREVIEW1 ERROR 1 1 "+std::to_string(error)));
        const bool recovering=f.host.Recovering();
        CHECK_EQ(recovering,error==12); // pre-COMMIT busy/snapshot errors are explicit rejection
        f.host.DiscoveryRequested();CHECK(f.Reply("BTPREVIEW1 NEXT 1 1"));
        CHECK(f.Reply("BTPREVIEW1 STATE 1 0 0 0"));
        if(recovering) {f.Read();f.host.RecoveredInstalled();}
        CHECK(!f.host.TryBegin(f.baseline,200)); // exact rejected/uncertain revision is not retried
        draft.cells[2][7]={60,8};f.host.Observe(draft,201);f.now=276;
        CHECK(f.host.TryBegin(f.baseline,276));char line[96];std::size_t n=0;
        CHECK(f.host.FormatLine(line,sizeof(line),n));CHECK(std::string(line,n).find("DBEGIN 1 2 0 2")!=std::string::npos);
        f.Transfer();f.Audio(0);CHECK_EQ(f.metadata.working_id,2);CHECK_EQ(f.host.Phase(),HostPreviewPhase::Synchronized);
    }
}
TEST_CASE(SparseReviewUnconfirmedIdentityFloorSurvivesReconnectAndDoesNotWrap) {
    SparseFixture f;auto draft=f.baseline.pattern;draft.cells[1][0]={60,8};f.host.Observe(draft,1);
    CHECK(f.host.TryBegin(f.baseline,76));f.host.RetainLocal(true);f.host.Disconnect();f.Discover();f.Read();f.host.RecoveredInstalled();
    draft.cells[2][7]={60,8};f.host.Observe(draft,100);CHECK(f.host.TryBegin(f.baseline,175));
    char line[96];std::size_t n=0;CHECK(f.host.FormatLine(line,sizeof(line),n));
    CHECK(std::string(line,n).find("DBEGIN 1 2 0 2")!=std::string::npos);
    SparseFixture exhausted;exhausted.host.DiscoveryRequested();CHECK(exhausted.Reply("BTPREVIEW1 NEXT 1 4294967295"));
    auto last=exhausted.baseline.pattern;last.cells[1][0]={60,8};exhausted.host.Observe(last,1);CHECK(exhausted.host.TryBegin(exhausted.baseline,76));
    CHECK(exhausted.Reply("BTPREVIEW1 ERROR 1 4294967295 12"));
    exhausted.host.DiscoveryRequested();CHECK(exhausted.Reply("BTPREVIEW1 NEXT 1 4294967295"));
    CHECK(exhausted.Reply("BTPREVIEW1 STATE 1 0 0 0"));exhausted.Read();exhausted.host.RecoveredInstalled();
    last.cells[2][7]={60,8};exhausted.host.Observe(last,100);CHECK(!exhausted.host.TryBegin(exhausted.baseline,175));
    CHECK(std::strstr(exhausted.host.Status(),"exhausted"));
}

#include "bringup/diagnostic_channel_gains.h"
#include "test_framework.h"
#include "ui/host_logging.h"
#include "bringup/host_pattern_preview.h"
#include "bringup/pattern_preview.h"
#include "bringup/pattern_control.h"
#include "ui/bringup_serial.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
using namespace BroTracker;
namespace {
DevicePatternSnapshot PreviewBaseline() {
    DevicePatternSnapshot s{}; s.version=2; s.transfer_id=1; s.tempo_hundredths=12753;
    s.pattern.active_rows=2; s.pattern.active_channels=1;
    s.pattern.cells[0][0]={60,7}; s.pattern.cells[1][0]={NOTE_OFF,255}; return s;
}
bool Reply(HostPatternPreview& p,const std::string& line,unsigned now=0) { return p.Response(line.data(),line.size(),now); }
void Discover(HostPatternPreview& p,unsigned next=1,unsigned working=0,unsigned pending=0) {
    p.SetBaseline(PreviewBaseline());
    p.DiscoveryRequested(); CHECK(Reply(p,"BTPREVIEW1 CAP 1"));
    CHECK(Reply(p,"BTPREVIEW1 NEXT 1 "+std::to_string(next)));
    CHECK(Reply(p,"BTPREVIEW1 STATE 1 0 "+std::to_string(working)+" "+std::to_string(pending)));
}
void Read(HostPatternPreview& p,const RealtimePattern& pattern,unsigned id,unsigned selector,unsigned revision) {
    char line[96]; std::size_t n=0; CHECK(p.BeginRead(id,0,line,sizeof(line),n));
    CHECK_EQ(std::string(line,n),"BTPREVIEW1 GET 1 "+std::to_string(id)+" "+std::to_string(selector)+"\n");
    CHECK(Reply(p,"BTPREVIEW1 READ 1 "+std::to_string(id)+" "+std::to_string(selector)+" 0 "+std::to_string(revision)));
    PatternSnapshotSender sender(pattern,12753); CHECK(sender.SetWorkingNamespace(true)); CHECK(sender.Begin(id,0,2));
    while(sender.HasData()) { const auto* data=sender.Data(n); CHECK(Reply(p,std::string(data,n-1))); sender.Consume(n); }
}
void Sent(HostPatternPreview& p,unsigned id) {
    char line[96]; std::size_t n=0; CHECK(p.FormatLine(line,sizeof(line),n));
    CHECK(Reply(p,"BTPREVIEW1 RECEIVING 1 "+std::to_string(id)));
    for(unsigned i=0;p.Sending() && i<130;++i) { CHECK(p.FormatLine(line,sizeof(line),n)); p.BeforeWrite(); p.LineComplete(); }
    CHECK(!p.Sending()); CHECK(Reply(p,"BTPREVIEW1 SUBMITTED 1 "+std::to_string(id)));
}
}
TEST_CASE(HostPreviewDebounceCoalescingStrictPhasesAndRejection) {
    HostPatternPreview p; const auto baseline=PreviewBaseline(); Discover(p); Read(p,baseline.pattern,1,0,0);
    CHECK_EQ(p.Phase(),HostPreviewPhase::Synchronized); p.RecoveredInstalled();
    auto draft=baseline.pattern; draft.cells[1][0]={60,7}; p.Observe(draft,10);
    CHECK(!p.TryBegin(baseline,84)); CHECK(p.TryBegin(baseline,85));
    CHECK(!Reply(p,"BTPREVIEW1 APPLIED 1 1")); CHECK(!Reply(p,"BTPREVIEW1 SUBMITTED 2 1"));
    CHECK(!Reply(p,"BTPREVIEW1 RECEIVING 1 4294967296"));
    draft.cells[0][0]={NOTE_EMPTY,7}; p.Observe(draft,90);
    Sent(p,1); CHECK(Reply(p,"BTPREVIEW1 ACCEPTED 1 1")); CHECK(!Reply(p,"BTPREVIEW1 ACCEPTED 1 1"));
    Discover(p,2,0,1); CHECK(p.TryBegin(baseline,165)); Sent(p,2);
    CHECK(Reply(p,"BTPREVIEW1 SUPERSEDED 1 1")); CHECK(!Reply(p,"BTPREVIEW1 SUPERSEDED 1 1"));
    CHECK(Reply(p,"BTPREVIEW1 ACCEPTED 1 2")); CHECK(Reply(p,"BTPREVIEW1 APPLIED 1 2"));
    Discover(p,3,2); Read(p,draft,2,0,2); CHECK_EQ(p.Phase(),HostPreviewPhase::Synchronized);
    draft.cells[0][0]={61,7}; p.Observe(draft,200); CHECK(p.TryBegin(baseline,275)); Sent(p,3);
    CHECK(Reply(p,"BTPREVIEW1 REJECTED 1 3 unsupported-pitch")); CHECK_EQ(p.Phase(),HostPreviewPhase::Rejected);
    Discover(p,4,2); CHECK(!p.TryBegin(baseline,1000)); // No automatic retry of the same rejected draft.
    CHECK(std::strstr(p.Status(),"unsupported-pitch"));
}
TEST_CASE(HostPreviewHeldEditsTimeoutReconnectAndVerifiedPendingRecovery) {
    HostPatternPreview p; const auto baseline=PreviewBaseline(); Discover(p); Read(p,baseline.pattern,1,0,0);
    auto draft=baseline.pattern;
    for(unsigned time=0;time<250;time+=25) { draft.cells[0][0].instrument=static_cast<unsigned char>(time/25); p.Observe(draft,time); CHECK(!p.TryBegin(baseline,time)); }
    CHECK(p.TryBegin(baseline,250)); Sent(p,1); p.Tick(6250); CHECK(p.Recovering());
    p.Disconnect(); CHECK(!p.Supported()); Discover(p,2,0,1);
    CHECK(!p.TryBegin(baseline,7000)); Read(p,baseline.pattern,2,0,0); Read(p,draft,3,1,1);
    CHECK_EQ(p.Phase(),HostPreviewPhase::Accepted); CHECK(SamePattern(*p.RecoveredDraft(),draft));
    CHECK(!p.TryBegin(baseline,8000)); CHECK(Reply(p,"BTPREVIEW1 APPLIED 1 1"));
    Discover(p,2,1,0); Read(p,draft,4,0,1); CHECK_EQ(p.Phase(),HostPreviewPhase::Synchronized);
    auto restore=baseline.pattern; p.Observe(restore,9000); CHECK(p.TryBegin(baseline,9075));
}

namespace {
struct PreviewPort : SerialTransport {
    PatternBringUpControl control; PatternPublication preview,publication;
    PatternPreviewReadback metadata; DevicePatternSnapshot baseline=PreviewBaseline();
    std::int16_t pcm[64]{}; bool online=true, capability=true; unsigned limit=3;
    std::string input, output, partial; std::vector<std::int16_t> audio; unsigned writes=0;
    bool asynchronous=false,ownership_violation=false,hold_snapshot=false;
    std::string delayed_snapshot;
    std::string owned_write; unsigned completed_write=0;
    void CompleteWrite(unsigned count=96) {
        CHECK(!owned_write.empty()); CHECK(!completed_write);
        completed_write=std::min<unsigned>(count,static_cast<unsigned>(owned_write.size()));
        output.append(owned_write.data(),completed_write); partial.append(owned_write.data(),completed_write);
        const auto end=partial.find('\n');
        if(end!=std::string::npos) {const auto line=partial.substr(0,end); partial.erase(0,end+1); Handle(line);}
    }
    PreviewPort() {
        std::fill(std::begin(pcm),std::end(pcm),11); NativeRateSampleBindings b{}; b.count=1;
        b.bindings[0]={7,60,{pcm,64,100}};
        CHECK_EQ(control.InitializeAudio(12753,100,baseline.pattern,b),PatternPlayerStatus::Success); metadata.working=baseline.pattern;
    }
    bool Open(FILE*,const char*) override { return online; }
    bool Healthy() override { return online; }
    void Close() override { input.clear(); partial.clear(); owned_write.clear(); completed_write=0; }
    const char* Error() const override { return "fixture disconnect"; }
    int Read(char* data,unsigned count) override {
        const auto n=std::min<std::size_t>(count,input.size()); std::memcpy(data,input.data(),n); input.erase(0,n); return static_cast<int>(n);
    }
    int Write(const char* data,unsigned count) override {
        ++writes;
        if(asynchronous) {
            if(!owned_write.empty() && (count<owned_write.size() || std::memcmp(data,owned_write.data(),owned_write.size()))) ownership_violation=true;
            if(completed_write) {const auto done=completed_write; completed_write=0; owned_write.clear(); return static_cast<int>(done);}
            if(owned_write.empty()) owned_write.assign(data,std::min(count,limit));
            return 0;
        }
        const auto n=std::min(count,limit); output.append(data,n); partial.append(data,n);
        auto end=partial.find('\n');
        if(end!=std::string::npos) { const auto line=partial.substr(0,end); partial.erase(0,end+1); Handle(line); }
        return static_cast<int>(n);
    }
    bool gain_capability=false;
    void State() {
        const auto s=control.Snapshot();
        input+="BTTEST1 STATE "+std::string(s.running?"PLAYING":s.transport==PatternTransportState::Paused?"PAUSED":"IDLE")+"\n";
        input+="BTPATTERN1 SNAPCAP 2\nBTPATTERN1 TRANSPORTCAP 1\nBTPUBLISH1 CAP 1\nBTPUBLISH1 PAUSEDCAP 1\n";
        input+="BTPUBLISH1 NEXT 1 "+std::to_string(publication.NextIdentity())+"\n";
        if(capability) input+="BTPREVIEW1 CAP 1\nBTPREVIEW1 NEXT 1 "+std::to_string(preview.NextIdentity())+"\nBTPREVIEW1 STATE 1 "+
            std::to_string(s.published_id)+" "+std::to_string(s.working_id)+" "+std::to_string(s.pending_preview_id)+"\n";
        if(gain_capability) {
            char gain_state[96]; CHECK(FormatPatternGainState(s.gains,s.applied_gain_id,s.next_gain_id,gain_state,sizeof(gain_state)));
            input+="BTGAIN1 CAP 1\n"; input+=gain_state;
        }
        input+="BTPATTERN1 POS 2 "+std::to_string(s.running?1:s.transport==PatternTransportState::Paused?2:0)+" "+
            std::to_string(s.position.valid)+" "+std::to_string(s.position.absolute_tick)+" "+std::to_string(s.position.pattern_row)+" "+
            std::to_string(s.position.loop_index)+" "+std::to_string(baseline.pattern.active_rows)+" "+std::to_string(baseline.pattern.active_channels)+"\n";
    }
    void Snapshot(const RealtimePattern& pattern,unsigned id,bool working) {
        PatternSnapshotSender sender(pattern,12753); CHECK(sender.SetWorkingNamespace(working)); CHECK(sender.Begin(id,0,2));
        while(sender.HasData()) { std::size_t n=0; const auto* data=sender.Data(n); if(hold_snapshot) delayed_snapshot.append(data,n); else input.append(data,n); sender.Consume(n); }
    }
    void Handle(const std::string& line) {
        if(line=="BTTEST1 HELLO" || line=="BTTEST1 STATUS") { if(line=="BTTEST1 HELLO") {preview.NewSession(); publication.NewSession();} State(); return; }
        std::uint32_t f[5]{}; unsigned version=0,id=0;
        if(ParseSnapshotRequest(line.data(),line.size(),id,version)) { Snapshot(baseline.pattern,id,false); return; }
        if(SnapshotFields(line.data(),line.size(),"BTPREVIEW1 GET 1 ",f,2)) {
            const auto revision=f[1]?metadata.pending_id:metadata.working_id;
            input+="BTPREVIEW1 READ 1 "+std::to_string(f[0])+" "+std::to_string(f[1])+" "+std::to_string(metadata.published_id)+" "+std::to_string(revision)+"\n";
            Snapshot(f[1]?metadata.pending:metadata.working,f[0],true); return;
        }
        if(line=="BTTEST1 START" || line=="BTTEST1 STOP" || line=="BTTEST1 PAUSE" || line=="BTTEST1 CONTINUE") {
            CHECK_EQ(control.Submit(line=="BTTEST1 START"?PatternRequest::Start:line=="BTTEST1 STOP"?PatternRequest::Stop:
                line=="BTTEST1 PAUSE"?PatternRequest::Pause:PatternRequest::Continue),PatternRequestStatus::Accepted); Advance(0); return;
        }
        if(line=="BTGAIN1 GET 1") {
            const auto s=control.Snapshot(); char state[96];
            CHECK(FormatPatternGainState(s.gains,s.applied_gain_id,s.next_gain_id,state,sizeof(state))); input+=state; return;
        }
        PatternGainMessage gain;
        if(ParsePatternGains(line.data(),line.size(),gain)) {
            CHECK_EQ(control.SubmitGains(gain.gains,gain.id),PatternRequestStatus::Accepted);
            input+="BTGAIN1 SUBMITTED 1 "+std::to_string(gain.id)+"\n"; Advance(0); return;
        }
        const bool is_preview=line.find("BTPREVIEW1")==0; PublicationMessage m;
        if(!ParsePublication(line.data(),line.size(),m,is_preview)) { CHECK(false); return; }
        auto& transfer=is_preview?preview:publication; const auto result=transfer.Accept(m,0,(is_preview?publication:preview).Busy());
        const auto prefix=is_preview?"BTPREVIEW1":"BTPUBLISH1";
        if(result==PublicationStatus::Ready) {
            CHECK(transfer.Freeze());
            CHECK_EQ(is_preview?control.SubmitPreview(*transfer.Frozen(),transfer.Identity()):control.SubmitPattern(*transfer.Frozen(),transfer.Identity()),PatternRequestStatus::Accepted);
            input+=std::string(prefix)+" SUBMITTED 1 "+std::to_string(m.fields[0])+"\n"; Advance(0);
        } else if(result==PublicationStatus::Receiving && is_preview && m.kind==PublicationKind::Begin)
            input+="BTPREVIEW1 RECEIVING 1 "+std::to_string(m.fields[0])+"\n";
        else if(result!=PublicationStatus::Receiving) input+=std::string(prefix)+" ERROR 1 "+std::to_string(m.fields[0])+" "+std::to_string(unsigned(result))+"\n";
    }
    void Advance(unsigned count) {
        std::int16_t out[128]{}; control.AudioBlock(out,count,true); audio.insert(audio.end(),out,out+count);
        PatternAppliedRequest ack;
        while(control.TakeApplied(ack)) {
            const auto id=std::to_string(ack.publication_id);
            if(ack.request==PatternRequest::Gains) { input+="BTGAIN1 APPLIED 1 "+id+"\n"; }
            else if(ack.request==PatternRequest::Preview) {
                CHECK(metadata.Accepted(preview,ack.publication_id,ack.result==PatternPlayerStatus::Success));
                input+=ack.result==PatternPlayerStatus::Success?"BTPREVIEW1 ACCEPTED 1 "+id+"\n":"BTPREVIEW1 REJECTED 1 "+id+" unsupported-pitch\n";
            } else if(ack.request==PatternRequest::PreviewApplied) {
                CHECK(metadata.Applied(ack.publication_id)); input+="BTPREVIEW1 APPLIED 1 "+id+"\n";
            } else if(ack.request==PatternRequest::PreviewSuperseded) {
                metadata.Superseded(ack.publication_id); input+="BTPREVIEW1 SUPERSEDED 1 "+id+"\n";
            } else if(ack.request==PatternRequest::Replace) {
                CHECK(publication.Complete(ack.publication_id,ack.result==PatternPlayerStatus::Success,baseline.pattern));
                if(ack.result==PatternPlayerStatus::Success) {metadata.Published(baseline.pattern,ack.publication_id); input+="BTPUBLISH1 APPLIED 1 "+id+"\n";}
                else input+="BTPUBLISH1 REJECTED 1 "+id+" playing-or-paused\n";
            } else input+="BTTEST1 "+std::string(ack.request==PatternRequest::Start?"STARTED":ack.request==PatternRequest::Stop?"STOPPED":
                ack.request==PatternRequest::Pause?"PAUSED":"CONTINUED")+"\n";
        }
    }
};
struct PreviewSession {
    PreviewPort* port=new PreviewPort;
    BringUpSerial serial;
    explicit PreviewSession(FILE* log=nullptr):serial(log,std::unique_ptr<SerialTransport>(port)) {}
    LocalPatternEditor editor; unsigned now=0,step_ms=10;
    void Tick() { serial.Tick(now); serial.SyncEditor(editor,now); now+=step_ms; }
    void Pump(unsigned steps=250) { for(unsigned i=0;i<steps;++i) Tick(); }
    void Note(unsigned value) { CHECK(editor.Apply(EditorAction::SetValue,value)); Tick(); }
};
}
TEST_CASE(AutomaticPreviewPcmRestorePublicationAndLegacyFallback) {
    PreviewSession s; s.Pump(); CHECK(s.editor.Ready()); CHECK(!s.editor.Dirty());
    CHECK(s.port->output.find("BTPREVIEW1 GET 1 ")!=std::string::npos);
    CHECK(s.editor.Apply(EditorAction::Down)); s.Note(60); CHECK(s.editor.Dirty()); s.Pump();
    CHECK_EQ(s.port->metadata.working.cells[1][0].note,60); CHECK_EQ(s.port->baseline.pattern.cells[1][0].note,NOTE_OFF);
    CHECK(s.editor.Dirty()); CHECK(s.serial.Start()); s.Pump(30); s.port->Advance(12);
    for(auto sample:s.port->audio) { if(sample) CHECK_EQ(sample,11); }
    CHECK_EQ(s.port->audio.size(),12);
    for(unsigned i=0;i<12;++i) CHECK_EQ(s.port->audio[i],11);
    CHECK_EQ(s.port->audio.back(),11); CHECK(s.serial.Stop()); s.Pump(30);
    CHECK(s.editor.Apply(EditorAction::Restore)); s.Tick(); s.Pump(); CHECK(!s.editor.Dirty());
    CHECK_EQ(s.port->metadata.working.cells[1][0].note,NOTE_OFF);
    CHECK(s.serial.Start()); s.Pump(30); s.port->Advance(12); CHECK_EQ(s.port->audio.back(),0);
    CHECK(s.serial.Stop()); s.Pump(30); s.Note(60);
    CHECK(s.serial.Publish(s.editor,s.now)); CHECK(s.editor.Locked()); s.Pump(400);
    CHECK(!s.editor.Dirty()); CHECK_EQ(s.port->baseline.pattern.cells[1][0].note,60);
    CHECK_EQ(s.port->metadata.working.cells[1][0].note,60);
    CHECK(s.editor.CanUndo()); CHECK(s.editor.Undo()); s.Tick(); s.Pump(400);
    CHECK(s.editor.Dirty()); CHECK_EQ(s.port->metadata.working.cells[1][0].note,NOTE_OFF);
    CHECK_EQ(s.port->baseline.pattern.cells[1][0].note,60);
    CHECK(s.editor.Redo()); s.Tick(); s.Pump(400); CHECK(!s.editor.Dirty());
    CHECK_EQ(s.port->metadata.working.cells[1][0].note,60);
    PreviewSession legacy; legacy.port->capability=false; legacy.Pump(); CHECK(legacy.editor.Ready());
    CHECK(legacy.editor.Apply(EditorAction::Down)); legacy.Note(60); legacy.Pump();
    CHECK(legacy.port->output.find("BTPREVIEW1")==std::string::npos); CHECK_EQ(legacy.port->metadata.working.cells[1][0].note,NOTE_OFF);
}
TEST_CASE(AutomaticPreviewRejectionReconnectAndF4InFlight) {
    PreviewSession s; s.Pump(); CHECK(s.editor.Apply(EditorAction::Down)); s.Note(61); s.Pump();
    CHECK(s.editor.Dirty()); CHECK_EQ(s.editor.Draft()->cells[1][0].note,61);
    CHECK(std::strstr(s.serial.PublicationStatus(),"unsupported-pitch"));
    const auto sent=s.port->output; s.Pump(); CHECK_EQ(s.port->output.find("BTPREVIEW1 BEGIN",sent.size()),std::string::npos);
    s.port->online=false; s.Tick(); CHECK(!s.editor.Ready()); s.port->online=true; s.Pump(400);
    CHECK(s.editor.Ready()); CHECK_EQ(s.editor.Draft()->cells[1][0].note,61); CHECK(s.editor.Dirty());
    CHECK(s.editor.Apply(EditorAction::Down)); // Connection-scoped cursor resets on disconnect.
    s.Note(60); s.now+=100; s.Tick(); s.Tick(); // Partial BEGIN in flight.
    CHECK(s.editor.Apply(EditorAction::Restore)); s.Tick(); s.Pump(400);
    CHECK_EQ(s.port->metadata.working.cells[1][0].note,NOTE_OFF); CHECK(!s.editor.Dirty());
    CHECK(s.serial.Start()); s.Pump(30); s.Note(60); s.Pump(100);
    CHECK(!s.serial.Publish(s.editor,s.now)); // Playing publication remains blocked.
    CHECK(s.serial.Pause()); s.Pump(60); // Transport interrupts preview at complete line boundaries.
    CHECK(s.serial.Paused()); s.Pump(400);
    CHECK(s.serial.Continue()); s.Pump(30); s.port->Advance(1); s.Pump(400);
    CHECK_EQ(s.port->metadata.working.cells[1][0].note,60);
}

TEST_CASE(AutomaticPreviewF5SelectedRevisionAndPausePriorityDuringPartialTransfer) {
    PreviewSession s; s.Pump(); CHECK(s.editor.Apply(EditorAction::Down)); s.Note(61);
    s.now+=100; s.Tick(); s.Tick(); // Older unsupported revision's BEGIN is partially written.
    s.Note(60); CHECK(s.serial.Publish(s.editor,s.now)); const auto cursor=s.editor.Cursor();
    CHECK(!s.editor.Apply(EditorAction::Restore)); s.Pump(400);
    CHECK(!s.editor.Dirty()); CHECK_EQ(s.editor.Cursor().row,cursor.row);
    CHECK_EQ(s.port->baseline.pattern.cells[1][0].note,60); CHECK_EQ(s.port->metadata.working.cells[1][0].note,60);
    const auto revision=s.port->metadata.published_id; s.Pump(300); CHECK_EQ(s.port->metadata.published_id,revision);
    CHECK(s.serial.Start()); s.Pump(20); CHECK(s.editor.Apply(EditorAction::Restore));
    const auto mark=s.port->output.size();
    CHECK(s.editor.Apply(EditorAction::NoteOff)); s.Tick(); s.now+=100;
    for(unsigned i=0;i<80 && s.port->output.find("BTPREVIEW1 BEGIN",mark)==std::string::npos;++i) s.Tick();
    const auto begin=s.port->output.find("BTPREVIEW1 BEGIN",mark); CHECK(begin!=std::string::npos);
    CHECK(s.serial.Pause()); s.Pump(100);
    const auto pause=s.port->output.find("BTTEST1 PAUSE\n",begin);
    const auto commit=s.port->output.find("BTPREVIEW1 COMMIT",begin);
    CHECK(pause!=std::string::npos); CHECK(commit!=std::string::npos); CHECK(pause<commit);
    CHECK(s.serial.Paused()); CHECK(s.serial.Stop()); s.Pump(30); CHECK(s.serial.Stopped());
}
TEST_CASE(HostPreviewMalformedReadbackNeverPublishesOrChangesDirtyBaseline) {
    HostPatternPreview p; Discover(p); auto b=PreviewBaseline();
    char request[96]; std::size_t n=0; CHECK(p.BeginRead(1,0,request,sizeof(request),n));
    CHECK(!Reply(p,"BTPREVIEW1 READ 2 1 0 0 0"));
    CHECK(!Reply(p,"BTPREVIEW1 READ 1 2 0 0 0"));
    CHECK(Reply(p,"BTPREVIEW1 READ 1 1 0 0 0"));
    CHECK(Reply(p,"BTPREVIEW1 BEGIN 2 1 2 1 12753 2"));
    CHECK(Reply(p,"BTPREVIEW1 CELL 2 1 0 60 7 2"));
    CHECK(p.Recovering()); CHECK(!p.RecoveredDraft());
    Discover(p); Read(p,b.pattern,2,0,0); CHECK(p.RecoveredDraft());
    LocalPatternEditor editor; std::optional<LivePlaybackView> live=LivePlaybackView{true,true,{false,false,0,0,0,2,1}};
    live->device_pattern=b; editor.Sync(live); CHECK(editor.Ready());
    auto edited=b.pattern; edited.cells[0][0].note=61; CHECK(editor.RecoverDraft(edited));
    CHECK(editor.Dirty()); CHECK_EQ(editor.Baseline()->pattern.cells[0][0].note,60);
}

TEST_CASE(HostPreviewLateReceiptAndFreshCleanReconnectDoNotResend) {
    HostPatternPreview p; auto baseline=PreviewBaseline(); Discover(p); Read(p,baseline.pattern,1,0,0);
    auto draft=baseline.pattern; draft.cells[1][0]={60,7}; p.Observe(draft,0); CHECK(p.TryBegin(baseline,75));
    for(unsigned i=0;p.Sending() && i<130;++i) {
        char line[96]; std::size_t n=0; CHECK(p.FormatLine(line,sizeof(line),n)); p.BeforeWrite(); p.LineComplete();
    }
    // USB backpressure may delay RECEIVING until after COMMIT was transmitted.
    CHECK(Reply(p,"BTPREVIEW1 RECEIVING 1 1")); CHECK(Reply(p,"BTPREVIEW1 SUBMITTED 1 1"));
    CHECK(Reply(p,"BTPREVIEW1 ACCEPTED 1 1")); CHECK(Reply(p,"BTPREVIEW1 APPLIED 1 1"));
    Discover(p,2,1); Read(p,draft,2,0,1); CHECK_EQ(p.Phase(),HostPreviewPhase::Synchronized);
    p.RetainLocal(false); p.Disconnect(); Discover(p,3,2); auto remote=draft; remote.cells[0][0]={NOTE_EMPTY,7};
    Read(p,remote,3,0,2); CHECK(SamePattern(*p.RecoveredDraft(),remote)); CHECK(!p.TryBegin(baseline,1000));
}

TEST_CASE(HostGainSerialPcmTransportPreviewPublicationAndReconnect) {
    PreviewSession s; s.port->gain_capability=true;
    Pcm16ChannelGains initial; initial.channels[0]=32768;
    CHECK_EQ(s.port->control.SubmitGains(initial,1),PatternRequestStatus::Accepted); s.port->Advance(0);
    s.Pump(); CHECK(s.serial.ChannelGainsAvailable()); CHECK_EQ(s.serial.ChannelGains()->gains.channels[0],32768u);
    CHECK(s.port->output.find("BTGAIN1 SET")==std::string::npos);
    const auto accepted=*s.serial.ChannelGains();
    s.port->input+="BTGAIN1 STATE 1 "+std::string(300,'9')+"\n";
    s.port->input+="BTGAIN1 STATE 1 1 2 65537 0 0 0 0 0 0 0\n";
    s.Tick(); CHECK(s.serial.Connected()); CHECK(s.editor.Ready());
    CHECK_EQ(s.serial.ChannelGains()->gains.channels[0],accepted.gains.channels[0]);
    CHECK(s.serial.Start()); s.Pump(30); s.port->Advance(1); CHECK_EQ(s.port->audio.back(),6); // ties away from zero
    // A queued complete gain line must yield to transport before its first byte.
    auto first=initial; first.channels[0]=16384; CHECK(s.serial.RequestChannelGains(first));
    const auto ordering=s.port->output.size(); CHECK(s.serial.Pause()); s.Pump(100);
    const auto pause=s.port->output.find("BTTEST1 PAUSE",ordering);
    const auto gain=s.port->output.find("BTGAIN1 SET",ordering);
    CHECK(pause!=std::string::npos); CHECK(gain!=std::string::npos); CHECK(pause<gain);
    CHECK(s.serial.Continue()); s.Pump(40);
    auto desired=initial; desired.channels[0]=0; CHECK(s.serial.RequestChannelGains(desired));
    s.port->limit=1; s.Tick(); s.Tick(); CHECK(s.serial.Pause());
    s.Pump(400); CHECK(s.serial.Paused()); CHECK_EQ(s.serial.ChannelGains()->gains.channels[0],0u);
    CHECK(!s.editor.Dirty()); CHECK(!s.editor.CanUndo());
    CHECK(s.serial.Continue()); s.Pump(40); s.port->Advance(1); CHECK_EQ(s.port->audio.back(),0);
    CHECK(s.editor.Apply(EditorAction::Down)); s.Note(60); s.Pump(1000); CHECK(s.editor.Dirty());
    CHECK(s.serial.Stop()); s.Pump(50); CHECK(s.serial.Publish(s.editor,s.now)); s.Pump(1000);
    CHECK(!s.editor.Dirty()); CHECK_EQ(s.serial.ChannelGains()->gains.channels[0],0u);
    CHECK_EQ(s.port->baseline.pattern.cells[1][0].note,60);
    s.port->online=false; s.Tick(); CHECK(!s.serial.ChannelGainsAvailable()); CHECK(!s.serial.DesiredChannelGains());
    const auto offset=s.port->output.size(); s.port->online=true; s.Pump(1000);
    CHECK(s.serial.ChannelGainsAvailable()); CHECK_EQ(s.serial.ChannelGains()->gains.channels[0],0u);
    CHECK(s.port->output.find("BTGAIN1 SET",offset)==std::string::npos);
    PreviewSession legacy; legacy.Pump(); CHECK(!legacy.serial.RequestChannelGains(initial));
    CHECK(legacy.port->output.find("BTGAIN1")==std::string::npos);
}

TEST_CASE(WindowsDiagnosticGainSubmissionOnceLeavesPatternAndTransportIndependent) {
    PreviewSession s; s.port->gain_capability=true; s.Pump();
    const auto baseline=s.port->baseline.pattern;
    BroTracker::DiagnosticChannelGainRequest config("16384,65536,65536,65536,65536,65536,65536,65536");
    BroTracker::Pcm16ChannelGains request;
    CHECK(config.TakeRequest(s.serial.Connected(),s.serial.ChannelGainsSupported(),s.serial.ChannelGainsSynchronized(),request));
    CHECK(s.serial.RequestChannelGains(request)); s.Pump();
    CHECK_EQ(s.serial.ChannelGains()->gains.channels[0],16384u);
    CHECK(s.serial.ChannelGainsSynchronized()); CHECK(!s.serial.Playing()); CHECK(!s.editor.Dirty()); CHECK(!s.editor.CanUndo());
    CHECK(BroTracker::SamePattern(baseline,s.port->baseline.pattern));
    const auto after=s.port->output.size();
    CHECK(!config.TakeRequest(true,true,true,request)); s.Pump();
    CHECK(s.port->output.find("BTGAIN1 SET",after)==std::string::npos);
    s.port->online=false; s.Tick(); CHECK(!config.TakeRequest(false,false,false,request));
    s.port->online=true; s.Pump(500); CHECK(!config.TakeRequest(true,true,true,request));
    CHECK_EQ(s.serial.ChannelGains()->gains.channels[0],16384u);
    CHECK(s.port->output.find("BTGAIN1 SET",after)==std::string::npos);
}

TEST_CASE(PreviewReadyWritesHaveBoundedServiceLatency) {
    PreviewSession s; s.port->limit=96;
    s.port->baseline.pattern.active_rows=16; s.port->baseline.pattern.active_channels=2;
    NativeRateSampleBindings bindings{}; bindings.count=1;
    bindings.bindings[0]={7,60,{s.port->pcm,64,100}};
    CHECK_EQ(s.port->control.InitializeAudio(12753,100,s.port->baseline.pattern,bindings),PatternPlayerStatus::Success);
    s.port->metadata.working=s.port->baseline.pattern;
    s.Pump(); CHECK(s.editor.Ready());
    CHECK(s.editor.Apply(EditorAction::Down)); s.Note(60);
    s.now+=100;
    // Ready I/O must drain a complete small transfer without one SDL frame per cell.
    s.Tick(); s.Tick();
    CHECK(s.port->output.find("BTPREVIEW1 COMMIT")!=std::string::npos);
}
TEST_CASE(PreviewResetDiscardsOldDraftButPoweredReconnectRetainsWorking) {
    HostPatternPreview p; auto baseline=PreviewBaseline();
    Discover(p,5,4); auto working=baseline.pattern; working.cells[0][0].note=62;
    Read(p,working,1,0,4); CHECK(SamePattern(*p.RecoveredDraft(),working));
    p.RecoveredInstalled(); p.RetainLocal(true); p.Disconnect();
    Discover(p,5,4); Read(p,working,2,0,4);
    CHECK(SamePattern(*p.RecoveredDraft(),working));
    p.RecoveredInstalled(); p.Disconnect(); Discover(p,1,0);
    Read(p,baseline.pattern,3,0,0);
    CHECK(SamePattern(*p.RecoveredDraft(),baseline.pattern));
    CHECK_EQ(p.Phase(),HostPreviewPhase::Synchronized);
    CHECK(!p.TryBegin(baseline,1000));
}

TEST_CASE(PreviewDelayedReadbackPreservesNewerEditsAndFreshHostAdoptsPoweredWorking) {
    HostPatternPreview p; auto baseline=PreviewBaseline(); Discover(p,5,4);
    auto working=baseline.pattern; working.cells[0][0].note=62;
    Read(p,working,1,0,4); CHECK(SamePattern(*p.RecoveredDraft(),working));
    CHECK_EQ(p.Phase(),HostPreviewPhase::Synchronized);
    CHECK(!p.TryBegin(baseline,1000)); // New host must not restore its boot baseline.
    p.RecoveredInstalled(); auto draft=working; draft.cells[0][0].note=63;
    p.Observe(draft,1100); CHECK(p.TryBegin(baseline,1175)); Sent(p,5);
    CHECK(Reply(p,"BTPREVIEW1 ACCEPTED 1 5")); CHECK(Reply(p,"BTPREVIEW1 APPLIED 1 5"));
    auto newer=draft; newer.cells[0][0].note=64; p.Observe(newer,1200);
    Discover(p,6,5); Read(p,draft,2,0,5);
    CHECK(SamePattern(*p.RecoveredDraft(),newer));
    CHECK(p.TryBegin(baseline,1300)); Sent(p,6);
    CHECK(Reply(p,"BTPREVIEW1 ACCEPTED 1 6")); CHECK(Reply(p,"BTPREVIEW1 APPLIED 1 6"));
    Discover(p,7,6); Read(p,newer,3,0,6);
    CHECK_EQ(p.Phase(),HostPreviewPhase::Synchronized);
}

TEST_CASE(PreviewServiceStopsOnBackpressureWithoutLosingLatestRevision) {
    PreviewSession s; s.Pump(); CHECK(s.editor.Apply(EditorAction::Down)); s.Note(60);
    s.port->limit=0; s.now+=100; s.Tick();
    const auto before=s.port->writes; s.Tick(); CHECK_EQ(s.port->writes,before+1);
    CHECK(s.serial.Connected()); CHECK(s.editor.Dirty());
    CHECK(s.editor.Apply(EditorAction::Restore)); s.Tick();
    s.port->limit=3; s.Pump(500);
    CHECK(SamePattern(s.port->metadata.working,s.port->baseline.pattern));
    CHECK(!s.editor.Dirty()); CHECK(s.serial.Connected());
}

TEST_CASE(PreviewResetAfterFirstReceiptBeforeNextRefreshDropsStaleDraft) {
    HostPatternPreview p; auto baseline=PreviewBaseline(); Discover(p); Read(p,baseline.pattern,1,0,0);
    auto draft=baseline.pattern; draft.cells[0][0].note=62;
    p.Observe(draft,100); CHECK(p.TryBegin(baseline,175)); Sent(p,1);
    p.RetainLocal(true); p.Disconnect(); Discover(p,1,0);
    Read(p,baseline.pattern,2,0,0);
    CHECK(SamePattern(*p.RecoveredDraft(),baseline.pattern));
    CHECK(!p.TryBegin(baseline,1000));
}

TEST_CASE(PreviewStopDuringTransferAndRecoveryPreservesDraftWithoutF5) {
    for(bool dirty : {false,true}) for(bool recovery : {false,true}) {
        PreviewSession s; s.Pump(); CHECK(s.serial.Start()); s.Pump(30);
        if(dirty) { CHECK(s.editor.Apply(EditorAction::Down)); s.Note(60); }
        s.port->limit=1; s.now+=100; s.Tick(); s.Tick();
        if(recovery) { s.port->online=false; s.Tick(); s.port->online=true; s.Pump(1000); }
        const auto draft=*s.editor.Draft(); const auto baseline=*s.editor.Baseline();
        const auto mark=s.port->output.size(); CHECK(s.serial.Stop()); s.port->limit=3; s.Pump(1000);
        CHECK(s.serial.Stopped()); CHECK(s.serial.Connected()); CHECK(s.editor.Ready());
        CHECK(SamePattern(draft,*s.editor.Draft())); CHECK(SamePattern(baseline.pattern,s.editor.Baseline()->pattern));
        CHECK(s.port->output.find("BTTEST1 STOP\n",mark)!=std::string::npos);
        CHECK(s.port->output.find("BTTEST1 START\n",mark)==std::string::npos);
        CHECK(s.port->output.find("BTPUBLISH1 BEGIN",mark)==std::string::npos);
    }
}

TEST_CASE(PreviewDiagnosticsCorrelateLocalRevisionAndWireTimestamps) {
    BroTracker::ConfigureHostLogging(BroTracker::HostLogMode::Debug);
    FILE* log=std::tmpfile(); CHECK(log!=nullptr);
    {
        PreviewSession s(log); s.Pump();
        CHECK(s.editor.Apply(EditorAction::Down)); s.Note(60); s.Pump();
        std::rewind(log); std::string text; char chunk[256];
        while(std::fgets(chunk,sizeof(chunk),log)) text+=chunk;
        CHECK(text.find("[host_ms=")!=std::string::npos);
        CHECK(text.find("local draft changed: checksum=")!=std::string::npos);
        CHECK(text.find("TX complete: BTPREVIEW1 COMMIT")!=std::string::npos);
        CHECK(text.find("RX: BTPREVIEW1 APPLIED")!=std::string::npos);
    }
    std::fclose(log);
    BroTracker::ConfigureHostLogging(BroTracker::HostLogMode::Basic);
}

TEST_CASE(PreviewAsyncOwnedCellCannotBeReplacedByStopAtZeroReportedOffset) {
    for(unsigned completion : {3u,96u}) {
        PreviewSession s; s.Pump(); CHECK(s.serial.Start()); s.Pump(30);
        CHECK(s.editor.Apply(EditorAction::Down)); s.Note(60); const auto draft=*s.editor.Draft();
        s.port->asynchronous=true; s.port->limit=96; s.now+=100;
        for(unsigned i=0;i<300 && s.port->owned_write.find("BTPREVIEW1 CELL")!=0;++i) {
            if(!s.port->owned_write.empty()) s.port->CompleteWrite();
            s.Tick();
        }
        CHECK(s.port->owned_write.find("BTPREVIEW1 CELL")==0);
        const auto cell=s.port->owned_write; CHECK(s.serial.Stop());
        const auto before=s.port->output.size(); s.Tick(); s.Tick(); // Backpressure must retain payload.
        CHECK_EQ(s.port->owned_write,cell); CHECK(!s.port->ownership_violation);
        s.port->CompleteWrite(completion); s.Tick();
        for(unsigned i=0;i<500 && !s.serial.Stopped();++i) {
            if(!s.port->owned_write.empty()) s.port->CompleteWrite(completion);
            s.Tick();
        }
        CHECK(!s.port->ownership_violation); CHECK(s.serial.Stopped()); CHECK(s.serial.Connected());
        CHECK(SamePattern(draft,*s.editor.Draft()));
        const auto wire=s.port->output.substr(before);
        CHECK(wire.find("BTTEST1 STOP\n")!=std::string::npos);
        CHECK(wire.find("BTTEST1 START\n")==std::string::npos);
        CHECK(wire.find("BTPUBLISH1 BEGIN")==std::string::npos);
    }
}

TEST_CASE(PreviewAsyncStopDisconnectRecoveryRetainsDraftWithoutReplay) {
    PreviewSession s; s.Pump(); CHECK(s.serial.Start()); s.Pump(30);
    CHECK(s.editor.Apply(EditorAction::Down)); s.Note(60); const auto draft=*s.editor.Draft();
    s.port->asynchronous=true; s.port->limit=96; s.now+=100;
    for(unsigned i=0;i<300 && s.port->owned_write.find("BTPREVIEW1 CELL")!=0;++i) {
        if(!s.port->owned_write.empty()) { s.port->CompleteWrite(); }
        s.Tick();
    }
    CHECK(s.port->owned_write.find("BTPREVIEW1 CELL")==0); CHECK(s.serial.Stop()); s.Tick();
    CHECK(!s.port->ownership_violation);
    s.port->online=false; s.Tick(); CHECK(!s.editor.Ready());
    const auto mark=s.port->output.size(); s.port->asynchronous=false; s.port->online=true; s.Pump(1000);
    CHECK(s.serial.Connected()); CHECK(s.serial.Playing()); CHECK(SamePattern(draft,*s.editor.Draft()));
    const auto recovery=s.port->output.substr(mark);
    CHECK(recovery.find("BTTEST1 START\n")==std::string::npos);
    CHECK(recovery.find("BTTEST1 STOP\n")==std::string::npos);
    CHECK(recovery.find("BTPUBLISH1 BEGIN")==std::string::npos);
    CHECK(s.serial.Stop()); s.Pump(100); CHECK(s.serial.Stopped());
}

TEST_CASE(PreviewLatestAppliedRevisionNeedNotReadBackObsoleteCandidate) {
    HostPatternPreview p; const auto baseline=PreviewBaseline(); Discover(p); Read(p,baseline.pattern,1,0,0);
    auto first=baseline.pattern; first.cells[1][0]={60,7}; p.Observe(first,100);
    CHECK(p.TryBegin(baseline,175)); Sent(p,1);
    auto latest=first; latest.cells[0][0]={NOTE_EMPTY,7}; p.Observe(latest,200);
    CHECK(Reply(p,"BTPREVIEW1 ACCEPTED 1 1",210)); CHECK(Reply(p,"BTPREVIEW1 APPLIED 1 1",220));
    Discover(p,2,1);
    CHECK(p.TryBegin(baseline,275)); // No obsolete-candidate GET round trip.
    Sent(p,2); CHECK(Reply(p,"BTPREVIEW1 ACCEPTED 1 2")); CHECK(Reply(p,"BTPREVIEW1 APPLIED 1 2"));
    Discover(p,3,2); Read(p,latest,2,0,2); CHECK_EQ(p.Phase(),HostPreviewPhase::Synchronized);
}

TEST_CASE(PreviewAsyncRepeatedEditsConvergeAndDoNotReplaceFrozenLines) {
    for(unsigned slice : {16u,2u}) {
        PreviewSession s; s.port->limit=96;
        s.port->baseline.pattern.active_rows=16; s.port->baseline.pattern.active_channels=3;
        NativeRateSampleBindings bindings{}; bindings.count=1; bindings.bindings[0]={7,60,{s.port->pcm,64,100}};
        CHECK_EQ(s.port->control.InitializeAudio(12753,100,s.port->baseline.pattern,bindings),PatternPlayerStatus::Success);
        s.port->metadata.working=s.port->baseline.pattern; s.Pump();
        CHECK(s.editor.Apply(EditorAction::Down)); s.Note(60);
        s.port->asynchronous=true; s.step_ms=slice; s.now+=100;
        unsigned cells=0; bool latest=false;
        for(unsigned i=0;i<2000;++i) {
            if(!s.port->owned_write.empty()) {
                if(s.port->owned_write.find("BTPREVIEW1 CELL")==0) {
                    ++cells;
                    if(cells==5) {CHECK(s.editor.Apply(EditorAction::NoteOff)); s.serial.SyncEditor(s.editor,s.now);}
                    if(cells==8) {CHECK(s.editor.Apply(EditorAction::SetValue,60)); s.serial.SyncEditor(s.editor,s.now);}
                    if(cells==10) {CHECK(s.editor.Apply(EditorAction::Clear)); s.serial.SyncEditor(s.editor,s.now); latest=true;}
                }
                s.port->CompleteWrite();
            }
            s.Tick();
            if(latest && SamePattern(s.port->metadata.working,*s.editor.Draft()) &&
                std::strstr(s.serial.PublicationStatus(),"synchronized")) break;
        }
        CHECK(latest); CHECK(!s.port->ownership_violation); CHECK(s.serial.Connected());
        CHECK(SamePattern(s.port->metadata.working,*s.editor.Draft()));
        CHECK_EQ(s.port->baseline.pattern.cells[1][0].note,NOTE_OFF);
        CHECK(s.editor.Dirty()); CHECK(s.port->output.find("BTPUBLISH1 BEGIN")==std::string::npos);
    }
}

TEST_CASE(PreviewAsyncEditsDuringStartedReadbackDrainThenConverge) {
    PreviewSession s; s.Pump(); CHECK(s.editor.Apply(EditorAction::Down));
    s.port->asynchronous=true; s.port->limit=96; s.port->hold_snapshot=true;
    s.Note(60); s.now+=100;
    for(unsigned i=0;i<500 && s.port->delayed_snapshot.empty();++i) {
        if(!s.port->owned_write.empty()) {s.port->CompleteWrite();}
        s.Tick();
    }
    CHECK(!s.port->delayed_snapshot.empty());
    CHECK(s.editor.Apply(EditorAction::Clear)); s.serial.SyncEditor(s.editor,s.now);
    const auto latest=*s.editor.Draft(); const auto mark=s.port->output.size();
    for(unsigned i=0;i<10;++i) s.Tick();
    CHECK(s.port->output.find("BTPREVIEW1 BEGIN",mark)==std::string::npos);
    s.port->input+=s.port->delayed_snapshot; s.port->delayed_snapshot.clear(); s.port->hold_snapshot=false;
    for(unsigned i=0;i<1000;++i) {
        if(!s.port->owned_write.empty()) {s.port->CompleteWrite(3);}
        s.Tick();
        if(SamePattern(s.port->metadata.working,latest) && std::strstr(s.serial.PublicationStatus(),"synchronized")) break;
    }
    CHECK(!s.port->ownership_violation); CHECK(s.serial.Connected());
    CHECK(SamePattern(latest,*s.editor.Draft())); CHECK(SamePattern(latest,s.port->metadata.working));
    CHECK(s.editor.Dirty()); CHECK_EQ(s.editor.Baseline()->pattern.cells[1][0].note,NOTE_OFF);
}

TEST_CASE(HostPreviewUnknownInstrumentStaysExplicitAndDoesNotBecomeApplied) {
    HostPatternPreview p; const auto baseline=PreviewBaseline(); Discover(p); Read(p,baseline.pattern,1,0,0);
    p.RecoveredInstalled(); auto draft=baseline.pattern; draft.cells[0][0]={60,99};
    p.Observe(draft,10); CHECK(p.TryBegin(baseline,85)); Sent(p,1);
    CHECK(Reply(p,"BTPREVIEW1 REJECTED 1 1 unknown-instrument"));
    CHECK_EQ(p.Phase(),HostPreviewPhase::Rejected); CHECK(std::strstr(p.Status(),"unknown-instrument"));
    CHECK(std::strstr(p.Status(),"draft not playing"));
    CHECK(!Reply(p,"BTPREVIEW1 APPLIED 1 1"));
    Discover(p,2); CHECK(!p.TryBegin(baseline,1000));
    CHECK_EQ(draft.cells[0][0].instrument,99); CHECK_EQ(draft.cells[0][0].note,60);
    CHECK_EQ(baseline.pattern.cells[0][0].instrument,7);
}

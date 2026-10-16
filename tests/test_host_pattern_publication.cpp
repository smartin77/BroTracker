#include "test_framework.h"
#include "bringup/host_pattern_publication.h"
#include "ui/bringup_serial.h"
#include "ui/bringup_controls.h"
#include "ui/pattern_screen.h"
#include "ui/text_renderer.h"
#include "../ui/windows/serial_write_limits.h"
#include <algorithm>
#include <string>
#include <vector>
#include <cstring>

using namespace BroTracker;
namespace {
    DevicePatternSnapshot Baseline() {
        DevicePatternSnapshot s; s.version=2; s.transfer_id=1; s.tempo_hundredths=12753;
        s.pattern.active_rows=2; s.pattern.active_channels=1;
        s.pattern.cells[0][0]={60,7}; s.pattern.cells[1][0]={NOTE_OFF,255};
        s.pattern.note_off_timing[1][0]=NoteOffTiming::EndOfPosition; return s;
    }
    void Sent(HostPatternPublication& p) {
        for(unsigned budget=0;p.Phase()==HostPublicationPhase::Sending && budget<130;++budget) {
            char line[96]; std::size_t length=0; CHECK(p.FormatLine(line,sizeof(line),length));
            p.BeforeWrite(); p.LineComplete();
        }
        CHECK_EQ(p.Phase(),HostPublicationPhase::Waiting);
    }
    bool Reply(HostPatternPublication& p,const std::string& line,unsigned now=1) {
        return p.Response(line.data(),line.size(),now);
    }
    struct Port : SerialTransport {
        bool online=true, windows_worker=false; unsigned limit=96, completed=0; std::string input,output;
        bool Open(FILE*,const char*) override { return online; }
        bool Healthy() override { return online; }
        void Close() override { input.clear(); }
        int Read(char* data,unsigned count) override {
            const auto n=std::min<std::size_t>(count,input.size());
            std::memcpy(data,input.data(),n); input.erase(0,n); return static_cast<int>(n);
        }
        int Write(const char* data,unsigned count) override {
            if (windows_worker) {
                if (completed) { const auto result=completed; completed=0; return static_cast<int>(result); }
                completed=WindowsSerialWritePrefix(count); output.append(data,completed); return 0;
            }
            count=std::min(count,limit); output.append(data,count); return static_cast<int>(count);
        }
        const char* Error() const override { return "test disconnect"; }
    };
    struct Session {
        Port* port=new Port;
        BringUpSerial serial{nullptr,std::unique_ptr<SerialTransport>(port)};
        LocalPatternEditor editor; unsigned now=0;
        void Tick() { serial.Tick(now++); serial.SyncEditor(editor,now); }
        void Receive(const std::string& text) { port->input+=text; Tick(); }
        void Snapshot(unsigned id,const DevicePatternSnapshot& s) {
            PatternSnapshotSender sender(s.pattern,s.tempo_hundredths); CHECK(sender.Begin(id,0,s.version));
            while(sender.HasData()) { std::size_t n; const auto* data=sender.Data(n); Receive(std::string(data,n)); sender.Consume(n); }
        }
        void Ready(bool capability=true,unsigned version=2,unsigned rows=2,unsigned channels=1) {
            Tick(); Tick(); port->output.clear();
            Receive("BTTEST1 STATE IDLE\nBTPATTERN1 SNAPCAP "+std::to_string(version)+"\n"+
                "BTPATTERN1 POS 2 0 0 0 0 0 "+std::to_string(rows)+" "+std::to_string(channels)+"\n"+
                (capability ? "BTPUBLISH1 CAP 1\nBTPUBLISH1 NEXT 1 7\n" : ""));
            Tick(); auto baseline=Baseline(); baseline.version=version;
            baseline.pattern.active_rows=rows; baseline.pattern.active_channels=channels; Snapshot(1,baseline);
            CHECK(editor.Ready()); port->output.clear();
        }
        void Dirty() {
            CHECK(editor.Apply(EditorAction::Down)); CHECK(editor.Apply(EditorAction::Right));
            CHECK(editor.Apply(EditorAction::Increment)); CHECK(editor.Dirty());
        }
        void Commit() {
            for(unsigned budget=0;port->output.find("BTPUBLISH1 COMMIT 1 7 2 4120551324\n")==std::string::npos && budget<200;++budget) Tick();
            CHECK(port->output.find("BTPUBLISH1 COMMIT 1 7 2 4120551324\n")!=std::string::npos);
        }
    };
}
TEST_CASE(HostPublicationSerializationPhaseAndExactReadback) {
    auto baseline=Baseline(); auto draft=baseline.pattern; draft.note_off_timing[1][0]=NoteOffTiming::Arrival;
    HostPatternPublication p; CHECK(p.Begin(7,baseline,draft,0)); draft.cells[0][0].note=61;
    std::string wire;
    while(p.Phase()==HostPublicationPhase::Sending) {
        char line[96]; std::size_t n=0; CHECK(p.FormatLine(line,sizeof(line),n)); wire.append(line,n); p.BeforeWrite(); p.LineComplete();
    }
    CHECK_EQ(wire,"BTPUBLISH1 BEGIN 1 7 2 1 2\nBTPUBLISH1 CELL 1 7 0 60 7 0\nBTPUBLISH1 CELL 1 7 1 254 255 0\nBTPUBLISH1 COMMIT 1 7 2 4120551324\n");
    CHECK(!Reply(p,"BTPUBLISH1 APPLIED 1 7")); CHECK(!Reply(p,"BTPUBLISH1 SUBMITTED 2 7"));
    CHECK(!Reply(p,"BTPUBLISH1 SUBMITTED 1 8")); CHECK(Reply(p,"BTPUBLISH1 SUBMITTED 1 7"));
    CHECK(!Reply(p,"BTPUBLISH1 SUBMITTED 1 7")); CHECK(p.Blocking());
    CHECK(Reply(p,"BTPUBLISH1 APPLIED 1 7")); CHECK(!Reply(p,"BTPUBLISH1 APPLIED 1 7"));
    CHECK(p.Blocking()); auto snapshot=baseline; snapshot.pattern=p.Frozen();
    CHECK(p.Verify(snapshot)); CHECK_EQ(p.Phase(),HostPublicationPhase::Success); CHECK(p.TakeConfirmed()); CHECK(!p.TakeConfirmed());
}
TEST_CASE(HostPublicationDiscoveryGatesPartialWritesAndConfirmedEditorRebase) {
    Session s; s.Ready(); CHECK(!s.serial.Publish(s.editor,s.now)); s.Dirty();
    const auto cursor=s.editor.Cursor(); const auto tempo=s.editor.Baseline()->tempo_hundredths;
    s.port->limit=3; CHECK(s.serial.Publish(s.editor,s.now)); CHECK(s.editor.Locked());
    CHECK(!s.editor.Apply(EditorAction::Restore)); CHECK(!s.editor.Apply(EditorAction::Increment));
    CHECK(s.editor.Apply(EditorAction::Up)); CHECK(!s.serial.Start()); CHECK(!s.serial.Pause()); CHECK(!s.serial.Continue());
    s.Commit();
    CHECK(s.port->output.find("BTPUBLISH1 BEGIN 1 7 2 1 2\nBTPUBLISH1 CELL 1 7 0 60 7 0\nBTPUBLISH1 CELL 1 7 1 254 255 0\nBTPUBLISH1 COMMIT 1 7 2 4120551324\n")==0);
    s.Receive("BTPUBLISH1 SUBMITTED 1 7\n"); CHECK(s.editor.Dirty()); CHECK(s.editor.Locked());
    s.Receive("BTPUBLISH1 APPLIED 1 7\nBTTEST1 STATE IDLE\nBTPUBLISH1 NEXT 1 8\n");
    for(unsigned i=0;i<20;++i) { s.Tick(); }
    CHECK(s.port->output.find("BTPATTERN1 GET 2 2\n")!=std::string::npos);
    auto accepted=Baseline(); accepted.pattern=*s.editor.Draft(); s.Snapshot(2,accepted);
    CHECK(!s.editor.Dirty()); CHECK(!s.editor.Locked()); CHECK_EQ(s.editor.Cursor().row,0);
    CHECK_EQ(s.editor.Cursor().channel,cursor.channel); CHECK_EQ(s.editor.Cursor().field,cursor.field);
    CHECK_EQ(s.editor.Baseline()->tempo_hundredths,tempo);
    CHECK(std::strstr(s.serial.PublicationStatus(),"confirmed"));
    // Fresh NEXT was obtained from STATUS, not assumed from arithmetic or APPLIED.
    CHECK(s.port->output.find("BTTEST1 STATUS\n")!=std::string::npos);
    CHECK(s.editor.Apply(EditorAction::Down)); CHECK(s.editor.Apply(EditorAction::Increment));
    CHECK(s.serial.Publish(s.editor,s.now));
    for (unsigned i = 0; i < 12; ++i) { s.Tick(); }
    CHECK(s.port->output.find("BTPUBLISH1 BEGIN 1 8")!=std::string::npos);
}
TEST_CASE(HostPublicationLegacyMissingDiscoveryAndNativeRejection) {
    for(unsigned mode=0;mode<3;++mode) {
        Session s; s.Ready(mode!=0,mode==1 ? 1 : 2); s.Dirty();
        if(mode==2) s.Receive("BTTEST1 STATE PLAYING\n");
        CHECK(!s.serial.Publish(s.editor,s.now)); CHECK(s.port->output.find("BTPUBLISH1 BEGIN")==std::string::npos);
    }
    Session s; s.Ready(); s.Dirty(); const auto baseline=*s.editor.Baseline(); const auto draft=*s.editor.Draft();
    CHECK(s.serial.Publish(s.editor,s.now)); s.Commit();
    s.Receive("BTPUBLISH1 SUBMITTED 1 7\nBTPUBLISH1 REJECTED 1 7 unsupported-pitch\n");
    CHECK(s.editor.Dirty()); CHECK(!s.editor.Locked()); CHECK(SamePattern(*s.editor.Draft(),draft));
    CHECK(SamePattern(s.editor.Baseline()->pattern,baseline.pattern)); CHECK(std::strstr(s.serial.PublicationStatus(),"unsupported-pitch"));
    CHECK(!s.serial.Publish(s.editor,s.now)); // Identity unavailable until fresh STATUS/NEXT.
    s.Receive("BTTEST1 STATE IDLE\nBTPUBLISH1 NEXT 1 8\n"); CHECK(s.serial.Publish(s.editor,s.now));
}
TEST_CASE(HostPublicationTimeoutMismatchAndDisconnectRequireReadback) {
    auto baseline=Baseline(); HostPatternPublication p;
    CHECK(p.Begin(7,baseline,baseline.pattern,0)); Sent(p); p.Tick(6000);
    CHECK_EQ(p.Phase(),HostPublicationPhase::Uncertain); CHECK(p.Blocking());
    CHECK(!p.Begin(8,baseline,baseline.pattern,6001));
    CHECK(p.Verify(baseline)); CHECK_EQ(p.Phase(),HostPublicationPhase::Success);
    CHECK(p.Begin(8,baseline,baseline.pattern,7000)); Sent(p);
    CHECK(Reply(p,"BTPUBLISH1 SUBMITTED 1 8",7001)); CHECK(Reply(p,"BTPUBLISH1 APPLIED 1 8",7002));
    auto changed=baseline; changed.pattern.note_off_timing[1][0]=NoteOffTiming::Arrival;
    CHECK(p.Verify(changed)); CHECK_EQ(p.Phase(),HostPublicationPhase::Mismatch); CHECK(!p.TakeConfirmed());
    Session s; s.Ready(); s.Dirty(); CHECK(s.serial.Publish(s.editor,s.now)); s.Commit();
    const auto frozen=*s.editor.Draft(); s.port->online=false; s.Tick();
    CHECK(!s.editor.Ready()); CHECK(s.serial.PublicationBusy()); CHECK(std::strstr(s.serial.PublicationStatus(),"uncertain"));
    s.port->online=true; s.now+=1001; s.Tick(); s.Tick(); s.port->output.clear();
    s.Receive("BTTEST1 STATE IDLE\nBTPATTERN1 SNAPCAP 2\nBTPUBLISH1 CAP 1\nBTPUBLISH1 NEXT 1 8\nBTPATTERN1 POS 2 0 0 0 0 0 2 1\n");
    auto accepted=Baseline(); accepted.pattern=frozen; s.Snapshot(2,accepted);
    CHECK(!s.serial.PublicationBusy()); CHECK(s.editor.Ready()); CHECK(!s.editor.Dirty());
    CHECK(s.port->output.find("BTPUBLISH1 BEGIN")==std::string::npos);
    CHECK(s.port->output.find("BTTEST1 START")==std::string::npos);
}
TEST_CASE(HostPublicationStopExitPreservesPartialLinesAndUncertainSubmittedWork) {
    Session s; s.Ready(); s.Dirty(); s.port->limit=2;
    CHECK(s.serial.Publish(s.editor,s.now)); s.Tick(); s.Tick(); // Partial BEGIN.
    BringUpControls controls(s.serial,nullptr); controls.Request(BringUpAction::Exit,"test",s.now);
    for(unsigned i=0;i<40;++i) { controls.Tick(s.now++); s.serial.SyncEditor(s.editor,s.now); }
    CHECK(s.port->output.find("BTPUBLISH1 BEGIN 1 7 2 1 2\nBTTEST1 STOP\n")==0);
    CHECK(s.port->output.find("BTPUBLISH1 COMMIT")==std::string::npos);
    s.Receive("BTTEST1 STOPPED\n"); controls.Tick(s.now++); CHECK(controls.Quit());
    HostPatternPublication submitted; auto b=Baseline(); CHECK(submitted.Begin(1,b,b.pattern,0)); Sent(submitted);
    submitted.StopRequested(); CHECK(submitted.Blocking()); submitted.Disconnect();
    CHECK_EQ(submitted.Phase(),HostPublicationPhase::Uncertain);
}
TEST_CASE(HostPublicationMessagesRemainWithinRightPanel) {
    CHECK(LoadUiFont("assets/fonts/brotracker.btf"));
    Framebuffer fb(640,480); Tune tune; Pattern preview;
    RenderMainScreen(fb,tune,preview);
    const std::vector<Color> baseline(fb.PixelData(),fb.PixelData()+640*480);
    for(const auto* message : {"Publication sending","Publication verifying device readback","Publication confirmed by readback",
        "Publication rejected: unsupported-pitch","Publication uncertain - F5 requests readback"}) {
        RenderMainScreen(fb,tune,preview,std::nullopt,nullptr,HostPanelInformation{"","",message});
        unsigned changed=0;
        for(unsigned y=0;y<480;++y) for(unsigned x=0;x<640;++x)
            if(std::memcmp(&baseline[y*640+x],&fb.PixelData()[y*640+x],sizeof(Color))) {
                CHECK(x>=466 && x<631); CHECK(y>=38 && y<442); ++changed;
            }
        CHECK(changed>0);
    }
}

TEST_CASE(HostPublicationStrictNextDiscoveryAndReadbackMismatchRetainsDraft) {
    Session s; s.Ready(); s.Dirty(); CHECK(s.serial.Publish(s.editor,s.now)); s.Commit();
    s.Receive("BTPUBLISH1 SUBMITTED 1 7\nBTPUBLISH1 APPLIED 1 7\nBTTEST1 STATE IDLE\nBTPUBLISH1 NEXT 1 8\n");
    const auto draft=*s.editor.Draft(); const auto old=*s.editor.Baseline();
    for(unsigned i=0;i<5;++i) { s.Tick(); } s.Snapshot(2,Baseline()); // Device still has EndOfPosition.
    CHECK(s.editor.Dirty()); CHECK(!s.editor.Locked()); CHECK(SamePattern(*s.editor.Draft(),draft));
    CHECK(SamePattern(s.editor.Baseline()->pattern,old.pattern)); CHECK(std::strstr(s.serial.PublicationStatus(),"differs"));
    // Consume an identity again only after a new STATUS discovery, never reuse 7.
    CHECK(s.serial.Publish(s.editor,s.now));
    s.serial.Stop(); for(unsigned i=0;i<4;++i) { s.Tick(); }
    s.Receive("BTTEST1 STOPPED\n"); s.now+=1001; s.Tick(); s.Tick();
    s.Receive("BTTEST1 STATE IDLE\nBTPUBLISH1 NEXT 2 9\nBTPUBLISH1 NEXT 1 4294967296\n");
    CHECK(!s.serial.Publish(s.editor,s.now));
    s.Receive("BTPUBLISH1 NEXT 1 0\n"); CHECK(!s.serial.Publish(s.editor,s.now));
}
TEST_CASE(HostPublicationReadbackFailureStaysUncertainAndDoesNotResend) {
    Session s; s.Ready(); s.Dirty(); CHECK(s.serial.Publish(s.editor,s.now)); s.Commit();
    s.Receive("BTPUBLISH1 SUBMITTED 1 7\nBTPUBLISH1 APPLIED 1 7\nBTTEST1 STATE IDLE\nBTPUBLISH1 NEXT 1 8\n");
    for(unsigned i=0;i<5;++i) { s.Tick(); }
    s.Receive("BTPATTERN1 SNAPERR 2 2\n");
    CHECK(s.serial.PublicationBusy()); CHECK(s.editor.Locked()); CHECK(std::strstr(s.serial.PublicationStatus(),"uncertain"));
    const auto before=s.port->output;
    for(unsigned i=0;i<5;++i) { s.Tick(); } CHECK_EQ(s.port->output,before);
    CHECK(s.serial.Publish(s.editor,s.now)); for(unsigned i=0;i<4;++i) { s.Tick(); }
    CHECK(s.port->output.find("BTPATTERN1 GET 2 3\n")!=std::string::npos);
    auto accepted=Baseline(); accepted.pattern=*s.editor.Draft(); s.Snapshot(3,accepted);
    CHECK(!s.serial.PublicationBusy()); CHECK(!s.editor.Dirty());
    CHECK(s.port->output.find("BTPUBLISH1 BEGIN 1 7") == s.port->output.rfind("BTPUBLISH1 BEGIN 1 7"));
}

TEST_CASE(WindowsPublicationLongCellLinesCompleteWithoutReopening) {
    for (unsigned rejected=0; rejected<2; ++rejected) {
        Session s; s.Ready(true,2,16,2); s.port->windows_worker=true;
        CHECK_EQ(WindowsSerialWritePrefix(33),33u);
        CHECK_EQ(WindowsSerialWritePrefix(kWindowsSerialWriteCapacity+1),kWindowsSerialWriteCapacity);
        CHECK(s.editor.Apply(rejected ? EditorAction::Increment : EditorAction::Clear));
        const auto draft=*s.editor.Draft(), baseline=s.editor.Baseline()->pattern;
        CHECK(s.serial.Publish(s.editor,s.now));
        for(unsigned i=0;i<200 && s.port->output.find("BTPUBLISH1 COMMIT 1 7 ")==std::string::npos;++i) s.Tick();
        s.Tick(); s.Tick(); // Observe asynchronous COMMIT completion before its replies.
        CHECK(s.serial.Connected());
        CHECK(s.port->output.find("BTPUBLISH1 CELL 1 7 11 255 255 0\n")!=std::string::npos);
        CHECK(s.port->output.find("BTPUBLISH1 CELL 1 7 31 255 255 0\n")!=std::string::npos);
        CHECK(s.port->output.find("BTPUBLISH1 COMMIT 1 7 32 ")!=std::string::npos);
        s.Receive("BTPUBLISH1 SUBMITTED 1 7\n");
        if(rejected) {
            s.Receive("BTPUBLISH1 REJECTED 1 7 unsupported-pitch\n");
            CHECK(SamePattern(*s.editor.Draft(),draft)); CHECK(SamePattern(s.editor.Baseline()->pattern,baseline));
            CHECK(s.editor.Dirty()); CHECK(!s.editor.Locked());
        } else {
            s.Receive("BTPUBLISH1 APPLIED 1 7\nBTTEST1 STATE IDLE\nBTPUBLISH1 NEXT 1 8\n");
            for(unsigned i=0;i<8;++i) s.Tick();
            auto accepted=Baseline(); accepted.pattern=draft; s.Snapshot(2,accepted);
            CHECK(!s.editor.Dirty()); CHECK(SamePattern(s.editor.Baseline()->pattern,draft));
        }
        CHECK(s.serial.Connected()); CHECK(s.port->output.find("BTTEST1 HELLO")==std::string::npos);
    }
}

TEST_CASE(OneBasedDisplayPublicationPreservesRawInstrumentIds) {
    auto baseline=Baseline(); auto draft=baseline.pattern;
    draft.cells[0][0].instrument=0; draft.cells[1][0]={NOTE_EMPTY,1};
    HostPatternPublication publication; CHECK(publication.Begin(1,baseline,draft,0));
    std::string wire;
    while(publication.Phase()==HostPublicationPhase::Sending) {
        char line[96]; std::size_t n=0; CHECK(publication.FormatLine(line,sizeof(line),n));
        wire.append(line,n); publication.BeforeWrite(); publication.LineComplete();
    }
    CHECK(wire.find("CELL 1 1 0 60 0 0\n")!=std::string::npos);
    CHECK(wire.find("CELL 1 1 1 255 1 1\n")!=std::string::npos);
    CHECK(Reply(publication,"BTPUBLISH1 SUBMITTED 1 1")); CHECK(Reply(publication,"BTPUBLISH1 APPLIED 1 1"));
    baseline.pattern=draft; CHECK(publication.Verify(baseline)); CHECK(publication.TakeConfirmed());
    PatternSnapshotSender sender(draft,baseline.tempo_hundredths); CHECK(sender.Begin(2,0,2));
    PatternSnapshotAssembler receiver; CHECK(receiver.Request(2,0,2));
    while(sender.HasData()) {
        std::size_t n; const auto* data=sender.Data(n);
        CHECK(n && data[n-1]=='\n'); receiver.Accept(data,n-1,0); sender.Consume(n);
    }
    CHECK(receiver.Snapshot()); CHECK_EQ(receiver.Snapshot()->pattern.cells[0][0].instrument,0);
    CHECK_EQ(receiver.Snapshot()->pattern.cells[1][0].instrument,1);
}

TEST_CASE(PausedHostPublicationRequiresCapabilityAndRebasesWithoutContinuing) {
    Session s; s.Ready(); s.Dirty(); s.Receive("BTTEST1 STATE PAUSED\n");
    CHECK(!s.serial.Publish(s.editor,s.now)); CHECK(std::strstr(s.serial.PublicationStatus(),"paused publication unsupported"));
    s.Receive("BTPUBLISH1 PAUSEDCAP 1\n"); CHECK_EQ(std::string(s.serial.PublicationStatus()),"");
    CHECK(s.serial.Publish(s.editor,s.now)); s.Commit(); const auto draft=*s.editor.Draft();
    s.Receive("BTPUBLISH1 SUBMITTED 1 7\nBTPUBLISH1 APPLIED 1 7\nBTTEST1 STATE PAUSED\nBTPUBLISH1 NEXT 1 8\n");
    for(unsigned i=0;i<5;++i) s.Tick(); auto accepted=Baseline(); accepted.pattern=draft; s.Snapshot(2,accepted);
    CHECK(s.serial.Paused()); CHECK(!s.editor.Dirty()); CHECK(!s.editor.Locked());
    CHECK(s.port->output.find("CONTINUE")==std::string::npos); CHECK(s.port->output.find("STOP")==std::string::npos);
    CHECK(!s.serial.Publish(s.editor,s.now)); CHECK(std::strstr(s.serial.PublicationStatus(),"No changes"));
    CHECK(s.editor.Apply(EditorAction::Increment)); s.serial.SyncEditor(s.editor,s.now);
    CHECK_EQ(std::string(s.serial.PublicationStatus()),"");
    s.port->online=false; s.Tick(); s.port->online=true; s.now=2000; s.Tick();
    s.Receive("BTTEST1 STATE PAUSED\nBTPUBLISH1 CAP 1\nBTPUBLISH1 NEXT 1 8\nBTPATTERN1 SNAPCAP 2\n");
    CHECK(!s.serial.Publish(s.editor,s.now)); CHECK(std::strstr(s.serial.PublicationStatus(),"paused publication unsupported"));
}

#include "test_framework.h"
#include "ui/bringup_serial.h"
#include "bringup/pattern_file_store.h"
#include "bringup/pattern_control.h"
#include "bringup/variable_rate_player_adapter.h"
#include "bringup/storage_ready_wait.h"
#include <string>
#include <vector>
#include <algorithm>
using namespace BroTracker;
namespace {
struct StoragePeer:SerialTransport {
    std::string rx,tx;std::vector<std::string> commands;bool healthy=true,cap=true,respond=true;unsigned pattern=1;std::string result="ok",state="IDLE";
    using Control=BasicPatternBringUpControl<VariableRateBringUpPlayer,VariableRateSampleBindings>;
    Control control;StorageReadyWait readiness;
    bool real_control=false,initialized=false,cleanup_busy=false,transfer_busy=false,readiness_cap=true;
    std::uint32_t now=0;
    void AudioService() {
        if(real_control && initialized) {
            std::int16_t audio[128]{};control.AudioBlock(audio,128,true);PatternAppliedRequest ack;
            while(control.TakeApplied(ack))if(ack.request==PatternRequest::Stop)rx+="BTTEST1 STOPPED\n";
        }
        if(readiness.Active()) {
            const bool ready=(!real_control || control.StorageReady()) && !cleanup_busy && !transfer_busy;
            const auto result=readiness.Poll(now,ready);
            if(result!=StorageReadyWait::Result::Waiting) {rx+=result==StorageReadyWait::Result::Ready?"BTSTORE1 READY 1\n":"BTSTORE1 NOTREADY 1 timeout\n";readiness.Cancel();}
        }
    }
    void Initialize() {
        const auto p=MakeDevelopmentPattern();const auto bindings=SelectDevelopmentFixture({}).bindings;
        CHECK_EQ(control.InitializeAudio(15500,44100,p,bindings),PatternPlayerStatus::Success);initialized=true;
    }
    bool preview_cap=false,hold_load=false,hold_baseline=false,hold_working=false;
    std::string load_reply,baseline_reply,working_reply;
    bool Open(FILE*,const char*) override {healthy=true;return true;}
    void Close() override {}
    bool Healthy() override {return healthy;}
    const char* Error() const override {return "fixture disconnect";}
    int Read(char* p,unsigned n) override {if(rx.empty()||!n)return 0;*p=rx.front();rx.erase(0,1);return 1;}
    int Write(const char* p,unsigned n) override {
        tx.append(p,n);auto end=tx.find('\n');if(end==std::string::npos)return n;
        const auto line=tx.substr(0,end);tx.erase(0,end+1);commands.push_back(line);
        if(line=="BTTEST1 HELLO" || line=="BTTEST1 STATUS") {
            rx+="BTTEST1 STATE "+state+"\n";
            rx+=pattern==5?"BTPATTERN1 POS 2 0 0 0 0 0 16 3\n":"BTPATTERN1 POS 2 0 0 0 0 0 32 8\n";
            if(line=="BTTEST1 HELLO")rx+="BTAUDIO1 CAP 1\nBTMIDI1 CAP 1\nBTPATTERN1 SNAPCAP 2\n"+(cap?std::string("BTSTORE1 CAP 1\n"):std::string{});
            if(line=="BTTEST1 HELLO" && readiness_cap)rx+="BTSTORE1 READYCAP 1\n";
            if(preview_cap) {
                if(line=="BTTEST1 HELLO")rx+="BTPREVIEW1 CAP 1\nBTPREVIEW1 DELTACAP 1\n";
                rx+="BTPREVIEW1 NEXT 1 1\nBTPREVIEW1 STATE 1 1 0 0\n";
            }
        }else if(line=="BTAUDIO1 SET 1 default")rx+="BTAUDIO1 SELECTED 1 bridge\n";
        else if(line=="BTMIDI1 SET 1 12")rx+="BTMIDI1 SELECTED 1 12\n";
        else if(line=="BTSTORE1 READY 1") {CHECK(readiness.Begin(now));AudioService();}
        else if(line.rfind("BTSTORE1 LOAD 1 ",0)==0 && respond) {
            if(real_control)CHECK(control.StorageReady() && !cleanup_busy && !transfer_busy);
            const auto name=line.substr(16);pattern=name=="none"?0:(name=="patern1" || name=="patern1.btdev")?1:(name=="patern2" || name=="patern2.btdev")?2:(name=="patern3" || name=="patern3.btdev")?3:(name=="patern4" || name=="patern4.btdev")?4:5;std::string reply;
            if(result=="ok")reply+="BTSTORE1 SUBMITTED 1 load-"+name+" 1\n";
            reply+="BTSTORE1 RESULT 1 load-"+name+" "+result+" 1\n";
            if(hold_load)load_reply+=reply;else rx+=reply;
        }else if(line.rfind("BTPATTERN1 GET 2 ",0)==0) {
            RealtimePattern pattern;MakeStoredDevelopmentPattern(pattern,this->pattern==5?0:this->pattern);
            if(this->pattern==5){pattern.active_rows=16;pattern.active_channels=3;pattern.cells[1][2]={NOTE_OFF,255};pattern.note_off_timing[1][2]=NoteOffTiming::EndOfPosition;}
            PatternSnapshotSender sender(pattern,this->pattern==5?12753:DevelopmentPatternTempo(this->pattern));
            (void)sender.Begin(static_cast<unsigned>(std::stoul(line.substr(17))),0,2);
            std::string reply;while(sender.HasData()){std::size_t count;const auto* bytes=sender.Data(count);reply.append(bytes,count);sender.Consume(count);}
            if(hold_baseline)baseline_reply+=reply;else rx+=reply;
        }else if(line.rfind("BTPREVIEW1 GET 1 ",0)==0) {
            const auto id=static_cast<unsigned>(std::stoul(line.substr(17)));
            RealtimePattern pattern;MakeStoredDevelopmentPattern(pattern,this->pattern==5?0:this->pattern);
            if(this->pattern==5){pattern.active_rows=16;pattern.active_channels=3;pattern.cells[1][2]={NOTE_OFF,255};pattern.note_off_timing[1][2]=NoteOffTiming::EndOfPosition;}
            PatternSnapshotSender sender(pattern,this->pattern==5?12753:DevelopmentPatternTempo(this->pattern));
            CHECK(sender.SetWorkingNamespace(true));CHECK(sender.Begin(id,0,2));
            std::string reply="BTPREVIEW1 READ 1 "+std::to_string(id)+" 0 1 0\n";
            while(sender.HasData()){std::size_t count;const auto* bytes=sender.Data(count);reply.append(bytes,count);sender.Consume(count);}
            if(hold_working)working_reply+=reply;else rx+=reply;
        }else if(line=="BTTEST1 STOP") {
            if(real_control)CHECK_EQ(control.Submit(PatternRequest::Stop),PatternRequestStatus::Accepted);
            else rx+="BTTEST1 STOPPED\n";
        }
        return n;
    }
    unsigned Loads()const {return static_cast<unsigned>(std::count_if(commands.begin(),commands.end(),[](const auto& c){return c.rfind("BTSTORE1 LOAD",0)==0;}));}
};
void Pump(BringUpSerial& serial,unsigned first=0,unsigned last=80,LocalPatternEditor* target=nullptr){
    LocalPatternEditor local;auto& editor=target?*target:local;
    for(unsigned i=first;i<last;++i){serial.Tick(i*20);serial.SyncEditor(editor,i*20);}
}
}
TEST_CASE(HostStartupSdLoadSetupReadbackAndNoAutomaticStart) {
    for(auto selection:{"none","patern1","patern2","patern3","patern4","patern4.btdev"}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern(selection);
        CHECK(!serial.Start());Pump(serial);CHECK(serial.StartupPatternReady());CHECK_EQ(wire->Loads(),1u);
        const auto load=std::find(wire->commands.begin(),wire->commands.end(),std::string("BTSTORE1 LOAD 1 ")+selection);CHECK(load!=wire->commands.end());
        CHECK(std::find(wire->commands.begin(),load,"BTMIDI1 SET 1 12")!=load);CHECK(std::find(wire->commands.begin(),load,"BTAUDIO1 SET 1 default")!=load);
        CHECK(std::find(wire->commands.begin(),wire->commands.end(),"BTTEST1 START")==wire->commands.end());
        const auto view=serial.PlaybackView(1600);CHECK(view);CHECK(view->device_pattern);CHECK_EQ(view->device_pattern->tempo_hundredths,std::string(selection)=="none"?12500u:15500u);
        CHECK_EQ(view->device_pattern->pattern.cells[0][4].note,(std::string(selection)=="patern3" || (std::string(selection)=="patern4" || std::string(selection)=="patern4.btdev"))?36:NOTE_EMPTY);CHECK(!serial.Playing());
        wire->healthy=false;serial.Tick(1700);Pump(serial,140,220);CHECK_EQ(wire->Loads(),1u); // no reconnect replay
    }
}
TEST_CASE(HostStartupFailuresNeverSeedStartOrRetry) {
    for(unsigned scenario=0;scenario<5;++scenario){
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
        if(scenario==0)wire->cap=false;if(scenario==1)wire->state="PLAYING";if(scenario==2)wire->result="missing";if(scenario==3)wire->result="invalid";if(scenario==4)wire->respond=false;
        BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern3");Pump(serial,0,900);
        CHECK(!serial.StartupPatternReady());CHECK(!serial.Start());CHECK(wire->Loads()<=1);
        CHECK(std::strstr(serial.Status(),"failed") || std::strstr(serial.Status(),"uncertain"));
        for(const auto& c:wire->commands){CHECK(c!="BTTEST1 START");CHECK(c.rfind("BTSTORE1 SEED",0)!=0);}
    }
}
TEST_CASE(HostStartupNoneEstablishesClean125Baseline) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("none");LocalPatternEditor editor;Pump(serial,0,80,&editor);CHECK(serial.StartupPatternReady());CHECK_EQ(wire->Loads(),1u);CHECK_EQ(editor.Baseline()->tempo_hundredths,12500u);for(unsigned r=0;r<32;++r)for(unsigned c=0;c<8;++c){CHECK_EQ(editor.Draft()->cells[r][c].note,NOTE_EMPTY);CHECK_EQ(editor.Draft()->cells[r][c].instrument,255);CHECK_EQ(editor.Draft()->note_off_timing[r][c],NoteOffTiming::Arrival);}
}
namespace {
void SeedEditor(LocalPatternEditor& editor) {
    LivePlaybackView view;DevicePatternSnapshot baseline;baseline.version=2;baseline.transfer_id=77;baseline.tempo_hundredths=12500;baseline.pattern=MakeDevelopmentPattern();
    view.device_pattern=baseline;view.telemetry_available=true;view.position.rows=32;view.position.channels=8;
    editor.Sync(view);CHECK(editor.Ready());CHECK(editor.Apply(EditorAction::SetValue,90));
}
void BlockedEdits(LocalPatternEditor& editor) {
    CHECK(editor.Locked());CHECK(!editor.Apply(EditorAction::SetValue,75));CHECK(!editor.Apply(EditorAction::NoteOff));
    (void)editor.Apply(EditorAction::Right);CHECK(!editor.Apply(EditorAction::SetValue,1));CHECK(!editor.Apply(EditorAction::Increment));
    CHECK(!editor.Apply(EditorAction::Clear));CHECK(!editor.Undo());CHECK(!editor.Redo());(void)editor.Apply(EditorAction::Left);
}
void NoPreviewWrites(const StoragePeer& peer) {
    for(const auto& c:peer.commands) {
        CHECK(c.rfind("BTPREVIEW1 BEGIN",0)!=0);CHECK(c.rfind("BTPREVIEW1 DBEGIN",0)!=0);
        CHECK(c.rfind("BTPREVIEW1 CELL",0)!=0);CHECK(c.rfind("BTPREVIEW1 DCELL",0)!=0);
        CHECK(c.rfind("BTPREVIEW1 COMMIT",0)!=0);CHECK(c.rfind("BTPREVIEW1 DCOMMIT",0)!=0);
    }
}
}
TEST_CASE(StartupEditorBarrierWaitsForLoadBaselineAndWorkingRecovery) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->preview_cap=true;wire->hold_load=wire->hold_baseline=wire->hold_working=true;
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern3");
    LocalPatternEditor editor;SeedEditor(editor);
    Pump(serial,0,25,&editor);CHECK_EQ(wire->Loads(),1u);CHECK(!wire->load_reply.empty());BlockedEdits(editor);NoPreviewWrites(*wire);CHECK(!serial.Start());
    wire->rx+=wire->load_reply;wire->load_reply.clear();Pump(serial,25,45,&editor);
    CHECK(!wire->baseline_reply.empty());BlockedEdits(editor);NoPreviewWrites(*wire);CHECK(!serial.StartupPatternReady());
    wire->rx+=wire->baseline_reply;wire->baseline_reply.clear();Pump(serial,45,65,&editor);
    CHECK(editor.Ready());CHECK(!wire->working_reply.empty());BlockedEdits(editor);NoPreviewWrites(*wire);CHECK(!serial.StartupPatternReady());
    // On MIDI row3, instrument-column increment is an OFF-timing edit.
    for(unsigned i=0;i<4;++i)CHECK(editor.Apply(EditorAction::NextChannel));
    for(unsigned i=0;i<3;++i)CHECK(editor.Apply(EditorAction::Down));CHECK(editor.Apply(EditorAction::Right));
    CHECK(!editor.Apply(EditorAction::Increment));CHECK(editor.OffTiming(3,4));if(editor.OffTiming(3,4))CHECK_EQ(*editor.OffTiming(3,4),NoteOffTiming::Arrival);
    wire->rx+=wire->working_reply;wire->working_reply.clear();Pump(serial,65,90,&editor);
    CHECK(serial.StartupPatternReady());CHECK(!editor.Locked());RealtimePattern expected;MakeStoredDevelopmentPattern(expected,3);
    CHECK(editor.Baseline());if(editor.Baseline())CHECK(SamePattern(editor.Baseline()->pattern,expected));
    CHECK(SamePattern(*editor.Draft(),expected));CHECK(!editor.Dirty());NoPreviewWrites(*wire);
    CHECK(editor.Apply(EditorAction::Left));CHECK(editor.Apply(EditorAction::Up));CHECK(editor.Apply(EditorAction::SetValue,75));
    Pump(serial,90,120,&editor);CHECK(std::any_of(wire->commands.begin(),wire->commands.end(),[](const auto& c){return c.rfind("BTPREVIEW1 DBEGIN",0)==0;}));
}
TEST_CASE(StartupEditorBarrierRetainsFailureAndAllowsStop) {
    for(bool timeout:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->preview_cap=true;wire->result="missing";wire->respond=!timeout;
        BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern3");LocalPatternEditor editor;SeedEditor(editor);
        Pump(serial,0,900,&editor);BlockedEdits(editor);NoPreviewWrites(*wire);CHECK(!serial.StartupPatternReady());CHECK(!serial.Start());
        CHECK(std::strstr(serial.Status(),"failed") || std::strstr(serial.Status(),"uncertain"));CHECK(serial.Stop());Pump(serial,900,910,&editor);
        CHECK(serial.Stopped());CHECK(editor.Locked());CHECK(std::find(wire->commands.begin(),wire->commands.end(),"BTTEST1 STOP")!=wire->commands.end());
    }
}
TEST_CASE(CleanStartupPreservesNormalEditorPreview) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->preview_cap=true;BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("none");LocalPatternEditor editor;
    Pump(serial,0,80,&editor);CHECK(editor.Ready());CHECK(!editor.Locked());CHECK_EQ(wire->Loads(),1u);
    CHECK(editor.Apply(EditorAction::SetValue,75));Pump(serial,80,110,&editor);
    CHECK(std::any_of(wire->commands.begin(),wire->commands.end(),[](const auto& c){return c.rfind("BTPREVIEW1 DBEGIN",0)==0;}));
}
TEST_CASE(StartupBarrierDoesNotResetDraftOrReloadOnOrdinaryReconnect) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->preview_cap=true;
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern3");LocalPatternEditor editor;
    Pump(serial,0,80,&editor);CHECK(serial.StartupPatternReady());CHECK(!editor.Locked());CHECK_EQ(wire->Loads(),1u);
    CHECK(editor.Apply(EditorAction::SetValue,75));serial.SyncEditor(editor,1601);
    wire->healthy=false;serial.Tick(1602);serial.SyncEditor(editor,1602);
    Pump(serial,140,175,&editor);
    CHECK_EQ(wire->Loads(),1u);CHECK(serial.StartupPatternReady());CHECK(editor.Ready());CHECK(!editor.Locked());
    CHECK(editor.Draft());if(editor.Draft())CHECK_EQ(editor.Draft()->cells[0][0].note,75);CHECK(editor.Dirty());
}
TEST_CASE(StartupArbitraryStemUsesAuthoritativeFileDimensionsTempoAndOffData) {
    for(const auto* filename:{"ambient_Take","none.btdev"}) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->preview_cap=true;BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;
    serial.SetStartupPattern(filename);Pump(serial,0,120,&editor);CHECK(serial.StartupPatternReady());CHECK(editor.Ready());
    CHECK_EQ(wire->Loads(),1u);CHECK_EQ(editor.Baseline()->pattern.active_rows,16);CHECK_EQ(editor.Baseline()->pattern.active_channels,3);
    CHECK_EQ(editor.Baseline()->tempo_hundredths,12753u);CHECK_EQ(editor.Draft()->cells[1][2].note,NOTE_OFF);CHECK_EQ(editor.Draft()->note_off_timing[1][2],NoteOffTiming::EndOfPosition);
    CHECK(std::find(wire->commands.begin(),wire->commands.end(),std::string("BTSTORE1 LOAD 1 ")+filename)!=wire->commands.end());
}
}
TEST_CASE(InvalidStartupStemCannotSendLoad) {
    for(const auto& name:std::vector<std::string>{"../x","x/y","x\\y","x.bad","x y",std::string(25,'a')}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;
        serial.SetStartupPattern(name);Pump(serial,0,80,&editor);CHECK_EQ(wire->Loads(),0u);CHECK(!serial.StartupPatternReady());CHECK(editor.Locked());CHECK(!serial.Start());
    }
}

TEST_CASE(StartupWaitsForRealAudioStopAndStorageReadinessBeforeLoad) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->real_control=true;wire->preview_cap=true;
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern4");LocalPatternEditor editor;
    Pump(serial,0,12,&editor);CHECK(!wire->control.StorageReady());CHECK_EQ(wire->Loads(),0u);
    CHECK(std::find(wire->commands.begin(),wire->commands.end(),"BTTEST1 STOP")!=wire->commands.end());
    CHECK(!serial.Start());BlockedEdits(editor);NoPreviewWrites(*wire);
    // Reproduces route SELECTED before the first audio callback. The real control
    // guard rejects here even though the protocol has already reported IDLE.
    wire->Initialize();wire->cleanup_busy=wire->transfer_busy=true;
    for(unsigned i=12;i<22;++i){wire->now=i*20;wire->AudioService();serial.Tick(i*20);serial.SyncEditor(editor,i*20);}
    CHECK(wire->control.StorageReady());CHECK_EQ(wire->Loads(),0u);CHECK(wire->readiness.Active());
    wire->transfer_busy=false;wire->AudioService();Pump(serial,22,24,&editor);CHECK_EQ(wire->Loads(),0u);
    wire->cleanup_busy=false;
    for(unsigned i=24;i<110;++i){wire->now=i*20;wire->AudioService();serial.Tick(i*20);serial.SyncEditor(editor,i*20);}
    CHECK_EQ(wire->Loads(),1u);CHECK(serial.StartupPatternReady());CHECK(editor.Ready());CHECK(!editor.Locked());
    CHECK_EQ(editor.Baseline()->tempo_hundredths,15500u);CHECK(!serial.Playing());NoPreviewWrites(*wire);
    const auto stop=std::find(wire->commands.begin(),wire->commands.end(),"BTTEST1 STOP");
    const auto ready=std::find(wire->commands.begin(),wire->commands.end(),"BTSTORE1 READY 1");
    const auto load=std::find(wire->commands.begin(),wire->commands.end(),"BTSTORE1 LOAD 1 patern4");CHECK(stop<ready && ready<load);
}
TEST_CASE(StartupReadinessTimeoutAndUnsupportedFirmwareRemainLockedWithoutLoadRetry) {
    for(bool supported:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->readiness_cap=supported;wire->cleanup_busy=true;
        BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern4");LocalPatternEditor editor;
        for(unsigned i=0;i<400;++i){wire->now=i*20;wire->AudioService();serial.Tick(i*20);serial.SyncEditor(editor,i*20);}
        CHECK_EQ(wire->Loads(),0u);CHECK(!serial.StartupPatternReady());CHECK(!serial.Start());BlockedEdits(editor);NoPreviewWrites(*wire);
        CHECK(std::strstr(serial.Status(),"failed"));CHECK(serial.Stop());Pump(serial,400,410,&editor);CHECK(serial.Stopped());
    }
}

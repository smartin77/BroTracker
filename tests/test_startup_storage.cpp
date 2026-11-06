#include "test_framework.h"
#include "ui/bringup_serial.h"
#include "ui/bringup_controls.h"
#include "ui/host_logging.h"
#include "ui/host_config.h"
#include "bringup/pattern_file_store.h"
#include "bringup/pattern_control.h"
#include "bringup/variable_rate_player_adapter.h"
#include "bringup/storage_ready_wait.h"
#include "bringup/master_volume_protocol.h"
#include "bringup/pattern_preview.h"
#include <string>
#include <vector>
#include <algorithm>
using namespace BroTracker;
namespace {
struct StoragePeer:SerialTransport {
    std::string rx,tx;std::vector<std::string> commands;bool healthy=true,cap=true,respond=true;unsigned pattern=1,draft_pattern=3;std::string result="ok",state="IDLE";
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
    bool volume_cap=false,hold_volume=false;unsigned volume=73,volume_id=0;std::string volume_reply;
    std::string VolumeState() const {return "BTVOL1 STATE 1 "+std::to_string(volume_id)+" "+std::to_string(volume_id+1)+" "+std::to_string(volume)+" "+std::to_string((volume*65536u+50)/100)+"\n";}
    unsigned working_note=0;
    bool pattern_save_cap=false,simulate_working=false,have_saved=false;
    RealtimePattern simulated_working{},saved_pattern{};
    PatternPublication simulated_preview;
    std::uint32_t simulated_revision=0;
    std::string pattern_save_result="ok",pattern_save_reply;bool hold_pattern_save=false;
    bool list_cap=false,hold_list=false;std::string list_reply;
    std::vector<std::string> files={"patern1.btdev","patern2.btdev","patern3.btdev","patern4.btdev","other.btdev"};
    std::string boot_sd,boot_session;unsigned configured_rows=0;bool recovery_publication=false;
    bool preview_cap=false,hold_load=false,hold_baseline=false,hold_working=false,hold_stop=false,hold_save=false;std::string save_reply;
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
            rx+=std::string("BTPATTERN1 POS 2 ")+(state=="PLAYING"?"1":"0")+(configured_rows?" 0 0 0 0 "+std::to_string(configured_rows)+" 8\n":pattern==5?" 0 0 0 0 16 3\n":" 0 0 0 0 32 8\n");
            if(line=="BTTEST1 HELLO")rx+="BTAUDIO1 CAP 1\nBTMIDI1 CAP 1\nBTPATTERN1 SNAPCAP 2\n"+(cap?std::string("BTSTORE1 CAP 1\nBTSTORE1 SESSIONCAP 2\n"):std::string{});
            if(recovery_publication)rx+="BTPUBLISH1 CAP 1\nBTPUBLISH1 NEXT 1 1\n";
            if(line=="BTTEST1 HELLO" && !boot_session.empty())rx+="BTSTORE1 NEWCAP 1\nBTSTORE1 RESULT 1 session-boot "+boot_session+" 0\n";
            if(line=="BTTEST1 HELLO" && pattern_save_cap)rx+="BTSTORE1 PATTERNSAVECAP 1\n";
            if(line=="BTTEST1 HELLO" && list_cap)rx+="BTSTORE1 LISTCAP 1\n";
            if(line=="BTTEST1 HELLO" && !boot_sd.empty())rx+="BTSTORE1 SDCAP 1\nBTSTORE1 SD 1 "+boot_sd+"\n";
            if(volume_cap) {if(line=="BTTEST1 HELLO")rx+="BTVOL1 CAP 1\n";rx+=VolumeState();}
            if(line=="BTTEST1 HELLO" && readiness_cap)rx+="BTSTORE1 READYCAP 1\n";
            if(preview_cap) {
                if(line=="BTTEST1 HELLO")rx+="BTPREVIEW1 CAP 1\nBTPREVIEW1 DELTACAP 1\n";
                rx+="BTPREVIEW1 NEXT 1 "+std::to_string(simulate_working?simulated_preview.NextIdentity():1)+"\nBTPREVIEW1 STATE 1 1 "+std::to_string(simulated_revision)+" 0\n";
            }
        }else if(line=="BTAUDIO1 SET 1 default")rx+="BTAUDIO1 SELECTED 1 bridge\n";
        else if(line=="BTMIDI1 SET 1 12")rx+="BTMIDI1 SELECTED 1 12\n";
        else if(line=="BTVOL1 GET 1")rx+=VolumeState();
        else if(line.rfind("BTVOL1 SET 1 ",0)==0) {
            MasterVolumeMessage m;CHECK(ParseMasterVolume(line.data(),line.size(),m));volume=m.percent;volume_id=m.id;
            const auto reply="BTVOL1 SUBMITTED 1 "+std::to_string(m.id)+"\nBTVOL1 APPLIED 1 "+std::to_string(m.id)+"\n"+VolumeState();
            if(hold_volume)volume_reply+=reply;else rx+=reply;
        }
        else if(line.rfind("BTSTORE1 LIST ",0)==0) {
            std::uint32_t fields[3]{};CHECK(SnapshotFields(line.data(),line.size(),"BTSTORE1 LIST ",fields,3));
            const auto first=fields[2]/32;const auto end=std::min<std::size_t>(files.size(),first+kPatternListPageSize);
            std::string reply;for(std::size_t i=first;i<end;++i)reply+="BTSTORE1 FILE 1 "+std::to_string(fields[1])+" "+files[i]+"\n";
            reply+="BTSTORE1 LISTEND 1 "+std::to_string(fields[1])+" "+std::to_string(end*32)+" "+(end==files.size() && end-first<kPatternListPageSize?"1":"0")+"\n";
            if(hold_list)list_reply=reply;else rx+=reply;
        }
        else if(line.rfind("BTPREVIEW1 D",0)==0 && simulate_working) {
            SparsePreviewMessage message{};CHECK(ParseSparsePreview(line.data(),line.size(),message));
            const auto status=simulated_preview.AcceptSparse(message,now,simulated_working,simulated_revision);
            if(message.kind==PublicationKind::Begin && status==PublicationStatus::Receiving)rx+="BTPREVIEW1 RECEIVING 1 "+std::to_string(message.fields[0])+"\n";
            if(status==PublicationStatus::Ready) {
                CHECK(simulated_preview.Freeze());simulated_working=*simulated_preview.Frozen();simulated_revision=message.fields[0];simulated_preview.Finish();
                const auto id=std::to_string(simulated_revision);
                rx+="BTPREVIEW1 SUBMITTED 1 "+id+"\nBTPREVIEW1 ACCEPTED 1 "+id+"\nBTPREVIEW1 APPLIED 1 "+id+"\nBTPREVIEW1 STATE 1 1 "+id+" 0\n";
            }
        }
        else if(line.rfind("BTSTORE1 PATTERNSAVE 1 ",0)==0) {
            if(pattern_save_result=="ok") {saved_pattern=simulated_working;have_saved=true;}
            const auto reply="BTSTORE1 RESULT 1 pattern-save-"+line.substr(sizeof("BTSTORE1 PATTERNSAVE 1 ")-1)+" "+pattern_save_result+" 0\n";
            if(hold_pattern_save)pattern_save_reply=reply;else rx+=reply;
        }
        else if(line=="BTSTORE1 SAVE 1" || line=="BTSTORE1 DRAFTSAVE 1") {
            const auto reply=std::string("BTSTORE1 RESULT 1 ")+(line=="BTSTORE1 SAVE 1"?"save":"draft-save")+" "+result+" 0\n";
            if(hold_save)save_reply+=reply;else rx+=reply;
        }
        else if(line=="BTSTORE1 DRAFTLOAD 1") {
            if(result=="ok") {pattern=draft_pattern;rx+="BTSTORE1 SUBMITTED 1 draft-load 1\n";}
            rx+="BTSTORE1 RESULT 1 draft-load "+result+" 1\n";
        }
        else if(line=="BTSTORE1 READY 1") {CHECK(readiness.Begin(now));AudioService();}
        else if(line.rfind("BTSTORE1 NEW 1 ",0)==0 && respond) {
            std::uint32_t rows=0;CHECK(SnapshotFields(line.data(),line.size(),"BTSTORE1 NEW 1 ",&rows,1));
            configured_rows=rows;pattern=0;
            rx+="BTSTORE1 SUBMITTED 1 new 1\nBTSTORE1 RESULT 1 new ok 1\n";
        }
        else if(line.rfind("BTSTORE1 LOAD 1 ",0)==0 && respond) {
            if(real_control)CHECK(control.StorageReady() && !cleanup_busy && !transfer_busy);
            const auto name=line.substr(16);const auto replacement=name=="none"?0:(name=="patern1" || name=="patern1.btdev")?1:(name=="patern2" || name=="patern2.btdev")?2:(name=="patern3" || name=="patern3.btdev")?3:(name=="patern4" || name=="patern4.btdev")?4:5;std::string reply;
            if(result=="ok") {pattern=replacement;working_note=0;if(simulate_working) {MakeStoredDevelopmentPattern(simulated_working,replacement);if(have_saved)simulated_working=saved_pattern;simulated_revision=0;}reply+="BTSTORE1 SUBMITTED 1 load-"+name+" 1\n";}
            reply+="BTSTORE1 RESULT 1 load-"+name+" "+result+" 1\n";
            if(hold_load)load_reply+=reply;else rx+=reply;
        }else if(line.rfind("BTPATTERN1 GET 2 ",0)==0) {
            RealtimePattern pattern;MakeStoredDevelopmentPattern(pattern,this->pattern==5?0:this->pattern);
            if(this->pattern==5){pattern.active_rows=16;pattern.active_channels=3;pattern.cells[1][2]={NOTE_OFF,255};pattern.note_off_timing[1][2]=NoteOffTiming::EndOfPosition;}
            if(configured_rows)pattern.active_rows=configured_rows;
            if(simulate_working)pattern=simulated_working;
            PatternSnapshotSender sender(pattern,this->pattern==5?12753:DevelopmentPatternTempo(this->pattern));
            (void)sender.Begin(static_cast<unsigned>(std::stoul(line.substr(17))),0,2);
            std::string reply;while(sender.HasData()){std::size_t count;const auto* bytes=sender.Data(count);reply.append(bytes,count);sender.Consume(count);}
            if(hold_baseline)baseline_reply+=reply;else rx+=reply;
        }else if(line.rfind("BTPREVIEW1 GET 1 ",0)==0) {
            const auto id=static_cast<unsigned>(std::stoul(line.substr(17)));
            RealtimePattern pattern;MakeStoredDevelopmentPattern(pattern,this->pattern==5?0:this->pattern);
            if(this->pattern==5){pattern.active_rows=16;pattern.active_channels=3;pattern.cells[1][2]={NOTE_OFF,255};pattern.note_off_timing[1][2]=NoteOffTiming::EndOfPosition;}
            if(working_note)pattern.cells[0][0].note=static_cast<Note>(working_note);
            if(configured_rows)pattern.active_rows=configured_rows;
            if(simulate_working)pattern=simulated_working;
            PatternSnapshotSender sender(pattern,this->pattern==5?12753:DevelopmentPatternTempo(this->pattern));
            CHECK(sender.SetWorkingNamespace(true));CHECK(sender.Begin(id,0,2));
            std::string reply="BTPREVIEW1 READ 1 "+std::to_string(id)+" 0 1 "+std::to_string(simulated_revision)+"\n";
            while(sender.HasData()){std::size_t count;const auto* bytes=sender.Data(count);reply.append(bytes,count);sender.Consume(count);}
            if(hold_working)working_reply+=reply;else rx+=reply;
        }else if(line=="BTTEST1 STOP") {
            if(real_control)CHECK_EQ(control.Submit(PatternRequest::Stop),PatternRequestStatus::Accepted);
            else if(!hold_stop)rx+="BTTEST1 STOPPED\n";
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
TEST_CASE(InitialSessionReadbackRetriesTransientTimeoutWithoutLoadingOrStarting) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->hold_baseline=true;
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern4");LocalPatternEditor editor;
    Pump(serial,0,270,&editor);CHECK(!editor.Ready());CHECK(!serial.StartupPatternReady());
    wire->hold_baseline=false;Pump(serial,270,390,&editor);
    CHECK(editor.Ready());CHECK(serial.StartupPatternReady());CHECK(!serial.PlaybackView(7800)->pattern_failed);
    CHECK_EQ(wire->Loads(),0u);
    CHECK_EQ(std::count_if(wire->commands.begin(),wire->commands.end(),[](const auto& s){return s.rfind("BTPATTERN1 GET 2 ",0)==0;}),2);
    CHECK(std::find(wire->commands.begin(),wire->commands.end(),"BTTEST1 START")==wire->commands.end());
}

TEST_CASE(InitialSessionReadbackFailureIsBoundedAndStopRemainsAvailable) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->hold_baseline=true;
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern4");LocalPatternEditor editor;
    Pump(serial,0,1500,&editor);
    CHECK(!editor.Ready());CHECK(!serial.StartupPatternReady());CHECK(serial.PlaybackView(30000)->pattern_failed);
    CHECK_EQ(std::count_if(wire->commands.begin(),wire->commands.end(),[](const auto& s){return s.rfind("BTPATTERN1 GET 2 ",0)==0;}),3);
    CHECK_EQ(wire->Loads(),0u);CHECK(serial.Stop());Pump(serial,1500,1510,&editor);CHECK(serial.Stopped());
    CHECK(std::find(wire->commands.begin(),wire->commands.end(),"BTTEST1 START")==wire->commands.end());
}

TEST_CASE(RejectedInitialReadbackRetriesWithFreshIdentity) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->hold_baseline=true;
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern4");LocalPatternEditor editor;
    Pump(serial,0,80,&editor);
    const auto first=std::find_if(wire->commands.begin(),wire->commands.end(),[](const auto& s){return s.rfind("BTPATTERN1 GET 2 ",0)==0;});
    CHECK(first!=wire->commands.end());const auto request=*first;
    wire->rx+="BTPATTERN1 SNAPERR 2 "+request.substr(std::string("BTPATTERN1 GET 2 ").size())+"\n";
    wire->hold_baseline=false;Pump(serial,80,180,&editor);
    CHECK(editor.Ready());CHECK(serial.StartupPatternReady());CHECK_EQ(wire->Loads(),0u);
    std::vector<std::string> reads;for(const auto& s:wire->commands)if(s.rfind("BTPATTERN1 GET 2 ",0)==0)reads.push_back(s);
    CHECK_EQ(reads.size(),2u);CHECK(reads[0]!=reads[1]);
}

TEST_CASE(ReconnectReadbackRetryPreservesDraftAndCursorWithoutStarting) {
    for(bool previously_loaded:{false,true}) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern4");LocalPatternEditor editor;
    Pump(serial,0,90,&editor);CHECK(editor.Ready());
    if(previously_loaded) {
        CHECK(serial.RequestFile(BringUpSerial::FileAction::Load,1801));Pump(serial,91,180,&editor);
        CHECK(serial.StartupPatternReady());CHECK(editor.Ready());
    }
    editor.RestoreCursor({4,3,EditField::Instrument,false});
    CHECK(editor.Apply(EditorAction::Increment));const auto retained=*editor.Draft();
    wire->healthy=false;serial.Tick(3601);serial.SyncEditor(editor,3601);
    wire->hold_baseline=true;Pump(serial,181,440,&editor);
    wire->hold_baseline=false;Pump(serial,440,610,&editor);
    CHECK(serial.PlaybackView(12200)->device_pattern.has_value());
    CHECK(editor.Ready());CHECK(SamePattern(retained,*editor.Draft()));
    CHECK_EQ(editor.Cursor().row,4u);CHECK_EQ(editor.Cursor().channel,3u);CHECK_EQ(editor.Cursor().field,EditField::Instrument);
    CHECK_EQ(wire->Loads(),0u);
    CHECK_EQ(std::count(wire->commands.begin(),wire->commands.end(),"BTSTORE1 DRAFTLOAD 1"),previously_loaded?1:0);
    CHECK(std::find(wire->commands.begin(),wire->commands.end(),"BTTEST1 START")==wire->commands.end());
    }
}
TEST_CASE(HostStartupSdLoadSetupReadbackAndNoAutomaticStart) {
    for(auto selection:{"none","patern1","patern2","patern3","patern4","patern4.btdev"}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern(selection);serial.RequestFile(BringUpSerial::FileAction::Test,0);
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
        if(scenario==0)wire->cap=false;if(scenario==1)wire->readiness_cap=false;if(scenario==2)wire->result="missing";if(scenario==3)wire->result="invalid";if(scenario==4)wire->respond=false;
        BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern3");serial.RequestFile(BringUpSerial::FileAction::Test,0);Pump(serial,0,900);
        CHECK_EQ(serial.StartupPatternReady(),scenario==2 || scenario==3);CHECK(wire->Loads()<=1);
        CHECK(std::strstr(serial.Status(),"failed") || std::strstr(serial.Status(),"uncertain"));
        for(const auto& c:wire->commands){CHECK(c!="BTTEST1 START");CHECK(c.rfind("BTSTORE1 SEED",0)!=0);}
    }
}
TEST_CASE(StartupSuccessIsNotReportedForDifferentReconnectBaseline) {
    ConfigureHostLogging(HostLogMode::Debug);
    for(bool preview:{false,true}) {
        auto* log=std::tmpfile();CHECK(log);if(!log)return;
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->preview_cap=preview;
        BringUpSerial serial(log,std::move(peer));serial.SetStartupPattern("patern4");serial.RequestFile(BringUpSerial::FileAction::Test,0);LocalPatternEditor editor;
        Pump(serial,0,80,&editor);CHECK(serial.StartupPatternReady());CHECK_EQ(wire->Loads(),1u);
        const auto startup=serial.PlaybackView(1600);CHECK(startup);CHECK(startup->device_pattern);
        CHECK_EQ(startup->device_pattern->pattern.cells[0][4].note,36);
        auto read_log=[&] {std::rewind(log);std::string text;char line[2048];while(std::fgets(line,sizeof(line),log))text+=line;std::fseek(log,0,SEEK_END);return text;};
        const std::string success="SD startup pattern loaded and baseline readback verified: patern4";
        const auto before=read_log();CHECK(before.find(success)!=std::string::npos);
        wire->healthy=false;serial.Tick(1700);serial.SyncEditor(editor,1700);
        wire->pattern=1; // Boot theme: only CH1/CH2 populated, unlike patern4.
        Pump(serial,140,220,&editor);
        CHECK(serial.Connected());CHECK(serial.StartupPatternReady());CHECK(editor.Ready());CHECK(editor.Locked());CHECK(serial.ReconnectRecoveryPending());
        CHECK_EQ(wire->Loads(),1u);CHECK(!serial.Playing());CHECK(!serial.Start());
        CHECK(std::find(wire->commands.begin(),wire->commands.end(),"BTTEST1 START")==wire->commands.end());
        const auto recovered=serial.PlaybackView(4400);CHECK(recovered);CHECK(recovered->device_pattern);
        RealtimePattern expected;MakeStoredDevelopmentPattern(expected,4);
        CHECK(SamePattern(recovered->device_pattern->pattern,expected));
        const auto after=read_log();const auto first=after.find(success);CHECK(first!=std::string::npos);
        CHECK(after.find(success,first+success.size())==std::string::npos);
        CHECK(after.find("continuity not established")!=std::string::npos);
        CHECK(serial.RestoreReconnectDraft(editor,4401));
        MakeStoredDevelopmentPattern(expected,4);
        CHECK(SamePattern(*editor.Draft(),expected));CHECK(serial.ReconnectRecoveryPending());CHECK(editor.Locked());
        std::fclose(log);
    }
    ConfigureHostLogging(HostLogMode::Basic);
}
TEST_CASE(HostStartupNoneEstablishesClean125Baseline) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("none");serial.RequestFile(BringUpSerial::FileAction::Test,0);LocalPatternEditor editor;Pump(serial,0,80,&editor);CHECK(serial.StartupPatternReady());CHECK_EQ(wire->Loads(),1u);CHECK_EQ(editor.Baseline()->tempo_hundredths,12500u);for(unsigned r=0;r<32;++r)for(unsigned c=0;c<8;++c){CHECK_EQ(editor.Draft()->cells[r][c].note,NOTE_EMPTY);CHECK_EQ(editor.Draft()->cells[r][c].instrument,255);CHECK_EQ(editor.Draft()->note_off_timing[r][c],NoteOffTiming::Arrival);}
}
TEST_CASE(StartupDisconnectBeforeBaselineCannotReportSuccessOnReconnect) {
    ConfigureHostLogging(HostLogMode::Debug);
    auto* log=std::tmpfile();CHECK(log);if(!log)return;
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->hold_baseline=true;
    BringUpSerial serial(log,std::move(peer));serial.SetStartupPattern("patern4");serial.RequestFile(BringUpSerial::FileAction::Test,0);LocalPatternEditor editor;
    Pump(serial,0,80,&editor);CHECK_EQ(wire->Loads(),1u);CHECK(!wire->baseline_reply.empty());
    CHECK(!serial.StartupPatternReady());CHECK(editor.Locked());CHECK(!serial.Start());
    wire->healthy=false;serial.Tick(1700);serial.SyncEditor(editor,1700);
    wire->baseline_reply.clear();wire->hold_baseline=false;wire->pattern=1;
    Pump(serial,140,220,&editor);
    CHECK(serial.Connected());CHECK_EQ(wire->Loads(),1u);CHECK(!serial.StartupPatternReady());
    CHECK(editor.Locked());CHECK(!serial.Start());CHECK(std::strstr(serial.Status(),"failed"));
    const auto recovered=serial.PlaybackView(4400);CHECK(recovered);CHECK(recovered->device_pattern);
    std::rewind(log);std::string text;char line[2048];while(std::fgets(line,sizeof(line),log))text+=line;
    CHECK(text.find("SD startup pattern loaded and baseline readback verified:")==std::string::npos);
    std::fclose(log);ConfigureHostLogging(HostLogMode::Basic);
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
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern3");serial.RequestFile(BringUpSerial::FileAction::Test,0);
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
        BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern3");serial.RequestFile(BringUpSerial::FileAction::Test,0);LocalPatternEditor editor;SeedEditor(editor);
        Pump(serial,0,900,&editor);CHECK_EQ(editor.Locked(),timeout);CHECK(editor.Ready());CHECK_EQ(serial.StartupPatternReady(),!timeout);
        CHECK(std::strstr(serial.Status(),"failed") || std::strstr(serial.Status(),"uncertain"));CHECK(serial.Stop());Pump(serial,900,910,&editor);
        CHECK(serial.Stopped());CHECK_EQ(editor.Locked(),timeout);CHECK(std::find(wire->commands.begin(),wire->commands.end(),"BTTEST1 STOP")!=wire->commands.end());
    }
}
TEST_CASE(CleanStartupPreservesNormalEditorPreview) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->preview_cap=true;BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("none");serial.RequestFile(BringUpSerial::FileAction::Test,0);LocalPatternEditor editor;
    Pump(serial,0,80,&editor);CHECK(editor.Ready());CHECK(!editor.Locked());CHECK_EQ(wire->Loads(),1u);
    CHECK(editor.Apply(EditorAction::SetValue,75));Pump(serial,80,110,&editor);
    CHECK(std::any_of(wire->commands.begin(),wire->commands.end(),[](const auto& c){return c.rfind("BTPREVIEW1 DBEGIN",0)==0;}));
}
TEST_CASE(StartupReconnectRetainsUnsentEditsAndRequiresExplicitResolution) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->preview_cap=true;
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern3");serial.RequestFile(BringUpSerial::FileAction::Test,0);LocalPatternEditor editor;
    Pump(serial,0,80,&editor);CHECK(serial.StartupPatternReady());CHECK(!editor.Locked());CHECK_EQ(wire->Loads(),1u);
    CHECK(editor.Apply(EditorAction::SetValue,75));serial.SyncEditor(editor,1601);
    wire->healthy=false;serial.Tick(1602);serial.SyncEditor(editor,1602);
    Pump(serial,140,175,&editor);
    CHECK_EQ(wire->Loads(),1u);CHECK(serial.StartupPatternReady());CHECK(editor.Ready());CHECK(editor.Locked());
    CHECK(serial.ReconnectRecoveryPending());CHECK(!serial.Start());
    CHECK(editor.Draft());if(editor.Draft())CHECK_EQ(editor.Draft()->cells[0][0].note,75);CHECK(editor.Dirty());
}
TEST_CASE(StartupArbitraryStemUsesAuthoritativeFileDimensionsTempoAndOffData) {
    for(const auto* filename:{"ambient_Take","none.btdev"}) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->preview_cap=true;BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;
    serial.SetStartupPattern(filename);serial.RequestFile(BringUpSerial::FileAction::Test,0);Pump(serial,0,120,&editor);CHECK(serial.StartupPatternReady());CHECK(editor.Ready());
    CHECK_EQ(wire->Loads(),1u);CHECK_EQ(editor.Baseline()->pattern.active_rows,16);CHECK_EQ(editor.Baseline()->pattern.active_channels,3);
    CHECK_EQ(editor.Baseline()->tempo_hundredths,12753u);CHECK_EQ(editor.Draft()->cells[1][2].note,NOTE_OFF);CHECK_EQ(editor.Draft()->note_off_timing[1][2],NoteOffTiming::EndOfPosition);
    CHECK(std::find(wire->commands.begin(),wire->commands.end(),std::string("BTSTORE1 LOAD 1 ")+filename)!=wire->commands.end());
}
}
TEST_CASE(InvalidStartupStemCannotSendLoad) {
    for(const auto& name:std::vector<std::string>{"../x","x/y","x\\y","x.bad","x y",std::string(25,'a')}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;
        serial.SetStartupPattern(name);serial.RequestFile(BringUpSerial::FileAction::Test,0);Pump(serial,0,80,&editor);CHECK_EQ(wire->Loads(),0u);CHECK(serial.StartupPatternReady());CHECK(!editor.Locked());
    }
}

TEST_CASE(StartupWaitsForRealAudioStopAndStorageReadinessBeforeLoad) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->real_control=true;wire->preview_cap=true;
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern4");serial.RequestFile(BringUpSerial::FileAction::Test,0);LocalPatternEditor editor;
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
        BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern4");serial.RequestFile(BringUpSerial::FileAction::Test,0);LocalPatternEditor editor;
        for(unsigned i=0;i<400;++i){wire->now=i*20;wire->AudioService();serial.Tick(i*20);serial.SyncEditor(editor,i*20);}
        CHECK_EQ(wire->Loads(),0u);CHECK(!serial.StartupPatternReady());CHECK(!serial.Start());BlockedEdits(editor);NoPreviewWrites(*wire);
        CHECK(std::strstr(serial.Status(),"failed"));CHECK(serial.Stop());Pump(serial,400,410,&editor);CHECK(serial.Stopped());
    }
}

TEST_CASE(HostVolumeUsesDeviceBaselineCoalescesAndNeverReplaysAcrossReconnect) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->volume_cap=true;wire->preview_cap=true;
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("none");serial.RequestFile(BringUpSerial::FileAction::Test,0);LocalPatternEditor editor;
    Pump(serial,0,80,&editor);CHECK(serial.StartupPatternReady());CHECK(serial.VolumeAvailable());CHECK(!std::strcmp(serial.VolumeStatus(),"Vol 73%"));
    for(const auto& command:wire->commands)CHECK(command.rfind("BTVOL1 SET",0)!=0);
    wire->hold_volume=true;CHECK(serial.StepVolume(-1));CHECK(!std::strcmp(serial.VolumeStatus(),"Vol 73%"));Pump(serial,80,82,&editor);CHECK(!std::strcmp(serial.VolumeStatus(),"Vol 73%"));CHECK_EQ(wire->volume,68u);CHECK(!wire->volume_reply.empty());CHECK(serial.StepVolume(-1));
    wire->rx+=wire->volume_reply;wire->volume_reply.clear();wire->hold_volume=false;Pump(serial,82,95,&editor);CHECK_EQ(wire->volume,63u);CHECK(!std::strcmp(serial.VolumeStatus(),"Vol 63%"));
    const auto count=wire->commands.size();wire->healthy=false;serial.Tick(1901);Pump(serial,140,180,&editor);
    CHECK(serial.VolumeAvailable());CHECK(!std::strcmp(serial.VolumeStatus(),"Vol 63%"));
    for(unsigned i=count;i<wire->commands.size();++i)CHECK(wire->commands[i].rfind("BTVOL1 SET",0)!=0);
    CHECK_EQ(wire->Loads(),1u);CHECK(!serial.Playing());
}
TEST_CASE(HostUnsupportedVolumeDoesNotBlockOtherwiseReadyTransport) {
    auto peer=std::make_unique<StoragePeer>();BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("none");serial.RequestFile(BringUpSerial::FileAction::Test,0);Pump(serial);
    CHECK(serial.StartupPatternReady());CHECK(!serial.VolumeAvailable());CHECK(!serial.StepVolume(-1));CHECK(std::strstr(serial.VolumeStatus(),"unsupported"));CHECK(serial.Start());
}

TEST_CASE(ReconnectPartialReadbackAndRepeatedDisconnectNeverDiscardRetainedWork) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->preview_cap=true;
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern4");serial.RequestFile(BringUpSerial::FileAction::Test,0);LocalPatternEditor editor;
    Pump(serial,0,80,&editor);CHECK(editor.Apply(EditorAction::SetValue,75));serial.SyncEditor(editor,1601);
    const auto draft=*editor.Draft();const auto baseline=*editor.Baseline();
    wire->healthy=false;serial.Tick(1700);serial.SyncEditor(editor,1700);
    wire->pattern=1;wire->hold_working=true;Pump(serial,140,175,&editor);
    CHECK(serial.ReconnectRecoveryPending());CHECK(editor.Locked());CHECK(!serial.Start());
    CHECK(!serial.Publish(editor,3501));CHECK(editor.Locked());
    CHECK(SamePattern(*editor.Draft(),draft));CHECK(SamePattern(editor.Baseline()->pattern,baseline.pattern));
    wire->healthy=false;serial.Tick(3502);serial.SyncEditor(editor,3502);
    wire->working_reply.clear();wire->hold_working=false;Pump(serial,230,280,&editor);
    CHECK(serial.ReconnectRecoveryPending());CHECK(SamePattern(*editor.Draft(),draft));
    CHECK_EQ(wire->Loads(),1u);
    for(const auto& cmd:wire->commands) {
        CHECK(cmd!="BTTEST1 START");CHECK(cmd.rfind("BTPUBLISH1 BEGIN",0)!=0);
        CHECK(cmd.rfind("BTPREVIEW1 BEGIN",0)!=0);CHECK(cmd.rfind("BTPREVIEW1 DBEGIN",0)!=0);
        CHECK(cmd.rfind("BTSTORE1 SAVE",0)!=0);CHECK(cmd.rfind("BTSTORE1 RESTORE",0)!=0);
    }
}

TEST_CASE(NormalLaunchAndRunningReconnectNeverLoadStopSaveOrStart) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->preview_cap=true;wire->state="PLAYING";
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern4");LocalPatternEditor editor;
    Pump(serial,0,90,&editor);CHECK(serial.Playing());CHECK(serial.StartupPatternReady());CHECK(editor.Ready());
    CHECK_EQ(wire->Loads(),0u);const auto before=*editor.Draft();
    wire->healthy=false;serial.Tick(1801);serial.SyncEditor(editor,1801);Pump(serial,145,230,&editor);
    CHECK(serial.Playing());CHECK(!serial.ReconnectRecoveryPending());CHECK(SamePattern(before,*editor.Draft()));
    for(const auto& c:wire->commands) {
        CHECK(c!="BTTEST1 STOP");CHECK(c!="BTTEST1 START");CHECK(c.rfind("BTSTORE1 LOAD",0)!=0);CHECK(c!="BTSTORE1 SAVE 1");
    }
}
TEST_CASE(TemporarySaveWaitsForAckAndDoesNotPublishAndDraftLoadUsesValidatedBarrier) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->hold_save=true;
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern4");LocalPatternEditor editor;
    Pump(serial,0,90,&editor);const auto baseline=*editor.Baseline();
    CHECK(serial.RequestFile(BringUpSerial::FileAction::Save,1801));Pump(serial,91,130,&editor);
    CHECK(std::strstr(serial.PublicationStatus(),"queued"));CHECK(!wire->save_reply.empty());CHECK(editor.Locked());
    CHECK(SamePattern(baseline.pattern,editor.Baseline()->pattern));
    wire->rx+=wire->save_reply;wire->save_reply.clear();Pump(serial,130,150,&editor);
    CHECK(std::strstr(serial.PublicationStatus(),"Tune Save: ok"));CHECK(!editor.Locked());
    CHECK(serial.RequestFile(BringUpSerial::FileAction::Load,3001));Pump(serial,151,220,&editor);
    CHECK(serial.StartupPatternReady());CHECK_EQ(editor.Draft()->cells[0][4].note,36);CHECK_EQ(wire->Loads(),0u);
    for(const auto& c:wire->commands)CHECK(c.rfind("BTPUBLISH1 BEGIN",0)!=0);
}
TEST_CASE(FailedDraftLoadPreservesDraftAndBaselineAndExitNeverStopsPlaying) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern4");LocalPatternEditor editor;
    Pump(serial,0,90,&editor);CHECK(editor.Apply(EditorAction::SetValue,75));serial.SyncEditor(editor,1801);
    const auto draft=*editor.Draft(),baseline=editor.Baseline()->pattern;
    wire->result="invalid";CHECK(serial.RequestFile(BringUpSerial::FileAction::Load,1802));Pump(serial,91,170,&editor);
    CHECK(SamePattern(draft,*editor.Draft()));CHECK(SamePattern(baseline,editor.Baseline()->pattern));CHECK(std::strstr(serial.PublicationStatus(),"failed"));
    wire->result="deferred-playing";wire->state="PLAYING";wire->rx+="BTTEST1 STATE PLAYING\n";Pump(serial,170,180,&editor);
    const auto count=wire->commands.size();serial.BeginExit(3601);Pump(serial,181,230,&editor);CHECK(serial.ExitReady());
    for(unsigned i=count;i<wire->commands.size();++i)CHECK(wire->commands[i]!="BTTEST1 STOP");
}

TEST_CASE(DeferredTuneCompletionCannotAcknowledgeAnotherPendingFileAction) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern4");LocalPatternEditor editor;
    Pump(serial,0,90,&editor);wire->result="deferred-playing";
    CHECK(serial.RequestFile(BringUpSerial::FileAction::Save,1801));Pump(serial,91,120,&editor);
    CHECK(std::strstr(serial.PublicationStatus(),"not yet saved"));
    CHECK(!serial.RequestFile(BringUpSerial::FileAction::Save,2401));
    wire->result="ok";wire->hold_save=true;
    serial.BeginExit(2402);Pump(serial,121,150,&editor);CHECK(!serial.ExitReady());CHECK(!wire->save_reply.empty());
    wire->rx+="BTSTORE1 RESULT 1 draft-save ok 0\n";Pump(serial,150,165,&editor);CHECK(!serial.ExitReady());
    wire->rx+=wire->save_reply;wire->save_reply.clear();Pump(serial,165,190,&editor);CHECK(serial.ExitReady());
}

TEST_CASE(DraftFileLoadRetainsCursorAndClampsToLoadedDimensions) {
    for(bool small:{false,true}) for(bool header:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->preview_cap=true;
        BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern4");LocalPatternEditor editor;
        Pump(serial,0,90,&editor);CHECK(editor.Ready());
        editor.RestoreCursor({29,7,EditField::Instrument,header});wire->draft_pattern=small?5:3;
        CHECK(serial.RequestFile(BringUpSerial::FileAction::Load,1801));Pump(serial,91,200,&editor);
        CHECK(serial.StartupPatternReady());CHECK(editor.Ready());CHECK(!editor.Locked());
        CHECK_EQ(editor.Cursor().row,header?0u:small?15u:29u);
        CHECK_EQ(editor.Cursor().channel,small?2u:7u);CHECK_EQ(editor.Cursor().field,EditField::Instrument);CHECK_EQ(editor.Cursor().header,header);
        CHECK(std::find(wire->commands.begin(),wire->commands.end(),"BTSTORE1 DRAFTLOAD 1")!=wire->commands.end());
        if(small) continue; // The dimension-only mock has no live-tempo protocol.
        const auto cursor=editor.Cursor();wire->healthy=false;serial.Tick(4001);serial.SyncEditor(editor,4001);Pump(serial,250,350,&editor);
        CHECK(!serial.ReconnectRecoveryPending());CHECK_EQ(editor.Cursor().row,cursor.row);CHECK_EQ(editor.Cursor().channel,cursor.channel);
        CHECK_EQ(editor.Cursor().field,cursor.field);CHECK_EQ(editor.Cursor().header,cursor.header);
    }
}
TEST_CASE(FailedDraftFileLoadDoesNotMoveKnownCursor) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();BringUpSerial serial(nullptr,std::move(peer));
    serial.SetStartupPattern("patern4");LocalPatternEditor editor;Pump(serial,0,90,&editor);
    editor.RestoreCursor({12,5,EditField::Instrument,false});wire->result="invalid";
    CHECK(serial.RequestFile(BringUpSerial::FileAction::Load,1801));Pump(serial,91,200,&editor);
    CHECK_EQ(editor.Cursor().row,12u);CHECK_EQ(editor.Cursor().channel,5u);CHECK_EQ(editor.Cursor().field,EditField::Instrument);
}

TEST_CASE(DraftLoadThenConfiguredTestLoadResetsCursorAndAllowsStart) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->preview_cap=true;
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern4");LocalPatternEditor editor;
    Pump(serial,0,90,&editor);editor.RestoreCursor({12,5,EditField::Instrument,false});
    CHECK(serial.RequestFile(BringUpSerial::FileAction::Load,1801));Pump(serial,91,190,&editor);
    CHECK(serial.StartupPatternReady());CHECK(!editor.Locked());CHECK_EQ(editor.Cursor().row,12u);
    CHECK(serial.RequestFile(BringUpSerial::FileAction::Test,3801));Pump(serial,191,290,&editor);
    CHECK(serial.StartupPatternReady());CHECK(!editor.Locked());CHECK_EQ(wire->Loads(),1u);
    CHECK_EQ(editor.Cursor().row,0u);CHECK_EQ(editor.Cursor().channel,0u);CHECK_EQ(editor.Cursor().field,EditField::Note);
    CHECK(serial.Start());Pump(serial,291,295,&editor);
    CHECK(std::find(wire->commands.begin(),wire->commands.end(),"BTTEST1 START")!=wire->commands.end());
}

TEST_CASE(ReadinessRejectionAfterDraftLoadRetainsWorkAndReleasesControls) {
    for(const auto action:{BringUpSerial::FileAction::Load,BringUpSerial::FileAction::Test}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->preview_cap=true;
        BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern4");LocalPatternEditor editor;
        Pump(serial,0,90,&editor);
        CHECK(serial.RequestFile(BringUpSerial::FileAction::Load,1801));Pump(serial,91,190,&editor);
        editor.RestoreCursor({12,5,EditField::Instrument,false});const auto draft=*editor.Draft();const auto baseline=*editor.Baseline();
        wire->cleanup_busy=true;CHECK(serial.RequestFile(action,3801));
        for(unsigned i=191;i<500;++i){wire->now=i*20;wire->AudioService();serial.Tick(i*20);serial.SyncEditor(editor,i*20);}
        CHECK(serial.StartupPatternReady());CHECK(editor.Ready());CHECK(!editor.Locked());
        CHECK(SamePattern(draft,*editor.Draft()));CHECK(SamePattern(baseline.pattern,editor.Baseline()->pattern));
        CHECK_EQ(editor.Cursor().row,12u);CHECK_EQ(editor.Cursor().channel,5u);CHECK_EQ(editor.Cursor().field,EditField::Instrument);
        CHECK(std::strstr(serial.PublicationStatus(),"Load not attempted"));CHECK_EQ(wire->Loads(),0u);
        CHECK(editor.Apply(EditorAction::Increment));CHECK(editor.Apply(EditorAction::Restore));
        CHECK(serial.Stop());Pump(serial,500,510,&editor);CHECK(serial.Stopped());CHECK(serial.Start());Pump(serial,510,515,&editor);
        wire->rx+="BTTEST1 STARTED\n";Pump(serial,515,520,&editor);
        CHECK(serial.Stop());Pump(serial,520,530,&editor);
        // No automatic load retry; a fresh user request may retry after cleanup finishes.
        wire->cleanup_busy=false;CHECK(serial.RequestFile(BringUpSerial::FileAction::Test,10601));Pump(serial,531,620,&editor);
        CHECK(serial.StartupPatternReady());CHECK(!editor.Locked());CHECK_EQ(wire->Loads(),1u);
    }
}

TEST_CASE(ExplicitSdInitializationStatusIsSeparateFromSessionErrorsAndClearedOnDisconnect) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("none");LocalPatternEditor editor;
    CHECK(serial.UserActionsBlocked());Pump(serial,0,90,&editor);CHECK(serial.Connected());
    CHECK_EQ(serial.SdStatus(),BringUpSerial::SdAvailability::Unknown);
    wire->rx+="BTSTORE1 RESULT 1 session-boot invalid 0\nBTSTORE1 RESULT 1 draft-load sd-unavailable 0\nBTSTORE1 SD 1 unavailable\n";
    Pump(serial,90,95,&editor);CHECK_EQ(serial.SdStatus(),BringUpSerial::SdAvailability::Unknown);
    wire->rx+="BTSTORE1 SDCAP 1\nBTSTORE1 SD 1 available\nBTSTORE1 RESULT 1 session-boot recovery-required 0\n";
    Pump(serial,95,100,&editor);CHECK_EQ(serial.SdStatus(),BringUpSerial::SdAvailability::Available);
    wire->rx+="BTSTORE1 SD 1 unavailable\n";Pump(serial,100,105,&editor);
    CHECK(serial.UserActionsBlocked());CHECK(editor.Locked());
    CHECK(!serial.Start());CHECK(!serial.Stop());CHECK(!serial.Pause());CHECK(!serial.Continue());
    CHECK(!serial.StepTempo(5));CHECK(!serial.ToggleChannelMute(0));CHECK(!serial.Publish(editor,2101));
    CHECK(!serial.RequestFile(BringUpSerial::FileAction::Test,2101));
    BringUpControls controls(serial,nullptr);const auto count=wire->commands.size();
    controls.Request(BringUpAction::PatternToggle,"fixture",2102);controls.Tick(2102);
    CHECK_EQ(wire->commands.size(),count);
    wire->rx+="BTSTORE1 SD 1 available\n";Pump(serial,106,110,&editor);
    CHECK_EQ(serial.SdStatus(),BringUpSerial::SdAvailability::Unavailable); // No hot-card recovery.
    wire->healthy=false;serial.Tick(2300);serial.SyncEditor(editor,2300);
    CHECK(!serial.Connected());CHECK_EQ(serial.SdStatus(),BringUpSerial::SdAvailability::Unknown);
    Pump(serial,200,290,&editor);CHECK(serial.Connected());
    CHECK_EQ(serial.SdStatus(),BringUpSerial::SdAvailability::Unknown);
    CHECK(std::find(wire->commands.begin(),wire->commands.end(),"BTTEST1 START")==wire->commands.end());
}
TEST_CASE(ConfirmedUnavailableSdAllowsImmediateExitWithoutPersistenceOrTransport) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();BringUpSerial serial(nullptr,std::move(peer));
    serial.SetStartupPattern("none");LocalPatternEditor editor;Pump(serial,0,90,&editor);
    wire->rx+="BTSTORE1 SDCAP 1\nBTSTORE1 SD 1 unavailable\n";Pump(serial,90,95,&editor);
    const auto count=wire->commands.size();BringUpControls controls(serial,nullptr);
    controls.Request(BringUpAction::ResetStop,"fixture",1901);CHECK(!controls.Quit());
    controls.Request(BringUpAction::Exit,"fixture",1902);CHECK(controls.Quit());CHECK_EQ(wire->commands.size(),count);
}

TEST_CASE(BootSdFailureBlocksAnEarlyQueuedTestLoadButKeepsStatusServiceAlive) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->boot_sd="unavailable";
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern4");
    CHECK(serial.RequestFile(BringUpSerial::FileAction::Test,0));LocalPatternEditor editor;
    Pump(serial,0,180,&editor);CHECK(serial.Connected());CHECK(serial.UserActionsBlocked());
    CHECK_EQ(serial.SdStatus(),BringUpSerial::SdAvailability::Unavailable);
    CHECK_EQ(wire->Loads(),0u);
    CHECK(std::find(wire->commands.begin(),wire->commands.end(),"BTTEST1 STOP")==wire->commands.end());
    CHECK(std::find(wire->commands.begin(),wire->commands.end(),"BTTEST1 START")==wire->commands.end());
    CHECK(std::find(wire->commands.begin(),wire->commands.end(),"BTTEST1 STATUS")!=wire->commands.end());
    serial.BeginExit(3601);CHECK(serial.ExitReady());
}

TEST_CASE(WarningBarrierWaitsForInitialBaselineAndWorkingReadbackWithStopAvailable) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
    wire->preview_cap=true;wire->hold_baseline=wire->hold_working=true;wire->boot_sd="available";
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("none");LocalPatternEditor editor;
    Pump(serial,0,60,&editor);CHECK(serial.Connected());CHECK(serial.UserActionsBlocked());
    CHECK_EQ(serial.SdStatus(),BringUpSerial::SdAvailability::Available);
    CHECK(!serial.Start());CHECK(!serial.Pause());CHECK(!serial.StepVolume(1));CHECK(!serial.StepTempo(5));
    CHECK(!serial.ToggleChannelMute(0));CHECK(!serial.Publish(editor,1201));
    CHECK(!serial.RequestFile(BringUpSerial::FileAction::Test,1201));
    BringUpControls controls(serial,nullptr);
    controls.Request(BringUpAction::PatternToggle,"delayed sync",1201);
    controls.Request(BringUpAction::ResetStop,"delayed sync",1202);Pump(serial,61,70,&editor);
    CHECK(std::find(wire->commands.begin(),wire->commands.end(),"BTTEST1 STOP")!=wire->commands.end());
    CHECK(serial.UserActionsBlocked());
    CHECK(!wire->baseline_reply.empty());wire->hold_baseline=false;wire->rx+=wire->baseline_reply;wire->baseline_reply.clear();
    Pump(serial,70,95,&editor);CHECK(editor.Ready());CHECK(editor.Locked());CHECK(serial.UserActionsBlocked());
    CHECK(!wire->working_reply.empty());wire->hold_working=false;wire->rx+=wire->working_reply;wire->working_reply.clear();
    Pump(serial,95,130,&editor);CHECK(serial.StartupPatternReady());CHECK(!serial.UserActionsBlocked());CHECK(!editor.Locked());
    CHECK_EQ(wire->Loads(),0u);CHECK(std::find(wire->commands.begin(),wire->commands.end(),"BTTEST1 START")==wire->commands.end());
    CHECK(serial.Start());
}
TEST_CASE(WarningBarrierAllowsExitWhileInitialWorkingReadbackIsDelayed) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->preview_cap=true;wire->hold_working=true;
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("none");LocalPatternEditor editor;
    Pump(serial,0,70,&editor);CHECK(serial.Connected());CHECK(serial.UserActionsBlocked());
    BringUpControls controls(serial,nullptr);controls.Request(BringUpAction::Exit,"delayed sync",1401);CHECK(controls.Quit());
    CHECK(std::find(wire->commands.begin(),wire->commands.end(),"BTSTORE1 SAVE 1")==wire->commands.end());
}

TEST_CASE(PatternScreenEntryWaitsForStopAndSessionSaveAndCoalescesRequests) {
    for(const char* state:{"IDLE","PLAYING","PAUSED","DONE"}) {
        const bool playing=std::strcmp(state,"IDLE")!=0;
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
        wire->hold_stop=true;wire->hold_save=true;
        BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("none");
        LocalPatternEditor editor;Pump(serial,0,90,&editor);
        CHECK(editor.Ready());if(!editor.Ready())return;
        wire->state=state;wire->rx+="BTTEST1 STATE "+wire->state+"\n";serial.Tick(1800);
        editor.RestoreCursor({12,5,EditField::Instrument,false});
        const auto draft=*editor.Draft();BringUpControls controls(serial,nullptr);
        const auto drive=[&](unsigned first,unsigned last) {for(unsigned i=first;i<last;++i){serial.SyncEditor(editor,i*20);controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
        const auto count=[&](const char* command){return std::count(wire->commands.begin(),wire->commands.end(),command);};
        for(unsigned n=0;n<4;++n)controls.Request(BringUpAction::ScreenLeft,"test",1801+n);
        drive(91,110);CHECK(controls.ScreenPending());CHECK(!controls.LoadSaveScreen());
        CHECK_EQ(count("BTTEST1 STOP"),playing?1:0);
        if(playing) {
            CHECK_EQ(count("BTSTORE1 SAVE 1"),0);wire->state="IDLE";wire->rx+="BTTEST1 STOPPED\n";drive(110,125);
        }
        CHECK_EQ(count("BTSTORE1 SAVE 1"),1);CHECK(!wire->save_reply.empty());CHECK(!controls.LoadSaveScreen());
        controls.Request(BringUpAction::ScreenLeft,"repeat",2501);CHECK_EQ(count("BTSTORE1 SAVE 1"),1);
        wire->rx+=wire->save_reply;wire->save_reply.clear();drive(126,145);
        CHECK(controls.LoadSaveScreen());CHECK(!controls.ScreenPending());
        controls.Request(BringUpAction::ScreenRight,"test",2901);CHECK(!controls.LoadSaveScreen());
        CHECK(SamePattern(draft,*editor.Draft()));CHECK_EQ(editor.Cursor().row,12u);CHECK_EQ(editor.Cursor().channel,5u);
        CHECK_EQ(editor.Cursor().field,EditField::Instrument);
        CHECK(!serial.Playing());CHECK(!serial.Paused());
        controls.Request(BringUpAction::PatternToggle,"next Play",2902);drive(146,160);
        CHECK_EQ(count("BTTEST1 START"),1);CHECK_EQ(count("BTTEST1 CONTINUE"),0);
        CHECK_EQ(editor.Cursor().row,12u); // Editor cursor does not seed playback START.
        for(const auto& c:wire->commands)CHECK(c.rfind("BTPUBLISH1 BEGIN",0)!=0);
    }
}
TEST_CASE(PatternScreenEntryFailureNeverEntersOrRetriesAutomatically) {
    for(unsigned failure=0;failure<7;++failure) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
        wire->state=failure==0?"PLAYING":"IDLE";wire->hold_stop=failure==0;
        wire->hold_save=failure>=2 && failure!=5;wire->result=failure==5?"deferred-playing":"invalid";
        BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("none");LocalPatternEditor editor;
        Pump(serial,0,90,&editor);BringUpControls controls(serial,nullptr);
        controls.Request(BringUpAction::ScreenLeft,"test",1801);
        for(unsigned i=91;i<110;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}
        if(failure==6) {wire->state="ERROR";wire->rx+="BTTEST1 STATE ERROR\nBTSTORE1 RESULT 1 save ok 0\n";}
        if(failure==3)wire->healthy=false;
        if(failure==4)wire->rx+="BTSTORE1 SDCAP 1\nBTSTORE1 SD 1 unavailable\n";
        for(unsigned i=110;i<1000;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}
        CHECK(!controls.LoadSaveScreen());CHECK(!controls.ScreenPending());
        CHECK_EQ(std::count(wire->commands.begin(),wire->commands.end(),"BTSTORE1 SAVE 1"),failure==0?0:1);
        CHECK(*controls.ScreenStatus());
    }
}

TEST_CASE(PatternBrowserLoadsAllFixturesOnlyAfterConfirmationAndReadback) {
    for(unsigned selected=0;selected<5;++selected) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->list_cap=true;wire->preview_cap=true;wire->working_note=75;
        BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern4");LocalPatternEditor editor;Pump(serial,0,90,&editor);
        CHECK(editor.Ready());if(!editor.Ready())return;editor.RestoreCursor({19,5,EditField::Instrument,false});
        CHECK(editor.Dirty());CHECK_EQ(editor.Draft()->cells[0][0].note,75u);
        const auto original=*editor.Draft();BringUpControls controls(serial,nullptr);
        auto drive=[&](unsigned first,unsigned last){for(unsigned i=first;i<last;++i){serial.SyncEditor(editor,i*20);controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
        controls.Request(BringUpAction::ScreenLeft,"entry",1801);drive(91,110);controls.Request(BringUpAction::BrowserConfirm,"open Load",2201);drive(111,120);
        CHECK(controls.LoadSaveScreen());CHECK(serial.PatternListReady());CHECK_EQ(serial.PatternFiles().count,5u);
        for(unsigned i=0;i<selected;++i)controls.Request(BringUpAction::BrowserDown,"select",2401);
        controls.Request(BringUpAction::BrowserConfirm,"prompt",2402);drive(121,125);
        CHECK_EQ(wire->Loads(),0u);CHECK(controls.BrowserText().find("Replace")!=std::string::npos);
        wire->hold_load=true;controls.Request(BringUpAction::BrowserConfirm,"confirm",2501);drive(126,150);
        CHECK_EQ(wire->Loads(),1u);CHECK(controls.LoadSaveScreen());CHECK(SamePattern(original,*editor.Draft()));
        const auto command="BTSTORE1 LOAD 1 "+controls.BrowserList().names[selected];
        CHECK(std::find(wire->commands.begin(),wire->commands.end(),command)!=wire->commands.end());
        wire->rx+="BTSTORE1 RESULT 1 load-stale.btdev missing 0\nBTSTORE1 LISTEND 1 999 0 1\n";drive(150,151);
        CHECK(controls.BrowserLoading());CHECK(controls.LoadSaveScreen());
        wire->hold_baseline=true;wire->hold_working=true;wire->rx+=wire->load_reply;wire->load_reply.clear();drive(151,175);
        CHECK(controls.LoadSaveScreen());CHECK(controls.BrowserLoading());CHECK(!wire->baseline_reply.empty());
        wire->hold_baseline=false;wire->rx+=wire->baseline_reply;wire->baseline_reply.clear();drive(176,200);
        CHECK(controls.BrowserLoading());CHECK(controls.LoadSaveScreen());CHECK(!wire->working_reply.empty());
        wire->hold_working=false;wire->rx+=wire->working_reply;wire->working_reply.clear();drive(201,260);
        CHECK(!controls.LoadSaveScreen());CHECK(!controls.BrowserLoading());CHECK(editor.Ready());CHECK(!editor.Locked());
        CHECK_EQ(editor.Cursor().row,0u);CHECK_EQ(editor.Cursor().channel,0u);CHECK(editor.Cursor().field==EditField::Note);CHECK(!editor.Cursor().header);
        CHECK(serial.MidiRecoveryStopped());CHECK_EQ(wire->pattern,selected==0?5u:selected);
        CHECK(std::find(wire->commands.begin(),wire->commands.end(),"BTTEST1 START")==wire->commands.end());
        // Browser selection never retargets the retained F3 configuration.
        CHECK(serial.RequestFile(BringUpSerial::FileAction::Test,5001));
    }
}
TEST_CASE(PatternBrowserCancelAndRejectedLoadRetainOriginalWorkAndCursor) {
    for(const char* failure:{"cancel","invalid","missing","unsupported","io","application-rejected"}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->list_cap=true;wire->preview_cap=true;wire->working_note=75;
        BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("none");LocalPatternEditor editor;Pump(serial,0,90,&editor);
        CHECK(editor.Ready());if(!editor.Ready())return;editor.RestoreCursor({11,4,EditField::Instrument,false});
        CHECK(editor.Dirty());CHECK_EQ(editor.Draft()->cells[0][0].note,75u);
        const auto original=*editor.Draft();BringUpControls controls(serial,nullptr);
        auto drive=[&](unsigned first,unsigned last){for(unsigned i=first;i<last;++i){serial.SyncEditor(editor,i*20);controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
        controls.Request(BringUpAction::ScreenLeft,"entry",1801);drive(91,110);controls.Request(BringUpAction::BrowserConfirm,"open Load",2201);drive(111,120);
        controls.Request(BringUpAction::BrowserDown,"select",2401);controls.Request(BringUpAction::BrowserConfirm,"prompt",2402);
        if(!std::strcmp(failure,"cancel"))controls.Request(BringUpAction::BrowserCancel,"cancel",2403);
        else {wire->result=failure;controls.Request(BringUpAction::BrowserConfirm,"confirm",2403);drive(121,220);}
        CHECK_EQ(wire->Loads(),!std::strcmp(failure,"cancel")?0u:1u);
        CHECK_EQ(wire->pattern,1u);CHECK(SamePattern(original,*editor.Draft()));CHECK_EQ(editor.Cursor().row,11u);CHECK_EQ(editor.Cursor().channel,4u);
        CHECK(editor.Cursor().field==EditField::Instrument);CHECK(!editor.Locked());
        controls.Request(BringUpAction::ScreenRight,"return",4401);CHECK(!controls.LoadSaveScreen());
    }
}
TEST_CASE(PatternLoadConfigOptOutAndStoppedGateAndListTimeout) {
    for(bool confirm:{false,true}) {
        HostConfig config;CHECK(config.confirm_load_replace);
        const std::string text="[btx]\nschema_version=1\nnew_pattern_length=32\n[pattern]\nconfirm_load_replace="+std::string(confirm?"true":"false")+"\n";
        CHECK_EQ(ParseHostConfig(text,config),HostConfigStatus::Loaded);CHECK_EQ(config.confirm_load_replace,confirm);
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->list_cap=true;
        BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("none");LocalPatternEditor editor;Pump(serial,0,90,&editor);
        BringUpControls controls(serial,nullptr);controls.SetConfirmLoadReplace(config.confirm_load_replace);
        controls.Request(BringUpAction::ScreenLeft,"entry",1801);
        for(unsigned i=91;i<110;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}
        controls.Request(BringUpAction::BrowserConfirm,"open Load",2201);
        for(unsigned i=111;i<120;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}
        controls.Request(BringUpAction::BrowserConfirm,"select",2401);
        for(unsigned i=121;i<170;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}
        CHECK_EQ(wire->Loads(),confirm?0u:1u);
    }
    HostConfig config;CHECK_EQ(ParseHostConfig("[btx]\nschema_version=1\nnew_pattern_length=32\n[pattern]\nconfirm_load_replace=nope\n",config),HostConfigStatus::Invalid);CHECK(config.confirm_load_replace);
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->list_cap=true;wire->hold_list=true;
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("none");LocalPatternEditor editor;Pump(serial,0,90,&editor);
    CHECK(!serial.RequestPatternLoad("../patern1.btdev",1800));CHECK(!serial.RequestPatternLoad("none",1800));
    wire->rx+="BTTEST1 STATE PLAYING\n";Pump(serial,91,95,&editor);
    CHECK(!serial.RequestPatternLoad("patern1.btdev",1901));CHECK(!serial.RequestPatternList(0,1901));
    wire->state="IDLE";wire->rx+="BTTEST1 STATE IDLE\n";Pump(serial,96,100,&editor);
    CHECK(serial.RequestPatternList(0,2001));Pump(serial,101,650,&editor);
    CHECK(!serial.PatternListBusy());CHECK(!serial.PatternListReady());CHECK(std::strstr(serial.PatternListStatus(),"timed out"));
    wire->hold_list=false;CHECK(serial.RequestPatternList(0,13001));Pump(serial,651,680,&editor);CHECK(serial.PatternListReady());
}

TEST_CASE(PatternBrowserSortsAllPagesAndMovesContinuousViewport) {
    for(unsigned total:{16u,18u})for(bool backwards:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->list_cap=true;wire->hold_list=true;
        wire->files.clear();for(unsigned i=total;i>0;--i)wire->files.push_back("file"+std::string(i<10?"0":"")+std::to_string(i)+".btdev");
        BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("none");LocalPatternEditor editor;Pump(serial,0,90,&editor);
        BringUpControls controls(serial,nullptr);unsigned tick=91;
        auto drive=[&](){for(unsigned end=tick+60;tick<end;++tick){serial.SyncEditor(editor,tick*20);controls.Tick(tick*20);serial.SyncEditor(editor,tick*20);}};
        controls.Request(BringUpAction::ScreenLeft,"entry",1801);drive();controls.Request(BringUpAction::BrowserConfirm,"open Load",tick*20);drive();
        CHECK(serial.PatternListBusy());controls.Request(BringUpAction::BrowserConfirm,"pending",tick*20);CHECK_EQ(wire->Loads(),0u);
        CHECK(controls.BrowserList().names.empty());
        wire->hold_list=false;wire->rx+=wire->list_reply;wire->list_reply.clear();drive();drive();
        auto view=controls.BrowserList();CHECK_EQ(view.names.size(),total);CHECK(std::is_sorted(view.names.begin(),view.names.end()));
        CHECK_EQ(view.names.front(),"file01.btdev");CHECK_EQ(view.selected,0u);
        controls.Request(BringUpAction::BrowserUp,"first boundary",tick*20);CHECK_EQ(controls.BrowserList().selected,0u);
        for(unsigned i=0;i<5;++i)controls.Request(BringUpAction::BrowserDown,"down",tick*20);
        view=controls.BrowserList();CHECK_EQ(view.selected,5u);CHECK_EQ(view.first,0u);
        controls.Request(BringUpAction::BrowserDown,"next block",tick*20);
        view=controls.BrowserList();CHECK_EQ(view.selected,6u);CHECK_EQ(view.first,6u);CHECK_EQ(view.names[view.selected],"file07.btdev");
        const auto bar=view.Indicator(108);CHECK_EQ(bar.offset,108u*6/total);CHECK_EQ(bar.length,108u*12/total-bar.offset);
        if(backwards) {
            controls.Request(BringUpAction::BrowserUp,"previous block",tick*20);
            view=controls.BrowserList();CHECK_EQ(view.selected,5u);CHECK_EQ(view.first,0u);
        } else {
            for(unsigned i=0;i<total+2;++i)controls.Request(BringUpAction::BrowserDown,"last boundary",tick*20);
            view=controls.BrowserList();CHECK_EQ(view.selected,total-1);CHECK_EQ(view.first,12u);
            const auto last=view.Indicator(108);CHECK_EQ(last.offset+last.length,108u);
        }
        const auto selected=view.names[view.selected];
        controls.Request(BringUpAction::BrowserConfirm,"prompt",tick*20);CHECK(controls.BrowserText().find(selected)!=std::string::npos);
        CHECK_EQ(wire->Loads(),0u);controls.Request(BringUpAction::BrowserConfirm,"load",tick*20);drive();drive();
        CHECK(std::find(wire->commands.begin(),wire->commands.end(),"BTSTORE1 LOAD 1 "+selected)!=wire->commands.end());
        CHECK(!controls.LoadSaveScreen());CHECK_EQ(editor.Cursor().row,0u);CHECK_EQ(editor.Cursor().channel,0u);
    }
}

TEST_CASE(PatternSaveRequiresSourceAndSavesUnpublishedEditsWithoutMovingCursor) {
    for(const char* result:{"ok","io","recovery-required"}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->list_cap=wire->preview_cap=wire->pattern_save_cap=wire->simulate_working=true;
        MakeStoredDevelopmentPattern(wire->simulated_working,1);
        BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("none");LocalPatternEditor editor;Pump(serial,0,90,&editor);
        BringUpControls controls(serial,nullptr);unsigned tick=91;
        auto drive=[&](){for(unsigned end=tick+90;tick<end;++tick){wire->now=tick*20;serial.SyncEditor(editor,tick*20);controls.Tick(tick*20);serial.SyncEditor(editor,tick*20);}};
        controls.Request(BringUpAction::ScreenLeft,"entry",1801);drive();
        CHECK(controls.BrowserText().find("no known source")!=std::string::npos);
        controls.Request(BringUpAction::BrowserDown,"Save",tick*20);controls.Request(BringUpAction::BrowserConfirm,"unnamed",tick*20);drive();
        CHECK(!serial.FilePending());CHECK(!serial.PatternSaveAvailable());CHECK(!wire->have_saved);
        controls.Request(BringUpAction::BrowserUp,"Load",tick*20);controls.Request(BringUpAction::BrowserConfirm,"open",tick*20);drive();
        controls.Request(BringUpAction::BrowserDown,"patern1",tick*20);
        controls.Request(BringUpAction::BrowserConfirm,"prompt",tick*20);controls.Request(BringUpAction::BrowserConfirm,"load",tick*20);drive();
        CHECK(!controls.LoadSaveScreen());CHECK_EQ(std::string(serial.PatternSource()),"patern1.btdev");
        controls.Request(BringUpAction::ScreenLeft,"entry cancel",tick*20);drive();
        controls.Request(BringUpAction::BrowserConfirm,"open Load",tick*20);drive();
        controls.Request(BringUpAction::BrowserDown,"select",tick*20);controls.Request(BringUpAction::BrowserConfirm,"prompt",tick*20);
        controls.Request(BringUpAction::BrowserCancel,"cancel",tick*20);
        CHECK_EQ(std::string(serial.PatternSource()),"patern1.btdev");
        controls.Request(BringUpAction::ScreenRight,"return",tick*20);
        CHECK(editor.Apply(EditorAction::SetValue,75));drive();CHECK(editor.Dirty());CHECK_EQ(wire->simulated_working.cells[0][0].note,75u);
        editor.RestoreCursor({9,3,EditField::Instrument,false});const auto original=*editor.Draft();
        wire->pattern_save_result=result;wire->hold_pattern_save=true;
        controls.Request(BringUpAction::ScreenLeft,"entry Save",tick*20);drive();
        controls.Request(BringUpAction::BrowserDown,"Save",tick*20);controls.Request(BringUpAction::BrowserConfirm,"save",tick*20);drive();
        CHECK(controls.LoadSaveScreen());CHECK(serial.FilePending());CHECK(!serial.PatternSaveSucceeded());
        CHECK(std::string(controls.ScreenStatus()).find("Pattern saved:")==std::string::npos);
        wire->rx+="BTSTORE1 RESULT 1 pattern-save-stale.btdev ok 0\n";drive();CHECK(serial.FilePending());
        wire->hold_pattern_save=false;wire->rx+=wire->pattern_save_reply;wire->pattern_save_reply.clear();drive();
        CHECK_EQ(std::string(controls.ScreenStatus()).find("Pattern saved:")!=std::string::npos,std::string(result)=="ok");
        if(std::string(result)=="ok") {CHECK_EQ(std::string(controls.ScreenStatus()),"Pattern saved: patern1.btdev");drive();CHECK_EQ(std::string(controls.ScreenStatus()),"Pattern saved: patern1.btdev");}
        CHECK_EQ(serial.PatternSaveSucceeded(),std::string(result)=="ok");CHECK_EQ(controls.LoadSaveScreen(),std::string(result)!="ok");
        CHECK(SamePattern(original,*editor.Draft()));CHECK(!editor.Locked());CHECK_EQ(editor.Cursor().row,9u);CHECK_EQ(editor.Cursor().channel,3u);
        CHECK(editor.Cursor().field==EditField::Instrument);CHECK_EQ(std::string(serial.PatternSource()),"patern1.btdev");
        CHECK(std::find(wire->commands.begin(),wire->commands.end(),"BTSTORE1 PATTERNSAVE 1 patern1.btdev")!=wire->commands.end());
        CHECK(std::none_of(wire->commands.begin(),wire->commands.end(),[](const auto& c){return c.rfind("BTPUBLISH1 BEGIN",0)==0 || c=="BTTEST1 START";}));
        if(std::string(result)=="ok") {
            CHECK(wire->have_saved);CHECK_EQ(wire->saved_pattern.cells[0][0].note,75u);
            CHECK(serial.RequestPatternLoad("patern1.btdev",tick*20));drive();
            CHECK_EQ(editor.Draft()->cells[0][0].note,75u);
            // A rejected replacement must not retarget the original file.
            wire->result="io";CHECK(serial.RequestPatternLoad("patern2.btdev",tick*20));drive();
            CHECK_EQ(std::string(serial.PatternSource()),"patern1.btdev");CHECK(!editor.Locked());
        }
    }
}

TEST_CASE(StartupSessionPrecedesConfigAndMissingInvalidSessionsUseConfiguredFallbackOnce) {
    for(const char* session:{"ok","backup-retained","missing","invalid"})for(unsigned rows:{16u,32u,64u}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->boot_session=session;
        BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("none");serial.ConfigureStartupFallback(rows);LocalPatternEditor editor;
        Pump(serial,0,230,&editor);CHECK(editor.Ready());CHECK(serial.StartupPatternReady());if(!editor.Ready()){std::printf("fallback session=%s rows=%u status=%s file=%s\n",session,rows,serial.Status(),serial.PublicationStatus());continue;}
        const bool valid=std::string(session)=="ok" || std::string(session)=="backup-retained";
        CHECK_EQ(editor.Draft()->active_rows,valid?32u:rows);
        CHECK_EQ(std::count(wire->commands.begin(),wire->commands.end(),"BTSTORE1 NEW 1 "+std::to_string(rows)),valid?0:1);
        CHECK(std::find(wire->commands.begin(),wire->commands.end(),"BTTEST1 START")==wire->commands.end());
        wire->healthy=false;serial.Tick(4601);serial.SyncEditor(editor,4601);wire->healthy=true;Pump(serial,231,460,&editor);
        CHECK_EQ(std::count(wire->commands.begin(),wire->commands.end(),"BTSTORE1 NEW 1 "+std::to_string(rows)),valid?0:1);
    }
    for(const char* session:{"ok","missing","invalid"}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->boot_session=session;
        BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern2.btdev");serial.ConfigureStartupFallback(64);LocalPatternEditor editor;
        Pump(serial,0,230,&editor);CHECK(serial.StartupPatternReady());CHECK_EQ(wire->Loads(),std::string(session)=="ok"?0u:1u);
    }
}
TEST_CASE(ReconnectRecoveryMenuRetainsWorkUntilExplicitConfirmationAndCancel) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->preview_cap=true;wire->recovery_publication=true;
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;Pump(serial,0,90,&editor);
    CHECK(editor.Apply(EditorAction::SetValue,75));serial.SyncEditor(editor,1801);
    wire->healthy=false;serial.Tick(1802);serial.SyncEditor(editor,1802);wire->healthy=true;wire->pattern=2;Pump(serial,91,220,&editor);
    CHECK(serial.ReconnectRecoveryPending());CHECK(serial.RecoveryMenuAvailable());const auto retained=*editor.Draft();
    BringUpControls controls(serial,nullptr);controls.Request(BringUpAction::ScreenLeft,"recovery menu",4401);
    CHECK(controls.RecoveryScreen());CHECK(controls.BrowserText().find("retained")!=std::string::npos);
    controls.Request(BringUpAction::BrowserConfirm,"review",4402);CHECK(controls.BrowserText().find("Confirm recovery")!=std::string::npos);
    controls.Request(BringUpAction::BrowserCancel,"cancel prompt",4403);CHECK(controls.RecoveryScreen());
    controls.Request(BringUpAction::BrowserCancel,"leave",4404);CHECK(!controls.LoadSaveScreen());CHECK(SamePattern(retained,*editor.Draft()));
    CHECK(std::none_of(wire->commands.begin(),wire->commands.end(),[](const auto& c){return c.rfind("BTPUBLISH1 BEGIN",0)==0;}));
    controls.Request(BringUpAction::ScreenLeft,"review again",4405);controls.Request(BringUpAction::BrowserConfirm,"review",4406);
    controls.Request(BringUpAction::BrowserConfirm,"explicit recovery",4407);Pump(serial,221,230,&editor);
    CHECK(std::any_of(wire->commands.begin(),wire->commands.end(),[](const auto& c){return c.rfind("BTPUBLISH1 BEGIN",0)==0;}));
    CHECK(SamePattern(retained,*editor.Draft()));
}

TEST_CASE(InterruptedFirstReadbackCannotApplyStaleMissingSessionFallback) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->boot_session="missing";wire->hold_baseline=true;
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern2.btdev");serial.ConfigureStartupFallback(64);LocalPatternEditor editor;
    Pump(serial,0,30,&editor);CHECK(!editor.Ready());CHECK_EQ(wire->Loads(),0u);
    wire->healthy=false;serial.Tick(601);serial.SyncEditor(editor,601);
    wire->boot_session="ok";wire->hold_baseline=false;wire->baseline_reply.clear();wire->rx.clear();wire->healthy=true;
    Pump(serial,80,220,&editor);CHECK(editor.Ready());CHECK(serial.StartupPatternReady());CHECK_EQ(wire->Loads(),0u);
}

TEST_CASE(PatternBrowserBackKeepsSelectionAndEscapeStillReturnsToPattern) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->list_cap=true;
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;Pump(serial,0,90,&editor);
    BringUpControls controls(serial,nullptr);unsigned tick=91;
    auto drive=[&](){for(unsigned end=tick+50;tick<end;++tick){serial.SyncEditor(editor,tick*20);controls.Tick(tick*20);serial.SyncEditor(editor,tick*20);}};
    controls.Request(BringUpAction::ScreenLeft,"entry",1801);drive();
    controls.Request(BringUpAction::BrowserConfirm,"open Load",tick*20);drive();
    controls.Request(BringUpAction::BrowserDown,"select",tick*20);const auto selected=controls.BrowserList().selected;
    controls.Request(BringUpAction::BrowserConfirm,"prompt",tick*20);CHECK(controls.BrowserConfirming());
    controls.Request(BringUpAction::BrowserBack,"Backspace/B",tick*20);CHECK(!controls.BrowserConfirming());CHECK_EQ(controls.BrowserList().selected,selected);
    controls.Request(BringUpAction::ScreenLeft,"Ctrl+Left",tick*20);CHECK(controls.LoadSaveScreen());CHECK_EQ(controls.BrowserList().names.front(),"Load");
    controls.Request(BringUpAction::BrowserConfirm,"reopen Load",tick*20);CHECK_EQ(controls.BrowserList().selected,selected);
    controls.Request(BringUpAction::BrowserCancel,"Escape",tick*20);CHECK(!controls.LoadSaveScreen());CHECK_EQ(wire->Loads(),0u);
}

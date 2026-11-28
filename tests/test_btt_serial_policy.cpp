#include "test_framework.h"
#include "bringup/btt_serial_policy.h"
#include "bringup/midi_note_delay_service.h"
#include "bringup/pattern_file_store.h"
#include "bringup/pattern_player_selection.h"
#include "ui/bringup_serial.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
using namespace BroTracker;
namespace {
using D=BttSerialDisposition;
const char* queries[]={"BTTEST1 STATUS","BTDEVICE1 GET 1","BTTEMPO1 GET 1","BTMUTE1 GET 1",
    "BTVOL1 GET 1","BTGAIN1 GET 1","BTMIDI1 GET 1","BTSTREAM1 GET","BTMIDIDELAY1 GET 1 5",
    "BTCLOCK1 GET 1 7","BTINS1 COLLECTION GET 1","BTINS1 GET 1 0","BTINS1 GET 2 0","BTINS1 GET 3 98"};
const char* mutations[]={"BTTEMPO1 SET 1 1 16000","BTMUTE1 SET 1 1 255","BTVOL1 SET 1 1 50",
    "BTGAIN1 SET 1 1 0 0 0 0 0 0 0 0","BTINS1 DEFINE 1 1 0 3 -","BTINS1 REPLACE 1 1 0 3 -",
    "BTINS1 RELINK 1 1 0 1 Samples/a.wav","BTINS1 EDIT 4 1 0 1 60 0","BTINS1 RESTORE 1 1 0",
    "BTINS1 VISIT BEGIN 1 1","BTINS1 VISIT END 1 1","BTINS1 CANCEL 1 1","BTWAV1 PLAY 1 1 Samples/a.wav",
    "BTWAV1 CANCEL 1 1","BTPUBLISH1 BEGIN 2 1 16 8 128 12500","BTPUBLISH1 CELL 1 1 0 60 0 0",
    "BTPUBLISH1 COMMIT 1 1 128 42","BTPREVIEW1 BEGIN 1 1 16 8 128","BTPREVIEW1 DBEGIN 1 1 0 1",
    "BTSTORE1 SAVE 1","BTSTORE1 SAVE 2 0 0 0 0","BTSTORE1 LOAD 1 patern1","BTSTORE1 RESTORE 1",
    "BTSTORE1 NEW 1 64","BTSTORE1 DRAFTLOAD 1","BTSTORE1 DRAFTSAVE 1","BTSTORE1 PATTERNSAVE 1 mine",
    "BTSTORE1 SEED 1","BTSTORE1 MIGRATE 1","BTSTORE1 REPLACE 3","BTSTORE1 READY 1","BTSTORE1 LIST 1 1 0"};
const char* readback[]={"BTPATTERN1 GET 1 1","BTPATTERN1 GET 2 1","BTPREVIEW1 GET 1 1 0","BTPREVIEW1 GET 1 1 1",
    "BTWAVE1 GET 1 1 0 @53616d706c65732f612e776176","BTWAVE1 GET 2 1 0 @53616d706c65732f612e776176",
    "BTWAVE1 NEXT 1 1 0","BTWAVE1 RESUME 1 1 0","BTWAVE1 RANGE 1 1 0 64","BTWAVE1 CANCEL 1 1"};
const char* setup[]={"BTTEST1 HELLO","BTAUDIO1 SET 1 default","BTAUDIO1 SET 2 bridge 1",
    "BTMIDIDELAY1 SET 1 2 82","BTCLOCK1 SET 1 7 1 1795820000 2 900"};
constexpr auto claim="BTSESSION1 CLAIM 1 1234567890abcdef1234567890abcdef";
// Real BTX client drives this transport. The production guard is used at the
// same complete-line boundary; response framing is an in-memory device peer.
struct ReconnectPeer:SerialTransport {
    std::string rx,partial;std::vector<std::string> commands,rejected;
    bool healthy=true,busy=false,retained=true,connected=false;
    AudioOutputSelection audio;bool swap=false;
    struct Output {AudioOutputChoice Active()const{return AudioOutputChoice::Bridge;}
        AudioInitialization Initialization()const{return AudioInitialization::Initialized;}} output;
    MidiOutput midi;WallClockDevice clock;unsigned now=0,clock_sets=0,claims=0,hellos=0;
    std::string lineage;bool persisted=false;
    RealtimePattern pattern;
    ReconnectPeer(){pattern.active_rows=3;pattern.active_channels=8;audio.Commit(AudioOutputChoice::Bridge,AudioOutputChoice::Bridge);}
    bool Open(FILE*,const char*)override {healthy=true;return true;}
    void Close()override {connected=false;partial.clear();rx.clear();clock.ResetConnection();}
    bool Healthy()override{return healthy;}
    const char* Error()const override{return "test disconnect";}
    int Read(char* out,unsigned size)override {
        const auto n=std::min<std::size_t>(size,rx.size());std::copy_n(rx.data(),n,out);rx.erase(0,n);return n;
    }
    int Write(const char* in,unsigned size)override {
        partial.append(in,size);auto newline=partial.find('\n');
        while(newline!=partial.npos){const auto line=partial.substr(0,newline);partial.erase(0,newline+1);Dispatch(line);newline=partial.find('\n');}
        return size;
    }
    void Dispatch(const std::string& line) {
        commands.push_back(line);
        const auto policy=BttSerialPolicy(line,busy,retained);
        if(policy==D::Reject){rejected.push_back(line);rx+="BTBTT1 ERROR retained-tune-or-load-busy\n";return;}
        if(policy==D::EphemeralLineage){++claims;rx+="BTSESSION1 LINEAGE 1 0 -\n";return;}
        const auto command=ClassifyBttSerialCommand(line);char reply[256];uint32_t f[6]{};
        if(line=="BTTEST1 HELLO" || line=="BTTEST1 STATUS") {
            if(command==BttSerialCommand::Hello){++hellos;clock.NewConnection();}
            connected=true;
            rx+="BTAUDIO1 CAP 2\nBTTEST1 STATE IDLE\nBTSESSION1 LINEAGE 1 0 -\nBTCLOCK1 CAP 1\n";
            rx+="BTAUDIO1 CAP 1\nBTMIDIDELAY1 CAP 1\nBTDEVICE1 CAP 1\nBTDEVICE1 FACTS 1 test bridge initialized default\n";
            rx+="BTPATTERN1 SNAPCAP 2\nBTPATTERN1 POS 2 0 0 0 0 0 3 8\nBTSTORE1 SDCAP 1\nBTSTORE1 SD 1 available\n";
            rx+="BTPREVIEW1 CAP 1\nBTPREVIEW1 DELTACAP 1\nBTPREVIEW1 NEXT 1 1\nBTPREVIEW1 STATE 1 1 0 0\nBTPUBLISH1 CAP 1\nBTPUBLISH1 NEXT 1 2\n";
            rx+="BTSTORE1 CAP 1\nBTSTORE1 SESSIONCAP 2\nBTSTORE1 READYCAP 1\nBTSTORE1 RESULT 1 session-boot ok 0\n";
            rx+="BTINS1 COLLECTIONCAP 1\nBTINS1 CAP 3\nBTVOL1 CAP 1\nBTTEMPO1 CAP 1\nBTMUTE1 CAP 1\nBTGAIN1 CAP 1\n";
            rx+="BTVOL1 STATE 1 0 1 100 65536\nBTTEMPO1 STATE 1 0 0 1 0 14500 14500\n";
            rx+="BTMUTE1 STATE 1 0 1 0\nBTGAIN1 STATE 1 0 1 65536 65536 65536 65536 65536 65536 65536 65536\n";
        } else if(command==BttSerialCommand::AudioSetup) {
            AudioOutputChoice route;bool next_swap=false;
            const bool v2=ParseAudioOutputRequestV2(line,route,next_swap);
            CHECK(v2 || ParseAudioOutputRequest(line,route));
            if(!audio.CanSelect(route,AudioOutputChoice::Bridge)){rx+="BTAUDIO1 ERROR 1 restart-required active=bridge\n";return;}
            audio.Commit(route,AudioOutputChoice::Bridge);swap=next_swap;
            rx+=v2?std::string("BTAUDIO1 SELECTED 2 bridge ")+(swap?"1\n":"0\n"):"BTAUDIO1 SELECTED 1 bridge\n";
        } else if(command==BttSerialCommand::MidiDelaySetup || line.find("BTMIDIDELAY1 GET ")==0) {
            MidiDelayRequest request;CHECK(ParseMidiDelayRequest(line.data(),line.size(),request));
            const auto size=FormatMidiDelayReply(ApplyMidiDelayRequest(request,connected,true,output,midi),reply,sizeof(reply));rx.append(reply,size);
        } else if(ParseClockFields(line,"BTCLOCK1 GET ",f,2)) {
            CHECK(clock.Query(f[1],now));std::snprintf(reply,sizeof(reply),"BTCLOCK1 STATE 1 %u %u 1795820000 2\n",unsigned(f[1]),unsigned(clock.Generation()));rx+=reply;
        } else if(ParseClockFields(line,"BTCLOCK1 SET ",f,6)) {
            CHECK_EQ(clock.Set(f[1],f[2],f[3],f[4],f[5],now),WallClockDevice::Result::Applied);++clock_sets;
            std::snprintf(reply,sizeof(reply),"BTCLOCK1 ACK 1 %u %u %u 2\n",unsigned(f[1]),unsigned(f[2]),unsigned(f[3]));rx+=reply;
        } else if(command==BttSerialCommand::PatternRead) {
            uint32_t id;unsigned version;
            const bool published=ParseSnapshotRequest(line.data(),line.size(),id,version);
            if(!published) {
                CHECK(SnapshotFields(line.data(),line.size(),"BTPREVIEW1 GET 1 ",f,2));id=f[0];version=2;
                CHECK_EQ(f[1],0u);std::snprintf(reply,sizeof(reply),"BTPREVIEW1 READ 1 %u 0 1 0\n",unsigned(id));rx+=reply;
            }
            PatternSnapshotSender sender(pattern,14500);CHECK(sender.SetWorkingNamespace(!published));CHECK(sender.Begin(id,now,version));
            while(sender.HasData()){std::size_t n;const auto* p=sender.Data(n);rx.append(p,n);sender.Consume(n);}
        } else if(line=="BTINS1 COLLECTION GET 1")rx+="BTINS1 COLLECTION STATE 1 0\n";
    }
    bool Sent(std::string_view prefix)const{return std::any_of(commands.begin(),commands.end(),[&](const auto& s){return s.find(prefix)==0;});}
};
}
TEST_CASE(BttSerial_read_queries_stay_available_during_staging_and_retention) {
    for(bool busy:{false,true})for(bool retained:{false,true}) {
        for(const auto line:queries)CHECK_EQ(BttSerialPolicy(line,busy,retained),D::Dispatch);
        CHECK_EQ(BttSerialPolicy("BTBTT1 STATUS",busy,retained),D::Dispatch);
        CHECK_EQ(BttSerialPolicy("BTBTT1 CANCEL",busy,retained),D::Dispatch);
    }
    for(const auto line:readback) {
        CHECK_EQ(BttSerialPolicy(line,false,true),D::Dispatch);
        CHECK_EQ(BttSerialPolicy(line,true,true),D::Reject);
        CHECK_EQ(BttSerialPolicy(line,true,false),D::Reject);
    }
}
TEST_CASE(BttSaveProtocol_version_identity_and_recovery_policy) {
    BttSaveRequest request;
    for(const auto line:{"BTBTT1 SAVE 1 7","BTBTT1 SAVEGET 1 7","BTBTT1 RECOVER 1 3 7"}) {
        CHECK(ParseBttSaveRequest(line,request));
        for(bool busy:{false,true})for(bool retained:{false,true})
            CHECK_EQ(BttSerialPolicy(line,busy,retained,true,false),D::Dispatch);
    }
    for(const auto line:{"BTBTT1 SAVE 1 0","BTBTT1 SAVE 2 7","BTBTT1 SAVE 1 7 Tunes/other.btt",
        "BTBTT1 SAVEGET 1 0","BTBTT1 RECOVER 1 0 7","BTBTT1 RECOVER 1 3 0",
        "BTBTT1 SAVE 1 4294967296","BTBTT1 SAVE 1 -1","BTBTT1 SAVE 1 7 extra"}) {
        CHECK(!ParseBttSaveRequest(line,request));CHECK_EQ(BttSerialPolicy(line,false,true,true),D::Reject);
    }
    for(const auto line:queries)CHECK_EQ(BttSerialPolicy(line,false,true,true),D::Dispatch);
    for(const auto line:mutations)CHECK_EQ(BttSerialPolicy(line,false,true,true),D::Reject);
    for(const auto line:readback)CHECK_EQ(BttSerialPolicy(line,false,true,true),D::Reject);
    for(const auto line:{"BTBTT1 LOAD 303 Tunes/a.btt","BTBTT1 CANCEL","BTTEST1 START","BTTEST1 STOP","BTTEST1 PAUSE","BTBTT1 MEDIA 1"})
        CHECK_EQ(BttSerialPolicy(line,false,true,true),D::Reject);
    for(const auto line:setup) {
        CHECK_EQ(BttSerialPolicy(line,false,true,true,true),D::Dispatch);
        CHECK_EQ(BttSerialPolicy(line,false,true,true,false),D::Reject);
    }
    CHECK_EQ(BttSerialPolicy(claim,false,true,true,true),D::EphemeralLineage);
    CHECK_EQ(BttSerialPolicy(claim,false,true,true,false),D::Reject);
}
TEST_CASE(BttSerial_mutations_unknown_verbs_and_prefix_lookalikes_remain_blocked) {
    for(const auto line:mutations)for(bool busy:{false,true})CHECK_EQ(BttSerialPolicy(line,busy,true),D::Reject);
    for(const auto line:{"BTTEST1 RESET","BTTEST1 HELLO extra","BTBTT1 ERASE","BTBTT1 LOAD 0 Tunes/a.btt",
        "BTDEVICE1 GET 1 extra","BTINS1 GET 4 0","BTINS1 GET 3 99","BTINS1 GET 3 1 extra",
        "BTAUDIO1 SET 2 bridge 1 extra","BTMIDIDELAY1 RESET 1 1","BTCLOCK1 ERASE 1",
        "BTWAVE1 DELETE 1 1","BTPATTERN1 GET 3 1","BTPREVIEW1 GET 1 1 2","BTSESSION1 CLAIM 1 bad"})
        CHECK_EQ(BttSerialPolicy(line,false,true),D::Reject);
    for(const auto line:mutations)CHECK_EQ(BttSerialPolicy(line,false,false),D::Dispatch);
}
TEST_CASE(BttSerial_reconnect_setup_is_explicit_and_never_allowed_while_busy) {
    for(const auto line:setup) {
        CHECK_EQ(BttSerialPolicy(line,false,true),D::Dispatch);
        CHECK_EQ(BttSerialPolicy(line,true,true),D::Reject);CHECK_EQ(BttSerialPolicy(line,true,false),D::Reject);
    }
    CHECK_EQ(BttSerialPolicy(claim,false,true),D::EphemeralLineage);
    CHECK_EQ(BttSerialPolicy(claim,true,true),D::Reject);
    CHECK_EQ(BttSerialPolicy(claim,false,false),D::Dispatch);
    for(const auto line:{"BTTEST1 START","BTTEST1 STOP","BTTEST1 PAUSE","BTTEST1 CONTINUE","BTBTT1 LOAD 303 Tunes/a.btt"}) {
        CHECK_EQ(BttSerialPolicy(line,false,true),D::Dispatch);CHECK_EQ(BttSerialPolicy(line,true,true),D::Reject);
    }
}
TEST_CASE(BttSerial_submitted_publication_remains_frozen_until_its_real_ack) {
    SelectedPatternBringUpControl control;PatternPublication publication;RealtimePattern old,next;next.active_rows=3;
    VariableRateSampleBindings bindings;CHECK_EQ(control.InitializeAudio(12500,44100,old,bindings),PatternPlayerStatus::Success);
    CHECK(StageStoredPattern(publication,next,14500,1));
    CHECK_EQ(control.SubmitStoppedTune(*publication.Frozen(),bindings,publication.Identity(),14500),PatternRequestStatus::Accepted);
    const auto* frozen=publication.Frozen();
    for(const auto line:{"BTTEST1 HELLO","BTTEST1 START","BTPATTERN1 GET 2 1","BTPREVIEW1 BEGIN 1 2 3 8 24",claim})
        CHECK_EQ(BttSerialPolicy(line,true,true),D::Reject);
    CHECK_EQ(BttSerialPolicy("BTDEVICE1 GET 1",true,true),D::Dispatch);
    publication.NewSession(); // Existing disconnect boundary cannot free submitted storage.
    CHECK_EQ(publication.Frozen(),frozen);CHECK(!control.StorageReady());CHECK_EQ(control.Snapshot().tempo,12500u);
    std::int16_t out[1];control.AudioBlock(out,1,true);PatternAppliedRequest ack;CHECK(control.TakeApplied(ack));
    CHECK(publication.Complete(ack.publication_id,true,old));CHECK_EQ(control.Snapshot().transport,PatternTransportState::Stopped);
    CHECK_EQ(old.active_rows,3);CHECK_EQ(control.Snapshot().tempo,14500u);
}
TEST_CASE(BttSerial_actual_BTX_connect_and_reconnect_setup_never_enables_edits) {
    auto peer=std::make_unique<ReconnectPeer>();auto* wire=peer.get();BringUpSerial client(nullptr,std::move(peer));
    client.SetAudioOutputPreference(AudioOutputChoice::Bridge);client.SetSwapChannelsPreference(true);client.SetMidiNoteDelay(82);
    LocalPatternEditor editor;
    const auto pump=[&](unsigned begin,unsigned end){for(unsigned i=begin;i<end;++i){wire->now=i*20;client.Tick(wire->now);client.SyncEditor(editor,wire->now);}};
    pump(0,160);CHECK(client.Connected());CHECK(client.AudioTransportReady());CHECK(client.MidiDelayReady());
    CHECK(editor.Ready());if(editor.Ready())CHECK_EQ(editor.Draft()->active_rows,3);CHECK_EQ(wire->midi.NoteDelayUs(),82000u);
    CHECK(wire->Sent("BTAUDIO1 SET 2 bridge 1"));CHECK(wire->Sent("BTMIDIDELAY1 GET 1 "));
    CHECK(wire->Sent("BTMIDIDELAY1 SET 1 "));CHECK(wire->Sent("BTPATTERN1 GET 2 "));
    CHECK(wire->Sent("BTCLOCK1 SET 1 "));CHECK(wire->claims>0);CHECK(wire->clock_sets>0);CHECK(wire->rejected.empty());
    wire->healthy=false;pump(160,161);CHECK(!client.Connected());pump(161,400);CHECK(client.Connected());
    CHECK(client.AudioTransportReady());CHECK(client.MidiDelayReady());CHECK(wire->hellos>=2);CHECK(wire->claims>=2);
    CHECK(wire->lineage.empty());CHECK(!wire->persisted);CHECK_EQ(wire->pattern.active_rows,3);
    CHECK(!wire->Sent("BTTEST1 START"));CHECK(!wire->Sent("BTSTORE1 "));CHECK(wire->rejected.empty());
    CHECK(wire->Sent("BTPREVIEW1 GET 1 "));
    // The same established connection still refuses every tune-edit/storage verb.
    for(const auto line:mutations)wire->Dispatch(line);
    CHECK_EQ(wire->rejected.size(),sizeof(mutations)/sizeof(*mutations));CHECK_EQ(wire->pattern.active_rows,3);
}

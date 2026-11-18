#include <map>
#include "test_framework.h"
#include "ui/bringup_serial.h"
#include "ui/bringup_controls.h"
#include "ui/host_logging.h"
#include "ui/host_config.h"
#ifdef BROTRACKER_TEST_HOST_INPUT_ROUTING
#include "ui/windows/input.h"
#include "ui/arkos/input.h"
#endif
#include "bringup/pattern_file_store.h"
#include "bringup/pattern_control.h"
#include "bringup/variable_rate_player_adapter.h"
#include "bringup/storage_ready_wait.h"
#include "bringup/master_volume_protocol.h"
#include "bringup/pattern_preview.h"
#include "bringup/instrument_identity.h"
#include "bringup/instrument_commit.h"
#include "bringup/live_instrument_visit.h"
#include <string>
#include <vector>
#include <algorithm>
using namespace BroTracker;
namespace {
struct StoragePeer:SerialTransport {
    std::string rx,tx;std::vector<std::string> commands;bool healthy=true,cap=true,respond=true,silent=false;unsigned pattern=1,draft_pattern=3;std::string result="ok",state="IDLE";
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
    bool define_cap=false,reject_define=false,hold_define=false;std::string define_reply;InstrumentDefinitions definitions;VariableRateSampleBindings definition_bindings=SelectDevelopmentFixture({}).bindings;
    bool collection_cap=false,hold_collection=false;unsigned collection_count=DevelopmentInstrumentCount();
    bool hold_identity=false;std::string identity_reply;
    bool facts_cap=false;BroTracker::InstrumentAvailability facts_availability=BroTracker::InstrumentAvailability::Ready;
    bool metadata_cap=false,identity_cap=false;std::string saved_view;
    bool volume_cap=false,hold_volume=false;unsigned volume=73,volume_id=0;std::string volume_reply;
    std::string VolumeState() const {return "BTVOL1 STATE 1 "+std::to_string(volume_id)+" "+std::to_string(volume_id+1)+" "+std::to_string(volume)+" "+std::to_string((volume*65536u+50)/100)+"\n";}
    unsigned working_note=0;
    bool mute_cap=false,hold_mute=false;unsigned mute_mask=0,mute_id=0;std::string mute_reply;
    bool reject_preview=false;std::string preview_reject_reason="unknown-instrument";
    bool hold_preview_response=false;std::string preview_response;
    bool pattern_save_cap=false,simulate_working=false,have_saved=false;
    RealtimePattern simulated_working{},saved_pattern{};
    PatternPublication simulated_preview;
    std::uint32_t simulated_revision=0;
    std::string pattern_save_result="ok",pattern_save_reply;bool hold_pattern_save=false;
    bool path_cap=false;
    bool relink_cap=false,replace_cap=false,live_type_cap=false,live_edit_cap=false;
    LiveInstrumentVisit visit;bool hold_prepare=false,edit_pending=false;InstrumentDefineRequest pending_edit;
    VariableRateSampleBinding live_prepared;
    void EditReply(unsigned id,unsigned result){const auto reply="BTINS1 REPLACED 1 "+std::to_string(id)+" "+std::to_string(result)+" "+std::to_string(definitions.Count())+"\n";if(hold_define)define_reply+=reply;else rx+=reply;}
    void ApplyEdit(const InstrumentDefineRequest& request) {
        const auto result=visit.Cancelled(request.request)?7u:reject_wav && request.type==1?6u:
            ReplaceInstrumentDefinition(definitions,definition_bindings,request,live_prepared,true,[&](const auto& b){return !definition_player || definition_player->ReplaceLiveInstrumentBinding(b);});
        EditReply(request.request,result);
    }
    void CompleteEdit(){CHECK(edit_pending);edit_pending=false;ApplyEdit(pending_edit);}
    VariableRatePatternPlayer* definition_player=nullptr;
    std::map<std::string,unsigned> wav_directory_errors;std::vector<std::string> empty_wav_directories;
    bool wav_cap=false,reject_wav=false,browser_stereo=false;
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
    unsigned write_limit=UINT32_MAX;bool zero_write_once=false;
    int Write(const char* p,unsigned n) override {
        if(zero_write_once){zero_write_once=false;return 0;}
        n=std::min(n,write_limit);
        tx.append(p,n);auto end=tx.find('\n');if(end==std::string::npos)return n;
        const auto line=tx.substr(0,end);tx.erase(0,end+1);commands.push_back(line);
        if(silent)return n; // No incoming bytes, including periodic STATUS replies.
        if(line=="BTTEST1 HELLO" || line=="BTTEST1 STATUS") {
            rx+="BTTEST1 STATE "+state+"\n";
            rx+=std::string("BTPATTERN1 POS 2 ")+(state=="PLAYING"?"1":"0")+(configured_rows?" 0 0 0 0 "+std::to_string(configured_rows)+" 8\n":pattern==5?" 0 0 0 0 16 3\n":" 0 0 0 0 32 8\n");
            if(line=="BTTEST1 HELLO")rx+="BTAUDIO1 CAP 1\nBTMIDI1 CAP 1\nBTPATTERN1 SNAPCAP 2\n"+(cap?std::string("BTSTORE1 CAP 1\nBTSTORE1 SESSIONCAP 2\n"):std::string{});
            if(line=="BTTEST1 HELLO" && metadata_cap)rx+="BTSESSION1 CAP 1\nBTSESSION1 VIEW 1 19 6 1 0\n";
            if(line=="BTTEST1 HELLO" && identity_cap)rx+="BTINS1 CAP 1\n";
            if(line=="BTTEST1 HELLO" && facts_cap)rx+="BTINS1 CAP 2\n";
            if(line=="BTTEST1 HELLO" && collection_cap)rx+="BTINS1 COLLECTIONCAP 1\n";
            if(line=="BTTEST1 HELLO" && define_cap)rx+="BTINS1 DEFINECAP 1\n";
            if(recovery_publication)rx+="BTPUBLISH1 CAP 1\nBTPUBLISH1 NEXT 1 1\n";
            if(line=="BTTEST1 HELLO" && !boot_session.empty())rx+="BTSTORE1 NEWCAP 1\nBTSTORE1 RESULT 1 session-boot "+boot_session+" 0\n";
            if(line=="BTTEST1 HELLO" && pattern_save_cap)rx+="BTSTORE1 PATTERNSAVECAP 1\n";
            if(line=="BTTEST1 HELLO" && replace_cap)rx+="BTINS1 REPLACECAP 1\n";
            if(line=="BTTEST1 HELLO" && live_type_cap)rx+="BTINS1 LIVETYPECAP 1\n";
            if(line=="BTTEST1 HELLO" && live_edit_cap)rx+="BTINS1 LIVEEDITCAP 1\n";
            if(line=="BTTEST1 HELLO" && relink_cap)rx+="BTINS1 RELINKCAP 1\n";
            if(line=="BTTEST1 HELLO" && path_cap)rx+="BTPATH1 CAP 1 255 8192\n";
            if(line=="BTTEST1 HELLO" && wav_cap)rx+="BTINS1 WAVCAP 1\nBTINS1 WAVBROWSECAP 2\n";
            if(line=="BTTEST1 HELLO" && list_cap)rx+="BTSTORE1 LISTCAP 1\n";
            if(line=="BTTEST1 HELLO" && !boot_sd.empty())rx+="BTSTORE1 SDCAP 1\nBTSTORE1 SD 1 "+boot_sd+"\n";
            if(mute_cap){if(line=="BTTEST1 HELLO")rx+="BTMUTE1 CAP 1\n";rx+="BTMUTE1 STATE 1 "+std::to_string(mute_id)+" "+std::to_string(mute_id+1)+" "+std::to_string(mute_mask)+"\n";}
            if(volume_cap) {if(line=="BTTEST1 HELLO")rx+="BTVOL1 CAP 1\n";rx+=VolumeState();}
            if(line=="BTTEST1 HELLO" && readiness_cap)rx+="BTSTORE1 READYCAP 1\n";
            if(preview_cap) {
                if(line=="BTTEST1 HELLO")rx+="BTPREVIEW1 CAP 1\nBTPREVIEW1 DELTACAP 1\n";
                rx+="BTPREVIEW1 NEXT 1 "+std::to_string(simulate_working?simulated_preview.NextIdentity():1)+"\nBTPREVIEW1 STATE 1 1 "+std::to_string(simulated_revision)+" 0\n";
            }
        }else if(line=="BTAUDIO1 SET 1 default")rx+="BTAUDIO1 SELECTED 1 bridge\n";
        else if(line=="BTMIDI1 SET 1 12")rx+="BTMIDI1 SELECTED 1 12\n";
        else if(line.rfind("BTMUTE1 SET 1 ",0)==0) {
            CHECK_EQ(std::sscanf(line.c_str(),"BTMUTE1 SET 1 %u %u",&mute_id,&mute_mask),2);
            const auto reply="BTMUTE1 STATE 1 "+std::to_string(mute_id)+" "+std::to_string(mute_id+1)+" "+std::to_string(mute_mask)+"\n";
            if(hold_mute)mute_reply+=reply;else rx+=reply;
        }
        else if(line=="BTMUTE1 GET 1")rx+="BTMUTE1 STATE 1 "+std::to_string(mute_id)+" "+std::to_string(mute_id+1)+" "+std::to_string(mute_mask)+"\n";
        else if(line=="BTVOL1 GET 1")rx+=VolumeState();
        else if(line.rfind("BTVOL1 SET 1 ",0)==0) {
            MasterVolumeMessage m;CHECK(ParseMasterVolume(line.data(),line.size(),m));volume=m.percent;volume_id=m.id;
            const auto reply="BTVOL1 SUBMITTED 1 "+std::to_string(m.id)+"\nBTVOL1 APPLIED 1 "+std::to_string(m.id)+"\n"+VolumeState();
            if(hold_volume)volume_reply+=reply;else rx+=reply;
        }
        else if(line.rfind("BTSTORE1 WAVOPEN ",0)==0) {
            WavOpenRequest request;CHECK(ParseWavOpen(line.data(),line.size(),request));const auto id=std::to_string(request.request);
            std::string reply="BTSTORE1 WAVDIR 3 "+id+" "+request.directory+"\nBTSTORE1 WAVINFO 3 "+id+" 0\nBTSTORE1 WAVITEM 3 "+id+" F selected.wav\nBTSTORE1 WAVMETA 3 "+id+" 0 0 1 1 16 44100\nBTSTORE1 WAVEND 3 "+id+" 32 1 1\n";
            if(browser_stereo)reply="BTSTORE1 WAVDIR 3 "+id+" "+request.directory+"\nBTSTORE1 WAVINFO 3 "+id+" 0\nBTSTORE1 WAVITEM 3 "+id+" F selected.wav\nBTSTORE1 WAVMETA 3 "+id+" 0 0 1 1 16 44100\nBTSTORE1 WAVITEM 3 "+id+" F stereo.wav\nBTSTORE1 WAVMETA 3 "+id+" 1 10 1 2 16 44100\nBTSTORE1 WAVEND 3 "+id+" 64 1 2\n";
            if(const auto failure=wav_directory_errors.find(request.directory);failure!=wav_directory_errors.end())
                reply="BTSTORE1 WAVERROR 3 "+id+" "+std::to_string(failure->second)+"\n";
            else if(std::find(empty_wav_directories.begin(),empty_wav_directories.end(),request.directory)!=empty_wav_directories.end())
                reply="BTSTORE1 WAVDIR 3 "+id+" "+request.directory+"\nBTSTORE1 WAVINFO 3 "+id+" 0\nBTSTORE1 WAVEND 3 "+id+" 0 1 0\n";
            if(request.version==4){
                for(const auto word:{"WAVDIR","WAVINFO","WAVITEM","WAVMETA","WAVEND","WAVERROR"}){
                    const auto from=std::string("BTSTORE1 ")+word+" 3 ",to=std::string("BTSTORE1 ")+word+" 4 ";
                    std::size_t at=0;while((at=reply.find(from,at))!=std::string::npos){reply.replace(at,from.size(),to);at+=to.size();}
                }
                char token[kPathProtocolCapacity];CHECK(EncodePathToken(request.directory,token,sizeof(token)));
                const auto marker=std::string("BTSTORE1 WAVDIR 4 ")+id+" ";const auto at=reply.find(marker);
                if(at!=std::string::npos){const auto end=reply.find('\n',at);reply.replace(at+marker.size(),end-at-marker.size(),token);}
            }
            if(hold_list)list_reply=reply;else rx+=reply;
        }
        else if(line.rfind("BTSTORE1 WAVLIST ",0)==0) {
            std::uint32_t fields[3]{};CHECK(SnapshotFields(line.data(),line.size(),"BTSTORE1 WAVLIST ",fields,3));
            std::string reply="BTSTORE1 WAVFILE 1 "+std::to_string(fields[1])+" selected.wav\nBTSTORE1 WAVEND 1 "+std::to_string(fields[1])+" 32 1\n";
            if(hold_list)list_reply=reply;else rx+=reply;
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
                if(reject_preview) {
                    CHECK(simulated_preview.Freeze());simulated_preview.Finish();
                    const auto id=std::to_string(message.fields[0]);rx+="BTPREVIEW1 SUBMITTED 1 "+id+"\nBTPREVIEW1 REJECTED 1 "+id+" "+preview_reject_reason+"\n";return n;
                }
                CHECK(simulated_preview.Freeze());simulated_working=*simulated_preview.Frozen();simulated_revision=message.fields[0];simulated_preview.Finish();
                const auto id=std::to_string(simulated_revision);
                const auto reply="BTPREVIEW1 SUBMITTED 1 "+id+"\nBTPREVIEW1 ACCEPTED 1 "+id+"\nBTPREVIEW1 APPLIED 1 "+id+"\nBTPREVIEW1 STATE 1 1 "+id+" 0\n";
                if(hold_preview_response)preview_response+=reply;else rx+=reply;
            }
        }
        else if(line.rfind("BTSTORE1 PATTERNSAVE 1 ",0)==0) {
            if(pattern_save_result=="ok") {saved_pattern=simulated_working;have_saved=true;}
            const auto reply="BTSTORE1 RESULT 1 pattern-save-"+line.substr(sizeof("BTSTORE1 PATTERNSAVE 1 ")-1)+" "+pattern_save_result+" 0\n";
            if(hold_pattern_save)pattern_save_reply=reply;else rx+=reply;
        }
        else if(line.rfind("BTINS1 VISIT BEGIN ",0)==0) {
            std::uint32_t f[2]{};CHECK(SnapshotFields(line.data(),line.size(),"BTINS1 VISIT BEGIN ",f,2));
            CHECK(visit.Begin(f[1],definitions,definition_bindings));rx+="BTINS1 VISIT READY 1 "+std::to_string(f[1])+"\n";
        }
        else if(line.rfind("BTINS1 VISIT END ",0)==0) {
            std::uint32_t f[2]{};CHECK(SnapshotFields(line.data(),line.size(),"BTINS1 VISIT END ",f,2));CHECK(!edit_pending);CHECK(visit.End(f[1]));rx+="BTINS1 VISIT ENDED 1 "+std::to_string(f[1])+"\n";
        }
        else if(line.rfind("BTINS1 CANCEL ",0)==0) {
            std::uint32_t f[2]{};CHECK(SnapshotFields(line.data(),line.size(),"BTINS1 CANCEL ",f,2));visit.Cancel(f[1]);
            if(edit_pending && visit.Cancelled(pending_edit.request)){edit_pending=false;EditReply(pending_edit.request,7);}
        }
        else if(line.rfind("BTINS1 RESTORE ",0)==0) {
            std::uint32_t f[3]{};CHECK(SnapshotFields(line.data(),line.size(),"BTINS1 RESTORE ",f,3));
            const auto result=visit.Restore(f[2],definitions,definition_bindings,[&](unsigned id,const VariableRateSampleBinding* b){return !definition_player || (b?definition_player->ReplaceLiveInstrumentBinding(*b):definition_player->RemoveLiveInstrumentBinding(id));});
            EditReply(f[1],result);
        }
        else if(line.rfind("BTINS1 EDIT ",0)==0) {
            InstrumentDefineRequest request;CHECK(ParseInstrumentEdit(line.data(),line.size(),request));CHECK(visit.Active());
            if(hold_prepare && request.type==1 && !visit.Cancelled(request.request)){pending_edit=request;edit_pending=true;}
            else ApplyEdit(request);
        }
        else if(line.rfind("BTINS1 DEFINE ",0)==0 || line.rfind("BTINS1 RELINK ",0)==0 || line.rfind("BTINS1 REPLACE ",0)==0) {
            const bool relink=line.rfind("BTINS1 RELINK ",0)==0,replace=line.rfind("BTINS1 REPLACE ",0)==0;
            InstrumentDefineRequest request;CHECK(replace?ParseInstrumentReplace(line.data(),line.size(),request):relink?ParseInstrumentRelink(line.data(),line.size(),request):ParseInstrumentDefine(line.data(),line.size(),request));
            static const std::int16_t pcm[32]={1,2,3};
            VariableRateSampleBinding prepared;prepared.reference_note=60;prepared.sample={pcm,32,44100};
            const auto result=reject_wav && request.type==1?6u:replace?ReplaceInstrumentDefinition(definitions,definition_bindings,request,prepared,!reject_define,[&](const auto& binding){return !definition_player || definition_player->ReplaceLiveInstrumentType(binding);}):relink?RelinkSampleDefinition(definitions,definition_bindings,request,prepared,!reject_define,[](const auto&){return true;}):CommitInstrumentDefinition(definitions,definition_bindings,request,!reject_define,[](const auto&){return true;},request.type==1 && wav_cap?&prepared:nullptr);
            if(!result)collection_count=definitions.Count();
            const auto reply=std::string(replace?"BTINS1 REPLACED 1 ":relink?"BTINS1 RELINKED 1 ":"BTINS1 DEFINED 1 ")+std::to_string(request.request)+" "+std::to_string(result)+" "+std::to_string(definitions.Count())+"\n";
            if(hold_define)define_reply=reply;else rx+=reply;
        }
        else if(line=="BTINS1 COLLECTION GET 1") {
            if(!hold_collection)rx+="BTINS1 COLLECTION STATE 1 "+std::to_string(collection_count)+"\n";
        }
        else if(line.rfind("BTINS1 GET 2 ",0)==0 || line.rfind("BTINS1 GET 3 ",0)==0) {
            unsigned id=0;if(ParseInstrumentFactsRequest(line.data(),line.size(),id)) {
                const auto* definition=define_cap?definitions.At(id):DevelopmentInstrumentDefinition(id);
                auto availability=definition && definition->type==InstrumentType::MIDI?InstrumentAvailability::ExternalUnknown:definition && definition->type==InstrumentType::Synth?InstrumentAvailability::Ready:facts_availability;
                char reply[kPathProtocolCapacity];if(FormatInstrumentFacts(InstrumentDefinitionFacts(id,definition,availability),reply,sizeof(reply),path_cap?3:2)) {if(hold_identity)identity_reply+=reply;else rx+=reply;}
            }
        }
        else if(line.rfind("BTINS1 GET 1 ",0)==0) {
            unsigned id=0;
            BroTracker::VariableRateSampleBindings bindings;bindings.count=3;
            bindings.bindings[0].instrument=0;bindings.bindings[0].type=BroTracker::InstrumentType::Sample;
            bindings.bindings[1].instrument=1;bindings.bindings[1].type=BroTracker::InstrumentType::MIDI;
            bindings.bindings[2].instrument=2;bindings.bindings[2].type=BroTracker::InstrumentType::Synth;
            if(BroTracker::ParseInstrumentIdentityRequest(line.data(),line.size(),id)) {
                const auto reply="BTINS1 STATE 1 "+std::to_string(id)+" "+std::to_string(BroTracker::InstrumentBindingIdentity(bindings,id))+"\n";
                if(hold_identity)identity_reply+=reply;else rx+=reply;
            }
        }
        else if(line.rfind("BTSTORE1 SAVE 2 ",0)==0) {
            saved_view=line.substr(16);
            const auto reply="BTSTORE1 RESULT 1 save "+result+" 0\n";
            if(hold_save)save_reply+=reply;else rx+=reply;
        }
        else if(line=="BTSTORE1 SAVE 1" || line=="BTSTORE1 DRAFTSAVE 1") {
            if(line=="BTSTORE1 SAVE 1" && cleanup_busy) {rx+="BTSTORE1 RESULT 1 save busy-or-not-ready 0\n";return n;}
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
            else if(!hold_stop){state="IDLE";rx+="BTTEST1 STOPPED\n";}
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
TEST_CASE(FailedDraftLoadRetainsWorkAndExitStopsBeforeReportingUnsaveableEdits) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
    BringUpSerial serial(nullptr,std::move(peer));serial.SetStartupPattern("patern4");LocalPatternEditor editor;
    Pump(serial,0,90,&editor);CHECK(editor.Apply(EditorAction::SetValue,75));serial.SyncEditor(editor,1801);
    const auto draft=*editor.Draft(),baseline=editor.Baseline()->pattern;
    wire->result="invalid";CHECK(serial.RequestFile(BringUpSerial::FileAction::Load,1802));Pump(serial,91,170,&editor);
    CHECK(SamePattern(draft,*editor.Draft()));CHECK(SamePattern(baseline,editor.Baseline()->pattern));CHECK(std::strstr(serial.PublicationStatus(),"failed"));
    wire->result="deferred-playing";wire->state="PLAYING";wire->rx+="BTTEST1 STATE PLAYING\n";Pump(serial,170,180,&editor);
    const auto count=wire->commands.size();serial.BeginExit(3601);Pump(serial,181,230,&editor);CHECK(serial.ExitReady());
    CHECK(std::find(wire->commands.begin()+count,wire->commands.end(),"BTTEST1 STOP")!=wire->commands.end());
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
    controls.Request(BringUpAction::Exit,"fixture",1902);controls.Tick(1903);CHECK(controls.Quit());CHECK_EQ(wire->commands.size(),count);
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
    serial.BeginExit(3601);serial.Tick(3602);CHECK(serial.ExitReady());
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
    BringUpControls controls(serial,nullptr);controls.Request(BringUpAction::Exit,"delayed sync",1401);controls.Tick(1402);CHECK(controls.Quit());
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
        controls.Request(BringUpAction::BrowserDown,"skip parent",0);controls.Request(BringUpAction::BrowserConfirm,"select",2401);
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

TEST_CASE(SingleScreenRequestWaitsForPostStopReadinessAndAutosaveThenCurrentSessionSave) {
    for(bool failure:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->hold_stop=wire->hold_save=true;
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;Pump(serial,0,90,&editor);
        wire->state="PLAYING";wire->rx+="BTTEST1 STATE PLAYING\n";serial.Tick(1800);
        BringUpControls controls(serial,nullptr);
        const auto drive=[&](unsigned first,unsigned last){for(unsigned i=first;i<last;++i){wire->now=i*20;wire->AudioService();serial.SyncEditor(editor,i*20);controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
        const auto count=[&](const char* line){return std::count(wire->commands.begin(),wire->commands.end(),line);};
        controls.Request(BringUpAction::ScreenLeft,"single navigation request",1801);
        drive(91,95);CHECK_EQ(count("BTTEST1 STOP"),1);CHECK_EQ(count("BTSTORE1 SAVE 1"),0);
        wire->cleanup_busy=true;wire->state="IDLE";wire->rx+="BTTEST1 STOPPED\n";drive(95,105);
        CHECK(controls.ScreenPending());CHECK(!controls.LoadSaveScreen());
        CHECK_EQ(count("BTSTORE1 READY 1"),1);CHECK_EQ(count("BTSTORE1 SAVE 1"),0);
        wire->rx+="BTSTORE1 RESULT 1 session-auto ok 0\n";drive(105,108);
        CHECK(controls.ScreenPending());CHECK(!controls.LoadSaveScreen()); // Autosave is not an ACK for this request.
        wire->cleanup_busy=false;drive(108,120);
        CHECK(controls.ScreenPending());CHECK_EQ(count("BTSTORE1 SAVE 1"),1);CHECK(!controls.LoadSaveScreen());
        if(failure)wire->rx+="BTSTORE1 RESULT 1 save io 0\n";
        else wire->rx+=wire->save_reply;
        wire->save_reply.clear();drive(120,130);
        CHECK_EQ(controls.LoadSaveScreen(),!failure);CHECK(!controls.ScreenPending());CHECK_EQ(count("BTTEST1 STOP"),1);
        if(failure)CHECK(std::string(controls.ScreenStatus()).find("failed")!=std::string::npos);
    }
}

TEST_CASE(PausedInstrumentEntryRetainsStopSaveAndCapturedContext) {
    for(bool failure:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->hold_stop=wire->hold_save=true;wire->metadata_cap=true;
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;Pump(serial,0,90,&editor);
        editor.RestoreCursor({0,0,EditField::Note,false});serial.SyncEditor(editor,1799);
        wire->state="PAUSED";wire->rx+="BTTEST1 STATE PAUSED\n";serial.Tick(1800);
        BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);const auto source=editor.Cursor();const auto before=*editor.Draft();
        const auto drive=[&](unsigned first,unsigned last){for(unsigned i=first;i<last;++i){wire->now=i*20;wire->AudioService();serial.SyncEditor(editor,i*20);controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
        const auto count=[&](const char* line){return std::count(wire->commands.begin(),wire->commands.end(),line);};
        controls.Request(BringUpAction::ScreenUp,"single navigation request",1801);
        editor.RestoreCursor({4,4,EditField::Instrument,false});
        controls.Request(BringUpAction::ScreenUp,"coalesce",1802);
        drive(91,95);CHECK_EQ(count("BTTEST1 STOP"),1);CHECK_EQ(count("BTSTORE1 SAVE 2 0 0 0 0"),0);
        wire->cleanup_busy=true;wire->state="IDLE";wire->rx+="BTTEST1 STOPPED\n";drive(95,105);
        CHECK(controls.ScreenPending());CHECK(!controls.LoadSaveScreen());
        CHECK_EQ(count("BTSTORE1 READY 1"),1);CHECK_EQ(count("BTSTORE1 SAVE 2 0 0 0 0"),0);
        wire->rx+="BTSTORE1 RESULT 1 session-auto ok 0\n";drive(105,108);
        CHECK(controls.ScreenPending());CHECK(!controls.LoadSaveScreen()); // Autosave is not an ACK for this request.
        wire->cleanup_busy=false;drive(108,120);
        CHECK(controls.ScreenPending());CHECK_EQ(count("BTSTORE1 SAVE 2 0 0 0 0"),1);CHECK(!controls.LoadSaveScreen());
        if(failure)wire->rx+="BTSTORE1 RESULT 1 save io 0\n";
        else wire->rx+=wire->save_reply;
        wire->save_reply.clear();drive(120,130);
        CHECK_EQ(controls.InstrumentScreen(),!failure);CHECK(!controls.ScreenPending());CHECK_EQ(count("BTTEST1 STOP"),1);
        if(!failure) {
            CHECK_EQ(controls.InstrumentNumber(),unsigned(before.cells[source.row][source.channel].instrument)+1);
            for(unsigned n=0;n<110;++n)controls.Request(BringUpAction::ScreenRight,"next",2700+n);
            CHECK_EQ(controls.InstrumentNumber(),unsigned(before.cells[source.row][source.channel].instrument)+1);
            for(unsigned n=0;n<110;++n)controls.Request(BringUpAction::ScreenLeft,"previous",2900+n);
            CHECK_EQ(controls.InstrumentNumber(),unsigned(before.cells[source.row][source.channel].instrument)+1);CHECK_EQ(count("BTSTORE1 SAVE 2 0 0 0 0"),1);
            controls.Request(BringUpAction::ScreenDown,"Pattern",3100);CHECK(!controls.InstrumentScreen());
            CHECK_EQ(editor.Cursor().row,source.row);CHECK_EQ(editor.Cursor().channel,source.channel);CHECK(editor.Cursor().field==source.field);
            CHECK(SamePattern(before,*editor.Draft()));CHECK_EQ(count("BTTEST1 START"),0);
        }
        if(failure)CHECK(std::string(controls.ScreenStatus()).find("failed")!=std::string::npos);
    }
}

TEST_CASE(SessionViewIsRestoredOnceSavedWithMusicAndNotReplayedOnReconnect) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->metadata_cap=true;wire->identity_cap=true;
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;Pump(serial,0,100,&editor);
    CHECK(editor.Ready());CHECK_EQ(editor.Cursor().row,19u);CHECK_EQ(editor.Cursor().channel,6u);CHECK(editor.Cursor().field==EditField::Instrument);
    editor.RestoreCursor({11,3,EditField::Note,true});serial.SyncEditor(editor,2000);
    CHECK(serial.RequestFile(BringUpSerial::FileAction::Session,2001,true));Pump(serial,101,160,&editor);
    CHECK(serial.SessionSaveSucceeded());CHECK_EQ(wire->saved_view,std::string("0 3 0 1"));
    serial.InspectInstrument(3);Pump(serial,160,170,&editor);CHECK_EQ(serial.InstrumentIdentity(),std::string("Sine Synth"));
    serial.InspectInstrument(99);Pump(serial,170,180,&editor);CHECK_EQ(serial.InstrumentIdentity(),std::string("Unbound (legacy binding only)"));
    wire->healthy=false;serial.Tick(3600);CHECK_EQ(serial.InstrumentIdentity(),std::string("Unknown (identity unavailable)"));
    wire->healthy=true;Pump(serial,240,350,&editor);
    CHECK_EQ(editor.Cursor().channel,3u);CHECK(editor.Cursor().header);
    CHECK_EQ(std::count_if(wire->commands.begin(),wire->commands.end(),[](const auto& c){return c.rfind("BTMUTE1 SET",0)==0;}),0);
}

TEST_CASE(InstrumentReconnectMismatchKeepsCapturedSlotSeparateFromExplicitRecovery) {
#ifdef BROTRACKER_TEST_HOST_INPUT_ROUTING
    for(unsigned route=0;route<4;++route) { // Direct, Windows, SDL controller, verified raw hat.
#else
    for(unsigned route=0;route<1;++route) {
#endif
    for(bool cancel_disconnected:{false,true}) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
    wire->metadata_cap=wire->preview_cap=wire->simulate_working=wire->recovery_publication=true;
    MakeStoredDevelopmentPattern(wire->simulated_working,1);
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;
    BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
#ifdef BROTRACKER_TEST_HOST_INPUT_ROUTING
    WindowsScreenInput windows_screen;WindowsEditorInput windows_editor;ArkOSEditorInput handheld;
    SDL_GameControllerButtonBind bindings[SDL_CONTROLLER_BUTTON_MAX]{};
    bindings[SDL_CONTROLLER_BUTTON_BACK].bindType=SDL_CONTROLLER_BINDTYPE_BUTTON;
    bindings[SDL_CONTROLLER_BUTTON_BACK].value.button=2;
    bindings[SDL_CONTROLLER_BUTTON_START].bindType=SDL_CONTROLLER_BINDTYPE_BUTTON;
    bindings[SDL_CONTROLLER_BUTTON_START].value.button=3;
    for(auto b:{SDL_CONTROLLER_BUTTON_B,SDL_CONTROLLER_BUTTON_A}) {
        bindings[b].bindType=SDL_CONTROLLER_BINDTYPE_BUTTON;bindings[b].value.button=b==SDL_CONTROLLER_BUTTON_B?4:5;
    }
    const unsigned hats[]={SDL_HAT_UP,SDL_HAT_DOWN,SDL_HAT_LEFT,SDL_HAT_RIGHT};
    for(unsigned i=0;i<4;++i) {
        auto& binding=bindings[SDL_CONTROLLER_BUTTON_DPAD_UP+i];binding.bindType=SDL_CONTROLLER_BINDTYPE_HAT;
        binding.value.hat.hat=0;binding.value.hat.hat_mask=hats[i];
    }
    handheld.Select(7,route==3?ArkOSInputStream::VerifiedRaw:ArkOSInputStream::Controller,bindings);
#endif
    const auto request=[&](BringUpAction action,const char* source,std::uint32_t now) {
        if(route==0 || (action!=BringUpAction::ScreenUp && action!=BringUpAction::ScreenDown &&
            action!=BringUpAction::ScreenLeft && action!=BringUpAction::ScreenRight && action!=BringUpAction::PatternToggle &&
            action!=BringUpAction::InstrumentCancel && action!=BringUpAction::BrowserConfirm && action!=BringUpAction::BrowserBack)) {
            controls.Request(action,source,now);return;
        }
#ifdef BROTRACKER_TEST_HOST_INPUT_ROUTING
        const bool blocked=serial.UserActionsBlocked() || controls.PatternInputBlocked();
        if(route==1) {
            SDL_Event e{};e.type=SDL_KEYDOWN;
            e.key.keysym.sym=action==BringUpAction::ScreenUp?SDLK_UP:action==BringUpAction::ScreenDown?SDLK_DOWN:
                action==BringUpAction::ScreenLeft?SDLK_LEFT:action==BringUpAction::ScreenRight?SDLK_RIGHT:
                action==BringUpAction::InstrumentCancel?SDLK_ESCAPE:action==BringUpAction::BrowserConfirm?SDLK_RETURN:action==BringUpAction::BrowserBack?SDLK_BACKSPACE:SDLK_SPACE;
            e.key.keysym.mod=(action==BringUpAction::PatternToggle || action>=BringUpAction::BrowserUp)?KMOD_NONE:KMOD_CTRL;
            const auto handle=[&]() {
                auto mapped=windows_screen.Handle(e);LocalPatternEditor sink;
                if((!serial.UserActionsBlocked() || controls.RecoveryScreen()) && controls.BrowserInputAvailable()) {
                    const auto browser=MapWindowsBrowserAction(e);if(browser!=BringUpAction::None)mapped=browser;
                }
                if(blocked)windows_editor.Cancel();
                windows_editor.Handle(e,now,blocked?sink:editor);
                const bool allowed=!blocked || AllowWindowsBlockedAction(mapped,serial,controls) ||
                    (controls.RecoveryScreen() && controls.BrowserInputAvailable() && mapped>=BringUpAction::BrowserUp && mapped<=BringUpAction::BrowserBack);
                if(action==BringUpAction::PatternToggle && blocked)CHECK(!allowed);
                if(mapped==action && action!=BringUpAction::PatternToggle)CHECK(allowed);
                if(allowed)controls.Request(mapped,source,now);
                return mapped;
            };
            CHECK(handle()==action);e.type=SDL_KEYUP;handle();
        } else {
            const auto button=action==BringUpAction::ScreenUp?SDL_CONTROLLER_BUTTON_DPAD_UP:action==BringUpAction::ScreenDown?SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                action==BringUpAction::ScreenLeft?SDL_CONTROLLER_BUTTON_DPAD_LEFT:action==BringUpAction::ScreenRight?SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                action==BringUpAction::InstrumentCancel || action==BringUpAction::BrowserBack?SDL_CONTROLLER_BUTTON_B:action==BringUpAction::BrowserConfirm?SDL_CONTROLLER_BUTTON_A:SDL_CONTROLLER_BUTTON_START;
            const auto edge=[&](SDL_GameControllerButton b,bool down) {
                ConfigureArkOSInputBarrier(handheld,serial,controls);
                SDL_Event e{};
                if(route==3 && b>=SDL_CONTROLLER_BUTTON_DPAD_UP && b<=SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
                    e.type=SDL_JOYHATMOTION;e.jhat.which=7;e.jhat.hat=0;e.jhat.value=down?hats[b-SDL_CONTROLLER_BUTTON_DPAD_UP]:SDL_HAT_CENTERED;
                } else if(route==3) {
                    e.type=down?SDL_JOYBUTTONDOWN:SDL_JOYBUTTONUP;e.jbutton.which=7;e.jbutton.button=b==SDL_CONTROLLER_BUTTON_BACK?2:b==SDL_CONTROLLER_BUTTON_START?3:b==SDL_CONTROLLER_BUTTON_B?4:5;
                } else {e.type=down?SDL_CONTROLLERBUTTONDOWN:SDL_CONTROLLERBUTTONUP;e.cbutton.which=7;e.cbutton.button=b;}
                LocalPatternEditor sink;if(blocked)handheld.Cancel();
                const auto result=handheld.Handle(e,now,!blocked,blocked?sink:editor);
                CHECK(!result.publish && !result.restore && result.mute_channel<0 && result.file==BringUpSerial::FileAction::None);
                const bool allowed=!blocked || AllowArkOSBlockedTransport(result.transport,serial,controls);
                if(result.transport==action && action!=BringUpAction::PatternToggle)CHECK(allowed);
                if(allowed)controls.Request(result.transport,source,now);
                if(blocked)handheld.Cancel();
                return result.transport;
            };
            const bool chord=action>=BringUpAction::ScreenLeft && action<=BringUpAction::ScreenDown;
            if(chord)edge(SDL_CONTROLLER_BUTTON_BACK,true);
            const auto pressed=edge(button,true);const auto released=edge(button,false);
            if(action!=BringUpAction::PatternToggle) {CHECK(pressed==(controls.InstrumentScreen() && (action==BringUpAction::ScreenLeft || action==BringUpAction::ScreenRight || action==BringUpAction::ScreenUp)?BringUpAction::None:action));if(chord)edge(SDL_CONTROLLER_BUTTON_BACK,false);}
            else {CHECK(pressed==BringUpAction::None);CHECK(released==BringUpAction::None);}
        }
#endif
    };
    const auto drive=[&](unsigned first,unsigned last) {
        for(unsigned i=first;i<last;++i) {
            wire->now=i*20;wire->AudioService();serial.SyncEditor(editor,i*20);
            controls.Tick(i*20);serial.SyncEditor(editor,i*20);
        }
    };
    drive(0,100);CHECK(editor.Ready());
    editor.RestoreCursor({0,0,EditField::Note,false});
    CHECK(editor.Apply(EditorAction::SetValue,75));drive(100,180);
    const auto retained=*editor.Draft();const auto baseline=*editor.Baseline();const auto source=editor.Cursor();
    CHECK(SamePattern(retained,wire->simulated_working));
    request(BringUpAction::ScreenUp,"Instrument",3601);drive(181,260);
    CHECK(controls.InstrumentScreen());CHECK(!controls.RecoveryScreen());CHECK(!controls.LoadSaveScreen());
    const auto number=controls.InstrumentNumber();CHECK(number>1 && number<99);

    // Normal cancellation restores the captured context, with no assignment.
    request(BringUpAction::ScreenLeft,"browse before cancel",5190);
    editor.RestoreCursor({3,1,EditField::Instrument,false});
    request(BringUpAction::InstrumentCancel,"normal cancel",5191);
    CHECK(!controls.InstrumentScreen());CHECK(SamePattern(retained,*editor.Draft()));
    CHECK_EQ(editor.Cursor().row,source.row);CHECK_EQ(editor.Cursor().channel,source.channel);CHECK(editor.Cursor().field==source.field);
    request(BringUpAction::ScreenUp,"re-enter",5192);drive(260,300);CHECK(controls.InstrumentScreen());
    wire->healthy=false;controls.Tick(6001);serial.SyncEditor(editor,6001);
    CHECK(!serial.Connected());CHECK(controls.InstrumentScreen());
    if(cancel_disconnected) {
        request(BringUpAction::InstrumentCancel,"disconnected cancel",6002);
        CHECK(!controls.InstrumentScreen());CHECK(SamePattern(retained,*editor.Draft()));
        CHECK_EQ(editor.Cursor().row,source.row);CHECK_EQ(editor.Cursor().channel,source.channel);CHECK(editor.Cursor().field==source.field);
        CHECK(serial.UserActionsBlocked());continue;
    }
    wire->healthy=true;wire->pattern=2;wire->simulated_revision=7;
    MakeStoredDevelopmentPattern(wire->simulated_working,2);
    const auto live=wire->simulated_working;
    drive(301,480);
    CHECK(serial.ReconnectRecoveryPending());CHECK(serial.RecoveryMenuAvailable());
    CHECK(serial.UserActionsBlocked());CHECK(editor.Locked());CHECK(!SamePattern(retained,live));

    request(BringUpAction::ScreenLeft,"previous",9601);
    CHECK(controls.InstrumentScreen());CHECK(!controls.RecoveryScreen());CHECK(!controls.LoadSaveScreen());
    CHECK_EQ(controls.InstrumentNumber(),number);
    request(BringUpAction::ScreenRight,"next",9602);
    CHECK(controls.InstrumentScreen());CHECK(!controls.RecoveryScreen());CHECK(!controls.LoadSaveScreen());
    CHECK_EQ(controls.InstrumentNumber(),number);
    request(BringUpAction::ScreenDown,"Pattern",9603);
    CHECK(controls.InstrumentScreen());CHECK(!controls.RecoveryScreen());CHECK(!controls.LoadSaveScreen());
    CHECK(serial.ReconnectRecoveryPending());CHECK(serial.UserActionsBlocked());CHECK(editor.Locked());
    CHECK_EQ(editor.Cursor().row,source.row);CHECK_EQ(editor.Cursor().channel,source.channel);
    CHECK(editor.Cursor().field==source.field);CHECK_EQ(editor.Cursor().header,source.header);
    CHECK(!editor.Apply(EditorAction::SetValue,76));
    request(BringUpAction::PatternToggle,"blocked playback",9604);
    request(BringUpAction::ScreenUp,"blocked re-entry",9605);CHECK(controls.InstrumentScreen());

    CHECK(std::string(controls.ScreenStatus()).find("Return unavailable")!=std::string::npos);
    CHECK(!controls.RecoveryScreen());CHECK(!controls.LoadSaveScreen());
    request(BringUpAction::InstrumentCancel,"recovery cancel",9606);
    CHECK(!controls.InstrumentScreen());CHECK(!controls.LoadSaveScreen());
    CHECK_EQ(editor.Cursor().row,source.row);CHECK_EQ(editor.Cursor().channel,source.channel);CHECK(editor.Cursor().field==source.field);
    request(BringUpAction::ScreenLeft,"explicit recovery entry",9607);
    CHECK(controls.RecoveryScreen());CHECK(controls.LoadSaveScreen());
    CHECK(controls.BrowserText().find("retained")!=std::string::npos);
    request(BringUpAction::BrowserConfirm,"review only",9608);CHECK(controls.BrowserConfirming());
    request(BringUpAction::BrowserBack,"cancel review",9609);CHECK(controls.RecoveryScreen());CHECK(!controls.BrowserConfirming());
    request(BringUpAction::BrowserBack,"leave recovery",9610);CHECK(!controls.LoadSaveScreen());
    drive(481,540);
    CHECK(serial.ReconnectRecoveryPending());CHECK(serial.RecoveryMenuAvailable());
    CHECK(serial.UserActionsBlocked());CHECK(editor.Locked());
    CHECK(SamePattern(retained,*editor.Draft()));CHECK(SamePattern(baseline.pattern,editor.Baseline()->pattern));
    CHECK(SamePattern(live,wire->simulated_working));CHECK_EQ(wire->simulated_revision,7u);
    const auto retained_view=serial.PlaybackView(10000); // Recovery hold presents the retained baseline, not live data.
    CHECK(retained_view && retained_view->device_pattern);
    if(retained_view && retained_view->device_pattern)CHECK(SamePattern(baseline.pattern,retained_view->device_pattern->pattern));
    // Discovery/readback only after reconnect: no preview, publication, file write,
    // assignment or transport commands, even while reviewing the recovery prompt.
    const auto reconnect=std::find(wire->commands.begin()+1,wire->commands.end(),"BTTEST1 HELLO");
    CHECK(reconnect!=wire->commands.end());
    for(auto it=reconnect;it!=wire->commands.end();++it)
        CHECK(*it=="BTTEST1 HELLO" || *it=="BTTEST1 STATUS" || it->rfind("BTPATTERN1 GET ",0)==0 ||
            it->rfind("BTPREVIEW1 GET ",0)==0 || it->rfind("BTAUDIO1 SET ",0)==0 || it->rfind("BTMIDI1 SET ",0)==0 || it->rfind("BTINS1 GET ",0)==0);
    }
    }
}

TEST_CASE(InstrumentSelectorReturnAssignsCapturedCellThroughPreviewAndUndo) {
    for(auto field:{EditField::Note,EditField::Instrument})for(bool explicit_id:{false,true})for(bool browse:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
        wire->metadata_cap=wire->preview_cap=wire->simulate_working=true;
        MakeStoredDevelopmentPattern(wire->simulated_working,1);
        wire->simulated_working.cells[0][1]={NOTE_EMPTY,4};wire->simulated_working.cells[2][1]={62,static_cast<std::uint8_t>(explicit_id?2:255)};
        wire->simulated_working.note_off_timing[2][1]=NoteOffTiming::EndOfPosition;
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
        auto drive=[&](unsigned first,unsigned last){for(unsigned i=first;i<last;++i){serial.SyncEditor(editor,i*20);controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
        drive(0,100);editor.RestoreCursor({2,1,field,false});const auto before=*editor.Draft();
        controls.Request(BringUpAction::ScreenUp,"entry",2001);drive(101,180);CHECK(controls.InstrumentScreen());
        CHECK_EQ(controls.OriginalInstrumentNumber(),explicit_id?3u:0u);CHECK_EQ(controls.InstrumentNumber(),explicit_id?3u:5u);
        if(browse)controls.Request(BringUpAction::ScreenRight,"next",3600);
        CHECK(SamePattern(before,*editor.Draft()));CHECK(SamePattern(before,wire->simulated_working));
        auto assigned=before;assigned.cells[2][1].instrument=controls.InstrumentNumber()-1;
        editor.RestoreCursor({6,4,EditField::Note,false}); // Later cursor movement cannot retarget confirmation.
        controls.Request(BringUpAction::ScreenDown,"return",3601);drive(181,260);
        CHECK(!controls.InstrumentScreen());CHECK(SamePattern(assigned,*editor.Draft()));CHECK(SamePattern(assigned,wire->simulated_working));
        CHECK_EQ(editor.Cursor().row,2u);CHECK_EQ(editor.Cursor().channel,1u);CHECK(editor.Cursor().field==field);
        CHECK_EQ(editor.CanUndo(),!explicit_id);
        if(!explicit_id) {CHECK(editor.Undo());drive(261,340);CHECK(SamePattern(before,wire->simulated_working));}
        else CHECK(std::none_of(wire->commands.begin(),wire->commands.end(),[](const auto& c){return c.rfind("BTPREVIEW1 DBEGIN",0)==0;}));
    }
}
TEST_CASE(InstrumentSelectorKeepsSelectionUnderEditorBarrierAndExitDoesNotConfirm) {
    for(bool close_instead:{false,true}) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->metadata_cap=true;
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;Pump(serial,0,100,&editor);
    editor.RestoreCursor({0,0,EditField::Note,false});BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
    controls.Request(BringUpAction::ScreenUp,"entry",2001);
    for(unsigned i=101;i<180;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}
    CHECK(controls.InstrumentScreen());const auto before=*editor.Draft();
    controls.Request(BringUpAction::ScreenRight,"browse",3600);editor.SetLocked(true);
    controls.Request(BringUpAction::ScreenDown,"blocked return",3601);CHECK(controls.InstrumentScreen());CHECK(SamePattern(before,*editor.Draft()));
    CHECK(std::string(controls.ScreenStatus()).find("Return unavailable")!=std::string::npos);
    if(close_instead) {
        controls.Request(BringUpAction::Exit,"close is not assignment",3602);
        CHECK(SamePattern(before,*editor.Draft()));
    } else {
        editor.SetLocked(false);CHECK(serial.RequestFile(BringUpSerial::FileAction::Session,3602,true));
        controls.Request(BringUpAction::ScreenDown,"file barrier",3603);CHECK(controls.InstrumentScreen());CHECK(SamePattern(before,*editor.Draft()));
        for(unsigned i=181;i<260;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}
        CHECK(!serial.FilePending());controls.Request(BringUpAction::ScreenDown,"retry after barrier",5201);
        CHECK(!controls.InstrumentScreen());CHECK_EQ(editor.Draft()->cells[0][0].instrument,before.cells[0][0].instrument);
    }
    }
}

TEST_CASE(Instrument01IdentityTraversesProductionGetStateAndHostDisplayedType) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->identity_cap=true;
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;Pump(serial,0,100,&editor);
    unsigned tick=100;
    for(auto item:{std::pair<unsigned,const char*>{1,"Sample"},{2,"MIDI"},{3,"Sine Synth"},{99,"Unbound (legacy binding only)"},{1,"Sample"}}) {
        serial.InspectInstrument(item.first);Pump(serial,tick,tick+10,&editor);tick+=10;
        CHECK_EQ(serial.InstrumentIdentity(),std::string(item.second));
        const auto get="BTINS1 GET 1 "+std::to_string(item.first-1);
        CHECK(std::find(wire->commands.begin(),wire->commands.end(),get)!=wire->commands.end());
    }
}

TEST_CASE(InstrumentDefinitionsAvailabilityAndLegacyReconnectAreIndependentFacts) {
    for(auto availability:{InstrumentAvailability::Ready,InstrumentAvailability::ResourceUnavailable,InstrumentAvailability::PreparationFailed}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->identity_cap=wire->facts_cap=true;wire->facts_availability=availability;
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;Pump(serial,0,100,&editor);
        serial.InspectInstrument(1);Pump(serial,100,110,&editor);CHECK_EQ(serial.InstrumentIdentity(),std::string("Sample"));CHECK_EQ(serial.InstrumentResource(),std::string("Samples/test2.wav"));
        CHECK_EQ(serial.InstrumentAvailabilityText(),std::string(availability==InstrumentAvailability::Ready?"Prepared":availability==InstrumentAvailability::ResourceUnavailable?"Resource unavailable":"Preparation failed"));
        wire->rx+="BTINS1 STATE 1 0 2\nBTINS1 STATE 2 0 0 1 -\n";serial.Tick(2199);
        CHECK_EQ(serial.InstrumentIdentity(),std::string("Sample")); // Late legacy/invalid v2 cannot overwrite negotiated facts.
        serial.InspectInstrument(2);CHECK_EQ(serial.InstrumentResource(),std::string("Unknown"));Pump(serial,110,120,&editor);
        CHECK_EQ(serial.InstrumentIdentity(),std::string("MIDI"));CHECK_EQ(serial.InstrumentAvailabilityText(),std::string("External MIDI device unknown"));
        serial.InspectInstrument(99);Pump(serial,120,130,&editor);CHECK_EQ(serial.InstrumentIdentity(),std::string("Not defined yet"));CHECK_EQ(serial.InstrumentAvailabilityText(),std::string("Not defined yet"));
        wire->healthy=false;serial.Tick(2601);CHECK_EQ(serial.InstrumentResource(),std::string("Unknown"));CHECK_EQ(serial.InstrumentAvailabilityText(),std::string("Unknown (device unavailable)"));
        wire->facts_cap=false;wire->healthy=true;Pump(serial,160,260,&editor);
        serial.InspectInstrument(1);Pump(serial,260,270,&editor);CHECK_EQ(serial.InstrumentIdentity(),std::string("Sample"));
        CHECK_EQ(serial.InstrumentAvailabilityText(),std::string("Unknown (legacy firmware)"));CHECK_EQ(serial.InstrumentResource(),std::string("Unknown"));
        CHECK(std::find(wire->commands.begin(),wire->commands.end(),"BTINS1 GET 2 0")!=wire->commands.end());
        CHECK(std::find(wire->commands.begin(),wire->commands.end(),"BTINS1 GET 1 0")!=wire->commands.end());
    }
}

TEST_CASE(SelectedInstrumentReconnectUpgradesSplitCapabilitiesExactlyOnce) {
    for(bool upgrade:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->identity_cap=wire->facts_cap=true;
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;Pump(serial,0,100,&editor);
        serial.InspectInstrument(1);Pump(serial,100,110,&editor);CHECK_EQ(serial.InstrumentResource(),std::string("Samples/test2.wav"));
        wire->healthy=false;serial.Tick(2201);CHECK_EQ(serial.InstrumentResource(),std::string("Unknown"));
        const auto reconnect=wire->commands.size();wire->facts_cap=false;wire->hold_identity=true;wire->healthy=true;
        Pump(serial,160,260,&editor);CHECK(serial.Connected());
        const auto count=[&](const char* command){return std::count(wire->commands.begin()+reconnect,wire->commands.end(),std::string(command));};
        CHECK_EQ(count("BTINS1 GET 1 0"),1);CHECK_EQ(count("BTINS1 GET 2 0"),0);
        const auto legacy=wire->identity_reply;CHECK_EQ(legacy,std::string("BTINS1 STATE 1 0 1\n"));wire->identity_reply.clear();
        if(!upgrade) {
            wire->rx+=legacy;Pump(serial,260,270,&editor);
            CHECK_EQ(serial.InstrumentIdentity(),std::string("Sample"));CHECK_EQ(serial.InstrumentAvailabilityText(),std::string("Unknown (legacy firmware)"));
            CHECK_EQ(count("BTINS1 GET 2 0"),0);continue;
        }
        // A later read negotiates v2 after GET1 was sent but before STATE1 arrives.
        wire->facts_availability=InstrumentAvailability::ResourceUnavailable;wire->rx+="BTINS1 CAP 2\n";Pump(serial,260,270,&editor);
        CHECK_EQ(count("BTINS1 GET 2 0"),1);const auto extended=wire->identity_reply;
        CHECK_EQ(extended,std::string("BTINS1 STATE 2 0 1 3 Samples/test2.wav\n"));
        // Duplicate CAPs, the outstanding legacy reply and a different-ID STATE2
        // cannot clear/requery/replace the current selection's negotiated facts.
        wire->rx+="BTINS1 CAP 2\n"+legacy+"BTINS1 STATE 2 1 2 7 -\n";Pump(serial,270,280,&editor);
        CHECK_EQ(serial.InstrumentIdentity(),std::string("Unknown (awaiting device)"));CHECK_EQ(count("BTINS1 GET 2 0"),1);
        wire->rx+=extended;Pump(serial,280,290,&editor);
        CHECK_EQ(serial.InstrumentIdentity(),std::string("Sample"));CHECK_EQ(serial.InstrumentAvailabilityText(),std::string("Resource unavailable"));CHECK_EQ(serial.InstrumentResource(),std::string("Samples/test2.wav"));
        wire->rx+="BTINS1 CAP 2\nBTINS1 CAP 2\n";Pump(serial,290,300,&editor);
        CHECK_EQ(count("BTINS1 GET 2 0"),1);CHECK_EQ(count("BTINS1 GET 1 0"),1);
        CHECK_EQ(serial.InstrumentAvailabilityText(),std::string("Resource unavailable"));
    }
}

TEST_CASE(InstrumentCollectionValidatesCapturedSlotAndKeepsSource) {
    for(unsigned count:{1u,5u})for(unsigned source:{0u,98u,200u}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->metadata_cap=wire->identity_cap=wire->facts_cap=wire->collection_cap=true;
        wire->collection_count=count;wire->hold_collection=true;wire->facts_availability=InstrumentAvailability::ResourceUnavailable;
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
        auto drive=[&](unsigned first,unsigned last){for(unsigned i=first;i<last;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
        drive(0,100);editor.RestoreCursor({2,1,EditField::Note,false});auto project=*editor.Draft();project.cells[2][1].instrument=source;CHECK(editor.RecoverDraft(project));
        const auto before=*editor.Draft();controls.Request(BringUpAction::ScreenUp,"entry",2001);drive(101,180);CHECK(controls.InstrumentScreen());
        const auto initial=controls.InstrumentNumber();CHECK_EQ(initial,source+1);CHECK_EQ(serial.InstrumentCollectionLimit(),0u);
        controls.Request(BringUpAction::ScreenRight,"held until count",3601);CHECK_EQ(controls.InstrumentNumber(),initial);
        controls.Request(BringUpAction::ScreenDown,"held confirmation",3602);CHECK(controls.InstrumentScreen());CHECK(SamePattern(before,*editor.Draft()));
        wire->rx+="BTINS1 COLLECTION STATE 1 "+std::to_string(count)+"\n";drive(181,190);CHECK_EQ(serial.InstrumentCollectionLimit(),BroTracker::InstrumentSelectionMaximum(count));
        serial.InspectInstrument(initial);drive(190,200);
        if(source) {CHECK_EQ(serial.InstrumentIdentity(),std::string("Not defined yet"));controls.Request(BringUpAction::ScreenDown,"undefined cannot confirm",4001);CHECK(controls.InstrumentScreen());}
        else {CHECK_EQ(serial.InstrumentIdentity(),std::string("Sample"));CHECK_EQ(serial.InstrumentAvailabilityText(),std::string("Resource unavailable"));}
        controls.Request(BringUpAction::ScreenLeft,"ignored old slot chord",4002);controls.Request(BringUpAction::ScreenRight,"ignored old slot chord",4003);CHECK_EQ(controls.InstrumentNumber(),initial);
        controls.Request(BringUpAction::InstrumentCancel,"cancel",4004);CHECK(!controls.InstrumentScreen());CHECK(SamePattern(before,*editor.Draft()));
    }
}
TEST_CASE(InstrumentCollectionInvalidatesReacquiresAndKeepsCapturedSlot) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->metadata_cap=wire->collection_cap=true;
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
    auto drive=[&](unsigned first,unsigned last){for(unsigned i=first;i<last;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
    drive(0,100);CHECK_EQ(serial.InstrumentCollectionLimit(),6u);editor.RestoreCursor({0,0,EditField::Note,false});
    controls.Request(BringUpAction::ScreenUp,"entry",2001);drive(101,180);CHECK(controls.InstrumentScreen());const auto initial=controls.InstrumentNumber();
    wire->healthy=false;controls.Tick(3601);CHECK_EQ(serial.InstrumentCollectionLimit(),0u);controls.Request(BringUpAction::ScreenLeft,"disconnected hold",3602);CHECK_EQ(controls.InstrumentNumber(),initial);
    wire->hold_collection=true;wire->healthy=true;drive(200,300);CHECK_EQ(serial.InstrumentCollectionLimit(),0u);
    controls.Request(BringUpAction::ScreenRight,"reconnect hold",6001);CHECK_EQ(controls.InstrumentNumber(),initial);
    wire->rx+="BTINS1 COLLECTION STATE 1 -1\nBTINS1 COLLECTION STATE 1 100\n";drive(301,310);CHECK_EQ(serial.InstrumentCollectionLimit(),0u);
    wire->rx+="BTINS1 COLLECTION STATE 1 1\nBTINS1 COLLECTIONCAP 1\n";drive(310,320);CHECK_EQ(serial.InstrumentCollectionLimit(),2u);
    CHECK_EQ(std::count(wire->commands.begin(),wire->commands.end(),"BTINS1 COLLECTION GET 1"),2);
    controls.Request(BringUpAction::ScreenLeft,"one collection",6401);CHECK_EQ(controls.InstrumentNumber(),initial);
    controls.Request(BringUpAction::InstrumentCancel,"cancel",6402);CHECK(!controls.InstrumentScreen());
    wire->healthy=false;controls.Tick(6403);wire->collection_cap=false;wire->healthy=true;drive(350,450);CHECK_EQ(serial.InstrumentCollectionLimit(),99u);
    controls.Request(BringUpAction::ScreenUp,"legacy entry",9001);drive(451,530);CHECK(controls.InstrumentScreen());
    for(unsigned i=0;i<100;++i)controls.Request(BringUpAction::ScreenLeft,"legacy clamp",10601+i);
    CHECK_EQ(controls.InstrumentNumber(),initial);controls.Request(BringUpAction::ScreenRight,"legacy next",10702);CHECK_EQ(controls.InstrumentNumber(),initial);
}

#ifdef BROTRACKER_TEST_HOST_INPUT_ROUTING
TEST_CASE(RetiredInstrumentSlotChordsDoNotChangeWindowsOrHandheldCapturedSlot) {
    for(unsigned count:{1u,5u})for(bool windows:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->metadata_cap=wire->collection_cap=true;wire->collection_count=count;
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
        auto drive=[&](unsigned first,unsigned last){for(unsigned i=first;i<last;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
        drive(0,100);editor.RestoreCursor({0,0,EditField::Note,false});const auto before=*editor.Draft();
        controls.Request(BringUpAction::ScreenUp,"entry",2001);drive(101,180);CHECK(controls.InstrumentScreen());
        WindowsScreenInput keyboard;ArkOSEditorInput handheld;handheld.Select(7);
        const auto arrow=[&](BringUpAction action) {
            CHECK(controls.PatternInputBlocked());
            if(windows) {
                SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.mod=KMOD_CTRL;
                e.key.keysym.sym=action==BringUpAction::ScreenLeft?SDLK_LEFT:SDLK_RIGHT;
                const auto mapped=keyboard.Handle(e);CHECK(mapped==action);CHECK(AllowWindowsBlockedAction(mapped,serial,controls));
                controls.Request(mapped,"Windows filtered arrow",3601);e.type=SDL_KEYUP;keyboard.Handle(e);
            } else {
                const auto edge=[&](SDL_GameControllerButton button,bool down) {
                    ConfigureArkOSInputBarrier(handheld,serial,controls);SDL_Event e{};e.type=down?SDL_CONTROLLERBUTTONDOWN:SDL_CONTROLLERBUTTONUP;e.cbutton.which=7;e.cbutton.button=button;
                    LocalPatternEditor sink;const auto result=handheld.Handle(e,3601,false,sink);
                    if(result.transport!=BringUpAction::None) {CHECK(AllowArkOSBlockedTransport(result.transport,serial,controls));controls.Request(result.transport,"handheld filtered arrow",3601);}
                    return result.transport;
                };
                edge(SDL_CONTROLLER_BUTTON_BACK,true);const auto button=action==BringUpAction::ScreenLeft?SDL_CONTROLLER_BUTTON_DPAD_LEFT:SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
                CHECK(edge(button,true)==BringUpAction::None);edge(button,false);edge(SDL_CONTROLLER_BUTTON_BACK,false);
            }
        };
        arrow(BringUpAction::ScreenLeft);arrow(BringUpAction::ScreenLeft);arrow(BringUpAction::ScreenLeft);
        CHECK_EQ(controls.InstrumentNumber(),unsigned(before.cells[0][0].instrument)+1);arrow(BringUpAction::ScreenRight);CHECK_EQ(controls.InstrumentNumber(),unsigned(before.cells[0][0].instrument)+1);
        CHECK(SamePattern(before,*editor.Draft()));
    }
}
#endif

TEST_CASE(InstrumentEntryCancellationWorksWhileSessionSaveIsStillPending) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->metadata_cap=true;wire->hold_save=true;
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
    Pump(serial,0,100,&editor);editor.RestoreCursor({0,0,EditField::Instrument,false});const auto before=*editor.Draft();
    controls.Request(BringUpAction::ScreenUp,"entry",2001);
    for(unsigned i=101;i<180;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}
    CHECK(controls.ScreenPending());CHECK(controls.InstrumentCancellationAvailable());
#ifdef BROTRACKER_TEST_HOST_INPUT_ROUTING
    WindowsScreenInput keys;SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_ESCAPE;
    const auto action=keys.Handle(e);CHECK(AllowWindowsBlockedAction(action,serial,controls));controls.Request(action,"filtered cancel",3601);
#else
    controls.Request(BringUpAction::InstrumentCancel,"cancel",3601);
#endif
    CHECK(!controls.ScreenPending());CHECK(!controls.InstrumentScreen());CHECK(SamePattern(before,*editor.Draft()));
    wire->rx+=wire->save_reply;wire->save_reply.clear();for(unsigned i=181;i<220;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}
    CHECK(!controls.InstrumentScreen());CHECK(SamePattern(before,*editor.Draft()));
}
TEST_CASE(ConfirmedEmptyInstrumentPoolIsDifferentFromPendingCount) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->collection_cap=true;wire->collection_count=0;wire->hold_collection=true;
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;Pump(serial,0,100,&editor);
    editor.RestoreCursor({0,0,EditField::Instrument,false});CHECK(!serial.InstrumentCollectionKnown());CHECK(!editor.Apply(EditorAction::SetValue,0));
    wire->rx+="BTINS1 COLLECTION STATE 1 0\n";Pump(serial,100,110,&editor);CHECK(serial.InstrumentCollectionKnown());CHECK_EQ(serial.InstrumentCollectionSize(),0u);
    editor.RestoreCursor({0,0,EditField::Instrument,false});const auto before=*editor.Draft();CHECK(editor.Apply(EditorAction::SetValue,0));CHECK_EQ(editor.Draft()->cells[0][0].instrument,0u);
}

TEST_CASE(RejectedPreviewEntryCancelsBeforeSaveAndRetainsUndefinedWork) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->metadata_cap=wire->collection_cap=wire->preview_cap=wire->simulate_working=true;
    MakeStoredDevelopmentPattern(wire->simulated_working,1);const auto live=wire->simulated_working;
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
    auto drive=[&](unsigned first,unsigned last){for(unsigned i=first;i<last;++i){wire->now=i*20;serial.SyncEditor(editor,i*20);controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
    drive(0,100);auto project=*editor.Draft();project.cells[0][0]={60,18};CHECK(editor.RecoverDraft(project));editor.RestoreCursor({0,0,EditField::Instrument,false});wire->reject_preview=true;
    drive(100,180);CHECK(SamePattern(live,wire->simulated_working));CHECK(SamePattern(project,*editor.Draft()));
    controls.Request(BringUpAction::ScreenUp,"entry after preview rejection",3601);drive(181,200);CHECK(controls.ScreenPending());CHECK(serial.FilePending());
    CHECK(std::none_of(wire->commands.begin(),wire->commands.end(),[](const auto& c){return c.rfind("BTSTORE1 SAVE",0)==0;}));
#ifdef BROTRACKER_TEST_HOST_INPUT_ROUTING
    WindowsScreenInput input;SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_ESCAPE;const auto action=input.Handle(e);
    CHECK(AllowWindowsBlockedAction(action,serial,controls));controls.Request(action,"Escape while rejected",4001);
#else
    controls.Request(BringUpAction::InstrumentCancel,"cancel while rejected",4001);
#endif
    drive(201,210);CHECK(!controls.ScreenPending());CHECK(!serial.FilePending());CHECK(!editor.Locked());CHECK(SamePattern(project,*editor.Draft()));
    wire->reject_preview=false;CHECK(editor.AssignInstrumentAt(editor.Cursor(),4));drive(210,260);CHECK_EQ(wire->simulated_working.cells[0][0].instrument,4u);
    CHECK(std::none_of(wire->commands.begin(),wire->commands.end(),[](const auto& c){return c=="BTTEST1 START" || c.rfind("BTSTORE1 SAVE",0)==0;}));
}

TEST_CASE(InstrumentTrailingDraftReturnCommitsOnlyEligibleExplicitTypesAfterAck) {
    for(unsigned type:{0u,1u,2u,3u})for(bool reject:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->metadata_cap=wire->identity_cap=wire->facts_cap=wire->collection_cap=wire->define_cap=true;wire->reject_define=reject;wire->hold_define=true;
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
        auto drive=[&](unsigned first,unsigned last){for(unsigned i=first;i<last;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
        drive(0,100);editor.RestoreCursor({0,0,EditField::Instrument,false});CHECK(editor.Apply(EditorAction::SetValue,5));const auto original=*editor.Draft();
        controls.Request(BringUpAction::ScreenUp,"open05",2001);drive(101,180);CHECK(controls.InstrumentScreen());CHECK_EQ(controls.InstrumentNumber(),6u);
        for(unsigned i=1;i<type;++i)controls.Request(BringUpAction::InstrumentIncrease,"Right",3601);
        controls.Request(BringUpAction::ScreenDown,"confirmed return",3602);drive(181,190);
        if(type<2) {CHECK(!controls.InstrumentScreen());CHECK_EQ(serial.InstrumentCollectionSize(),5u);CHECK(wire->define_reply.empty());CHECK(SamePattern(original,*editor.Draft()));}
        else {
            CHECK(controls.InstrumentScreen());CHECK_EQ(serial.InstrumentCollectionSize(),5u);CHECK(SamePattern(original,*editor.Draft()));CHECK(serial.InstrumentCommitPending());
            wire->rx+=wire->define_reply;drive(190,200);
            CHECK_EQ(serial.InstrumentCollectionSize(),reject?5u:6u);CHECK_EQ(controls.InstrumentScreen(),reject);
            if(reject)CHECK(SamePattern(original,*editor.Draft()));
            else {auto expected=original;expected.cells[0][0].instrument=5;CHECK(SamePattern(expected,*editor.Draft()));CHECK(SamePattern(original,*editor.Draft()));}
            if(reject) {CHECK(std::string(controls.ScreenStatus()).find("rejected")!=std::string::npos);controls.Request(BringUpAction::InstrumentCancel,"discard",4001);CHECK(!controls.InstrumentScreen());}
        }
    }
}
TEST_CASE(InstrumentTrailingDraftCancellationDiscardsExplicitTypeWithoutCommit) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->metadata_cap=wire->collection_cap=wire->define_cap=true;
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
    Pump(serial,0,100,&editor);editor.RestoreCursor({0,0,EditField::Note,false});auto selected=*editor.Draft();selected.cells[0][0].instrument=5;CHECK(editor.RecoverDraft(selected));const auto before=*editor.Draft();controls.Request(BringUpAction::ScreenUp,"open",2001);
    for(unsigned i=101;i<180;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}controls.Request(BringUpAction::ScreenRight,"04",3601);controls.Request(BringUpAction::ScreenRight,"05",3602);controls.Request(BringUpAction::ScreenRight,"06",3603);
    controls.Request(BringUpAction::InstrumentIncrease,"Right",3604);controls.Request(BringUpAction::InstrumentCancel,"discard",3605);CHECK(SamePattern(before,*editor.Draft()));CHECK_EQ(serial.InstrumentCollectionSize(),5u);
    CHECK(std::none_of(wire->commands.begin(),wire->commands.end(),[](const auto& c){return c.rfind("BTINS1 DEFINE ",0)==0;}));
}

TEST_CASE(InstrumentCancelDuringCommitNeverAssignsAfterLateAcknowledgement) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->metadata_cap=wire->collection_cap=wire->define_cap=true;wire->hold_define=true;
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
    auto drive=[&](unsigned first,unsigned last){for(unsigned i=first;i<last;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
    drive(0,100);editor.RestoreCursor({0,0,EditField::Note,false});auto selected=*editor.Draft();selected.cells[0][0].instrument=5;CHECK(editor.RecoverDraft(selected));const auto original=*editor.Draft();
    controls.Request(BringUpAction::ScreenUp,"entry",2001);drive(101,180);

    controls.Request(BringUpAction::InstrumentIncrease,"explicit",3605);controls.Request(BringUpAction::ScreenDown,"commit",3606);drive(181,190);
    CHECK(serial.InstrumentCommitPending());CHECK(controls.InstrumentScreen());
    controls.Request(BringUpAction::InstrumentCancel,"cancel in flight",3801);CHECK(!controls.InstrumentScreen());CHECK(SamePattern(original,*editor.Draft()));
    wire->rx+=wire->define_reply;drive(191,210);CHECK_EQ(serial.InstrumentCollectionSize(),6u);CHECK(!controls.InstrumentScreen());CHECK(SamePattern(original,*editor.Draft()));CHECK(editor.Cursor().field==EditField::Note);
}

TEST_CASE(InstrumentDefineSilentTimeoutRequiresReconnectCountAndExplicitReturn) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
    wire->metadata_cap=wire->collection_cap=wire->define_cap=wire->facts_cap=true;wire->hold_define=true;wire->simulate_working=true;MakeStoredDevelopmentPattern(wire->simulated_working,1);wire->simulated_working.cells[0][0].instrument=5;
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
    auto drive=[&](unsigned first,unsigned last){for(unsigned i=first;i<last;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
    const auto defines=[&]{return std::count_if(wire->commands.begin(),wire->commands.end(),[](const auto& c){return c.rfind("BTINS1 DEFINE ",0)==0;});};
    drive(0,100);editor.RestoreCursor({0,0,EditField::Note,false});auto selected=*editor.Draft();selected.cells[0][0].instrument=5;CHECK(editor.RecoverDraft(selected));const auto source=editor.Cursor();const auto original=*editor.Draft();
    controls.Request(BringUpAction::ScreenUp,"open03",2001);drive(101,180);
    CHECK(controls.InstrumentScreen());
    CHECK_EQ(controls.InstrumentNumber(),6u);controls.Request(BringUpAction::InstrumentIncrease,"explicit MIDI",3605);
    controls.Request(BringUpAction::ScreenDown,"confirm",3606);drive(181,190);
    CHECK_EQ(defines(),1);CHECK_EQ(wire->definitions.Count(),6u);CHECK(serial.InstrumentCommitPending());
    const auto late_reply=wire->define_reply;CHECK(!late_reply.empty());
    wire->rx.clear();wire->silent=true;drive(190,480);CHECK(wire->rx.empty());
    controls.Tick(9605);CHECK(serial.InstrumentCommitPending());
    controls.Tick(9606);CHECK(!serial.InstrumentCommitPending());CHECK(!serial.InstrumentCommitSucceeded());
    CHECK(controls.InstrumentScreen());CHECK(SamePattern(original,*editor.Draft()));CHECK_EQ(serial.InstrumentCollectionSize(),5u);
    CHECK(std::string(controls.ScreenStatus()).find("unconfirmed")!=std::string::npos);
    controls.Request(BringUpAction::ScreenDown,"retry before reconciliation",9607);drive(481,490);
    CHECK_EQ(defines(),1);CHECK(controls.InstrumentScreen());CHECK(SamePattern(original,*editor.Draft()));
    // Neither a late success nor an unsolicited count on the old connection reconciles.
    wire->rx+=late_reply+"BTINS1 COLLECTION STATE 1 6\n";drive(490,500);
    CHECK_EQ(serial.InstrumentCollectionSize(),5u);CHECK(controls.InstrumentScreen());CHECK(SamePattern(original,*editor.Draft()));
    controls.Request(BringUpAction::ScreenDown,"retry after late reply",10001);drive(501,510);CHECK_EQ(defines(),1);
    wire->healthy=false;controls.Tick(10201);wire->silent=false;wire->hold_collection=true;wire->healthy=true;drive(520,630);
    CHECK(serial.Connected());CHECK(!serial.InstrumentCollectionKnown());CHECK(controls.InstrumentScreen());CHECK(SamePattern(original,*editor.Draft()));
    CHECK(!serial.CommitInstrument(2,nullptr,12601));CHECK_EQ(defines(),1);
    wire->rx+="BTINS1 COLLECTION STATE 1 100\n";drive(631,635);CHECK(!serial.InstrumentCollectionKnown());
    wire->rx+="BTINS1 COLLECTION STATE 1 6\n";drive(635,650);
    CHECK_EQ(serial.InstrumentCollectionSize(),6u);CHECK(serial.InstrumentCollectionKnown());CHECK(controls.InstrumentScreen());CHECK(SamePattern(original,*editor.Draft()));
    CHECK(!serial.UserActionsBlocked());wire->rx+=late_reply;drive(650,660);CHECK(controls.InstrumentScreen());CHECK(SamePattern(original,*editor.Draft()));
    controls.Request(BringUpAction::ScreenDown,"explicit reconciled return",13201);CHECK(!controls.InstrumentScreen());
    auto expected=original;expected.cells[source.row][source.channel].instrument=5;CHECK(SamePattern(expected,*editor.Draft()));CHECK_EQ(defines(),1);
    CHECK_EQ(editor.Cursor().row,source.row);CHECK_EQ(editor.Cursor().channel,source.channel);CHECK(editor.Cursor().field==source.field);
    CHECK(SamePattern(original,*editor.Draft()));
    CHECK(std::none_of(wire->commands.begin(),wire->commands.end(),[](const auto& c){return c=="BTTEST1 START" || c=="BTTEST1 CONTINUE";}));
}

TEST_CASE(InstrumentDefineUncommittedTimeoutAllowsRetryOnlyAfterFreshReconnectCount) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
    wire->collection_cap=wire->define_cap=wire->reject_define=wire->hold_define=true;
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;Pump(serial,0,100,&editor);
    CHECK(serial.CommitInstrument(2,nullptr,2001));Pump(serial,101,110,&editor);CHECK_EQ(wire->definitions.Count(),5u);
    wire->rx.clear();wire->silent=true;serial.Tick(8001);CHECK(!serial.InstrumentCommitPending());CHECK(!serial.CommitInstrument(2,nullptr,8002));
    wire->healthy=false;serial.Tick(8003);wire->silent=false;wire->hold_collection=true;wire->healthy=true;Pump(serial,450,550,&editor);
    CHECK(!serial.CommitInstrument(2,nullptr,11001));CHECK(!serial.InstrumentCollectionKnown());
    wire->rx+="BTINS1 COLLECTION STATE 1 5\n";Pump(serial,551,560,&editor);CHECK(serial.InstrumentCollectionKnown());
    wire->reject_define=wire->hold_define=false;CHECK(serial.CommitInstrument(2,nullptr,11201));Pump(serial,561,570,&editor);
    CHECK(serial.InstrumentCommitSucceeded());CHECK_EQ(serial.InstrumentCollectionSize(),6u);
    CHECK_EQ(std::count_if(wire->commands.begin(),wire->commands.end(),[](const auto& c){return c.rfind("BTINS1 DEFINE ",0)==0;}),2);
}

TEST_CASE(SamplePickerCancellationRetainsPreviousDraftAndInstrumentCancellationKeepsSource) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
    wire->metadata_cap=wire->collection_cap=wire->define_cap=wire->wav_cap=true;
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
    auto drive=[&](unsigned first,unsigned last){for(unsigned i=first;i<last;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
    drive(0,100);editor.RestoreCursor({0,0,EditField::Note,false});auto selected=*editor.Draft();selected.cells[0][0].instrument=5;CHECK(editor.RecoverDraft(selected));const auto before=*editor.Draft();
    controls.Request(BringUpAction::ScreenUp,"entry",2001);drive(101,180);

    CHECK(controls.InstrumentLoadAvailable());
    controls.Request(BringUpAction::BrowserConfirm,"Load",3606);drive(181,200);CHECK(controls.WavPicker());
    CHECK_EQ(controls.BrowserList().names.size(),2u);controls.Request(BringUpAction::BrowserCancel,"cancel picker",4001);
    CHECK(controls.InstrumentScreen());CHECK(!controls.WavPicker());CHECK(controls.InstrumentDraftResource().empty());
    controls.Request(BringUpAction::BrowserConfirm,"Load",4002);drive(201,220);
    controls.Request(BringUpAction::BrowserDown,"skip parent",0);controls.Request(BringUpAction::BrowserConfirm,"choose WAV",4401);CHECK(!controls.WavPicker());
    CHECK_EQ(controls.InstrumentDraftResource(),std::string("Samples/selected.wav"));
    controls.Request(BringUpAction::BrowserConfirm,"Load again",4402);drive(221,240);
    controls.Request(BringUpAction::BrowserBack,"B picker back",4801);CHECK(!controls.WavPicker());
    CHECK_EQ(controls.InstrumentDraftResource(),std::string("Samples/selected.wav"));
    controls.Request(BringUpAction::BrowserCancel,"Escape Instrument",4802);
    CHECK(!controls.InstrumentScreen());CHECK(SamePattern(before,*editor.Draft()));CHECK_EQ(serial.InstrumentCollectionSize(),5u);
    CHECK(std::none_of(wire->commands.begin(),wire->commands.end(),[](const auto& line){return line.rfind("BTINS1 DEFINE ",0)==0;}));
    CHECK(serial.RequestPatternLoad("patern1.btdev",4803)); // WAV browsing must not alter Pattern filename validation.
}
TEST_CASE(SelectedSampleAssignsOnlyAfterSuccessfulDefinitionAcknowledgement) {
    for(bool fail:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
        wire->metadata_cap=wire->collection_cap=wire->define_cap=wire->wav_cap=wire->hold_define=true;wire->reject_wav=fail;
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
        auto drive=[&](unsigned first,unsigned last){for(unsigned i=first;i<last;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
        drive(0,100);editor.RestoreCursor({0,0,EditField::Note,false});auto selected=*editor.Draft();selected.cells[0][0].instrument=5;CHECK(editor.RecoverDraft(selected));const auto before=*editor.Draft();
        controls.Request(BringUpAction::ScreenUp,"entry",2001);drive(101,180);

        controls.Request(BringUpAction::BrowserConfirm,"Load",3606);drive(181,200);
        controls.Request(BringUpAction::BrowserDown,"skip parent",0);controls.Request(BringUpAction::BrowserConfirm,"select",4001);controls.Request(BringUpAction::ScreenDown,"confirm definition",4002);drive(201,220);
        CHECK(controls.InstrumentScreen());CHECK(SamePattern(before,*editor.Draft()));CHECK_EQ(serial.InstrumentCollectionSize(),5u);
        CHECK(std::any_of(wire->commands.begin(),wire->commands.end(),[](const auto& line){return line.find("5 1 Samples/selected.wav")!=std::string::npos;}));
        wire->rx+=wire->define_reply;wire->define_reply.clear();drive(221,240);
        if(fail) {
            CHECK(controls.InstrumentScreen());CHECK(SamePattern(before,*editor.Draft()));CHECK_EQ(serial.InstrumentCollectionSize(),5u);
            CHECK(std::string(controls.ScreenStatus()).find("preparation failed")!=std::string::npos);
        } else {
            CHECK(!controls.InstrumentScreen());CHECK_EQ(editor.Draft()->cells[0][0].instrument,5u);CHECK_EQ(serial.InstrumentCollectionSize(),6u);
            CHECK_EQ(serial.InstrumentCollectionLimit(),7u);CHECK(SamePattern(before,*editor.Draft()));
        }
    }
}

TEST_CASE(InstrumentDefaultSampleAndFocusedTypeLoadNavigation) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->metadata_cap=wire->collection_cap=wire->define_cap=wire->wav_cap=wire->facts_cap=true;
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
    auto drive=[&](unsigned first,unsigned last){for(unsigned i=first;i<last;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
    drive(0,100);editor.RestoreCursor({0,0,EditField::Note,false});auto selected=*editor.Draft();selected.cells[0][0].instrument=5;CHECK(editor.RecoverDraft(selected));const auto before=*editor.Draft();
    controls.Request(BringUpAction::ScreenUp,"entry",2001);drive(101,180);

    CHECK_EQ(controls.InstrumentDraftType(),std::string("Sample"));CHECK(controls.InstrumentLoadAvailable());CHECK_EQ(controls.InstrumentFocus(),0);
    controls.Request(BringUpAction::InstrumentNextField,"Down",3605);CHECK_EQ(controls.InstrumentFocus(),1);
    controls.Request(BringUpAction::InstrumentIncrease,"not Type",3606);CHECK_EQ(controls.InstrumentDraftType(),std::string("Sample"));
    controls.Request(BringUpAction::InstrumentActivate,"Load without explicit Type",3607);drive(181,200);CHECK(controls.WavPicker());
    controls.Request(BringUpAction::BrowserCancel,"cancel",4001);CHECK(!controls.WavPicker());CHECK_EQ(serial.InstrumentCollectionSize(),5u);
    controls.Request(BringUpAction::InstrumentPreviousField,"Up",4002);CHECK_EQ(controls.InstrumentFocus(),0);
    controls.Request(BringUpAction::InstrumentIncrease,"edit Type",4003);CHECK_EQ(controls.InstrumentDraftType(),std::string("MIDI"));CHECK(!controls.InstrumentLoadAvailable());
    controls.Request(BringUpAction::InstrumentDecrease,"edit Type back",4004);CHECK_EQ(controls.InstrumentDraftType(),std::string("Sample"));
    controls.Request(BringUpAction::InstrumentActivate,"Type Enter has no edit",4005);CHECK_EQ(controls.InstrumentDraftType(),std::string("Sample"));
    controls.Request(BringUpAction::InstrumentCancel,"cancel",4006);CHECK(SamePattern(before,*editor.Draft()));CHECK_EQ(wire->definitions.Count(),5u);
}
TEST_CASE(DefinedUnavailableSampleRelinkConfirmationFailureAndAcknowledgementKeepReferencesAndCount) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->metadata_cap=wire->collection_cap=wire->define_cap=wire->wav_cap=wire->facts_cap=wire->relink_cap=true;
    wire->facts_availability=InstrumentAvailability::ResourceUnavailable;
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
    auto drive=[&](unsigned first,unsigned last){for(unsigned i=first;i<last;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
    drive(0,100);editor.RestoreCursor({0,0,EditField::Note,false});auto pattern=*editor.Draft();pattern.cells[0][0]={60,0};CHECK(editor.RecoverDraft(pattern));
    const auto before=*editor.Draft();controls.Request(BringUpAction::ScreenUp,"entry",2001);drive(101,180);
    serial.InspectInstrument(1);drive(181,200);CHECK_EQ(controls.InstrumentFocus(),1);CHECK(controls.InstrumentLoadAvailable());
    CHECK_EQ(controls.SavedInstrumentPath(),std::string("Samples/test2.wav"));CHECK(!controls.InstrumentLoadWarning().empty());
    controls.Request(BringUpAction::InstrumentActivate,"Load",4001);drive(201,220);controls.Request(BringUpAction::BrowserDown,"skip parent",0);controls.Request(BringUpAction::BrowserConfirm,"choose",4401);
    CHECK(controls.ReplacementConfirming());CHECK(controls.BrowserText().find("Samples/test2.wav")!=std::string::npos);CHECK(controls.BrowserText().find("Samples/selected.wav")!=std::string::npos);
    controls.Request(BringUpAction::BrowserCancel,"decline",4402);CHECK(controls.InstrumentDraftResource().empty());CHECK_EQ(controls.SavedInstrumentPath(),std::string("Samples/test2.wav"));
    controls.Request(BringUpAction::InstrumentActivate,"Load",4403);drive(221,240);controls.Request(BringUpAction::BrowserDown,"skip parent",0);controls.Request(BringUpAction::BrowserConfirm,"choose",4801);controls.Request(BringUpAction::BrowserConfirm,"accept",4802);
    CHECK(!controls.WavPicker());CHECK_EQ(controls.InstrumentDraftResource(),std::string("Samples/selected.wav"));
    wire->reject_wav=true;controls.Request(BringUpAction::ScreenDown,"failed relink",4803);drive(241,260);
    CHECK(controls.InstrumentScreen());CHECK(SamePattern(before,*editor.Draft()));CHECK_EQ(serial.InstrumentCollectionSize(),5u);CHECK_EQ(controls.SavedInstrumentPath(),std::string("Samples/test2.wav"));
    wire->reject_wav=false;wire->hold_define=true;controls.Request(BringUpAction::ScreenDown,"retry",5201);drive(261,280);
    CHECK(serial.InstrumentCommitPending());CHECK(SamePattern(before,*editor.Draft()));CHECK_EQ(serial.InstrumentCollectionSize(),5u);
    wire->rx+=wire->define_reply;wire->define_reply.clear();drive(281,300);
    CHECK(!controls.InstrumentScreen());CHECK(SamePattern(before,*editor.Draft()));CHECK_EQ(serial.InstrumentCollectionSize(),5u);
    CHECK_EQ(std::string(wire->definitions.At(0)->resource),std::string("Samples/selected.wav"));
}

TEST_CASE(SharedWavBrowserPublishesConfirmedBatchesAndExplainsEmptyMissingAndLongPaths) {
    WavBrowser browser;WavBrowsePage page;std::strcpy(page.directory,"/BroTracker");page.fallback=1;page.done=true;page.scanned=1;page.count=1;
    page.entries[0].kind=WavEntryKind::Directory;std::strcpy(page.entries[0].name,"Nested");browser.Begin(kWavDirectory);CHECK(browser.Directory().empty());CHECK(browser.Accept(page));
    CHECK_EQ(browser.Directory(),std::string("/BroTracker"));CHECK(browser.Message().find("Folder not found")!=std::string::npos);CHECK_EQ(browser.View().names[0],std::string(".."));
    std::string path;CHECK_EQ(browser.Select(path),1u);CHECK_EQ(path,std::string("/"));browser.Move(1);CHECK_EQ(browser.Select(path),1u);CHECK_EQ(path,std::string("/BroTracker/Nested"));
    browser.Begin(path);page={};std::strcpy(page.directory,path.c_str());page.done=true;CHECK(browser.Accept(page));CHECK(browser.Message().find("No supported samples")!=std::string::npos);
    browser.Begin(path);page.scanned=2;CHECK(browser.Accept(page));CHECK(browser.Message().find("No supported samples")!=std::string::npos);
    browser.Begin("/gone");browser.Fail(WavDirectoryResult::Missing);CHECK_EQ(browser.Directory(),path);CHECK(browser.Message().find("Folder not found")!=std::string::npos);CHECK_EQ(browser.View().names[0],std::string(".."));
    browser.Begin("/unreadable");browser.Fail(WavDirectoryResult::ReadError);CHECK(browser.Message().find("Unable to read folder")!=std::string::npos);CHECK_EQ(browser.Directory(),path);
    page={};std::strcpy(page.directory,"/BroTracker/New");page.count=1;page.scanned=1;std::strcpy(page.entries[0].name,"tone.wav");page.entries[0].facts={WavBufferStatus::Success,44100,1,1,16,true};browser.Begin(page.directory);CHECK(browser.Accept(page));CHECK_EQ(browser.Directory(),std::string(page.directory));CHECK(!browser.Loading());CHECK(browser.Reading()); // first correlated batch is usable immediately
    page.done=true;page.count=0;CHECK(browser.Accept(page));browser.Move(1);CHECK_EQ(browser.Select(path),2u);CHECK_EQ(path,std::string("BroTracker/New/tone.wav"));
    page={};const auto longdir="/"+std::string(248,'a');std::strcpy(page.directory,longdir.c_str());page.done=true;page.scanned=1;page.count=1;std::strcpy(page.entries[0].name,"test.wav");page.entries[0].facts={WavBufferStatus::Success,44100,1,1,16,true};browser.Begin(longdir);CHECK(browser.Accept(page));browser.Move(1);CHECK_EQ(browser.Select(path),0u);CHECK(browser.Message().find("255-byte limit")!=std::string::npos);
    page={};std::strcpy(page.directory,"/");page.done=true;browser.Begin("/");CHECK(browser.Accept(page));CHECK(browser.View().names.empty());
}

TEST_CASE(WavDirectorySerialCorrelatesPagesAndRetainsLastPageOnReadFailure) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->wav_cap=wire->hold_list=true;
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;Pump(serial,0,100,&editor);
    CHECK(serial.WavPickerAvailable());CHECK(serial.RequestWavDirectory(kWavDirectory,0,true,2001));Pump(serial,101,110,&editor);
    CHECK(serial.PatternListBusy());wire->rx+="BTSTORE1 WAVDIR 3 99 /wrong\nBTSTORE1 WAVEND 3 99 0 1 0\n";Pump(serial,111,115,&editor);CHECK(serial.PatternListBusy());
    wire->rx+="BTSTORE1 WAVDIR 3 1 /BroTracker\nBTSTORE1 WAVINFO 3 1 1\nBTSTORE1 WAVITEM 3 1 D Nested\nBTSTORE1 WAVITEM 3 1 F test.wav\nBTSTORE1 WAVMETA 3 1 1 0 1 1 16 44100\nBTSTORE1 WAVEND 3 1 64 1 2\n";Pump(serial,116,125,&editor);
    CHECK(serial.PatternListReady());CHECK_EQ(std::string(serial.WavDirectoryPage().directory),std::string("/BroTracker"));CHECK_EQ(serial.WavDirectoryPage().fallback,1u);CHECK_EQ(serial.WavDirectoryPage().count,2u);
    CHECK(serial.RequestWavDirectory("/gone",0,false,2501));Pump(serial,126,135,&editor);wire->rx+="BTSTORE1 WAVERROR 3 2 1\n";Pump(serial,136,140,&editor);
    CHECK(!serial.PatternListReady());CHECK(serial.WavDirectoryError()==WavDirectoryResult::Missing);CHECK_EQ(std::string(serial.WavDirectoryPage().directory),std::string("/BroTracker"));
    const auto longest="/"+std::string(62,'a');CHECK(serial.RequestWavDirectory(longest,4294967264u,false,2801));Pump(serial,141,150,&editor);
    CHECK(std::any_of(wire->commands.begin(),wire->commands.end(),[&](const auto& c){return c=="BTSTORE1 WAVOPEN 3 3 4294967264 0 "+longest;}));
    wire->rx+="BTSTORE1 WAVDIR 3 3 /\nBTSTORE1 WAVINFO 3 3 0\nBTSTORE1 WAVITEM 3 3 F ../bad.wav\n";Pump(serial,151,155,&editor);
    CHECK(!serial.PatternListBusy());CHECK(!serial.PatternListReady());CHECK_EQ(std::string(serial.WavDirectoryPage().directory),std::string("/BroTracker"));
}

TEST_CASE(RejectedStereoSelectionKeepsBrowserOpenAndPreviousInstrumentDraftUnchanged) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->metadata_cap=wire->collection_cap=wire->define_cap=wire->wav_cap=wire->browser_stereo=true;
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
    auto drive=[&](unsigned first,unsigned last){for(unsigned i=first;i<last;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
    drive(0,100);editor.RestoreCursor({0,0,EditField::Note,false});auto selected=*editor.Draft();selected.cells[0][0].instrument=5;CHECK(editor.RecoverDraft(selected));const auto before=*editor.Draft();
    controls.Request(BringUpAction::ScreenUp,"entry",2001);drive(101,180);

    controls.Request(BringUpAction::BrowserConfirm,"Load",3606);drive(181,200);
    controls.Request(BringUpAction::BrowserDown,"mono",4001);CHECK(controls.BrowserText().find("PCM")!=std::string::npos);controls.Request(BringUpAction::BrowserConfirm,"stage",4002);
    const auto previous=controls.InstrumentDraftResource();CHECK_EQ(previous,std::string("Samples/selected.wav"));
    controls.Request(BringUpAction::BrowserConfirm,"Load again",4003);drive(201,220);
    controls.Request(BringUpAction::BrowserDown,"mono",4401);controls.Request(BringUpAction::BrowserDown,"stereo",4402);
    CHECK(controls.BrowserText().find("Stereo WAV not supported")!=std::string::npos);controls.Request(BringUpAction::BrowserConfirm,"reject",4403);
    CHECK(controls.WavPicker());CHECK_EQ(controls.InstrumentDraftResource(),previous);CHECK(SamePattern(before,*editor.Draft()));CHECK_EQ(serial.InstrumentCollectionSize(),5u);
    CHECK(std::none_of(wire->commands.begin(),wire->commands.end(),[](const auto& line){return line.rfind("BTINS1 DEFINE ",0)==0 || line.rfind("BTINS1 RELINK ",0)==0;}));
    controls.Request(BringUpAction::BrowserCancel,"cancel",4404);CHECK(!controls.WavPicker());CHECK_EQ(controls.InstrumentDraftResource(),previous);
}

TEST_CASE(WavMetadataMustBeCorrelatedCompleteAndConsistentBeforePageAcceptance) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->wav_cap=wire->hold_list=true;
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;Pump(serial,0,100,&editor);
    CHECK(serial.RequestWavDirectory("/Samples",0,false,2001));Pump(serial,101,110,&editor);
    wire->rx+="BTSTORE1 WAVDIR 3 1 /Samples\nBTSTORE1 WAVINFO 3 1 0\nBTSTORE1 WAVITEM 3 1 F tone.wav\nBTSTORE1 WAVEND 3 1 32 1 1\n";Pump(serial,111,120,&editor);
    CHECK(!serial.PatternListReady());CHECK(!serial.PatternListBusy()); // Missing metadata cannot enable selection.
    CHECK(serial.RequestWavDirectory("/Samples",0,false,2401));Pump(serial,121,130,&editor);
    wire->rx+="BTSTORE1 WAVDIR 3 2 /Samples\nBTSTORE1 WAVINFO 3 2 0\nBTSTORE1 WAVITEM 3 2 F stereo.wav\nBTSTORE1 WAVMETA 3 2 0 0 1 2 16 44100\nBTSTORE1 WAVEND 3 2 32 1 1\n";Pump(serial,131,140,&editor);
    CHECK(!serial.PatternListReady());CHECK(!serial.PatternListBusy()); // Success must match firmware's supported layout.
    const auto long_name=std::string(230,'x')+".wav";
    CHECK(serial.RequestWavDirectory("/Samples",0,false,2801));Pump(serial,141,150,&editor);
    wire->rx+="BTSTORE1 WAVDIR 3 3 /Samples\nBTSTORE1 WAVINFO 3 3 0\nBTSTORE1 WAVITEM 3 3 F "+long_name+"\nBTSTORE1 WAVMETA 3 99 0 10 1 2 16 44100\nBTSTORE1 WAVMETA 3 3 0 0 1 1 16 44100\nBTSTORE1 WAVEND 3 3 32 1 1\n";Pump(serial,151,165,&editor);
    CHECK(serial.PatternListReady());CHECK_EQ(std::string(serial.WavDirectoryPage().entries[0].name),long_name);CHECK(serial.WavDirectoryPage().entries[0].facts.status==WavBufferStatus::Success);
}

TEST_CASE(BuiltInSynthTypeDraftRequiresPreparedWavAndSameSlotAcknowledgement) {
    for(bool cancel:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->metadata_cap=wire->collection_cap=wire->define_cap=wire->facts_cap=wire->wav_cap=wire->replace_cap=true;
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
        auto drive=[&](unsigned a,unsigned b){for(unsigned i=a;i<b;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
        drive(0,100);editor.RestoreCursor({0,0,EditField::Note,false});const auto original=*editor.Draft();CHECK_EQ(original.cells[0][0].instrument,2u);
        controls.Request(BringUpAction::ScreenUp,"entry03",2001);drive(101,180);CHECK(controls.InstrumentTypeEditable());CHECK_EQ(controls.InstrumentFocus(),0);CHECK_EQ(controls.InstrumentDraftType(),std::string("Sine Synth"));
        controls.Request(BringUpAction::ScreenLeft,"retired slot chord",3600);CHECK_EQ(controls.InstrumentNumber(),3u);
        controls.Request(BringUpAction::InstrumentIncrease,"Right",3601);CHECK_EQ(controls.InstrumentDraftType(),std::string("Sample"));CHECK(controls.InstrumentLoadAvailable());
        controls.Request(BringUpAction::ScreenDown,"no WAV",3602);CHECK(controls.InstrumentScreen());CHECK_EQ(wire->definitions.At(2)->type,InstrumentType::Synth);CHECK(SamePattern(original,*editor.Draft()));
        controls.Request(BringUpAction::InstrumentNextField,"Down",3603);CHECK_EQ(controls.InstrumentFocus(),1);controls.Request(BringUpAction::InstrumentActivate,"Load",3604);drive(181,200);
        controls.Request(BringUpAction::BrowserDown,"selected WAV",4001);controls.Request(BringUpAction::BrowserConfirm,"stage",4002);CHECK(!controls.WavPicker());
        if(cancel){controls.Request(BringUpAction::InstrumentCancel,"B",4003);CHECK(!controls.InstrumentScreen());CHECK_EQ(wire->definitions.At(2)->type,InstrumentType::Synth);CHECK(SamePattern(original,*editor.Draft()));continue;}
        wire->reject_wav=true;controls.Request(BringUpAction::ScreenDown,"failed preparation",4003);drive(201,220);
        CHECK(controls.InstrumentScreen());CHECK_EQ(wire->definitions.At(2)->type,InstrumentType::Synth);CHECK_EQ(InstrumentBindingIdentity(wire->definition_bindings,2),3u);CHECK(SamePattern(original,*editor.Draft()));CHECK_EQ(serial.InstrumentCollectionSize(),5u);
        wire->reject_wav=false;wire->hold_define=true;controls.Request(BringUpAction::ScreenDown,"retry",4401);drive(221,240);CHECK(serial.InstrumentCommitPending());CHECK(controls.InstrumentScreen());CHECK(SamePattern(original,*editor.Draft()));
        wire->rx+=wire->define_reply;wire->define_reply.clear();drive(241,260);CHECK(!controls.InstrumentScreen());CHECK_EQ(serial.InstrumentCollectionSize(),5u);CHECK_EQ(wire->definitions.At(2)->type,InstrumentType::Sample);CHECK_EQ(std::string(wire->definitions.At(2)->resource),std::string("Samples/selected.wav"));CHECK(SamePattern(original,*editor.Draft()));
    }
}

TEST_CASE(UndefinedSampleConfirmedReturnAssignsWithoutDefiningDefaultOrCycledDraft) {
    for(int cycle:{0,3,-3})for(bool handheld:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->metadata_cap=wire->collection_cap=wire->define_cap=wire->facts_cap=wire->replace_cap=true;
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
        auto drive=[&](unsigned a,unsigned b){for(unsigned i=a;i<b;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
        drive(0,100);auto source=*editor.Draft();source.cells[0][0].instrument=5;source.cells[1][0]={62,255};CHECK(editor.RecoverDraft(source));editor.RestoreCursor({1,0,EditField::Instrument,false});
        const auto before=*editor.Draft();const auto cursor=editor.Cursor();controls.Request(BringUpAction::ScreenUp,"inherited trailing06",2001);drive(101,180);
        CHECK_EQ(controls.InstrumentNumber(),6u);CHECK_EQ(controls.InstrumentDraftType(),std::string("Sample"));
        for(int i=0;i<(cycle<0?-cycle:cycle);++i)controls.Request(cycle<0?BringUpAction::InstrumentDecrease:BringUpAction::InstrumentIncrease,"cycle back to Sample",3601+i);
        CHECK_EQ(controls.InstrumentDraftType(),std::string("Sample"));CHECK(controls.InstrumentDraftResource().empty());
        BringUpAction confirm=BringUpAction::ScreenDown;
#ifdef BROTRACKER_TEST_HOST_INPUT_ROUTING
        if(handheld) {
            ArkOSEditorInput input;input.Select(7);ConfigureArkOSInputBarrier(input,serial,controls);
            SDL_Event event{};event.type=SDL_CONTROLLERBUTTONDOWN;event.cbutton.which=7;event.cbutton.button=SDL_CONTROLLER_BUTTON_BACK;CHECK(input.Handle(event,3610,true,editor).transport==BringUpAction::None);
            event.cbutton.button=SDL_CONTROLLER_BUTTON_DPAD_DOWN;confirm=input.Handle(event,3611,true,editor).transport;
            CHECK(AllowArkOSBlockedTransport(confirm,serial,controls));
        } else {
            SDL_Event event{};event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_DOWN;event.key.keysym.mod=KMOD_CTRL;confirm=MapWindowsInstrumentAction(event);
            CHECK(AllowWindowsBlockedAction(confirm,serial,controls));
        }
#endif
        CHECK(confirm==BringUpAction::ScreenDown);controls.Request(confirm,handheld?"Select+Down":"Ctrl+Down",3620);drive(182,200);
        CHECK(!controls.InstrumentScreen());CHECK(!serial.InstrumentCommitPending());CHECK_EQ(serial.InstrumentCollectionSize(),5u);CHECK_EQ(serial.InstrumentCollectionLimit(),6u);
        CHECK_EQ(wire->definitions.Count(),5u);CHECK(!wire->definitions.At(5));
        auto expected=before;expected.cells[1][0].instrument=5;CHECK(SamePattern(expected,*editor.Draft()));CHECK_EQ(editor.Cursor().row,cursor.row);CHECK_EQ(editor.Cursor().channel,cursor.channel);CHECK(editor.Cursor().field==cursor.field);
        CHECK(editor.Undo());CHECK(SamePattern(before,*editor.Draft()));CHECK(editor.Redo());CHECK(SamePattern(expected,*editor.Draft()));
        CHECK(!editor.Apply(EditorAction::SetValue,6));CHECK(SamePattern(expected,*editor.Draft())); // Display slot07 remains unavailable.
        CHECK(std::none_of(wire->commands.begin(),wire->commands.end(),[](const auto& line){return line.rfind("BTINS1 DEFINE ",0)==0 || line.rfind("BTINS1 REPLACE ",0)==0 || line.rfind("BTINS1 RELINK ",0)==0;}));
    }
}
TEST_CASE(ExistingMidiAndSynthConversionWithoutWavCannotUseUndefinedSampleReturn) {
    for(unsigned slot:{1u,2u}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->metadata_cap=wire->collection_cap=wire->define_cap=wire->facts_cap=wire->replace_cap=true;
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
        auto drive=[&](unsigned a,unsigned b){for(unsigned i=a;i<b;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
        drive(0,100);auto source=*editor.Draft();source.cells[0][0].instrument=slot;CHECK(editor.RecoverDraft(source));editor.RestoreCursor({0,0,EditField::Note,false});const auto before=*editor.Draft();
        controls.Request(BringUpAction::ScreenUp,"existing conversion",2001);drive(101,180);const auto original_type=wire->definitions.At(slot)->type;
        for(unsigned i=slot;i<3;++i)controls.Request(BringUpAction::InstrumentIncrease,"Sample draft",3601+i);
        CHECK_EQ(controls.InstrumentDraftType(),std::string("Sample"));CHECK(controls.InstrumentDraftResource().empty());
        controls.Request(BringUpAction::ScreenDown,"no WAV conversion",3610);drive(182,200);
        CHECK(controls.InstrumentScreen());CHECK_EQ(wire->definitions.At(slot)->type,original_type);CHECK_EQ(InstrumentBindingIdentity(wire->definition_bindings,slot),slot+1);CHECK_EQ(serial.InstrumentCollectionSize(),5u);CHECK(SamePattern(before,*editor.Draft()));CHECK(!serial.InstrumentCommitPending());
        CHECK(std::none_of(wire->commands.begin(),wire->commands.end(),[](const auto& line){return line.rfind("BTINS1 REPLACE ",0)==0;}));
    }
}

TEST_CASE(PausedExitStopsWaitsForReadinessSynchronizesLatestDraftAndWaitsForSaveAck) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->preview_cap=wire->simulate_working=wire->metadata_cap=true;MakeStoredDevelopmentPattern(wire->simulated_working,1);
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
    auto drive=[&](unsigned a,unsigned b){for(unsigned i=a;i<b;++i){wire->now=i*20;wire->AudioService();serial.SyncEditor(editor,i*20);controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
    drive(0,100);wire->state="PAUSED";wire->rx+="BTTEST1 STATE PAUSED\n";controls.Tick(2000);serial.SyncEditor(editor,2000);editor.RestoreCursor({0,7,EditField::Note,false});CHECK(editor.Ready());CHECK(serial.Paused());CHECK(editor.Apply(EditorAction::SetValue,75));CHECK_EQ(editor.Draft()->cells[0][7].instrument,255);serial.SyncEditor(editor,2001);const auto latest=*editor.Draft();
    wire->hold_stop=wire->hold_save=wire->cleanup_busy=true;const auto from=wire->commands.size();controls.Request(BringUpAction::Exit,"X or Ctrl+X",2002);drive(101,115);
    CHECK(!controls.Quit());CHECK_EQ(std::count(wire->commands.begin()+from,wire->commands.end(),"BTTEST1 STOP"),1);CHECK(wire->readiness.Active()==false);
    wire->state="IDLE";wire->rx+="BTTEST1 STOPPED\n";drive(115,130);CHECK(wire->readiness.Active());CHECK(wire->save_reply.empty());
    wire->cleanup_busy=false;drive(130,170);CHECK(!controls.Quit());CHECK(!wire->save_reply.empty());CHECK(SamePattern(latest,wire->simulated_working));
    const auto first=[&](const char* prefix){return std::find_if(wire->commands.begin()+from,wire->commands.end(),[&](const auto& line){return line.rfind(prefix,0)==0;})-wire->commands.begin();};
    CHECK(first("BTTEST1 STOP")<first("BTSTORE1 READY"));CHECK(first("BTSTORE1 READY")<first("BTPREVIEW1 D"));CHECK(first("BTPREVIEW1 D")<first("BTSTORE1 SAVE"));
    wire->rx+="BTSTORE1 RESULT 1 session-auto ok 0\n";drive(170,175);CHECK(!controls.Quit());wire->rx+=wire->save_reply;wire->save_reply.clear();drive(175,185);CHECK(controls.Quit());CHECK(serial.SessionSaveSucceeded());
}
TEST_CASE(ShutdownSaveErrorAndLostAcknowledgementLogFailureAndCloseWithoutReplay) {
    for(bool timeout:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->metadata_cap=true;wire->hold_save=timeout;
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
        auto drive=[&](unsigned a,unsigned b){for(unsigned i=a;i<b;++i){wire->now=i*20;wire->AudioService();controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
        drive(0,100);if(!timeout)wire->result="io";const auto before=*editor.Draft();controls.Request(BringUpAction::Exit,"save failure",2001);drive(101,130);
        if(timeout){CHECK(!controls.Quit());drive(130,900);}
        CHECK(controls.Quit());CHECK(!serial.SessionSaveSucceeded());CHECK(SamePattern(before,*editor.Draft()));
        CHECK_EQ(std::count_if(wire->commands.begin(),wire->commands.end(),[](const auto& line){return line.rfind("BTSTORE1 SAVE ",0)==0;}),1);
        CHECK(controls.ExitStatus().find(timeout?"uncertain":"io")!=std::string::npos);
    }
}

#ifdef BROTRACKER_TEST_HOST_INPUT_ROUTING
TEST_CASE(WindowsHeaderKeysUseTheAcknowledgedMutePathAndRespectInputBarrier) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->mute_cap=true;
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;Pump(serial,0,100,&editor);CHECK(editor.SelectHeader(1));const auto before=*editor.Draft();WindowsHeaderInput input;
    const auto key=[&](unsigned type,unsigned repeat,bool enabled){SDL_Event event{};event.type=type;event.key.keysym.sym=SDLK_PAGEUP;event.key.repeat=repeat;const int channel=input.Handle(event,0,0,false,enabled?&editor:nullptr);if(channel>=0){CHECK(editor.SelectHeader(channel));CHECK(serial.ToggleChannelMute(channel));}return channel;};
    wire->hold_mute=true;CHECK_EQ(key(SDL_KEYDOWN,0,true),1);Pump(serial,101,105,&editor);CHECK_EQ(wire->mute_mask,2u);CHECK(!wire->mute_reply.empty());CHECK_EQ(key(SDL_KEYDOWN,1,true),-1);CHECK_EQ(key(SDL_KEYDOWN,0,true),-1);
    CHECK_EQ(std::count_if(wire->commands.begin(),wire->commands.end(),[](const auto& line){return line.rfind("BTMUTE1 SET ",0)==0;}),1);wire->rx+=wire->mute_reply;wire->mute_reply.clear();Pump(serial,105,110,&editor);
    key(SDL_KEYUP,0,true);CHECK_EQ(key(SDL_KEYDOWN,0,false),-1);CHECK_EQ(key(SDL_KEYDOWN,1,true),-1);key(SDL_KEYUP,0,true);CHECK_EQ(key(SDL_KEYDOWN,0,true),1);Pump(serial,111,115,&editor);CHECK_EQ(wire->mute_mask,0u);CHECK(SamePattern(before,*editor.Draft()));
}
#endif

TEST_CASE(ExitCancelsOnlyWhollyUnsentManualPatternSaveAndPreservesStartedFrames) {
    ConfigureHostLogging(HostLogMode::Debug);
    // Unsent queue for X/Ctrl+X, partial write, uncertain zero Write,
    // complete transmission with successful/rejected/missing acknowledgement.
    for(unsigned mode=0;mode<7;++mode) {
        auto* log=std::tmpfile();CHECK(log);if(!log)return;
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->pattern_save_cap=wire->metadata_cap=true;
        BringUpSerial serial(log,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,log);controls.ObserveEditor(editor);
        unsigned tick=0;const auto step=[&]{wire->now=tick*20;wire->AudioService();serial.SyncEditor(editor,tick*20);controls.Tick(tick++*20);serial.SyncEditor(editor,tick*20);};
        while(tick<100)step();CHECK(serial.RequestPatternLoad("patern1.btdev",tick*20));while(tick<180)step();
        CHECK_EQ(std::string(serial.PatternSource()),"patern1.btdev");const auto original=*editor.Draft();
        const auto logs=[&]{std::fflush(log);std::rewind(log);std::string text;char line[2048];while(std::fgets(line,sizeof(line),log))text+=line;std::fseek(log,0,SEEK_END);return text;};
        wire->hold_pattern_save=true;wire->pattern_save_result=mode==5?"io":"ok";
        CHECK(serial.RequestFile(BringUpSerial::FileAction::PatternSave,tick*20));
        if(mode>=2){wire->write_limit=mode==2?3:UINT32_MAX;wire->zero_write_once=mode==3;wire->state="PAUSED";wire->rx+="BTTEST1 STATE PAUSED\n";step();}
        const auto began=tick*20;
        controls.Request(BringUpAction::Exit,mode%2?"Ctrl+X":"window close",began);
        if(mode<2) {
            while(tick*20<began+1500 && !controls.Quit())step();
            CHECK(logs().find("Queued Pattern Save cancelled on exit before transmission")!=std::string::npos);
            CHECK_EQ(std::count_if(wire->commands.begin(),wire->commands.end(),[](const auto& c){return c.rfind("BTSTORE1 PATTERNSAVE",0)==0;}),0);
        } else {
            wire->write_limit=UINT32_MAX;for(unsigned end=tick+5;tick<end;)step();
            CHECK(!wire->pattern_save_reply.empty());CHECK(!controls.Quit());CHECK(serial.Paused());
            CHECK(logs().find("Pattern Save transmission started; preserving framing")!=std::string::npos);
            // The manual save can legitimately outlast the STOP wait budget.
            while(tick*20<began+7000)step();CHECK(!controls.Quit());
            CHECK(std::none_of(wire->commands.begin(),wire->commands.end(),[](const auto& c){return c.rfind("BTSTORE1 SAVE",0)==0;}));
            if(mode!=6){wire->rx+=wire->pattern_save_reply;wire->pattern_save_reply.clear();}
            while(tick*20<began+17000 && !controls.Quit())step();
            CHECK_EQ(std::count(wire->commands.begin(),wire->commands.end(),"BTSTORE1 PATTERNSAVE 1 patern1.btdev"),1);
            if(mode!=6){
                const auto pattern=std::find(wire->commands.begin(),wire->commands.end(),"BTSTORE1 PATTERNSAVE 1 patern1.btdev");
                const auto stop=std::find(wire->commands.begin(),wire->commands.end(),"BTTEST1 STOP");
                const auto session=std::find_if(wire->commands.begin(),wire->commands.end(),[](const auto& c){return c.rfind("BTSTORE1 SAVE ",0)==0;});
                CHECK(pattern<stop && stop<session);
            }
            CHECK_EQ(serial.PatternSaveSucceeded(),mode!=5 && mode!=6);
            CHECK(logs().find(mode==6?"Pattern Save outcome uncertain: timed out":mode==5?"Pattern Save: io":"Pattern Save: ok")!=std::string::npos);
        }
        CHECK(controls.Quit());CHECK_EQ(serial.SessionSaveSucceeded(),mode!=6);CHECK(SamePattern(original,*editor.Draft()));
        CHECK_EQ(std::count_if(wire->commands.begin(),wire->commands.end(),[](const auto& c){return c.rfind("BTSTORE1 SAVE ",0)==0;}),mode==6?0:1);
        CHECK(wire->tx.empty());std::fclose(log);
    }
    ConfigureHostLogging(HostLogMode::Basic);
}

#ifdef BROTRACKER_TEST_HOST_INPUT_ROUTING
TEST_CASE(WindowsSlot06SineConfirmationFromMidiOrUndefinedWaitsForAckAndReopensSameType) {
    for(bool existing:{false,true})for(bool left:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
        wire->metadata_cap=wire->collection_cap=wire->define_cap=wire->facts_cap=wire->replace_cap=wire->hold_define=true;
        if(existing){CHECK(wire->definitions.Append(5,2,nullptr));wire->collection_count=6;}
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
        auto drive=[&](unsigned a,unsigned b){for(unsigned i=a;i<b;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
        drive(0,100);auto pattern=*editor.Draft();pattern.cells[0][3]={60,5};pattern.cells[1][3]={64,static_cast<std::uint8_t>(existing?5:255)};
        CHECK(editor.RecoverDraft(pattern));editor.RestoreCursor({static_cast<std::uint8_t>(existing?0:1),3,EditField::Instrument,false});const auto before=*editor.Draft();auto expected=before;if(!existing)expected.cells[1][3].instrument=5;
        controls.Request(BringUpAction::ScreenUp,"Ctrl+Up",2001);drive(101,180);
        CHECK(controls.InstrumentScreen());CHECK_EQ(controls.InstrumentNumber(),6u);CHECK_EQ(controls.InstrumentFocus(),0);
        CHECK_EQ(controls.InstrumentDraftType(),std::string(existing?"MIDI":"Sample"));
        const unsigned steps=existing?(left?2:1):(left?1:2);
        for(unsigned i=0;i<steps;++i){SDL_Event event{};event.type=SDL_KEYDOWN;event.key.keysym.sym=left?SDLK_LEFT:SDLK_RIGHT;controls.Request(MapWindowsInstrumentAction(event),"Type arrow",3601+i);}
        CHECK_EQ(controls.InstrumentDraftType(),std::string("Sine Synth"));CHECK_EQ(controls.InstrumentFocus(),0);
        SDL_Event confirm{};confirm.type=SDL_KEYDOWN;confirm.key.keysym.sym=SDLK_DOWN;confirm.key.keysym.scancode=SDL_SCANCODE_DOWN;confirm.key.keysym.mod=KMOD_LCTRL;
        const auto action=MapWindowsInstrumentAction(confirm);CHECK(action==BringUpAction::ScreenDown);CHECK(AllowWindowsBlockedAction(action,serial,controls));
        controls.Request(action,"Ctrl+Down",3610);drive(181,190);
        CHECK(controls.InstrumentScreen());CHECK(serial.InstrumentCommitPending());CHECK_EQ(serial.InstrumentCollectionSize(),existing?6u:5u);
        CHECK(SamePattern(before,*editor.Draft()));CHECK_EQ(wire->definitions.Count(),6u);CHECK_EQ(wire->definitions.At(5)->type,InstrumentType::Synth);
        CHECK_EQ(std::count_if(wire->commands.begin(),wire->commands.end(),[&](const auto& command){return command.rfind(existing?"BTINS1 REPLACE ":"BTINS1 DEFINE ",0)==0;}),1);
        CHECK(!wire->define_reply.empty());wire->rx+=wire->define_reply;wire->define_reply.clear();drive(190,205);
        CHECK(!controls.InstrumentScreen());CHECK_EQ(serial.InstrumentCollectionSize(),6u);CHECK(SamePattern(expected,*editor.Draft()));
        controls.Request(BringUpAction::ScreenUp,"reopen06",4101);drive(206,280);
        CHECK(controls.InstrumentScreen());CHECK_EQ(controls.InstrumentNumber(),6u);CHECK_EQ(controls.InstrumentFocus(),0);
        CHECK_EQ(controls.InstrumentDraftType(),std::string("Sine Synth"));
    }
}
#endif
TEST_CASE(PlayingInstrumentEntryCapturesRamWithoutStorageOrTransportAndCancelPreservesUndo) {
    for(unsigned raw:{0u,255u})for(bool keyboard:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
        wire->collection_cap=wire->facts_cap=wire->define_cap=true;
        // Playing entry must not depend on the SD snapshot capability.
        wire->metadata_cap=false;
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;
        Pump(serial,0,100,&editor);editor.RestoreCursor({3,1,EditField::Note,false});
        auto pattern=*editor.Draft();pattern.cells[3][1].instrument=raw;
        CHECK(editor.RecoverDraft(pattern));
        CHECK(editor.Apply(EditorAction::Increment));CHECK(editor.CanUndo());
        const auto before=*editor.Draft();const auto source=editor.Cursor();
        serial.SyncEditor(editor,2000);
        wire->state="PLAYING";wire->rx+="BTTEST1 STATE PLAYING\n";serial.Tick(2001);
        CHECK(serial.Playing());const auto command_start=wire->commands.size();
        BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
        controls.Request(BringUpAction::ScreenUp,"playing entry",2002);
        CHECK(controls.InstrumentScreen());CHECK(!controls.ScreenPending());
        CHECK(controls.InstrumentEntryPattern());CHECK(SamePattern(before,*controls.InstrumentEntryPattern()));
        CHECK_EQ(controls.InstrumentEntryPattern()->cells[3][1].instrument,raw);
        CHECK_EQ(controls.OriginalInstrumentNumber(),raw==255?0u:raw+1);
        CHECK(!controls.InstrumentTypeEditable());CHECK(!controls.InstrumentLoadAvailable());
        controls.Request(BringUpAction::InstrumentIncrease,"blocked live Type",2003);
        controls.Request(BringUpAction::BrowserConfirm,"blocked live WAV",2004);
        CHECK(controls.InstrumentScreen());CHECK(!controls.WavPicker());
        controls.Request(BringUpAction::ScreenUp,"repeat entry",2006);
        for(unsigned i=101;i<120;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}
        CHECK(serial.Playing());CHECK(SamePattern(before,*editor.Draft()));
        // Simulate cursor movement independent of screen ownership, then route real cancel input.
        editor.RestoreCursor({4,2,EditField::Instrument,false});
        BringUpAction cancel=BringUpAction::InstrumentCancel;
#ifdef BROTRACKER_TEST_HOST_INPUT_ROUTING
        if(keyboard) {
            WindowsScreenInput input;SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_ESCAPE;
            cancel=input.Handle(e);CHECK(AllowWindowsBlockedAction(cancel,serial,controls));
        } else {
            ArkOSEditorInput input;input.Select(7);ConfigureArkOSInputBarrier(input,serial,controls);
            SDL_Event e{};e.type=SDL_CONTROLLERBUTTONDOWN;e.cbutton.which=7;e.cbutton.button=SDL_CONTROLLER_BUTTON_B;
            LocalPatternEditor sink;cancel=input.Handle(e,2401,false,sink).transport;
            CHECK(AllowArkOSBlockedTransport(cancel,serial,controls));
        }
#endif
        CHECK(cancel==BringUpAction::InstrumentCancel);controls.Request(cancel,"cancel",2402);
        CHECK(!controls.InstrumentScreen());CHECK(!controls.InstrumentEntryPattern());
        CHECK_EQ(editor.Cursor().row,source.row);CHECK_EQ(editor.Cursor().channel,source.channel);
        CHECK(editor.Cursor().field==source.field);CHECK(SamePattern(before,*editor.Draft()));
        for(unsigned i=121;i<130;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}
        CHECK(serial.Playing());CHECK(editor.CanUndo());CHECK(editor.Undo());CHECK(SamePattern(pattern,*editor.Draft()));
        for(auto it=wire->commands.begin()+command_start;it!=wire->commands.end();++it) {
            CHECK(it->rfind("BTTEST1 STOP",0)!=0);CHECK(it->rfind("BTTEST1 PAUSE",0)!=0);
            CHECK(it->rfind("BTTEST1 START",0)!=0);CHECK(it->rfind("BTTEST1 CONTINUE",0)!=0);
            CHECK(it->rfind("BTSTORE1 READY",0)!=0);CHECK(it->rfind("BTSTORE1 SAVE",0)!=0);
            CHECK(it->rfind("BTINS1 DEFINE",0)!=0);CHECK(it->rfind("BTINS1 REPLACE",0)!=0);
        }
    }
}

TEST_CASE(InstrumentCommonTypeDraftEscapeAndNoOpReturnPreserveExplicitCellInBothStates) {
    for(bool playing:{false,true})for(bool escape:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
        wire->metadata_cap=wire->collection_cap=wire->facts_cap=wire->replace_cap=true;
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;
        BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
        auto drive=[&](unsigned a,unsigned b){for(unsigned i=a;i<b;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
        drive(0,100);auto fixture=*editor.Draft();fixture.cells[3][1]={62,1};
        CHECK(editor.RecoverDraft(fixture));editor.RestoreCursor({3,1,EditField::Instrument,false});
        const auto cursor=editor.Cursor();const auto before=*editor.Draft();
        if(playing){wire->state="PLAYING";wire->rx+="BTTEST1 STATE PLAYING\n";controls.Tick(2001);}
        const auto entry_commands=wire->commands.size();
        controls.Request(BringUpAction::ScreenUp,"entry",2002);drive(101,180);
        CHECK(controls.InstrumentScreen());CHECK_EQ(controls.InstrumentNumber(),2u);
        CHECK_EQ(controls.InstrumentFocus(),0);CHECK_EQ(controls.InstrumentDraftTypeCode(),2u);
        CHECK_EQ(serial.Playing(),playing);
        const auto return_commands=wire->commands.size();
        controls.Request(BringUpAction::InstrumentNextField,"down",3601);CHECK_EQ(controls.InstrumentFocus(),2);
        controls.Request(BringUpAction::InstrumentPreviousField,"up",3602);CHECK_EQ(controls.InstrumentFocus(),0);
        if(escape) {
            controls.Request(BringUpAction::InstrumentIncrease,"right",3603);
            CHECK_EQ(controls.InstrumentDraftTypeCode(),3u);CHECK(SamePattern(before,*editor.Draft()));
            if(playing) {
                controls.Request(BringUpAction::ScreenDown,"pending Type is not no-op",3604);
                CHECK(controls.InstrumentScreen());CHECK_EQ(controls.InstrumentDraftTypeCode(),3u);
                CHECK(std::string(controls.ScreenStatus()).find("draft retained")!=std::string::npos);
            }
        }
        editor.RestoreCursor({4,2,EditField::Note,false});
        auto action=escape?BringUpAction::InstrumentCancel:BringUpAction::ScreenDown;
#ifdef BROTRACKER_TEST_HOST_INPUT_ROUTING
        SDL_Event event{};event.type=SDL_KEYDOWN;
        event.key.keysym.sym=escape?SDLK_ESCAPE:SDLK_DOWN;event.key.keysym.mod=escape?KMOD_NONE:KMOD_LCTRL;
        WindowsScreenInput input;action=escape?input.Handle(event):MapWindowsInstrumentAction(event);
        CHECK(AllowWindowsBlockedAction(action,serial,controls));
#endif
        controls.Request(action,"fresh visit return",3605);drive(181,200);
        CHECK(!controls.InstrumentScreen());CHECK(SamePattern(before,*editor.Draft()));
        CHECK_EQ(editor.Cursor().row,cursor.row);CHECK_EQ(editor.Cursor().channel,cursor.channel);
        CHECK(editor.Cursor().field==cursor.field);CHECK_EQ(serial.Playing(),playing);
        CHECK_EQ(wire->definitions.At(1)->type,InstrumentType::MIDI);
        const auto from=playing?entry_commands:return_commands;
        for(auto it=wire->commands.begin()+from;it!=wire->commands.end();++it) {
            CHECK(it->rfind("BTTEST1 STOP",0)!=0);CHECK(it->rfind("BTTEST1 PAUSE",0)!=0);
            CHECK(it->rfind("BTTEST1 START",0)!=0);CHECK(it->rfind("BTTEST1 CONTINUE",0)!=0);
            CHECK(it->rfind("BTSTORE1 SAVE",0)!=0);CHECK(it->rfind("BTSTORE1 READY",0)!=0);
            CHECK(it->rfind("BTINS1 REPLACE",0)!=0);CHECK(it->rfind("BTINS1 DEFINE",0)!=0);
        }
    }
}

TEST_CASE(PlayingEmptyRawAssignmentCannotConfirmAsInheritedOrDefaultNoOp) {
    for(bool inherited:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
        wire->metadata_cap=wire->collection_cap=wire->facts_cap=wire->replace_cap=true;
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;
        BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
        auto drive=[&](unsigned a,unsigned b){for(unsigned i=a;i<b;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
        drive(0,100);auto fixture=*editor.Draft();
        for(unsigned r=0;r<=3;++r)fixture.cells[r][1].instrument=255;
        if(inherited)fixture.cells[2][1].instrument=1;
        CHECK(editor.RecoverDraft(fixture));editor.RestoreCursor({3,1,EditField::Instrument,false});
        const auto before=*editor.Draft();wire->state="PLAYING";wire->rx+="BTTEST1 STATE PLAYING\n";controls.Tick(2001);
        const auto from=wire->commands.size();controls.Request(BringUpAction::ScreenUp,"empty entry",2002);drive(101,180);
        CHECK_EQ(controls.InstrumentNumber(),inherited?2u:1u);CHECK_EQ(controls.OriginalInstrumentNumber(),0u);
        controls.Request(BringUpAction::ScreenDown,"would assign raw slot",3601);
        CHECK(controls.InstrumentScreen());CHECK(SamePattern(before,*editor.Draft()));
        CHECK(std::string(controls.ScreenStatus()).find("assignment change requires STOP")!=std::string::npos);
        controls.Request(BringUpAction::InstrumentCancel,"cancel",3602);drive(181,200);
        CHECK(!controls.InstrumentScreen());CHECK_EQ(editor.Cursor().row,3u);CHECK_EQ(editor.Cursor().channel,1u);
        CHECK(editor.Cursor().field==EditField::Instrument);CHECK(SamePattern(before,*editor.Draft()));CHECK(serial.Playing());
        for(auto it=wire->commands.begin()+from;it!=wire->commands.end();++it) {
            CHECK(it->rfind("BTTEST1 STOP",0)!=0);CHECK(it->rfind("BTTEST1 PAUSE",0)!=0);
            CHECK(it->rfind("BTTEST1 START",0)!=0);CHECK(it->rfind("BTSTORE1 SAVE",0)!=0);
            CHECK(it->rfind("BTSTORE1 READY",0)!=0);
        }
    }
}

TEST_CASE(InstrumentLiveSlotSelectionConfirmsOrRollsBackThroughWireAndProductionPlayer) {
    for(bool playing:{false,true})for(bool empty:{false,true})for(unsigned return_case:{0u,1u,2u}) {
        const bool cancel=return_case!=0,partial=return_case==2;
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
        wire->metadata_cap=wire->collection_cap=wire->facts_cap=wire->replace_cap=wire->preview_cap=wire->simulate_working=true;
        wire->configured_rows=2;wire->simulated_working={};wire->simulated_working.active_rows=2;wire->simulated_working.active_channels=8;
        wire->simulated_working.cells[0][0]={60,0};
        wire->simulated_working.cells[1][0]={60,static_cast<std::uint8_t>(empty?255:0)};
        wire->simulated_working.note_off_timing[1][0]=NoteOffTiming::EndOfPosition;
        const auto original=wire->simulated_working;
        // The real production playback consumer receives exactly the Pattern
        // reconstructed by the wire parser, not the host editor's displayed draft.
        std::int16_t pcm[5][256]{};StreamedVoiceBuffer buffers[8]{};StreamedSample sources[5]{};
        VariableRateSampleBindings bindings;bindings.count=5;
        for(unsigned i=0;i<5;++i) {
            for(auto& value:pcm[i])value=static_cast<std::int16_t>(1000*(i+1));
            sources[i]={pcm[i],256,buffers,{100,1,16}};
            bindings.bindings[i]={static_cast<std::uint8_t>(i),60,{pcm[i],256,100,&sources[i]}};
        }
        VariableRatePatternPlayer player;CHECK_EQ(player.Configure(12500,100,original,bindings),VariableRatePatternPlayerStatus::Success);
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
        unsigned revision=0,last_trigger_slot=99,first_trigger_slot=99;std::uint64_t last_generation=0;
        auto drive=[&](unsigned a,unsigned b){for(unsigned i=a;i<b;++i){
            wire->now=i*20;serial.SyncEditor(editor,i*20);controls.Tick(i*20);serial.SyncEditor(editor,i*20);
            if(revision!=wire->simulated_revision){revision=wire->simulated_revision;first_trigger_slot=99;CHECK_EQ(player.ReplaceWorkingPattern(wire->simulated_working),VariableRatePatternPlayerStatus::Success);}
            if(playing && player.GetTransportState()==VariableRatePatternTransportState::Playing) {
                std::int16_t audio[1];CHECK_EQ(player.Render(audio,1),VariableRatePatternPlayerStatus::Success);
                const auto position=player.GetPlaybackPosition();
                const auto voice=player.SampleVoice(0)->StreamSnapshot();
                if(position.valid && position.pattern_row==1) {
                    for(unsigned slot=0;slot<5;++slot)if(voice.sample==&sources[slot]) {
                        last_trigger_slot=slot;
                        if(voice.generation!=last_generation && !player.HasPendingWorkingPattern() && first_trigger_slot==99)first_trigger_slot=slot;
                    }
                }
                last_generation=voice.generation;
            }
        }};
        drive(0,100);editor.RestoreCursor({1,0,EditField::Instrument,false});
        if(playing){wire->state="PLAYING";wire->rx+="BTTEST1 STATE PLAYING\n";controls.Tick(2001);CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);}
        const auto entry_from=wire->commands.size();controls.Request(BringUpAction::ScreenUp,"entry",2002);drive(101,180);
        CHECK_EQ(controls.InstrumentFocus(),0);CHECK_EQ(controls.OriginalInstrumentNumber(),empty?0u:1u);
        controls.Request(BringUpAction::InstrumentPreviousField,"Up to slot",3601);CHECK_EQ(controls.InstrumentFocus(),2);
        controls.Request(BringUpAction::InstrumentIncrease,"Right selects 02",3602);
        CHECK_EQ(editor.Draft()->cells[1][0].instrument,1u);CHECK_EQ(controls.InstrumentNumber(),2u);
        CHECK_EQ(editor.Draft()->cells[1][0].note,60u);CHECK(editor.Draft()->note_off_timing[1][0]==NoteOffTiming::EndOfPosition);
        CHECK_EQ(editor.Cursor().row,1u);CHECK_EQ(controls.OriginalInstrumentNumber(),empty?0u:1u);
        drive(181,260);CHECK(controls.InstrumentScreen());CHECK_EQ(wire->simulated_working.cells[1][0].instrument,1u);
        if(playing){CHECK_EQ(last_trigger_slot,1u);CHECK_EQ(first_trigger_slot,1u);}
        const auto return_from=wire->commands.size();
        if(cancel) {
            // Commit a newer frozen selection on the wire but delay its ACK.
            // Escape must queue the original AFTER that revision, not mistake
            // an earlier matching readback for completed rollback.
            wire->hold_preview_response=true;
            if(partial)wire->write_limit=1;
            controls.Request(BringUpAction::InstrumentIncrease,"rapid Right 03",5201);drive(261,270);
            if(partial)CHECK(!wire->tx.empty());else CHECK(!wire->preview_response.empty());
            controls.Request(BringUpAction::InstrumentDecrease,"rapid Left 02",5401);
            controls.Request(BringUpAction::InstrumentCancel,"Escape",5402);
            CHECK(controls.InstrumentScreen());CHECK_EQ(editor.Draft()->cells[1][0].instrument,empty?255u:0u);
            drive(271,280);CHECK(controls.InstrumentScreen());
            wire->hold_preview_response=false;wire->write_limit=UINT32_MAX;wire->rx+=wire->preview_response;wire->preview_response.clear();
            drive(281,380);CHECK(SamePattern(original,*editor.Draft()));CHECK(SamePattern(original,wire->simulated_working));
            if(playing){CHECK_EQ(last_trigger_slot,0u);CHECK_EQ(first_trigger_slot,0u);}
        } else {
            controls.Request(BringUpAction::ScreenDown,"Ctrl+Down",5201);drive(261,340);
            CHECK_EQ(wire->simulated_working.cells[1][0].instrument,1u);CHECK_EQ(editor.Draft()->cells[1][0].instrument,1u);
            if(playing){CHECK_EQ(last_trigger_slot,1u);CHECK_EQ(first_trigger_slot,1u);}
        }
        CHECK(!controls.InstrumentScreen());CHECK_EQ(editor.Cursor().row,1u);CHECK_EQ(editor.Cursor().channel,0u);CHECK(editor.Cursor().field==EditField::Instrument);
        CHECK_EQ(serial.Playing(),playing);
        if(playing)CHECK(player.GetTransportState()==VariableRatePatternTransportState::Playing);
        for(auto it=wire->commands.begin()+(playing?entry_from:return_from);it!=wire->commands.end();++it) {
            CHECK(it->rfind("BTTEST1 STOP",0)!=0);CHECK(it->rfind("BTTEST1 PAUSE",0)!=0);CHECK(it->rfind("BTTEST1 START",0)!=0);
            CHECK(it->rfind("BTTEST1 CONTINUE",0)!=0);CHECK(it->rfind("BTSTORE1 SAVE",0)!=0);CHECK(it->rfind("BTSTORE1 READY",0)!=0);
        }
    }
}

TEST_CASE(SampleLoadBrowserRequestAndCancellationWorkStoppedButAreBlockedPlaying) {
    for(bool playing:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
        wire->metadata_cap=wire->collection_cap=wire->facts_cap=wire->replace_cap=wire->define_cap=wire->wav_cap=true;
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
        auto drive=[&](unsigned a,unsigned b){for(unsigned i=a;i<b;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
        drive(0,100);auto fixture=*editor.Draft();fixture.cells[0][0]={60,1};CHECK(editor.RecoverDraft(fixture));editor.RestoreCursor({0,0,EditField::Instrument,false});
        if(playing){wire->state="PLAYING";wire->rx+="BTTEST1 STATE PLAYING\n";controls.Tick(2001);}
        const auto entry_from=wire->commands.size();controls.Request(BringUpAction::ScreenUp,"entry",2002);drive(101,180);
        const auto from=wire->commands.size();CHECK_EQ(controls.InstrumentDraftTypeCode(),2u);
        controls.Request(BringUpAction::InstrumentDecrease,"select Sample draft",3601);CHECK_EQ(controls.InstrumentDraftTypeCode(),1u);
        controls.Request(BringUpAction::InstrumentNextField,"Down to Load",3602);
        CHECK_EQ(controls.InstrumentFocus(),playing?2:1); // Playing skips unavailable Load and reaches slot.
        controls.Request(BringUpAction::InstrumentActivate,"Enter",3603);drive(181,200);
        CHECK_EQ(controls.WavPicker(),!playing);
        CHECK_EQ(std::any_of(wire->commands.begin()+from,wire->commands.end(),[](const auto& line){return line.rfind("BTSTORE1 WAVOPEN ",0)==0;}),!playing);
        if(playing) {
            controls.Request(BringUpAction::BrowserConfirm,"direct browser action also blocked",4001);
            CHECK(std::string(controls.ScreenStatus()).find("WAV Load requires STOP")!=std::string::npos);
        } else {
            controls.Request(BringUpAction::BrowserCancel,"browser Escape",4001);
            CHECK(!controls.WavPicker());CHECK(controls.InstrumentScreen());CHECK_EQ(controls.InstrumentDraftTypeCode(),1u);
        }
        controls.Request(BringUpAction::InstrumentCancel,"discard local draft",4002);drive(201,220);
        CHECK(!controls.InstrumentScreen());CHECK_EQ(serial.Playing(),playing);CHECK(SamePattern(fixture,*editor.Draft()));
        for(auto it=wire->commands.begin()+(playing?entry_from:from);it!=wire->commands.end();++it) {
            CHECK(it->rfind("BTTEST1 STOP",0)!=0);CHECK(it->rfind("BTTEST1 PAUSE",0)!=0);CHECK(it->rfind("BTTEST1 START",0)!=0);
            CHECK(it->rfind("BTSTORE1 SAVE",0)!=0);CHECK(it->rfind("BTSTORE1 READY",0)!=0);
        }
    }
}

TEST_CASE(LiveMidiSynthTypeAppliesBeforeReturnAndRollbackWaitsForInFlightAcknowledgement) {
    for(bool playing:{false,true})for(bool was_midi:{false,true})for(bool cancel:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
        wire->metadata_cap=wire->collection_cap=wire->facts_cap=wire->replace_cap=wire->live_type_cap=wire->define_cap=wire->preview_cap=wire->simulate_working=true;
        const unsigned slot=was_midi?1:2, original_type=was_midi?2:3,new_type=was_midi?3:2;
        wire->configured_rows=2;wire->simulated_working={};wire->simulated_working.active_rows=2;wire->simulated_working.active_channels=8;
        wire->simulated_working.cells[0][0]={60,static_cast<std::uint8_t>(slot)};
        wire->simulated_working.cells[1][0]={62,255};const auto original=wire->simulated_working;
        VariableRateSampleBindings bindings;bindings.count=2;bindings.bindings[0]={1,60,{},InstrumentType::MIDI};bindings.bindings[1]={2,60,{},InstrumentType::Synth};
        VariableRatePatternPlayer player;CHECK_EQ(player.Configure(15500,44100,original,bindings),VariableRatePatternPlayerStatus::Success);wire->definition_player=&player;
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
        unsigned last_trigger=0,first_trigger=0,observed_type=original_type;
        auto drive=[&](unsigned a,unsigned b){for(unsigned i=a;i<b;++i){
            wire->now=i*20;controls.Tick(i*20);serial.SyncEditor(editor,i*20);
            const auto type=InstrumentTypeCode(wire->definitions.At(slot)->type);
            if(type!=observed_type){observed_type=type;first_trigger=0;}
            if(playing && player.GetTransportState()==VariableRatePatternTransportState::Playing) {
                std::int16_t audio[128];CHECK_EQ(player.Render(audio,128),VariableRatePatternPlayerStatus::Success);
                const auto commands=player.TakeMidiCommands();
                for(unsigned c=0;c<commands.count;++c) {
                    const auto action=commands.commands[c].action;
                    if(action==VariableRateVoiceAction::TriggerMidi || action==VariableRateVoiceAction::TriggerSynth) {
                        last_trigger=action==VariableRateVoiceAction::TriggerMidi?2:3;
                        if(!first_trigger)first_trigger=last_trigger;
                    }
                }
            }
        }};
        drive(0,100);editor.RestoreCursor({0,0,EditField::Instrument,false});
        if(playing){wire->state="PLAYING";wire->rx+="BTTEST1 STATE PLAYING\n";controls.Tick(2001);CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);}
        const auto entry_from=wire->commands.size();controls.Request(BringUpAction::ScreenUp,"entry",2002);drive(101,180);
        CHECK_EQ(controls.InstrumentFocus(),0);CHECK_EQ(controls.InstrumentDraftTypeCode(),original_type);
        const auto from=wire->commands.size();wire->hold_define=cancel;
        controls.Request(was_midi?BringUpAction::InstrumentIncrease:BringUpAction::InstrumentDecrease,"live Type",3601);drive(181,220);
        CHECK(controls.InstrumentScreen());CHECK_EQ(InstrumentTypeCode(wire->definitions.At(slot)->type),new_type);
        if(playing){CHECK_EQ(first_trigger,new_type);CHECK_EQ(last_trigger,new_type);CHECK(player.GetTransportState()==VariableRatePatternTransportState::Playing);}
        if(cancel) {
            CHECK(serial.InstrumentCommitPending());CHECK(!wire->define_reply.empty());
            const auto delayed=wire->define_reply;wire->define_reply.clear();
            // A rapid opposite edit and Escape both request the original. They
            // cannot pass an outstanding update or close on the old baseline.
            controls.Request(was_midi?BringUpAction::InstrumentDecrease:BringUpAction::InstrumentIncrease,"rapid opposite",4401);
            controls.Request(BringUpAction::InstrumentCancel,"Escape in flight",4402);drive(221,240);
            CHECK(controls.InstrumentScreen());CHECK_EQ(InstrumentTypeCode(wire->definitions.At(slot)->type),new_type);
            wire->hold_define=false;wire->rx+=delayed;drive(241,320);
            CHECK_EQ(InstrumentTypeCode(wire->definitions.At(slot)->type),original_type);
            if(playing){CHECK_EQ(first_trigger,original_type);CHECK_EQ(last_trigger,original_type);}
            wire->rx+=delayed;drive(321,340); // stale ACK cannot reinstate the update
            CHECK_EQ(InstrumentTypeCode(wire->definitions.At(slot)->type),original_type);
        } else {
            controls.Request(BringUpAction::ScreenDown,"Ctrl+Down keeps live Type",4401);drive(221,260);
            CHECK_EQ(InstrumentTypeCode(wire->definitions.At(slot)->type),new_type);
        }
        CHECK(!controls.InstrumentScreen());CHECK_EQ(editor.Cursor().row,0u);CHECK_EQ(editor.Cursor().channel,0u);CHECK(editor.Cursor().field==EditField::Instrument);
        CHECK(SamePattern(original,*editor.Draft()));CHECK(SamePattern(original,wire->simulated_working));CHECK_EQ(serial.Playing(),playing);
        for(auto it=wire->commands.begin()+(playing?entry_from:from);it!=wire->commands.end();++it) {
            CHECK(it->rfind("BTTEST1 STOP",0)!=0);CHECK(it->rfind("BTTEST1 PAUSE",0)!=0);CHECK(it->rfind("BTTEST1 START",0)!=0);CHECK(it->rfind("BTTEST1 CONTINUE",0)!=0);
            CHECK(it->rfind("BTSTORE1 SAVE",0)!=0);CHECK(it->rfind("BTSTORE1 READY",0)!=0);
        }
    }
}

TEST_CASE(EscapeRestoresEveryLiveTypeAndExplicitOrEmptyLiveAssignment) {
    for(bool empty:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
        wire->metadata_cap=wire->collection_cap=wire->facts_cap=wire->replace_cap=wire->live_type_cap=wire->define_cap=wire->preview_cap=wire->simulate_working=true;
        wire->configured_rows=2;wire->simulated_working={};wire->simulated_working.active_rows=2;wire->simulated_working.active_channels=8;
        wire->simulated_working.cells[0][0]={60,1};wire->simulated_working.cells[1][0]={62,static_cast<std::uint8_t>(empty?255:1)};
        const auto original=wire->simulated_working;
        VariableRateSampleBindings bindings;bindings.count=2;bindings.bindings[0]={1,60,{},InstrumentType::MIDI};bindings.bindings[1]={2,60,{},InstrumentType::Synth};
        VariableRatePatternPlayer player;CHECK_EQ(player.Configure(15500,44100,original,bindings),VariableRatePatternPlayerStatus::Success);wire->definition_player=&player;
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
        unsigned revision=0;
        auto drive=[&](unsigned a,unsigned b){for(unsigned i=a;i<b;++i){
            controls.Tick(i*20);serial.SyncEditor(editor,i*20);
            if(revision!=wire->simulated_revision){revision=wire->simulated_revision;CHECK_EQ(player.ReplaceWorkingPattern(wire->simulated_working),VariableRatePatternPlayerStatus::Success);}
            if(player.GetTransportState()==VariableRatePatternTransportState::Playing){std::int16_t audio[128];CHECK_EQ(player.Render(audio,128),VariableRatePatternPlayerStatus::Success);(void)player.TakeMidiCommands();}
        }};
        drive(0,100);editor.RestoreCursor({1,0,EditField::Instrument,false});
        wire->state="PLAYING";wire->rx+="BTTEST1 STATE PLAYING\n";controls.Tick(2001);CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
        const auto from=wire->commands.size();controls.Request(BringUpAction::ScreenUp,"entry",2002);drive(101,180);
        controls.Request(BringUpAction::InstrumentPreviousField,"slot",3601);controls.Request(BringUpAction::InstrumentIncrease,"slot03",3602);drive(181,220);
        CHECK_EQ(wire->simulated_working.cells[1][0].instrument,2u);
        controls.Request(BringUpAction::InstrumentNextField,"Type",4401);controls.Request(BringUpAction::InstrumentDecrease,"Synth to MIDI",4402);drive(221,260);
        CHECK_EQ(wire->definitions.At(2)->type,InstrumentType::MIDI);
        controls.Request(BringUpAction::InstrumentPreviousField,"slot",5201);controls.Request(BringUpAction::InstrumentDecrease,"slot02",5202);drive(261,300);
        controls.Request(BringUpAction::InstrumentNextField,"Type",6001);controls.Request(BringUpAction::InstrumentIncrease,"MIDI to Synth",6002);drive(301,340);
        CHECK_EQ(wire->definitions.At(1)->type,InstrumentType::Synth);CHECK_EQ(wire->definitions.At(2)->type,InstrumentType::MIDI);
        controls.Request(BringUpAction::InstrumentCancel,"Escape all visit edits",6801);drive(341,440);
        CHECK(!controls.InstrumentScreen());CHECK_EQ(wire->definitions.At(1)->type,InstrumentType::MIDI);CHECK_EQ(wire->definitions.At(2)->type,InstrumentType::Synth);
        CHECK(SamePattern(original,*editor.Draft()));CHECK(SamePattern(original,wire->simulated_working));CHECK_EQ(editor.Cursor().row,1u);CHECK_EQ(editor.Cursor().channel,0u);CHECK(editor.Cursor().field==EditField::Instrument);
        CHECK(serial.Playing());CHECK(player.GetTransportState()==VariableRatePatternTransportState::Playing);
        for(auto it=wire->commands.begin()+from;it!=wire->commands.end();++it) {
            CHECK(it->rfind("BTTEST1 STOP",0)!=0);CHECK(it->rfind("BTTEST1 PAUSE",0)!=0);CHECK(it->rfind("BTTEST1 START",0)!=0);CHECK(it->rfind("BTTEST1 CONTINUE",0)!=0);
            CHECK(it->rfind("BTSTORE1 SAVE",0)!=0);CHECK(it->rfind("BTSTORE1 READY",0)!=0);
        }
    }
}

TEST_CASE(AllSixLiveTypeTransitionsUseProductionTriggersAndConfirmOrRestoreExactBindings) {
    for(bool playing:{false,true})for(unsigned from=1;from<=3;++from)for(unsigned to=1;to<=3;++to)if(from!=to)for(bool cancel:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
        wire->metadata_cap=wire->collection_cap=wire->facts_cap=wire->replace_cap=wire->live_type_cap=wire->live_edit_cap=wire->define_cap=wire->wav_cap=wire->preview_cap=wire->simulate_working=true;
        std::int16_t old_pcm[256],new_pcm[256];std::fill_n(old_pcm,256,1000);std::fill_n(new_pcm,256,3000);
        CHECK(wire->definitions.Replace(0,from,from==1?"Samples/test2.wav":nullptr));
        VariableRateSampleBinding original_binding;original_binding.instrument=0;original_binding.reference_note=60;original_binding.sample_level_percent=37;
        original_binding.type=from==1?InstrumentType::Sample:from==2?InstrumentType::MIDI:InstrumentType::Synth;
        if(from==1)original_binding.sample={old_pcm,256,44100};
        wire->definition_bindings={};wire->definition_bindings.count=1;wire->definition_bindings.bindings[0]=original_binding;
        wire->live_prepared={0,60,{new_pcm,256,44100},InstrumentType::Sample,80};
        wire->configured_rows=2;wire->simulated_working={};wire->simulated_working.active_rows=2;wire->simulated_working.active_channels=8;
        wire->simulated_working.cells[0][0]={60,0};wire->simulated_working.cells[1][0]={62,255};const auto pattern=wire->simulated_working;
        VariableRatePatternPlayer player;CHECK_EQ(player.Configure(15500,44100,pattern,wire->definition_bindings),VariableRatePatternPlayerStatus::Success);wire->definition_player=&player;
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
        unsigned observed=from,first_trigger=0,last_trigger=0;const std::int16_t* last_sample=nullptr;
        auto drive=[&](unsigned a,unsigned b){for(unsigned i=a;i<b;++i){
            controls.Tick(i*20);serial.SyncEditor(editor,i*20);
            const auto type=InstrumentTypeCode(wire->definitions.At(0)->type);if(type!=observed){observed=type;first_trigger=0;}
            if(playing && player.GetTransportState()==VariableRatePatternTransportState::Playing) {
                std::int16_t audio[128];CHECK_EQ(player.Render(audio,128),VariableRatePatternPlayerStatus::Success);
                const auto batch=player.TakeMidiCommands();for(unsigned k=0;k<batch.count;++k){const auto& command=batch.commands[k];
                    unsigned type=command.action==VariableRateVoiceAction::Trigger?1:command.action==VariableRateVoiceAction::TriggerMidi?2:command.action==VariableRateVoiceAction::TriggerSynth?3:0;
                    if(type){last_trigger=type;if(!first_trigger)first_trigger=type;if(type==1)last_sample=command.sample.data;}
                }
            }
        }};
        drive(0,100);editor.RestoreCursor({0,0,EditField::Instrument,false});
        if(playing){wire->state="PLAYING";wire->rx+="BTTEST1 STATE PLAYING\n";controls.Tick(2001);CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);}
        const auto commands_from=wire->commands.size();controls.Request(BringUpAction::ScreenUp,"RAM entry",2002);drive(101,160);
        CHECK(serial.InstrumentVisitReady());CHECK_EQ(controls.InstrumentFocus(),0);CHECK_EQ(controls.InstrumentDraftTypeCode(),from);
        controls.Request(from%3+1==to?BringUpAction::InstrumentIncrease:BringUpAction::InstrumentDecrease,"Type arrow",3201);
        if(to==1) {
            CHECK_EQ(controls.InstrumentDraftTypeCode(),1u);CHECK_EQ(InstrumentTypeCode(wire->definitions.At(0)->type),from);
            controls.Request(BringUpAction::ScreenDown,"WAV needed",3202);CHECK(controls.InstrumentScreen());
            controls.Request(BringUpAction::InstrumentNextField,"Load focus",3203);CHECK_EQ(controls.InstrumentFocus(),1);
            controls.Request(BringUpAction::InstrumentActivate,"open WAV browser",3204);drive(161,190);CHECK(controls.WavPicker());
            controls.Request(BringUpAction::BrowserCancel,"browser cancellation",3801);CHECK(!controls.WavPicker());CHECK(controls.InstrumentScreen());
            controls.Request(BringUpAction::InstrumentActivate,"reopen Load",3802);drive(191,220);
            wire->hold_prepare=true;controls.Request(BringUpAction::BrowserDown,"selected.wav",4401);controls.Request(BringUpAction::BrowserConfirm,"prepare WAV",4402);drive(221,260);
            CHECK(wire->edit_pending);CHECK_EQ(InstrumentTypeCode(wire->definitions.At(0)->type),from);
            if(playing)CHECK_EQ(last_trigger,from); // proposal alone did not change the engine
            wire->hold_prepare=false;wire->CompleteEdit();drive(261,340);
        } else drive(161,340);
        CHECK(controls.InstrumentScreen());CHECK_EQ(InstrumentTypeCode(wire->definitions.At(0)->type),to);
        if(playing){CHECK_EQ(first_trigger,to);CHECK_EQ(last_trigger,to);if(to==1)CHECK_EQ(last_sample,new_pcm);}
        controls.Request(cancel?BringUpAction::InstrumentCancel:BringUpAction::ScreenDown,"finish",6801);drive(341,440);
        CHECK(!controls.InstrumentScreen());CHECK(!wire->visit.Active());CHECK(!serial.InstrumentVisitActive());
        CHECK_EQ(InstrumentTypeCode(wire->definitions.At(0)->type),cancel?from:to);
        if(cancel) {
            CHECK_EQ(std::string(wire->definitions.At(0)->resource?wire->definitions.At(0)->resource:""),from==1?"Samples/test2.wav":"");
            const auto& restored=wire->definition_bindings.bindings[0];CHECK(restored.type==original_binding.type);CHECK_EQ(restored.sample.data,original_binding.sample.data);
            CHECK_EQ(restored.reference_note,original_binding.reference_note);CHECK_EQ(restored.sample_level_percent,37u);
            if(playing){CHECK_EQ(first_trigger,from);CHECK_EQ(last_trigger,from);if(from==1)CHECK_EQ(last_sample,old_pcm);}
        }
        CHECK_EQ(editor.Cursor().row,0u);CHECK_EQ(editor.Cursor().channel,0u);CHECK(editor.Cursor().field==EditField::Instrument);
        CHECK(SamePattern(pattern,*editor.Draft()));CHECK_EQ(serial.Playing(),playing);
        for(auto it=wire->commands.begin()+commands_from;it!=wire->commands.end();++it) {
            CHECK(it->rfind("BTTEST1 STOP",0)!=0);CHECK(it->rfind("BTTEST1 PAUSE",0)!=0);CHECK(it->rfind("BTTEST1 START",0)!=0);CHECK(it->rfind("BTTEST1 CONTINUE",0)!=0);
            CHECK(it->rfind("BTSTORE1 SAVE",0)!=0);CHECK(it->rfind("BTSTORE1 READY",0)!=0);
        }
    }
}

TEST_CASE(EscapeCancelsInFlightSamplePreparationAndLateCompletionCannotReplaceRollback) {
    for(bool original_sample:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
        wire->metadata_cap=wire->collection_cap=wire->facts_cap=wire->replace_cap=wire->live_type_cap=wire->live_edit_cap=wire->define_cap=wire->wav_cap=wire->preview_cap=wire->simulate_working=true;
        if(!original_sample)CHECK(wire->definitions.Replace(0,2,nullptr));
        wire->configured_rows=2;wire->simulated_working={};wire->simulated_working.active_rows=2;wire->simulated_working.active_channels=8;wire->simulated_working.cells[0][0]={60,0};
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
        auto drive=[&](unsigned a,unsigned b){for(unsigned i=a;i<b;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
        drive(0,100);editor.RestoreCursor({0,0,EditField::Instrument,false});wire->state="PLAYING";wire->rx+="BTTEST1 STATE PLAYING\n";controls.Tick(2001);
        controls.Request(BringUpAction::ScreenUp,"entry",2002);drive(101,160);
        if(!original_sample)controls.Request(BringUpAction::InstrumentDecrease,"Sample proposal",3201);
        controls.Request(BringUpAction::InstrumentNextField,"Load",3202);controls.Request(BringUpAction::InstrumentActivate,"browse",3203);drive(161,200);
        wire->hold_prepare=true;controls.Request(BringUpAction::BrowserDown,"file",4001);controls.Request(BringUpAction::BrowserConfirm,"file select",4002);
        if(controls.ReplacementConfirming())controls.Request(BringUpAction::BrowserConfirm,"accept replacement",4003);
        drive(201,240);CHECK(wire->edit_pending);const auto delayed=wire->pending_edit;
        controls.Request(BringUpAction::InstrumentCancel,"Escape during load",4801);drive(241,320);
        CHECK(!wire->edit_pending);CHECK(!controls.InstrumentScreen());CHECK_EQ(InstrumentTypeCode(wire->definitions.At(0)->type),original_sample?1u:2u);
        wire->ApplyEdit(delayed);drive(321,340);CHECK_EQ(InstrumentTypeCode(wire->definitions.At(0)->type),original_sample?1u:2u);
        CHECK_EQ(std::string(wire->definitions.At(0)->resource?wire->definitions.At(0)->resource:""),original_sample?"Samples/test2.wav":"");CHECK(serial.Playing());
    }
}

TEST_CASE(ManualWavAuditionIsStoppedOnlyAndIndependentOfBrowserSelectionAndDraft) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
    wire->metadata_cap=wire->collection_cap=wire->facts_cap=wire->replace_cap=wire->live_edit_cap=wire->wav_cap=wire->preview_cap=wire->simulate_working=true;
    wire->configured_rows=2;wire->simulated_working.active_rows=2;wire->simulated_working.active_channels=8;wire->simulated_working.cells[0][0]={60,0};
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
    auto drive=[&](unsigned a,unsigned b){for(unsigned i=a;i<b;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
    drive(0,100);wire->rx+="BTWAV1 CAP 1\n";drive(100,110);
    CHECK(editor.Draft());editor.RestoreCursor({0,0,EditField::Instrument,false});const auto before=*editor.Draft();
    controls.Request(BringUpAction::ScreenUp,"entry",2201);drive(111,160);
    controls.Request(BringUpAction::InstrumentNextField,"Load",3201);controls.Request(BringUpAction::InstrumentActivate,"browse",3202);drive(161,210);
    CHECK(controls.WavPicker());
    const auto plays=[&](){return std::count_if(wire->commands.begin(),wire->commands.end(),[](const auto& c){return c.rfind("BTWAV1 PLAY",0)==0;});};
    controls.Request(BringUpAction::BrowserAudition,"directory",4201);drive(211,220);CHECK_EQ(plays(),0);
    controls.Request(BringUpAction::BrowserDown,"focus WAV",4401);drive(221,230);CHECK_EQ(plays(),0);
    controls.Request(BringUpAction::BrowserAudition,"P/X",4601);drive(231,240);CHECK_EQ(plays(),1);
    CHECK(controls.WavPicker());CHECK(controls.InstrumentDraftResource().empty());CHECK(SamePattern(before,*editor.Draft()));
    wire->state="PLAYING";wire->rx+="BTTEST1 STATE PLAYING\n";drive(241,250);
    controls.Request(BringUpAction::BrowserAudition,"blocked playing",5001);drive(251,260);CHECK_EQ(plays(),1);
    CHECK_EQ(controls.BrowserText(),std::string("Audition requires confirmed STOP and audition-capable firmware"));
    controls.Request(BringUpAction::BrowserUp,"change selection clears feedback",5201);
    CHECK(controls.BrowserText().find("Audition requires")==std::string::npos);
    controls.Request(BringUpAction::BrowserDown,"back to WAV",5202);
    wire->state="PAUSED";wire->rx+="BTTEST1 STATE PAUSED\n";drive(261,270);
    controls.Request(BringUpAction::BrowserAudition,"blocked paused",5401);drive(271,280);CHECK_EQ(plays(),1);
    CHECK_EQ(controls.BrowserText(),std::string("Audition requires confirmed STOP and audition-capable firmware"));
    wire->state="IDLE";wire->rx+="BTTEST1 STATE IDLE\n";drive(281,290);
    controls.Request(BringUpAction::BrowserAudition,"restart",5801);drive(291,300);CHECK_EQ(plays(),2);
    CHECK(controls.BrowserText().find("Audition requires")==std::string::npos);
    controls.Request(BringUpAction::BrowserCancel,"leave",6001);drive(301,320);CHECK(!controls.WavPicker());
    CHECK(std::any_of(wire->commands.begin(),wire->commands.end(),[](const auto& c){return c.rfind("BTWAV1 CANCEL",0)==0;}));
    CHECK(controls.InstrumentDraftResource().empty());CHECK(SamePattern(before,*editor.Draft()));
}

TEST_CASE(WavListingCancellationIgnoresOldLongItemsAndAllowsImmediateNewFolderRequest) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();wire->wav_cap=wire->hold_list=true;
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;Pump(serial,0,100,&editor);
    CHECK(serial.RequestWavDirectory("/old",0,false,2001));Pump(serial,101,110,&editor);CHECK(serial.PatternListBusy());
    serial.CancelWavDirectory();CHECK(!serial.PatternListBusy());CHECK(serial.RequestWavDirectory("/new",0,false,2201));Pump(serial,111,120,&editor);
    wire->rx+="BTSTORE1 WAVDIR 3 1 /old\nBTSTORE1 WAVITEM 3 1 F "+std::string(230,'x')+".wav\nBTSTORE1 WAVEND 3 1 32 1 1\n";
    Pump(serial,121,130,&editor);CHECK(serial.PatternListBusy());CHECK(!serial.PatternListReady());
    wire->rx+="BTSTORE1 WAVDIR 3 2 /new\nBTSTORE1 WAVINFO 3 2 0\nBTSTORE1 WAVITEM 3 2 F new.wav\nBTSTORE1 WAVMETA 3 2 0 0 1 1 16 44100\nBTSTORE1 WAVEND 3 2 32 1 1\n";
    Pump(serial,131,140,&editor);CHECK(serial.PatternListReady());CHECK_EQ(std::string(serial.WavDirectoryPage().directory),std::string("/new"));
    serial.CancelWavDirectory();wire->rx+="BTSTORE1 WAVERROR 3 2 2\n";Pump(serial,141,150,&editor);CHECK(!serial.PatternListReady());CHECK(!serial.PatternListBusy());
}

TEST_CASE(ProgressiveBrowserControllerAcceptsFileOrFolderWhileNextBatchIsPending) {
    for(bool folder:{false,true}) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
        wire->metadata_cap=wire->collection_cap=wire->facts_cap=wire->replace_cap=wire->live_edit_cap=wire->wav_cap=wire->preview_cap=wire->simulate_working=wire->hold_list=true;
        wire->configured_rows=2;wire->simulated_working.active_rows=2;wire->simulated_working.active_channels=8;wire->simulated_working.cells[0][0]={60,0};
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
        auto drive=[&](unsigned a,unsigned b){for(unsigned i=a;i<b;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
        drive(0,100);editor.RestoreCursor({0,0,EditField::Instrument,false});controls.Request(BringUpAction::ScreenUp,"entry",2001);drive(101,160);
        controls.Request(BringUpAction::InstrumentNextField,"Load",3201);controls.Request(BringUpAction::InstrumentActivate,"browse",3202);drive(161,200);
        wire->rx+="BTSTORE1 WAVDIR 3 1 /Samples\nBTSTORE1 WAVINFO 3 1 0\nBTSTORE1 WAVITEM 3 1 D Sub\nBTSTORE1 WAVITEM 3 1 F early.wav\nBTSTORE1 WAVMETA 3 1 1 0 1 1 16 44100\nBTSTORE1 WAVEND 3 1 64 0 2\n";
        drive(201,220);CHECK(controls.WavPicker());CHECK(serial.PatternListBusy());CHECK_EQ(controls.BrowserList().names.size(),3u);CHECK(controls.BrowserText().find("1 samples read")!=std::string::npos);
        controls.Request(BringUpAction::BrowserDown,"navigate before complete",4401);
        if(!folder)controls.Request(BringUpAction::BrowserDown,"early WAV",4402);
        controls.Request(BringUpAction::BrowserConfirm,"select before complete",4403);drive(221,240);
        if(folder) {
            CHECK(std::any_of(wire->commands.begin(),wire->commands.end(),[](const auto& c){return c=="BTSTORE1 WAVOPEN 3 3 0 0 /Samples/Sub";}));
            wire->rx+="BTSTORE1 WAVDIR 3 2 /Samples\nBTSTORE1 WAVINFO 3 2 0\nBTSTORE1 WAVEND 3 2 64 1 0\nBTSTORE1 WAVDIR 3 3 /Samples/Sub\nBTSTORE1 WAVINFO 3 3 0\nBTSTORE1 WAVEND 3 3 0 1 0\n";drive(241,260);
            CHECK_EQ(controls.WavDirectory(),std::string("/Samples/Sub"));CHECK_EQ(controls.BrowserList().names.size(),1u);
        }else CHECK(controls.ReplacementConfirming());
        controls.Request(BringUpAction::BrowserCancel,"leave",5201);drive(261,280);CHECK(!controls.WavPicker());CHECK(!serial.PatternListBusy());
    }
}

TEST_CASE(SampleBrowserStartupUsesMissingOnlyHostChainAndReusesTheSuccessfulListing) {
    // Preferred, first fallback, root fallback, empty preferred, and each
    // non-missing failure (read error, not-directory, busy, invalid path).
    for(unsigned scenario=0;scenario<8;++scenario) {
        auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
        wire->metadata_cap=wire->collection_cap=wire->facts_cap=wire->replace_cap=wire->live_edit_cap=wire->wav_cap=wire->preview_cap=wire->simulate_working=true;
        wire->configured_rows=2;wire->simulated_working.active_rows=2;wire->simulated_working.active_channels=8;wire->simulated_working.cells[0][0]={60,0};
        if(scenario==1 || scenario==2)wire->wav_directory_errors["/Samples"]=1;
        if(scenario==2)wire->wav_directory_errors["/BroTracker"]=1;
        if(scenario==3)wire->empty_wav_directories.push_back("/Samples");
        if(scenario>=4)wire->wav_directory_errors["/Samples"]=scenario-2;
        BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;BringUpControls controls(serial,nullptr);controls.ObserveEditor(editor);
        auto drive=[&](unsigned a,unsigned b){for(unsigned i=a;i<b;++i){controls.Tick(i*20);serial.SyncEditor(editor,i*20);}};
        drive(0,100);editor.RestoreCursor({0,0,EditField::Instrument,false});controls.Request(BringUpAction::ScreenUp,"entry",2001);drive(101,160);
        controls.Request(BringUpAction::InstrumentNextField,"Load",3201);controls.Request(BringUpAction::InstrumentActivate,"browse",3202);drive(161,260);
        std::vector<WavOpenRequest> requests;
        for(const auto& line:wire->commands)if(line.rfind("BTSTORE1 WAVOPEN ",0)==0){WavOpenRequest r;CHECK(ParseWavOpen(line.data(),line.size(),r));CHECK(!r.fallback);requests.push_back(r);}
        const unsigned expected=scenario==2?3:scenario==1?2:1;CHECK_EQ(requests.size(),expected);
        CHECK_EQ(std::string(requests[0].directory),std::string("/Samples"));
        if(expected>1)CHECK_EQ(std::string(requests[1].directory),std::string("/BroTracker"));
        if(expected>2)CHECK_EQ(std::string(requests[2].directory),std::string("/"));
        CHECK(controls.WavPicker());CHECK(!serial.PatternListBusy());
        if(scenario>=4){CHECK(controls.BrowserText().find("Unable to read folder")!=std::string::npos);CHECK(controls.WavDirectory().empty());}
        else {
            CHECK_EQ(controls.WavDirectory(),std::string(scenario==2?"/":scenario==1?"/BroTracker":"/Samples"));
            CHECK(controls.BrowserText().find(scenario==3?"0 samples read":"1 samples read")!=std::string::npos);
            // No second offset-zero open after success, including an empty page.
            CHECK_EQ(std::count_if(requests.begin(),requests.end(),[&](const auto& r){return std::string(r.directory)==controls.WavDirectory();}),1);
        }
        if(scenario==0) {
            wire->wav_directory_errors["/"]=1;
            controls.Request(BringUpAction::BrowserConfirm,"explicit parent navigation",5201);drive(261,300);
            CHECK(controls.BrowserText().find("Folder not found")!=std::string::npos);CHECK_EQ(controls.WavDirectory(),std::string("/Samples"));
            const auto opens=std::count_if(wire->commands.begin(),wire->commands.end(),[](const auto& line){return line.rfind("BTSTORE1 WAVOPEN ",0)==0;});CHECK_EQ(opens,2);
        }
    }
}

TEST_CASE(HostNegotiatesLongSpacedPathsForListingAuditionLiveAssignmentAndRollback) {
    auto peer=std::make_unique<StoragePeer>();auto* wire=peer.get();
    wire->define_cap=wire->path_cap=wire->metadata_cap=wire->collection_cap=wire->facts_cap=wire->replace_cap=wire->live_edit_cap=wire->wav_cap=wire->preview_cap=wire->simulate_working=true;
    wire->configured_rows=2;wire->simulated_working.active_rows=2;wire->simulated_working.active_channels=8;wire->simulated_working.cells[0][0]={60,0};
    static const std::int16_t sample[]={100,-100,50,0};wire->live_prepared.type=InstrumentType::Sample;wire->live_prepared.sample={sample,4,44100};
    BringUpSerial serial(nullptr,std::move(peer));LocalPatternEditor editor;Pump(serial,0,100,&editor);CHECK(serial.LongSamplePathsSupported());
    wire->rx+="BTWAV1 CAP 1\n";Pump(serial,100,105,&editor);
    const auto directory="/Samples/Jungle riddims/"+std::string(100,'d');CHECK(serial.RequestWavDirectory(directory,0,false,2101));Pump(serial,106,140,&editor);
    CHECK(serial.PatternListReady());CHECK_EQ(std::string(serial.WavDirectoryPage().directory),directory);
    WavBrowser browser;browser.Begin(directory);CHECK(browser.Accept(serial.WavDirectoryPage()));browser.Move(1);std::string path;CHECK_EQ(browser.Select(path),2u);CHECK(path.size()>63);
    CHECK(serial.RequestWavAudition(path));Pump(serial,141,155,&editor);
    CHECK(std::any_of(wire->commands.begin(),wire->commands.end(),[](const auto& c){return c.rfind("BTWAV1 PLAY 2 ",0)==0;}));serial.CancelWavAudition();
    CHECK(serial.BeginInstrumentVisit(3101));Pump(serial,156,180,&editor);CHECK(serial.InstrumentVisitReady());serial.InspectInstrument(1);Pump(serial,181,200,&editor);
    const auto original=std::string(wire->definitions.At(0)->resource);
    CHECK(serial.ReplaceLiveDefinition(1,1,path.c_str(),4001));Pump(serial,201,230,&editor);CHECK(serial.InstrumentCommitSucceeded());CHECK_EQ(std::string(wire->definitions.At(0)->resource),path);
    Pump(serial,231,250,&editor);CHECK_EQ(serial.InstrumentResource(),path);
    wire->hold_prepare=true;CHECK(serial.ReplaceLiveDefinition(1,1,(path+"x").c_str(),5000));Pump(serial,251,260,&editor);CHECK(wire->edit_pending);
    wire->edit_pending=false;wire->EditReply(wire->pending_edit.request,9);wire->hold_prepare=false;Pump(serial,261,270,&editor);
    CHECK(!serial.InstrumentCommitSucceeded());CHECK(serial.InstrumentCommitError().find("path storage full")!=std::string::npos);CHECK_EQ(std::string(wire->definitions.At(0)->resource),path);
    CHECK(serial.ReplaceLiveDefinition(1,1,original.c_str(),5401,true));Pump(serial,271,300,&editor);CHECK(serial.InstrumentCommitSucceeded());CHECK_EQ(std::string(wire->definitions.At(0)->resource),original);
    serial.EndInstrumentVisit();Pump(serial,301,320,&editor);CHECK(!serial.InstrumentVisitActive());
}

#include "test_framework.h"
#include "stopped_load.h"
#include "btt_pattern_layout.h"
#include "firmware/teensy/BroTracker/sd_wav_audition.h"
#include "bringup/midi_output.h"
#include <memory>
#include <string>
#include <cstdlib>
#include <new>
static bool count_save_allocations=false;
static size_t save_allocations=0,fake_save_allocations=0,save_calls=0;
void* operator new(size_t n) {
    if(auto* p=std::malloc(n?n:1)) {
        if(count_save_allocations){if(DeviceSd::allocation_scope)++fake_save_allocations;else ++save_allocations;}
        return p;
    }
    throw std::bad_alloc();
}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
void operator delete[](void* p,size_t) noexcept {std::free(p);}
namespace BroTracker {
StreamInterruptGuard::StreamInterruptGuard() noexcept {mask_=FakeStreamPlatform::primask;FakeStreamPlatform::primask=1;}
StreamInterruptGuard::~StreamInterruptGuard() noexcept {FakeStreamPlatform::primask=mask_;}
}
namespace {
using namespace BroTracker;
using namespace btt::production;
namespace f=btt_layout;
void Put(std::vector<uint8_t>& b,unsigned at,uint32_t value,unsigned width){for(unsigned i=0;i<width;++i)b[at+i]=uint8_t(value>>(8*i));}
std::vector<uint8_t> Wav(){
    std::vector<uint8_t> b(44+7000*2);std::memcpy(b.data(),"RIFF",4);Put(b,4,b.size()-8,4);
    std::memcpy(b.data()+8,"WAVEfmt ",8);Put(b,16,16,4);Put(b,20,1,2);Put(b,22,1,2);
    Put(b,24,44100,4);Put(b,28,88200,4);Put(b,32,2,2);Put(b,34,16,2);
    std::memcpy(b.data()+36,"data",4);Put(b,40,14000,4);return b;
}
std::vector<uint8_t> Tune(bool fx=false,bool missing=false,uint32_t bpm=14500) {
    std::vector<uint8_t> tune,order,p1,p2;
    f::Tlv(tune,1,4,std::vector<uint8_t>(16,1));f::Tlv(tune,2,5,{'T','u','n','e'});
    f::Scalar(tune,3,4,bpm);f::Scalar(tune,4,1,8);f::Scalar(tune,5,4,100);f::Scalar(tune,6,4,1000);
    f::Tlv(tune,777,99,{0,255,42},false);
    std::vector<uint8_t> ids;for(auto id:{101,303,101})f::Put(ids,id,4);f::Tlv(order,1,6,ids);
    std::vector<f::Section> sections{{"TUNE",0,tune},{"ORDR",0,order}};
    for(unsigned id:{22,11,33}) {
        std::vector<uint8_t> inst;f::Scalar(inst,1,2,1);
        const char* path=missing?"Samples/missing.wav":"Samples/a.wav";std::vector<uint8_t> resource{0};
        while(*path)resource.push_back(*path++);f::Tlv(inst,2,6,resource,false);
        if(id!=11)f::Scalar(inst,3,2,id==33?0:72,false);
        if(id!=22)f::Scalar(inst,4,2,id==33?199:12,false);
        f::Tlv(inst,777,99,{0,254,42},false);sections.push_back({"INST",id,inst});
    }
    f::Pattern pattern;pattern.Clear(2);f::Cell cell;cell.note=48;cell.instrument=22;pattern.Put(0,cell);
    // All three FX are retained on an unselected pattern, even a zero value.
    cell.fx={601,602,603};cell.values={0,42,255};pattern.Put(8,cell);CHECK(f::Encode(pattern,3,p1));
    pattern.Clear(3);cell={};cell.note=60;cell.instrument=11;
    if(fx){cell.fx={601,602,603};cell.values={0,42,255};}pattern.Put(0,cell);
    cell={};cell.note=72;cell.instrument=22;pattern.Put(1,cell);CHECK(f::Encode(pattern,3,p2));
    sections.push_back({"PATT",101,p1});sections.push_back({"PATT",303,p2});
    sections.push_back({"XTRA",77,{0,1,255,7},false});return f::Envelope(sections);
}
static SdSampleStreaming::Storage pcm;
struct Fixture {
    SdSampleStreaming producer;
    SelectedPatternBringUpControl control;
    PatternPublication publication;
    InstrumentDefinitions definitions{false};
    VariableRateSampleBindings bindings;
    InstrumentAvailability availability[99]{};
    std::unique_ptr<StoppedLoad> load;
    RealtimePattern old,readback;
    Fixture() {
        DeviceSd::files.clear();DeviceSd::fail_write=false;DeviceSd::fail_close_path.clear();DeviceSd::fail_write_path.clear();
        DeviceSd::fail_read_path.clear();DeviceSd::fail_sync_path.clear();DeviceSd::fail_remove_path.clear();
        DeviceSd::close_failure_closes_inner=false;
        DeviceSd::fail_open_path.clear();DeviceSd::short_read=DeviceSd::short_write=0;
        DeviceSd::rename_calls=DeviceSd::fail_rename=DeviceSd::fail_rename_second=0;DeviceSd::read_open_hook={};
        DevelopmentWavInspection inspection;producer.Prepare(true,pcm,inspection);
        DeviceSd::files["/Samples/a.wav"].bytes=Wav();DeviceSd::files["/Tunes/test.btt"].bytes=Tune();
        old.active_rows=7;old.active_channels=8;
        CHECK_EQ(control.InitializeAudio(12500,44100,old,bindings),PatternPlayerStatus::Success);
        readback=old;load=std::make_unique<StoppedLoad>(producer);
    }
    ~Fixture() {
        DeviceSd::fail_write=false;DeviceSd::fail_close_path.clear();
        DeviceSd::fail_read_path.clear();DeviceSd::fail_sync_path.clear();DeviceSd::fail_remove_path.clear();
        DeviceSd::fail_rename=DeviceSd::fail_rename_second=0;
        if(load->SaveRecoveryPending())CHECK_EQ(load->Recover(999,load->SavedStatus(1).id,true).status,RecoveryStatus::Cleaned);
        load->Cancel();for(unsigned i=0;i<200 && load->Busy();++i)load->Step(true);
        CHECK(!load->Busy());
        CHECK_EQ(control.OutputPlayer().Configure(12500,44100,old,VariableRateSampleBindings{}),PatternPlayerStatus::Success);
        producer.Publish(control.OutputPlayer().MidiPlayer());definitions.Reset(false);bindings={};load.reset();
        CHECK(producer.Reset());CHECK_EQ(ResourcePaths().UniqueCount(),0u);
        for(const auto& file:DeviceSd::files)CHECK_EQ(file.second.opens,0u);
    }
    bool Begin(uint32_t pattern=303){return load->Begin("Tunes/test.btt",pattern,control.StorageReady());}
    SaveReport Save(uint32_t id,bool platform=true){
        count_save_allocations=true;++save_calls;
        const auto result=load->Save(id,platform && control.StorageReady() && !publication.Busy());
        count_save_allocations=false;CHECK_EQ(save_allocations,0u);return result;
    }
    void Turn(){producer.Service();load->Step(control.StorageReady());}
    void Ready(){for(unsigned i=0;i<400 && load->phase()!=LoadPhase::Ready && load->phase()!=LoadPhase::Failed;++i)Turn();CHECK_EQ(load->phase(),LoadPhase::Ready);}
    void Commit() {
        const auto before=control.Snapshot().tempo;
        const auto before_definitions=definitions.Count();
        CHECK(load->Submit(control,publication,100));CHECK_EQ(control.Snapshot().tempo,before);
        std::int16_t out[1];control.AudioBlock(out,1,true);producer.Publish(control.OutputPlayer().MidiPlayer());
        CHECK_EQ(definitions.Count(),before_definitions); // Foreground tables update only at ACK consumption.
        PatternAppliedRequest ack;CHECK(control.TakeApplied(ack));
        CHECK(load->Complete(ack,definitions,bindings,availability));
        CHECK(publication.Complete(ack.publication_id,ack.result==PatternPlayerStatus::Success,readback));
    }
};
}
TEST_CASE(BttStopped_supported_load_coherent_and_full_saved_ownership) {
    Fixture f;const auto original=DeviceSd::files["/Tunes/test.btt"].bytes;
    CHECK(f.Begin());f.Ready();CHECK_EQ(f.load->omitted(),1u);
    CHECK_EQ(f.load->projection().active_rows,3);CHECK_EQ(f.load->projection().cells[0][0].instrument,1);
    CHECK_EQ(f.load->projection().cells[0][1].instrument,0);
    f.Commit();CHECK_EQ(f.control.Snapshot().tempo,14500u);CHECK_EQ(f.control.Snapshot().active_rows,3u);
    CHECK_EQ(f.control.Snapshot().transport,PatternTransportState::Stopped);CHECK(f.control.StorageReady());
    CHECK_EQ(f.definitions.Count(),2u);CHECK_EQ(f.definitions.At(0)->tuning.root,72u);CHECK_EQ(f.definitions.At(0)->tuning.cents,0u);
    CHECK_EQ(f.load->StableId(0),22u);CHECK_EQ(f.load->StableId(1),11u);CHECK_EQ(f.load->StableId(2),0u);
    CHECK_EQ(f.definitions.At(1)->tuning.root,60u);CHECK_EQ(f.definitions.At(1)->tuning.cents,12u);
    CHECK_EQ(f.load->active()->Find(33)->saved.root,0);CHECK_EQ(f.load->active()->Find(33)->saved.fine,199);
    btt::Document doc;btt::Workspace work;btt::Pattern other;
    CHECK_EQ(btt::Validate(f.load->active()->backing(),doc,work),btt::Result::Ok);
    CHECK_EQ(btt::Materialize(doc,101,other,work),btt::Result::Ok);CHECK_EQ(other.At(8).fx[2],603);
    CHECK_EQ(other.At(8).values[0],0);CHECK(btt::Find(doc,"XTRA",77));CHECK(btt::Find(doc,"ORDR",0));
    std::vector<uint8_t> retained(original.size());
    for(unsigned at=0;at<retained.size();at+=512){const auto n=std::min<unsigned>(512,retained.size()-at);CHECK_EQ(doc.backing->read(at,retained.data()+at,n),n);}
    CHECK(retained==original);CHECK(DeviceSd::files["/Tunes/test.btt"].bytes==original);
}
TEST_CASE(BttStopped_playing_paused_pending_and_unconfirmed_reject_without_io) {
    Fixture f;const auto opens=DeviceSd::open_calls;
    CHECK(!f.load->Begin("BroTracker/.btt-load/owned.btt",303,true));
    CHECK(!f.load->Begin("BROTRACKER/.BTT-LOAD/owned.btt",303,true));
    CHECK_EQ(DeviceSd::open_calls,opens);
    CHECK_EQ(f.control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    const auto creates=DeviceSd::created;CHECK(!f.Begin());
    std::int16_t out[1];f.control.AudioBlock(out,1,true);PatternAppliedRequest ack;CHECK(f.control.TakeApplied(ack));CHECK(!f.Begin());
    CHECK_EQ(f.control.Submit(PatternRequest::Pause),PatternRequestStatus::Accepted);f.control.AudioBlock(out,1,true);CHECK(f.control.TakeApplied(ack));
    CHECK(!f.Begin());CHECK(!f.load->Begin("Tunes/test.btt",303,false));CHECK_EQ(DeviceSd::created,creates);
    CHECK_EQ(f.control.Snapshot().transport,PatternTransportState::Paused);
}
TEST_CASE(BttStopped_precise_fx_missing_and_selection_failures_preserve_old) {
    for(unsigned which=0;which<4;++which) {
        Fixture f;if(which<2)DeviceSd::files["/Tunes/test.btt"].bytes=Tune(which==0,which==1);
        CHECK(f.Begin(which==2?999:which==3?101:303));
        for(unsigned i=0;i<400 && f.load->phase()!=LoadPhase::Failed;++i)f.Turn();
        CHECK_EQ(f.load->phase(),LoadPhase::Failed);
        CHECK_EQ(f.control.Snapshot().tempo,12500u);CHECK_EQ(f.control.Snapshot().active_rows,7u);CHECK(!f.load->active());
        if(which==0 || which==3)CHECK_EQ(std::string(f.load->reason()),"initial-pattern-fx-unsupported");
        if(which==1)CHECK_EQ(std::string(f.load->reason()),"sample-missing");
    }
}
TEST_CASE(BttStopped_cancellation_at_every_staging_boundary_and_repeat) {
    Fixture f;
    for(unsigned turn=0;turn<80;++turn) {
        CHECK(f.Begin());for(unsigned i=0;i<turn && f.load->phase()!=LoadPhase::Ready;++i)f.Turn();
        CHECK(f.load->Cancel());for(unsigned i=0;i<300 && f.load->Busy();++i)f.Turn();
        CHECK(!f.load->Busy());CHECK(!f.load->active());CHECK_EQ(f.control.Snapshot().tempo,12500u);
        while(f.producer.ReclaimOne(f.bindings)){}CHECK_EQ(f.producer.SourceCount(),3u);
        CHECK_EQ(ResourcePaths().UniqueCount(),3u);
    }
}
TEST_CASE(BttStopped_foreign_legacy_completion_is_never_stolen) {
    Fixture f;CHECK(f.producer.BeginPrepareAsync("Samples/a.wav"));
    CHECK(f.Begin());for(unsigned i=0;i<100;++i)f.Turn();
    CHECK_EQ(f.load->phase(),LoadPhase::Prepare);CHECK(!f.producer.PreparationDone());
    for(unsigned i=0;i<100 && !f.producer.PreparationDone();++i)f.producer.PreparationStep();
    // Existing prepared identity may be shared without consuming the foreign result.
    f.Ready();CHECK(f.producer.PreparationDone());CHECK(f.producer.AcknowledgePreparation());
}
TEST_CASE(BttStopped_io_failure_and_input_close_quarantine_keep_old) {
    Fixture f;CHECK(f.Begin());f.Turn();f.Turn();DeviceSd::fail_write=true;
    for(unsigned i=0;i<5;++i)f.Turn();CHECK_EQ(f.load->phase(),LoadPhase::Failed);
    DeviceSd::fail_write=false;CHECK(f.Begin());f.Turn();DeviceSd::fail_close_path="/Tunes/test.btt";
    for(unsigned i=0;i<100;++i)f.Turn();CHECK_EQ(f.load->phase(),LoadPhase::Cleanup);
    CHECK(!f.Begin());CHECK_EQ(f.control.Snapshot().tempo,12500u);
    DeviceSd::fail_close_path.clear();f.Turn();CHECK_EQ(f.load->phase(),LoadPhase::Failed);
}
TEST_CASE(BttStopped_repeated_publication_releases_previous_snapshot) {
    Fixture f;
    for(unsigned i=0;i<12;++i) {
        CHECK(f.Begin());f.Ready();
        CHECK(f.load->Submit(f.control,f.publication,100+i));CHECK(!f.load->Cancel());
        std::int16_t out[1];f.control.AudioBlock(out,1,true);f.producer.Publish(f.control.OutputPlayer().MidiPlayer());
        PatternAppliedRequest ack;CHECK(f.control.TakeApplied(ack));
        CHECK(f.load->Complete(ack,f.definitions,f.bindings,f.availability));
        CHECK(f.publication.Complete(ack.publication_id,true,f.readback));
        CHECK_EQ(f.load->phase(),LoadPhase::Complete);CHECK(f.control.StorageReady());
        unsigned snapshots=0;for(const auto& file:DeviceSd::files)if(file.first.find(".btt-load/")!=std::string::npos)++snapshots;
        CHECK_EQ(snapshots,1u);
    }
}
TEST_CASE(BttStopped_existing_namespace_load_and_rejection_preserve_other_files) {
    Fixture f;
    DeviceSd::files[BTT_BACKING_DIRECTORY].directory=true;
    const std::string historical=std::string(BTT_BACKING_DIRECTORY)+"/historical.btt";
    DeviceSd::files[historical].bytes={1,2,3,4};
    CHECK(!SD.sdfs.mkdir(BTT_BACKING_DIRECTORY,true)); // Installed SdFat behavior.
    CHECK(f.Begin());f.Ready();f.Commit();
    DeviceSd::files["/Tunes/test.btt"].bytes=Tune(true);
    CHECK(f.Begin());
    for(unsigned i=0;i<400 && f.load->Busy();++i)f.Turn();
    CHECK_EQ(f.load->phase(),LoadPhase::Failed);
    CHECK_EQ(std::string(f.load->reason()),"initial-pattern-fx-unsupported");
    CHECK(f.load->active());CHECK_EQ(f.control.Snapshot().tempo,14500u);
    CHECK_EQ(f.control.Snapshot().active_rows,3u);
    CHECK(DeviceSd::files[historical].bytes==std::vector<uint8_t>({1,2,3,4}));
}
TEST_CASE(BttStopped_namespace_file_and_close_quarantine_never_stage_input) {
    {
        Fixture f;DeviceSd::files[BTT_BACKING_DIRECTORY].bytes={9,8,7};
        const auto creates=DeviceSd::created,removes=DeviceSd::removed;
        CHECK(f.Begin());for(unsigned i=0;i<10 && f.load->Busy();++i)f.Turn();
        CHECK_EQ(f.load->phase(),LoadPhase::Failed);
        CHECK(DeviceSd::files[BTT_BACKING_DIRECTORY].bytes==std::vector<uint8_t>({9,8,7}));
        CHECK_EQ(DeviceSd::created,creates);CHECK_EQ(DeviceSd::removed,removes);
        CHECK_EQ(DeviceSd::files["/Tunes/test.btt"].opens,0u);
    }
    {
        Fixture f;DeviceSd::files[BTT_BACKING_DIRECTORY].directory=true;
        DeviceSd::fail_close_path=BTT_BACKING_DIRECTORY;
        CHECK(f.Begin());f.Turn();
        CHECK_EQ(f.load->phase(),LoadPhase::Cleanup);
        CHECK_EQ(std::string(f.load->reason()),"namespace-close-quarantine");
        CHECK_EQ(DeviceSd::files[BTT_BACKING_DIRECTORY].opens,1u);
        CHECK_EQ(DeviceSd::files["/Tunes/test.btt"].opens,0u);
        CHECK(!f.Begin());f.Turn();CHECK(f.load->Busy());
        DeviceSd::fail_close_path.clear();f.Turn();
        CHECK_EQ(f.load->phase(),LoadPhase::Failed);
        CHECK_EQ(DeviceSd::files[BTT_BACKING_DIRECTORY].opens,0u);
        CHECK_EQ(f.control.Snapshot().tempo,12500u);
    }
}
TEST_CASE(BttStopped_audition_acknowledges_consumed_and_cancelled_legacy_jobs) {
    Fixture f;SdWavAudition audition(f.producer);
    CHECK(audition.Request(1,"Samples/a.wav"));
    for(unsigned i=0;i<100 && audition.Busy();++i)audition.Service(f.bindings);
    CHECK(!audition.Busy());uint32_t ticket=0;CHECK(f.producer.BeginOwnedPreparation("Samples/a.wav",ticket));
    SdSampleStreaming::CandidatePin pin;CHECK(f.producer.TakeOwnedPreparation(ticket,pin));CHECK(f.producer.ReleaseCandidatePin(pin));
    audition.Cancel();CHECK(audition.Request(2,"Samples/a.wav"));audition.Service(f.bindings);audition.Cancel();
    for(unsigned i=0;i<100 && audition.Busy();++i)audition.Service(f.bindings);
    ticket=0;pin={};CHECK(f.producer.BeginOwnedPreparation("Samples/a.wav",ticket));
    for(unsigned i=0;i<100 && f.producer.OwnedPreparationStatus(ticket)==SdSampleStreaming::PreparationStatus::Pending;++i)f.producer.StepOwnedPreparation(ticket);
    CHECK(f.producer.TakeOwnedPreparation(ticket,pin));CHECK(f.producer.ReleaseCandidatePin(pin));
}
TEST_CASE(BttStopped_source_and_path_capacity_preserve_working_state) {
    {
        Fixture f;SdSampleStreaming::CandidatePin pins[13];
        for(unsigned i=0;i<13;++i) {
            const auto path="Samples/filled"+std::to_string(i)+".wav";DeviceSd::files["/"+path].bytes=Wav();
            Pcm16MixedSampleView view;CHECK_EQ(f.producer.PreparePath(path.c_str(),view),InstrumentAvailability::Ready);
            CHECK_EQ(f.producer.AcquireCandidatePin(f.producer.PreparedGeneration(path.c_str()),pins[i]),SdSampleStreaming::PinResult::Ready);
        }
        CHECK(f.Begin());for(unsigned i=0;i<150 && f.load->phase()!=LoadPhase::Failed;++i)f.Turn();
        CHECK_EQ(std::string(f.load->reason()),"sample-capacity");CHECK_EQ(f.control.Snapshot().tempo,12500u);
        for(auto pin:pins)CHECK(f.producer.ReleaseCandidatePin(pin));
    }
    {
        Fixture f;std::vector<ResourcePathRef> held;
        for(unsigned i=0;i<256;++i) {
            auto path="Samples/"+std::to_string(i)+std::string(180,'x')+".wav";ResourcePathRef ref;
            if(!ref.Assign(path.c_str()))break;held.push_back(std::move(ref));
        }
        for(unsigned i=0;i<256;++i){ResourcePathRef ref;const auto path="p"+std::to_string(i);if(!ref.Assign(path.c_str()))break;held.push_back(ref);}
        CHECK(f.Begin());for(unsigned i=0;i<150 && f.load->phase()!=LoadPhase::Failed;++i)f.Turn();
        CHECK_EQ(std::string(f.load->reason()),"path-capacity");CHECK_EQ(f.definitions.Count(),0u);
    }
}
TEST_CASE(BttStopped_owned_sample_close_quarantine_drains_without_replacing_active) {
    Fixture f;CHECK(f.Begin());
    for(unsigned i=0;i<150 && !DeviceSd::files["/Samples/a.wav"].opens;++i)f.Turn();
    CHECK(DeviceSd::files["/Samples/a.wav"].opens>0);
    DeviceSd::fail_close_path="/Samples/a.wav";CHECK(f.load->Cancel());
    for(unsigned i=0;i<10;++i)f.Turn();CHECK_EQ(f.load->phase(),LoadPhase::Cleanup);
    CHECK_EQ(f.control.Snapshot().tempo,12500u);CHECK(!f.Begin());
    DeviceSd::fail_close_path.clear();for(unsigned i=0;i<100 && f.load->Busy();++i)f.Turn();
    CHECK_EQ(f.load->phase(),LoadPhase::Failed);CHECK(!f.load->active());
}
TEST_CASE(BttStopped_cancel_and_failure_keep_previous_full_tune) {
    Fixture f;CHECK(f.Begin());f.Ready();f.Commit();const auto* old=f.load->active();
    const auto old_identity=old->backing()->identity();
    DeviceSd::files["/Tunes/test.btt"].bytes=Tune(false,true,16000);
    CHECK(f.Begin());for(unsigned i=0;i<150 && f.load->phase()!=LoadPhase::Failed;++i)f.Turn();
    CHECK_EQ(f.load->phase(),LoadPhase::Failed);CHECK_EQ(f.load->active()->backing()->identity(),old_identity);
    CHECK_EQ(f.control.Snapshot().tempo,14500u);CHECK_EQ(f.definitions.Count(),2u);
    CHECK_EQ(f.load->StableId(0),22u);CHECK_EQ(f.load->StableId(1),11u);
    CHECK(f.Begin());f.Turn();CHECK(f.load->Cancel());for(unsigned i=0;i<100 && f.load->Busy();++i)f.Turn();
    CHECK_EQ(f.load->active()->backing()->identity(),old_identity);CHECK_EQ(f.control.Snapshot().active_rows,3u);
}
TEST_CASE(BttStopped_rechecks_idle_before_submit_and_rejects_invalid_configuration_atomically) {
    Fixture f;CHECK(f.Begin());f.Ready();CHECK_EQ(f.control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    CHECK(!f.load->Submit(f.control,f.publication,0));CHECK(!f.publication.Busy());CHECK_EQ(f.control.Snapshot().tempo,12500u);
    CHECK(f.load->Cancel());for(unsigned i=0;i<100 && f.load->Busy();++i)f.Turn();
    // Separate real FIFO rejection: Configure must leave the old tempo/pattern
    // and playable configuration intact when a frozen binding is invalid.
    std::int16_t out[1];f.control.AudioBlock(out,1,true);PatternAppliedRequest ack;CHECK(f.control.TakeApplied(ack));
    CHECK_EQ(f.control.Submit(PatternRequest::Stop),PatternRequestStatus::Accepted);f.control.AudioBlock(out,1,true);CHECK(f.control.TakeApplied(ack));
    RealtimePattern next;next.active_rows=3;VariableRateSampleBindings invalid;invalid.count=1;
    invalid.bindings[0].instrument=0;invalid.bindings[0].reference_note=60;
    CHECK_EQ(f.control.SubmitStoppedTune(next,invalid,77,16000),PatternRequestStatus::Accepted);
    f.control.AudioBlock(out,1,true);CHECK(f.control.TakeApplied(ack));CHECK(ack.result!=PatternPlayerStatus::Success);
    CHECK_EQ(f.control.Snapshot().tempo,12500u);CHECK_EQ(f.control.Snapshot().active_rows,7u);
    CHECK_EQ(f.control.Snapshot().transport,PatternTransportState::Stopped);
}
TEST_CASE(BttStopped_read_seal_scan_and_projection_faults_preserve_active_tune) {
    for(const auto boundary:{LoadPhase::Copy,LoadPhase::Seal,LoadPhase::Validate,LoadPhase::Scan,LoadPhase::Project}) {
        Fixture f;CHECK(f.Begin());f.Ready();f.Commit();const auto* identity=f.load->active()->backing()->identity();
        CHECK(f.Begin());for(unsigned i=0;i<200 && f.load->phase()!=boundary;++i)f.Turn();
        CHECK_EQ(f.load->phase(),boundary);
        std::string snapshot;
        for(const auto& file:DeviceSd::files)if(file.first.find(".btt-load/")!=std::string::npos && !file.second.bytes.empty())snapshot=file.first;
        // Select the new snapshot by its open exclusive writer (the accepted
        // old snapshot also remains open); Copy still starts before create.
        if(boundary==LoadPhase::Copy)DeviceSd::fail_read_path="/Tunes/test.btt";
        else {
            for(const auto& file:DeviceSd::files)if(file.first.find(".btt-load/")!=std::string::npos && file.first!=snapshot) {
                // Registry slot 1 is the candidate after first publication.
                if(file.first.find("-1-")!=std::string::npos)snapshot=file.first;
            }
            if(boundary==LoadPhase::Seal)DeviceSd::fail_sync_path=snapshot;
            else DeviceSd::fail_read_path=snapshot;
        }
        for(unsigned i=0;i<200 && f.load->phase()!=LoadPhase::Failed;++i)f.Turn();
        CHECK_EQ(f.load->phase(),LoadPhase::Failed);CHECK_EQ(f.load->active()->backing()->identity(),identity);
        CHECK_EQ(f.control.Snapshot().tempo,14500u);CHECK_EQ(f.definitions.Count(),2u);
    }
}
namespace {
struct SavedBytes final:btt::Reader {
    std::vector<uint8_t> bytes;
    explicit SavedBytes(std::vector<uint8_t> b):bytes(std::move(b)){}
    uint32_t size() const override{return bytes.size();}
    const void* identity() const override{return this;}
    unsigned read(uint32_t at,void* p,unsigned n)const override {
        if(n>512 || at>bytes.size() || n>bytes.size()-at)return 0;
        std::memcpy(p,bytes.data()+at,n);return n;
    }
};
void CheckSaved(const std::vector<uint8_t>& original,const std::vector<uint8_t>& saved) {
    btt::Document old,now;btt::Workspace work;
    CHECK_EQ(btt::Validate(std::make_shared<SavedBytes>(original),old,work),btt::Result::Ok);
    CHECK_EQ(btt::Validate(std::make_shared<SavedBytes>(saved),now,work),btt::Result::Ok);
    CHECK_EQ(old.count,now.count);CHECK_EQ(old.bpm,now.bpm);CHECK_EQ(saved.size(),original.size()+20);
    for(unsigned i=0;i<old.count;++i) {
        const auto& a=old.entries[i];const auto& b=now.entries[i];
        CHECK(!std::memcmp(a.type,b.type,4));CHECK_EQ(a.id,b.id);CHECK_EQ(a.flags,b.flags);
        if(std::memcmp(a.type,"INST",4) || a.id==33) {
            CHECK_EQ(a.length,b.length);CHECK(!std::memcmp(original.data()+a.offset,saved.data()+b.offset,a.length));
        } else {
            // Original complete TLVs are unchanged; only the absent default is appended.
            CHECK_EQ(a.length+10,b.length);CHECK(!std::memcmp(original.data()+a.offset,saved.data()+b.offset,a.length));
        }
    }
    btt::SavedInstrument inst;
    CHECK_EQ(btt::InspectInstrument(now,11,inst,work),btt::Result::Ok);CHECK_EQ(inst.root,60u);CHECK_EQ(inst.fine,12u);
    CHECK_EQ(btt::InspectInstrument(now,22,inst,work),btt::Result::Ok);CHECK_EQ(inst.root,72u);CHECK_EQ(inst.fine,0u);
    CHECK_EQ(btt::InspectInstrument(now,33,inst,work),btt::Result::Ok);CHECK_EQ(inst.root,0u);CHECK_EQ(inst.fine,199u);
}
}
TEST_CASE(BttStoppedSave_full_document_repeats_duplicates_and_original_destination) {
    Fixture f;const auto original=DeviceSd::files["/Tunes/test.btt"].bytes;
    const std::vector<uint8_t> legacy{4,8,3,42};
    DeviceSd::files["/BroTracker/current.btsession"].bytes=legacy;
    DeviceSd::files["/BroTracker/current.btsession.previous"].bytes=legacy;
    CHECK(f.Begin());f.Ready();f.Commit();const auto* owner=f.load->active();const auto* identity=owner->backing()->identity();
    const auto refs=owner->backing().use_count();const auto paths=ResourcePaths().UniqueCount();
    const auto bindings=f.bindings;const auto notes=f.readback;
    for(uint32_t id=1;id<=8;++id) {
        CHECK_EQ(f.Save(id).status,SaveStatus::Saved);
        CheckSaved(original,DeviceSd::files["/Tunes/test.btt"].bytes);
        const auto writes=DeviceSd::write_calls,opens=DeviceSd::open_calls,renames=DeviceSd::rename_calls;
        CHECK_EQ(f.Save(id,false).status,SaveStatus::Saved); // Replay even if transport is no longer settled.
        CHECK_EQ(DeviceSd::write_calls,writes);CHECK_EQ(DeviceSd::open_calls,opens);CHECK_EQ(DeviceSd::rename_calls,renames);
        CHECK_EQ(f.load->active(),owner);CHECK_EQ(owner->backing()->identity(),identity);CHECK_EQ(owner->backing().use_count(),refs);
        CHECK_EQ(ResourcePaths().UniqueCount(),paths);CHECK(!std::memcmp(&f.bindings,&bindings,sizeof(bindings)));
        CHECK(!std::memcmp(&f.readback,&notes,sizeof(notes)));CHECK_EQ(f.control.Snapshot().tempo,14500u);
        CHECK_EQ(f.control.Snapshot().transport,PatternTransportState::Stopped);
        uint8_t block[512];for(unsigned at=0;at<original.size();at+=512){const auto n=std::min<size_t>(512,original.size()-at);
            CHECK_EQ(owner->backing()->read(at,block,n),n);CHECK(!std::memcmp(block,original.data()+at,n));}
    }
    const auto writes=DeviceSd::write_calls;CHECK_EQ(f.Save(1).status,SaveStatus::Rejected);CHECK_EQ(DeviceSd::write_calls,writes);
    DeviceSd::files["/Tunes/failed.btt"].bytes=Tune(true);
    CHECK(f.load->Begin("Tunes/failed.btt",303,true));for(unsigned i=0;i<300 && f.load->Busy();++i)f.Turn();
    CHECK_EQ(f.load->phase(),LoadPhase::Failed);CHECK_EQ(std::string(f.load->active_path()),"Tunes/test.btt");
    const auto bad=DeviceSd::files["/Tunes/failed.btt"].bytes;CHECK_EQ(f.Save(9).status,SaveStatus::Saved);
    CHECK(DeviceSd::files["/Tunes/failed.btt"].bytes==bad);CheckSaved(original,DeviceSd::files["/Tunes/test.btt"].bytes);
    CHECK(f.load->Begin("Tunes/failed.btt",303,true));CHECK(f.load->Cancel());for(unsigned i=0;i<50 && f.load->Busy();++i)f.Turn();
    CHECK_EQ(f.Save(10).status,SaveStatus::Saved);CHECK_EQ(std::string(f.load->active_path()),"Tunes/test.btt");
    // Switching the scratch union back to Pattern and then comparison is repeatable.
    DeviceSd::files["/Tunes/new.btt"].bytes=Tune(false,false,16000);
    CHECK(f.load->Begin("Tunes/new.btt",303,true));f.Ready();f.Commit();
    CHECK_EQ(std::string(f.load->active_path()),"Tunes/new.btt");CHECK_EQ(f.Save(11).status,SaveStatus::Saved);
    CHECK_EQ(f.control.Snapshot().tempo,16000u);
    CHECK(DeviceSd::files["/BroTracker/current.btsession"].bytes==legacy);
    CHECK(DeviceSd::files["/BroTracker/current.btsession.previous"].bytes==legacy);
    CHECK(!SD.sdfs.exists("/BroTracker/current.btsession.candidate"));CHECK(!SD.sdfs.exists("/BroTracker/tune.btdraft"));
}
TEST_CASE(BttStoppedSave_guards_reject_before_io_and_cache_rejections) {
    Fixture f;CHECK_EQ(f.Save(1).status,SaveStatus::Rejected);
    CHECK_EQ(std::string(f.load->SavedStatus(1).reason),"no-retained-tune");
    CHECK(f.Begin());const auto opens=DeviceSd::open_calls;
    CHECK_EQ(f.Save(2).status,SaveStatus::Rejected);CHECK_EQ(DeviceSd::open_calls,opens);
    f.Ready();f.Commit();CHECK_EQ(f.Save(1).status,SaveStatus::Rejected);
    uint32_t id=3;
    for(auto transport:{PatternRequest::Start,PatternRequest::Pause,PatternRequest::Stop}) {
        CHECK_EQ(f.control.Submit(transport),PatternRequestStatus::Accepted);
        const auto before=DeviceSd::open_calls;CHECK_EQ(f.Save(id++).status,SaveStatus::Rejected);CHECK_EQ(DeviceSd::open_calls,before);
        std::int16_t out[1];f.control.AudioBlock(out,1,true);f.producer.Publish(f.control.OutputPlayer().MidiPlayer());
        CHECK_EQ(f.Save(id++).status,SaveStatus::Rejected);CHECK_EQ(DeviceSd::open_calls,before); // ACK still pending.
        PatternAppliedRequest ack;CHECK(f.control.TakeApplied(ack));
        if(transport!=PatternRequest::Stop){CHECK_EQ(f.Save(id++).status,SaveStatus::Rejected);CHECK_EQ(DeviceSd::open_calls,before);}
    }
    const auto before=DeviceSd::open_calls;const auto rejected=id++;
    CHECK_EQ(f.Save(rejected,false).status,SaveStatus::Rejected);CHECK_EQ(f.Save(rejected).status,SaveStatus::Rejected);
    CHECK_EQ(DeviceSd::open_calls,before); // Rejected ID cannot become an implicit deferred Save.
    CHECK_EQ(f.Save(id).status,SaveStatus::Saved);CHECK_EQ(f.Save(0).status,SaveStatus::Rejected);
}
TEST_CASE(BttStoppedSave_real_publication_audition_and_midi_readiness_reject_before_io) {
    Fixture f;CHECK(f.Begin());f.Ready();f.Commit();uint32_t id=1;
    CHECK(StageStoredPattern(f.publication,f.readback,14500,0));
    auto before=DeviceSd::open_calls;CHECK_EQ(f.Save(id++).status,SaveStatus::Rejected);CHECK_EQ(DeviceSd::open_calls,before);
    f.publication.Finish();
    SdWavAudition audition(f.producer);CHECK(audition.Request(1,"Samples/a.wav"));CHECK(audition.Busy());
    before=DeviceSd::open_calls;
    CHECK_EQ(f.Save(id++,!audition.Busy() && !audition.Snapshot().sample).status,SaveStatus::Rejected);
    CHECK_EQ(DeviceSd::open_calls,before);audition.Cancel();
    MidiOutput midi;CHECK(!midi.StorageReady(false));
    CHECK_EQ(f.Save(id++,midi.StorageReady(false)).status,SaveStatus::Rejected);CHECK_EQ(DeviceSd::open_calls,before);
    midi.Reset();CHECK(!midi.StorageReady(true));
    CHECK_EQ(f.Save(id++,midi.StorageReady(true)).status,SaveStatus::Rejected);CHECK_EQ(DeviceSd::open_calls,before);
    CHECK_EQ(f.control.SubmitMute(1,1),PatternRequestStatus::Accepted);
    CHECK_EQ(f.Save(id++).status,SaveStatus::Rejected);CHECK_EQ(DeviceSd::open_calls,before);
    std::int16_t out[1];f.control.AudioBlock(out,1,true);CHECK_EQ(f.control.Snapshot().applied_mute_id,1u);
    CHECK_EQ(f.Save(id++).status,SaveStatus::Saved);
    CHECK_EQ(f.Save(UINT32_MAX).status,SaveStatus::Saved);
    before=DeviceSd::open_calls;CHECK_EQ(f.Save(1).status,SaveStatus::Rejected);CHECK_EQ(DeviceSd::open_calls,before);
    CHECK_EQ(f.Save(UINT32_MAX).status,SaveStatus::Saved);CHECK_EQ(DeviceSd::open_calls,before);
}
TEST_CASE(BttStoppedSave_fault_recovery_keeps_failure_commit_and_active_ownership) {
    constexpr auto candidate="/Tunes/test.btt.candidate",previous="/Tunes/test.btt.previous";
    for(unsigned fault=0;fault<7;++fault) {
        Fixture f;CHECK(f.Begin());f.Ready();f.Commit();const auto old=DeviceSd::files["/Tunes/test.btt"].bytes;
        const auto* identity=f.load->active()->backing()->identity();const auto paths=ResourcePaths().UniqueCount();
        if(fault==0)DeviceSd::fail_write_path=candidate;
        if(fault==1)DeviceSd::fail_close_path=candidate;
        if(fault==2 || fault==3 || fault==5)DeviceSd::fail_rename=2;
        if(fault==3)DeviceSd::fail_rename_second=3;
        if(fault==4)DeviceSd::fail_remove_path=previous;
        if(fault==5)DeviceSd::fail_remove_path=candidate;
        if(fault==6)DeviceSd::files[candidate].bytes={9,8,7};
        const auto result=f.Save(1);CHECK_EQ(result.committed,fault==4);
        if(fault==0 || fault==2)CHECK_EQ(result.status,SaveStatus::Failed);
        else CHECK_EQ(result.status,fault==4?SaveStatus::BackupRetained:SaveStatus::RecoveryRequired);
        if(fault==3){CHECK(!SD.sdfs.exists("/Tunes/test.btt"));CHECK(DeviceSd::files[previous].bytes==old);CheckSaved(old,DeviceSd::files[candidate].bytes);}
        else if(fault==4){CheckSaved(old,DeviceSd::files["/Tunes/test.btt"].bytes);CHECK(DeviceSd::files[previous].bytes==old);}
        else CHECK(DeviceSd::files["/Tunes/test.btt"].bytes==old);
        if(fault==5)CHECK_EQ(result.cause,SaveOutcome::PromotionFailed);
        CHECK_EQ(f.load->active()->backing()->identity(),identity);CHECK_EQ(ResourcePaths().UniqueCount(),paths);
        CHECK_EQ(f.control.Snapshot().tempo,14500u);CHECK_EQ(f.definitions.Count(),2u);
        const auto opens=DeviceSd::open_calls;CHECK_EQ(f.Save(1).status,result.status);CHECK_EQ(DeviceSd::open_calls,opens);
        const bool pending=fault==1 || fault==3 || fault==4 || fault==5;
        CHECK_EQ(f.load->SaveRecoveryPending(),pending);
        if(pending) {
            CHECK(!f.Begin());CHECK_EQ(f.Save(2).status,SaveStatus::Rejected);
            CHECK_EQ(f.load->SavedStatus(1).status,result.status);CHECK_EQ(f.load->SavedStatus(1).committed,result.committed);
            CHECK_EQ(f.load->Recover(1,1,false).status,RecoveryStatus::Rejected);
            CHECK_EQ(f.load->Recover(1,1,true).status,RecoveryStatus::Rejected); // Replay, no automatic cleanup retry.
        }
        DeviceSd::fail_write_path.clear();DeviceSd::fail_close_path.clear();DeviceSd::fail_remove_path.clear();
        DeviceSd::fail_rename=DeviceSd::fail_rename_second=0;
        if(fault==6) {
            CHECK_EQ(f.load->Recover(2,1,true).status,RecoveryStatus::Rejected);
            CHECK(DeviceSd::files[candidate].bytes==std::vector<uint8_t>({9,8,7}));DeviceSd::files.erase(candidate);
        } else {
            CHECK_EQ(f.load->Recover(2,1,true).status,RecoveryStatus::Cleaned);CHECK(!f.load->SaveRecoveryPending());
            const auto calls=DeviceSd::rename_calls;CHECK_EQ(f.load->Recover(2,1,true).status,RecoveryStatus::Cleaned);CHECK_EQ(DeviceSd::rename_calls,calls);
            CHECK_EQ(f.load->Recover(2,999,true).status,RecoveryStatus::Rejected);
        }
        CHECK_EQ(f.load->SavedStatus(1).outcome,result.outcome);CHECK_EQ(f.load->SavedStatus(1).cause,result.cause);
        CHECK_EQ(f.Save(3).status,SaveStatus::Saved);CheckSaved(old,DeviceSd::files["/Tunes/test.btt"].bytes);
    }
}
TEST_CASE(BttStoppedSave_source_read_failure_releases_only_disposable_index) {
    Fixture f;CHECK(f.Begin());f.Ready();f.Commit();const auto* owner=f.load->active();
    const auto refs=owner->backing().use_count();const auto* identity=owner->backing()->identity();
    for(const auto& file:DeviceSd::files)if(file.first.find(".btt-load/")!=std::string::npos && file.second.opens)
        DeviceSd::fail_read_path=file.first;
    const auto created=DeviceSd::created,opens=DeviceSd::open_calls;
    const auto failed=f.Save(1);CHECK_EQ(failed.status,SaveStatus::Failed);CHECK_EQ(failed.detail,btt::Result::Io);
    CHECK_EQ(std::string(failed.reason),"source-validation");CHECK_EQ(DeviceSd::created,created);CHECK_EQ(DeviceSd::open_calls,opens);
    CHECK_EQ(owner->backing().use_count(),refs);CHECK_EQ(owner->backing()->identity(),identity);
    CHECK_EQ(f.control.Snapshot().tempo,14500u);CHECK(!f.load->SaveRecoveryPending());
    DeviceSd::fail_read_path.clear();CHECK_EQ(f.Save(1).status,SaveStatus::Failed);CHECK_EQ(DeviceSd::open_calls,opens);
    CHECK_EQ(f.Save(2).status,SaveStatus::Saved);
}
TEST_CASE(BttStoppedSave_retirement_cleanup_blocks_save_without_changing_confirmed_destination) {
    Fixture f;CHECK(f.Begin());f.Ready();f.Commit();
    for(const auto& file:DeviceSd::files)if(file.first.find(".btt-load/")!=std::string::npos && file.second.opens)
        DeviceSd::fail_close_path=file.first;
    DeviceSd::files["/Tunes/new.btt"].bytes=Tune(false,false,16000);
    CHECK(f.load->Begin("Tunes/new.btt",303,true));f.Ready();f.Commit();
    CHECK_EQ(f.load->phase(),LoadPhase::Cleanup);CHECK_EQ(std::string(f.load->active_path()),"Tunes/new.btt");
    const auto opens=DeviceSd::open_calls;CHECK_EQ(f.Save(1).status,SaveStatus::Rejected);CHECK_EQ(DeviceSd::open_calls,opens);
    CHECK_EQ(f.control.Snapshot().tempo,16000u);
    DeviceSd::fail_close_path.clear();for(unsigned i=0;i<100 && f.load->Busy();++i)f.Turn();
    CHECK_EQ(f.load->phase(),LoadPhase::Complete);CHECK_EQ(f.Save(2).status,SaveStatus::Saved);
}
TEST_CASE(BttStoppedSave_rejects_even_valid_substitution_of_accepted_snapshot) {
    Fixture f;CHECK(f.Begin());f.Ready();f.Commit();std::string path;
    for(const auto& file:DeviceSd::files)if(file.first.find(".btt-load/")!=std::string::npos && file.second.opens)path=file.first;
    CHECK(!path.empty());const auto original=DeviceSd::files[path].bytes;
    const auto* identity=f.load->active()->backing()->identity();
    const auto replacement=Tune(false,false,14600);CHECK_EQ(replacement.size(),original.size());
    DeviceSd::files[path].bytes=replacement; // Deliberate registry-contract violation, with valid recomputed CRCs.
    const auto created=DeviceSd::created;const auto result=f.Save(1);
    CHECK_EQ(result.status,SaveStatus::Failed);CHECK_EQ(result.detail,btt::Result::Backing);
    CHECK_EQ(DeviceSd::created,created);CHECK_EQ(f.control.Snapshot().tempo,14500u);
    CHECK_EQ(f.load->active()->backing()->identity(),identity);
    DeviceSd::files[path].bytes=original;CHECK_EQ(f.Save(2).status,SaveStatus::Saved);
    std::cout<<"production-save allocation audit: calls="<<save_calls<<" adapter="<<save_allocations
        <<" fake-STL="<<fake_save_allocations<<"\n";
}


#include "ui/bringup_serial.h"
#include "bringup/btt_serial_policy.h"
#include "temporary_directory.h"
namespace {
constexpr char export_boot[]="1234567890abcdef1234567890abcdef";
BttExportRequest TestExportRequest(BttExportVerb v,uint32_t id,uint32_t g,uint32_t o) {
    BttExportRequest r{v,id,g,o};std::strcpy(r.nonce,export_boot);return r;
}
struct ExportPeer final:SerialTransport {
    Fixture& fixture;std::string rx,partial;bool healthy=true,hold=false,advertise=true,boot_available=true;
    unsigned write_limit=7,read_limit=5,now=0,export_lines=0,backpressure=0;
    std::string fault;bool disconnect_end=false,disconnect_last=false;std::vector<std::string> commands;
    explicit ExportPeer(Fixture& f):fixture(f){}
    bool Open(FILE*,const char*)override{return healthy;}
    void Close()override{fixture.load->ExportDisconnect();rx.clear();partial.clear();}
    bool Healthy()override{return healthy;}
    const char* Error()const override{return "export test disconnect";}
    int Read(char* out,unsigned capacity)override {
        if(hold)return 0;
        const auto n=std::min<unsigned>({capacity,read_limit,unsigned(rx.size())});
        std::memcpy(out,rx.data(),n);rx.erase(0,n);return n;
    }
    int Write(const char* in,unsigned count)override {
        if(++backpressure%4==0)return 0;
        const auto n=std::min(count,write_limit);partial.append(in,n);
        if(partial.find('\n')!=partial.npos){const auto line=partial.substr(0,partial.find('\n'));partial.erase(0,line.size()+1);Dispatch(line);}
        return n;
    }
    void Dispatch(const std::string& line) {
        commands.push_back(line);char reply[kBttExportLine];BttExportRequest request;
        if(ParseBttExportRequest(line,request)) {
            ++export_lines;
            if(request.verb==BttExportVerb::Next && fault=="substitute")
                for(auto& pair:DeviceSd::files)if(pair.first.find(".btt-load/")!=std::string::npos)pair.second.bytes=Tune(false,false,14501);
            if(request.verb==BttExportVerb::Next && fault=="source-read")
                for(const auto& pair:DeviceSd::files)if(pair.first.find(".btt-load/")!=std::string::npos)DeviceSd::fail_read_path=pair.first;
            const auto* result=fixture.load->ExportCommand(request,boot_available?export_boot:"",fixture.control.StorageReady(),now,reply);
            DeviceSd::fail_read_path.clear();
            std::string response=result;
            if((disconnect_end && request.verb==BttExportVerb::End) ||
               (disconnect_last && request.verb==BttExportVerb::Next && request.offset+64>=fixture.load->active()->backing()->size())){healthy=false;return;}
            if(request.verb==BttExportVerb::Begin && fault=="boot")response.replace(response.find(export_boot),32,"abcdef1234567890abcdef1234567890");
            if((request.verb==BttExportVerb::Next && (fault=="offset" || fault=="generation")) ||
               (request.verb==BttExportVerb::Begin && fault=="capacity")) {
                std::array<std::string_view,10> words{};const auto count=BttExportWords(std::string_view(response).substr(0,response.size()-1),words);
                const unsigned field=fault=="generation"?4:5;std::string changed;
                for(unsigned i=0;i<count;++i){if(i)changed+=' ';changed+=i==field?(fault=="capacity"?"1048577":"999"):std::string(words[i]);}
                response=changed+'\n';
            }
            if(request.verb==BttExportVerb::Next && !fault.empty()) {
                if(fault=="challenge")response[response.size()-2]=response[response.size()-2]=='0'?'1':'0';
                if(fault=="hex")response[response.find_last_of(' ')-1]='z';
                if(fault=="crc")response[response.find_last_of(' ')-1]=response[response.find_last_of(' ')-1]=='0'?'1':'0';
                if(fault=="duplicate")response+=response;
                if(fault=="identity")response.replace(response.find(" 1 ")+3,1,"9");
                if(fault=="truncated")response="BTBTT1 EXPORTDATA 1 1\n";
            }
            rx+=response;return;
        }
        if(line=="BTTEST1 HELLO" || line=="BTTEST1 STATUS") {
            rx+="BTAUDIO1 CAP 1\nBTTEST1 STATE IDLE\nBTPATTERN1 SNAPCAP 2\nBTPATTERN1 POS 2 0 0 0 0 0 3 8\n";
            rx+=std::string("BTDEVICE1 CAP 1\nBTDEVICE1 FACTS 1 17681760 ")+(boot_available?export_boot:"unavailable")+" initialized bridge\n";
            if(advertise)rx+="BTBTT1 EXPORTCAP 1 64\n";
        }else if(line=="BTAUDIO1 SET 1 default")rx+="BTAUDIO1 SELECTED 1 bridge\n";
        else if(line=="BTTEST1 STOP")rx+="BTTEST1 STOPPED\n";
        else {uint32_t id=0;unsigned version=0;if(ParseSnapshotRequest(line.data(),line.size(),id,version)) {
            PatternSnapshotSender sender(fixture.load->projection(),fixture.load->tempo());CHECK(sender.Begin(id,now,version));
            while(sender.HasData()){size_t n;const auto* p=sender.Data(n);rx.append(p,n);sender.Consume(n);}
        }}
    }
};
struct ClientFixture {
    TemporaryTestDirectory directory;Fixture device;ExportPeer* wire;
    std::unique_ptr<BringUpSerial> client;LocalPatternEditor editor;unsigned now=1;
    ClientFixture(bool boot=true,bool capability=true,std::shared_ptr<BttCandidateIo> io={},FILE* log=nullptr) {
        CHECK(device.Begin());device.Ready();device.Commit();
        auto peer=std::make_unique<ExportPeer>(device);wire=peer.get();wire->boot_available=boot;wire->advertise=capability;client=std::make_unique<BringUpSerial>(log,std::move(peer),std::move(io));
        Pump(400);CHECK(!client->UserActionsBlocked());CHECK(client->BttExportSupported()==capability);
    }
    void Pump(unsigned turns) {for(unsigned i=0;i<turns;++i){wire->now=now;client->Tick(now);client->SyncEditor(editor,now);now+=2;}}
    void Export() {CHECK(client->RequestBttExport(now,directory.Path()));for(unsigned i=0;i<5000 && client->BttExportActive();++i)Pump(1);CHECK(!client->BttExportActive());}
};
std::vector<uint8_t> CandidateBytes(const btt::ReaderOwner& owner) {
    CHECK(owner);if(!owner)return {};std::vector<uint8_t> data(owner->size());
    for(unsigned at=0;at<data.size();at+=512){const unsigned n=std::min<unsigned>(512,data.size()-at);CHECK_EQ(owner->read(at,data.data()+at,n),n);}return data;
}
}
TEST_CASE(BttExport_actual_client_complete_exact_independent_candidate_and_repeated) {
    ClientFixture f;const auto expected=DeviceSd::files["/Tunes/test.btt"].bytes;
    const auto created=DeviceSd::created;f.Export();CHECK(f.client->BttCandidate());
    CHECK(CandidateBytes(f.client->BttCandidate())==expected);CHECK_EQ(DeviceSd::created,created);
    btt::Document doc;btt::Workspace work;CHECK_EQ(btt::Validate(f.client->BttCandidate(),doc,work),btt::Result::Ok);
    // Changing the original pathname cannot substitute for the immutable source.
    DeviceSd::files["/Tunes/test.btt"].bytes=Tune(false,false,14600);
    // A failed new LOAD changes attempt metadata, not the accepted export identity.
    CHECK(f.device.load->Begin("Tunes/absent.btt",999,true));for(unsigned i=0;i<50 && f.device.load->Busy();++i)f.device.Turn();
    f.Export();CHECK(CandidateBytes(f.client->BttCandidate())==expected);
    const auto independent=f.client->BttCandidate();f.wire->healthy=false;f.Pump(1);
    CHECK(CandidateBytes(independent)==expected);CHECK(!f.device.load->ExportActive());
}
TEST_CASE(BttExport_actual_client_rejects_malformed_corrupt_duplicate_stale_boot_and_preserves_prior) {
    for(const auto* fault:{"hex","crc","duplicate","identity","truncated","boot","substitute","source-read","offset","generation","capacity","challenge"}) {
        ClientFixture f;f.Export();const auto old=f.client->BttCandidate();f.wire->fault=fault;
        f.Export();CHECK(f.client->BttCandidate().get()==old.get());CHECK(!f.device.load->ExportActive());
        CHECK(CandidateBytes(old)==DeviceSd::files["/Tunes/test.btt"].bytes);
    }
}
TEST_CASE(BttExport_guards_pin_cancel_timeout_read_failure_and_source_substitution) {
    Fixture f;char reply[kBttExportLine];auto command=[&](BttExportRequest r,bool ready=true,unsigned now=1){std::strcpy(r.nonce,export_boot);return std::string(f.load->ExportCommand(r,export_boot,ready,now,reply));};
    CHECK(command({BttExportVerb::Begin,1,0,0}).find("no-source")!=std::string::npos);
    CHECK(f.Begin());f.Ready();f.Commit();const auto pins=f.load->active()->backing().use_count();
    CHECK(command({BttExportVerb::Begin,2,0,0},false).find("not-settled")!=std::string::npos);
    CHECK(command({BttExportVerb::Begin,3,0,0}).find("EXPORTBEGIN 1 3 1 ")!=std::string::npos);
    CHECK_EQ(f.load->active()->backing().use_count(),pins+1);
    CHECK(!f.load->Begin("Tunes/test.btt",303,true));CHECK_EQ(f.load->Save(1,true).status,SaveStatus::Rejected);
    CHECK(BttSerialPolicy("BTTEST1 STOP",false,true,false,true,true)==BttSerialDisposition::Dispatch);
    for(const auto* line:{"BTTEST1 START","BTTEST1 HELLO","BTBTT1 SAVE 1 2","BTBTT1 LOAD 303 Tunes/test.btt","BTPATTERN1 GET 2 17"})
        CHECK(BttSerialPolicy(line,false,true,false,true,true)==BttSerialDisposition::Reject);
    CHECK(command({BttExportVerb::Cancel,3,0,0}).find("EXPORTCANCELLED")!=std::string::npos);CHECK_EQ(f.load->active()->backing().use_count(),pins);
    CHECK(command({BttExportVerb::Begin,4,0,0}).find("EXPORTBEGIN")!=std::string::npos);
    CHECK(f.load->ExportPoll(kBttExportTimeout+2,true,reply));CHECK(std::string(reply).find("timeout")!=std::string::npos);
    CHECK_EQ(f.load->active()->backing().use_count(),pins);
    CHECK(command({BttExportVerb::Begin,5,0,0}).find("EXPORTBEGIN")!=std::string::npos);
    for(auto& pair:DeviceSd::files)if(pair.first.find(".btt-load/")!=std::string::npos)DeviceSd::fail_read_path=pair.first;
    CHECK(command({BttExportVerb::Next,5,1,0}).find(" read")!=std::string::npos);DeviceSd::fail_read_path.clear();
    CHECK(!f.load->ExportActive());CHECK_EQ(f.load->active()->backing().use_count(),pins);
    CHECK(command({BttExportVerb::Begin,6,0,0}).find("EXPORTBEGIN")!=std::string::npos);
    CHECK(command({BttExportVerb::Next,6,1,64}).find("offset")!=std::string::npos);CHECK(!f.load->ExportActive());
}
TEST_CASE(BttExport_actual_client_cancel_disconnect_and_host_storage_failure_keep_previous) {
    for(unsigned boundary:{0u,1u,2u,4u,8u,16u}) {
        ClientFixture f;f.Export();const auto old=f.client->BttCandidate();
        CHECK(f.client->RequestBttExport(f.now,f.directory.Path()));f.Pump(boundary);f.client->CancelBttExport();
        for(unsigned i=0;i<5000 && f.client->BttExportActive();++i)f.Pump(1);
        CHECK(!f.client->BttExportActive());CHECK(f.client->BttCandidate().get()==old.get());
        CHECK(!f.device.load->ExportActive());
        CHECK(f.client->RequestBttExport(f.now,f.directory.Path()));f.Pump(boundary);f.wire->healthy=false;f.Pump(1);
        CHECK(!f.client->BttExportActive());CHECK(f.client->BttCandidate().get()==old.get());CHECK(!f.device.load->ExportActive());
    }
    ClientFixture f;f.Export();const auto old=f.client->BttCandidate();
    const auto path=f.directory.Path()/"not-a-directory";FILE* file=std::fopen(path.string().c_str(),"wb");CHECK(file);CHECK_EQ(std::fclose(file),0);
    CHECK(!f.client->RequestBttExport(f.now,path));CHECK(f.client->BttCandidate().get()==old.get());
}
TEST_CASE(BttExport_actual_client_timeout_preserves_previous_without_restart) {
    ClientFixture f;f.Export();const auto old=f.client->BttCandidate();
    CHECK(f.client->RequestBttExport(f.now,f.directory.Path()));f.wire->hold=true;f.Pump(100);
    f.now+=kBttExportTimeout;f.Pump(1);f.wire->hold=false;f.Pump(500);
    CHECK(f.client->BttCandidate().get()==old.get());CHECK(!f.client->BttExportActive());CHECK(!f.device.load->ExportActive());
}

TEST_CASE(BttExport_completion_boundaries_and_unsupported_firmware_do_not_replace_prior) {
    for(bool final_ack:{false,true}) {
        ClientFixture f;f.Export();const auto old=f.client->BttCandidate();
        f.wire->disconnect_end=final_ack;f.wire->disconnect_last=!final_ack;
        CHECK(f.client->RequestBttExport(f.now,f.directory.Path()));
        for(unsigned i=0;i<5000 && f.client->BttExportActive();++i)f.Pump(1);
        CHECK(!f.client->BttExportActive());CHECK(f.client->BttCandidate().get()==old.get());CHECK(!f.device.load->ExportActive());
    }
    ClientFixture f(true,false);CHECK(!f.client->RequestBttExport(f.now,f.directory.Path()));
    // An unchanged legacy host connection never schedules export spontaneously.
    CHECK(std::none_of(f.wire->commands.begin(),f.wire->commands.end(),[](const auto& line){return line.find("BTBTT1 EXPORT")==0;}));
}
TEST_CASE(BttExport_capacity_stale_identity_and_unsettled_poll_are_bounded) {
    struct Huge final:btt::Reader {
        uint32_t size()const override{return kBttExportMax+1;}const void* identity()const override{return this;}
        unsigned read(uint32_t,void*,unsigned)const override{CHECK(false);return 0;}
    };
    btt::Export sender;char line[kBttExportLine];auto source=std::make_shared<Huge>();
    CHECK(std::string(sender.Command(TestExportRequest(BttExportVerb::Begin,1,0,0),source,1,1,export_boot,true,1,line)).find("no-source")!=std::string::npos);
    CHECK(!sender.Active());
    Fixture f;CHECK(f.Begin());f.Ready();f.Commit();
    CHECK(std::string(f.load->ExportCommand(TestExportRequest(BttExportVerb::Begin,1,0,0),export_boot,true,1,line)).find("EXPORTBEGIN")!=std::string::npos);
    CHECK(f.load->ExportPoll(2,false,line));CHECK(!f.load->ExportActive());
    CHECK(std::string(f.load->ExportCommand(TestExportRequest(BttExportVerb::Begin,1,0,0),export_boot,true,3,line)).find("stale-request")!=std::string::npos);
}

TEST_CASE(BttExport_unknown_boot_uses_fresh_transfer_challenge_without_claiming_boot_identity) {
    ClientFixture f(false);f.Export();CHECK(f.client->BttCandidate());
    CHECK(CandidateBytes(f.client->BttCandidate())==DeviceSd::files["/Tunes/test.btt"].bytes);
    const auto old=f.client->BttCandidate();
    // A complete stale transfer frame with the same R/G must still fail a new
    // challenge. Explicit new connection starts R at one on the device too.
    f.device.load->ExportDisconnect();
    char reply[kBttExportLine];auto r=TestExportRequest(BttExportVerb::Begin,1,0,0);
    CHECK(std::string(f.device.load->ExportCommand(r,"",true,f.now,reply)).find("unavailable")!=std::string::npos);
    r.verb=BttExportVerb::Next;r.nonce[0]='f';
    CHECK(std::string(f.device.load->ExportCommand(r,"",true,f.now,reply)).find("identity")!=std::string::npos);
    CHECK(f.device.load->ExportActive());f.device.load->ExportDisconnect();
    CHECK(f.client->BttCandidate().get()==old.get());
}

TEST_CASE(BttExport_fresh_poll_samples_and_natural_millis_rollover_keep_timeout_correct) {
    Fixture f;CHECK(f.Begin());f.Ready();f.Commit();char reply[kBttExportLine];
    const uint32_t before_wrap=UINT32_MAX-7;
    auto begin=TestExportRequest(BttExportVerb::Begin,1,0,0);
    CHECK(std::string(f.load->ExportCommand(begin,export_boot,true,before_wrap,reply)).find("EXPORTBEGIN")!=std::string::npos);
    // The real dispatcher now polls with millis() after Command, not its older
    // serial-loop sample. Several credits can cross millisecond boundaries.
    for(unsigned i=0;i<3;++i){auto next=TestExportRequest(BttExportVerb::Next,1,1,i*64);
        const uint32_t command_now=before_wrap+1+i*5;
        CHECK(std::string(f.load->ExportCommand(next,export_boot,true,command_now,reply)).find("EXPORTDATA")!=std::string::npos);
        CHECK(!f.load->ExportPoll(command_now+1,true,reply));CHECK(f.load->ExportActive());}
    CHECK(f.load->ExportPoll(before_wrap+11+kBttExportTimeout,true,reply));CHECK(!f.load->ExportActive());
}


#include "btt_candidate_io_faults.h"
#include <fstream>
#include <thread>
namespace {
using Fault=CandidateIoFaults::Fault;
template<class Predicate> void PumpUntil(ClientFixture& f,Predicate predicate) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!predicate() && std::chrono::steady_clock::now()<deadline) {
        // Worker scheduling, not an artificial protocol timeout, determines this
        // wait. Freeze simulated time and yield; the worker gate provides order.
        const auto now=f.now;f.Pump(1);f.now=now;std::this_thread::yield();
    }
    CHECK(predicate());if(!predicate())throw std::runtime_error("Host IO regression wait timed out");
}
std::set<std::filesystem::path> CandidateDirectories(const ClientFixture& f) {
    std::set<std::filesystem::path> result;
    for(const auto& entry:std::filesystem::directory_iterator(f.directory.Path()))if(entry.is_directory())result.insert(entry.path());
    return result;
}
std::filesystem::path StartIoAttempt(ClientFixture& f) {
    const auto before=CandidateDirectories(f);CHECK(f.client->RequestBttExport(f.now,f.directory.Path()));
    auto after=CandidateDirectories(f);for(const auto& path:before)after.erase(path);
    CHECK_EQ(after.size(),1u);if(after.size()!=1)throw std::runtime_error("Missing exclusive candidate directory");return *after.begin();
}
unsigned ExportEnds(const ClientFixture& f) {return std::count_if(f.wire->commands.begin(),f.wire->commands.end(),[](const auto& line){return line.find("BTBTT1 EXPORTEND ")==0;});}
void CheckPrior(ClientFixture& f,const btt::ReaderOwner& old,const std::vector<uint8_t>& bytes) {
    CHECK(f.client->BttCandidate().get()==old.get());CHECK(CandidateBytes(old)==bytes);
}
std::string LogContents(FILE* file) {
    CHECK_EQ(std::fflush(file),0);CHECK_EQ(std::fseek(file,0,SEEK_SET),0);
    std::string result;char block[512];while(const auto n=std::fread(block,1,sizeof(block),file))result.append(block,n);return result;
}
}
TEST_CASE(BttHostIoFailures_preserve_prior_candidate_and_report_cleanup) {
    for(auto fault:{Fault::ShortWrite,Fault::FlushFailure,Fault::SyncFailure,Fault::CloseFailure,Fault::ReopenFailure}) {
        auto io=std::make_shared<CandidateIoFaults>();ClientFixture f(true,true,io);f.Export();
        const auto old=f.client->BttCandidate();const auto bytes=CandidateBytes(old);const auto ends=ExportEnds(f);
        const auto sentinel=f.directory.Path()/"current-session-sentinel";
        {std::ofstream file(sentinel);file<<"recoverable-current-session";}
        io->Arm(fault);const auto dir=StartIoAttempt(f);
        PumpUntil(f,[&]{return !f.client->BttExportActive();});
        CHECK_EQ(io->Hits(fault),1u);CheckPrior(f,old,bytes);CHECK_EQ(ExportEnds(f),ends);CHECK_EQ(io->Handles(),1u);
        std::ifstream file(sentinel);std::string text;std::getline(file,text);CHECK(text=="recoverable-current-session");
        const auto& status=f.client->BttExportStatus();
        if(fault==Fault::ShortWrite || fault==Fault::ReopenFailure) {
            CHECK(!std::filesystem::exists(dir));CHECK(status.find("temporary artifact cleaned")!=std::string::npos);
            CHECK(status.find("artifact retained at "+dir.string())==std::string::npos);
        } else {
            CHECK(std::filesystem::exists(dir/"candidate.btt"));CHECK(status.find("artifact retained at "+dir.string())!=std::string::npos);
            CHECK_EQ(io->Removes(),0u);
        }
        const char* reason=fault==Fault::ShortWrite?"write failed":fault==Fault::FlushFailure?"flush uncertain":fault==Fault::SyncFailure?"sync uncertain":fault==Fault::CloseFailure?"close uncertain":"reopen failed";
        CHECK(status.find(reason)!=std::string::npos);
    }
}
TEST_CASE(BttHostCleanupFailures_retain_artifacts_without_destructor_retry) {
    for(auto secondary:{Fault::CloseFailure,Fault::RemoveFileFailure,Fault::RemoveDirectoryFailure}) {
        auto io=std::make_shared<CandidateIoFaults>();ClientFixture f(true,true,io);f.Export();
        const auto old=f.client->BttCandidate();const auto bytes=CandidateBytes(old);
        const auto primary=secondary==Fault::CloseFailure?Fault::ShortWrite:Fault::ReopenFailure;
        io->Arm({primary,secondary});const auto dir=StartIoAttempt(f);
        PumpUntil(f,[&]{return !f.client->BttExportActive();});
        CHECK_EQ(io->Hits(primary),1u);CHECK_EQ(io->Hits(secondary),1u);CheckPrior(f,old,bytes);
        CHECK(std::filesystem::exists(dir));CHECK(std::filesystem::exists(dir/"candidate.btt")==bool(secondary!=Fault::RemoveDirectoryFailure));
        CHECK(f.client->BttExportStatus().find("artifact retained at "+dir.string())!=std::string::npos);
        const auto removals=io->Removes();f.Pump(10);CHECK_EQ(io->Removes(),removals);
        f.client.reset();CHECK_EQ(io->Removes(),removals);CHECK(std::filesystem::exists(dir));CHECK(CandidateBytes(old)==bytes);
    }
}
TEST_CASE(BttHostLateValidation_after_cancel_or_disconnect_never_promotes_and_keeps_diagnostics) {
    for(unsigned action:{0u,1u,2u})for(auto fault:{Fault::None,Fault::FlushFailure,Fault::SyncFailure,Fault::CloseFailure,
        Fault::ReopenFailure,Fault::ReaderCloseFailure,Fault::RemoveFileFailure,Fault::RemoveDirectoryFailure}) {
        std::unique_ptr<FILE,decltype(&std::fclose)> log(std::tmpfile(),std::fclose);CHECK(log);
        auto io=std::make_shared<CandidateIoFaults>();ClientFixture f(true,true,io,log.get());f.Export();
        const auto old=f.client->BttCandidate();const auto bytes=CandidateBytes(old);const auto ends=ExportEnds(f);
        io->Arm(fault,true);CandidateGateRelease release{io};const auto dir=StartIoAttempt(f);
        PumpUntil(f,[&]{return io->Written()==bytes.size();});CHECK(io->AwaitFlush());
        // Worker owns the file and is blocked before reporting Finish's outcome.
        if(action==2){f.wire->healthy=false;f.Pump(1);CHECK(!f.client->BttExportActive());}
        else {f.client->CancelBttExport();if(action==0)f.wire->hold=true;
            else PumpUntil(f,[&]{return !f.client->BttExportActive();});}
        io->Release();
        const bool retained=fault!=Fault::None && fault!=Fault::ReopenFailure;
        PumpUntil(f,[&]{const auto& status=f.client->BttExportStatus();return retained?
            status.find("artifact retained at "+dir.string())!=std::string::npos:
            status.find(fault==Fault::None?"late validation discarded":"Candidate reopen failed")!=std::string::npos;});
        CheckPrior(f,old,bytes);CHECK_EQ(ExportEnds(f),ends);CHECK_EQ(io->Handles(),1u);
        CHECK(std::filesystem::exists(dir)==retained);
        if(retained){CHECK(io->Reported(dir.string()));CHECK(LogContents(log.get()).find(dir.string())!=std::string::npos);}
        else CHECK(f.client->BttExportStatus().find("temporary artifact cleaned")!=std::string::npos);
        if(fault!=Fault::None)CHECK_EQ(io->Hits(fault),1u);
        if(fault==Fault::RemoveDirectoryFailure)CHECK(!std::filesystem::exists(dir/"candidate.btt"));
        if(action==0) {
            CHECK(f.client->BttExportActive());
            PumpUntil(f,[&]{return std::any_of(f.wire->commands.begin(),f.wire->commands.end(),[](const auto& line){return line.find("BTBTT1 EXPORTCANCEL ")==0;});});
            if(fault==Fault::CloseFailure) {
                // A subsequent cancellation error must not overwrite the late
                // worker's retained-artifact notice and absolute path.
                BttExportRequest request;CHECK(ParseBttExportRequest(f.wire->commands.back(),request));
                f.wire->rx="BTBTT1 EXPORTERROR 1 "+std::to_string(request.id)+" cancelled-late "+request.nonce+"\n";
            }
            f.wire->hold=false;PumpUntil(f,[&]{return !f.client->BttExportActive();});
            if(retained)CHECK(f.client->BttExportStatus().find("artifact retained at "+dir.string())!=std::string::npos);
        }
        const auto removals=io->Removes();f.Pump(3);CHECK_EQ(io->Removes(),removals);CheckPrior(f,old,bytes);
    }
}
TEST_CASE(BttHostValidation_destructor_drains_late_uncertainty_without_replacing_prior) {
    auto io=std::make_shared<CandidateIoFaults>();ClientFixture f(true,true,io);f.Export();
    auto old=f.client->BttCandidate();const auto bytes=CandidateBytes(old);
    io->Arm(Fault::CloseFailure,true);CandidateGateRelease release{io};const auto dir=StartIoAttempt(f);
    PumpUntil(f,[&]{return io->Written()==bytes.size();});CHECK(io->AwaitFlush());
    f.wire->healthy=false;f.Pump(1);io->Release();f.client.reset();
    CHECK(io->Reported("artifact retained at "+dir.string()));CHECK(std::filesystem::exists(dir/"candidate.btt"));
    CHECK(CandidateBytes(old)==bytes);CHECK_EQ(io->Handles(),1u);old.reset();CHECK_EQ(io->Handles(),0u);
}

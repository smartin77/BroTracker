#include "test_framework.h"
#include "stopped_load.h"
#include "btt_pattern_layout.h"
#include "firmware/teensy/BroTracker/sd_wav_audition.h"
#include <memory>
#include <string>
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
        DevelopmentWavInspection inspection;producer.Prepare(true,pcm,inspection);
        DeviceSd::files["/Samples/a.wav"].bytes=Wav();DeviceSd::files["/Tunes/test.btt"].bytes=Tune();
        old.active_rows=7;old.active_channels=8;
        CHECK_EQ(control.InitializeAudio(12500,44100,old,bindings),PatternPlayerStatus::Success);
        readback=old;load=std::make_unique<StoppedLoad>(producer);
    }
    ~Fixture() {
        DeviceSd::fail_write=false;DeviceSd::fail_close_path.clear();
        DeviceSd::fail_read_path.clear();DeviceSd::fail_sync_path.clear();DeviceSd::fail_remove_path.clear();
        load->Cancel();for(unsigned i=0;i<200 && load->Busy();++i)load->Step(true);
        CHECK(!load->Busy());
        CHECK_EQ(control.OutputPlayer().Configure(12500,44100,old,VariableRateSampleBindings{}),PatternPlayerStatus::Success);
        producer.Publish(control.OutputPlayer().MidiPlayer());definitions.Reset(false);bindings={};load.reset();
        CHECK(producer.Reset());CHECK_EQ(ResourcePaths().UniqueCount(),0u);
        for(const auto& file:DeviceSd::files)CHECK_EQ(file.second.opens,0u);
    }
    bool Begin(uint32_t pattern=303){return load->Begin("Tunes/test.btt",pattern,control.StorageReady());}
    void Turn(){producer.Service();load->Step(control.StorageReady());}
    void Ready(){for(unsigned i=0;i<400 && load->phase()!=LoadPhase::Ready && load->phase()!=LoadPhase::Failed;++i)Turn();CHECK_EQ(load->phase(),LoadPhase::Ready);}
    void Commit() {
        CHECK(load->Submit(control,publication,100));CHECK_EQ(control.Snapshot().tempo,12500u);
        std::int16_t out[1];control.AudioBlock(out,1,true);producer.Publish(control.OutputPlayer().MidiPlayer());
        CHECK_EQ(definitions.Count(),0u); // Foreground tables update only at ACK consumption.
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

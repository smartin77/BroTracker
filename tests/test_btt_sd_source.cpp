#include "test_framework.h"
#include "sd_source_service.h"
#include "btt_pattern_layout.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>

static bool count_allocations=false;
static unsigned allocations=0;
void* operator new(size_t n) {if(auto* p=std::malloc(n?n:1)){if(count_allocations && !FakePatternSd::allocation_exclusions)++allocations;return p;}throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void operator delete[](void* p,size_t)noexcept{std::free(p);}

namespace BroTracker {
struct SdSampleStreamingTestAccess {
    static void ExhaustTickets(SdSampleStreaming& p){p.job_sequence_=UINT32_MAX;}
};
StreamInterruptGuard::StreamInterruptGuard() noexcept {mask_=FakeStreamPlatform::primask;FakeStreamPlatform::primask=1;}
StreamInterruptGuard::~StreamInterruptGuard() noexcept {FakeStreamPlatform::primask=mask_;}
}
namespace {
using namespace btt;
using namespace btt::prototype;
using namespace BroTracker;
using Producer=SdSampleStreaming;
using Status=SourceStatus;
struct EmptyBindings {unsigned count=0;struct Binding {Pcm16MixedSampleView sample;} bindings[1];};
static Producer::Storage pcm;
struct Count {unsigned before=allocations;Count(){count_allocations=true;}~Count(){count_allocations=false;CHECK_EQ(allocations,before);}};
void Put(std::vector<uint8_t>& bytes,unsigned at,uint32_t v,unsigned n){for(unsigned i=0;i<n;++i)bytes[at+i]=uint8_t(v>>(i*8));}
std::vector<uint8_t> Wav(unsigned rate=44100,unsigned frames=7000) {
    std::vector<uint8_t> b(44+frames*2);std::memcpy(b.data(),"RIFF",4);Put(b,4,b.size()-8,4);std::memcpy(b.data()+8,"WAVEfmt ",8);
    Put(b,16,16,4);Put(b,20,1,2);Put(b,22,1,2);Put(b,24,rate,4);Put(b,28,rate*2,4);Put(b,32,2,2);Put(b,34,16,2);
    std::memcpy(b.data()+36,"data",4);Put(b,40,frames*2,4);return b;
}
struct MemoryReader:Reader,ReaderControl {
    const std::vector<uint8_t>& bytes;unsigned refs=0;
    explicit MemoryReader(const std::vector<uint8_t>& b):bytes(b){}
    uint32_t size()const override{return bytes.size();}
    const void* identity()const override{return this;}
    unsigned read(uint32_t at,void* p,unsigned n)const override {if(at>bytes.size() || n>bytes.size()-at)return 0;std::memcpy(p,bytes.data()+at,n);return n;}
    void retain()noexcept override{++refs;}void release()noexcept override{--refs;}
    const Reader* reader()const noexcept override{return this;}uint32_t count()const noexcept override{return refs;}
};
std::vector<uint8_t> Tune(const char* second="Samples/b.wav") {
    namespace f=btt_layout;
    std::vector<uint8_t> tune,order,pattern;f::Tlv(tune,1,4,std::vector<uint8_t>(16,1));f::Tlv(tune,2,5,{'S','D'});
    f::Scalar(tune,3,4,12500);f::Scalar(tune,4,1,8);f::Scalar(tune,5,4,100);f::Scalar(tune,6,4,1000);
    std::vector<uint8_t> ids;f::Put(ids,101,4);f::Tlv(order,1,6,ids);
    f::Pattern p;p.Clear(2);for(unsigned i=0;i<2;++i){f::Cell c;c.note=60;c.instrument=i+1;p.Put(i,c);}CHECK(f::Encode(p,3,pattern));
    std::vector<f::Section> sections{{"TUNE",0,tune},{"ORDR",0,order}};
    for(unsigned i=0;i<2;++i){std::vector<uint8_t> inst;f::Scalar(inst,1,2,1);std::vector<uint8_t> path{0};
        for(const char* s=i?second:"Samples/a.wav";*s;++s)path.push_back(*s);f::Tlv(inst,2,6,path,false);
        f::Scalar(inst,3,2,i?72:60,false);f::Scalar(inst,4,2,i?25:0,false);sections.push_back({"INST",i+1,inst});}
    sections.push_back({"PATT",101,pattern});return f::Envelope(sections);
}
struct Fixture {
    Producer producer;SdSourceService service{producer};
    Fixture(){FakePatternSd::Reset();FakeStreamPlatform::primask=0;DevelopmentWavInspection inspection{};producer.Prepare(true,pcm,inspection);}
    ~Fixture(){CHECK(service.CanDestroy());CHECK(producer.Reset());}
    void File(const char* path,unsigned rate=44100){FakePatternSd::files[path].bytes=Wav(rate);}
    Status Finish(){Status status=Status::Pending;for(unsigned i=0;i<200 && status==Status::Pending;++i)status=service.Service();return status;}
    void Reclaim(){while(producer.ReclaimOne(EmptyBindings{})){} }
};

TEST_CASE(real_sd_adapter_prepared_sharing_and_candidate_tuning) {
    auto bytes=Tune("Samples/a.wav");MemoryReader reader(bytes);Document doc;Workspace work;Pattern pattern;AdmissionScratch scratch;
    Fixture f;f.File("Samples/a.wav");Pcm16MixedSampleView prepared;
    CHECK_EQ(f.producer.PreparePath("Samples/a.wav",prepared),InstrumentAvailability::Ready);
    CHECK_EQ(Validate(ReaderOwner::Pin(reader),doc,work),Result::Ok);
    {Count count;Candidate candidate;CHECK_EQ(candidate.Begin(doc,{101,{}},work,pattern,scratch),Result::Ok);
        for(unsigned i=0;i<8 && !candidate.initial_resources_ready();++i)candidate.Step(f.service,scratch);
        CHECK(candidate.initial_resources_ready());auto* a=candidate.Find(1);auto* b=candidate.Find(2);
        CHECK(a->source.token()==b->source.token());CHECK_EQ(a->effective_root,60);CHECK_EQ(b->effective_root,72);CHECK_EQ(b->effective_fine,25);
        SourceLease clone;CHECK(a->source.Clone(clone));CHECK_EQ(candidate.Cancel(),Code::Ok);
        CHECK(!f.producer.ReclaimOne(EmptyBindings{}));clone.Reset();f.Reclaim();CHECK_EQ(f.producer.SourceCount(),3u);}
}

TEST_CASE(real_sd_candidate_pending_retains_paths_and_earlier_pins) {
    auto bytes=Tune();MemoryReader reader(bytes);Document doc;Workspace work;Pattern pattern;AdmissionScratch scratch;
    Fixture f;f.File("Samples/a.wav");f.File("Samples/b.wav");CHECK_EQ(Validate(ReaderOwner::Pin(reader),doc,work),Result::Ok);
    {Count count;Candidate c;CHECK_EQ(c.Begin(doc,{101,{}},work,pattern,scratch),Result::Ok);
        c.Step(f.service,scratch);c.Step(f.service,scratch);CHECK_EQ(c.Find(1)->source_status,Status::Pending);CHECK(c.Find(1)->path.Get());
        for(unsigned i=0;i<4;++i)c.Step(f.service,scratch);CHECK(FakePatternSd::reads.empty());CHECK(!c.initial_resources_ready());
        CHECK_EQ(f.Finish(),Status::Ready);CHECK_EQ(c.Step(f.service,scratch),Admission::Progress);CHECK(c.Find(1)->source);
        c.Step(f.service,scratch);c.Step(f.service,scratch);CHECK_EQ(c.Find(2)->source_status,Status::Pending);CHECK(c.Find(1)->source);
        f.Reclaim();CHECK_EQ(f.producer.SourceCount(),4u);CHECK_EQ(f.Finish(),Status::Ready);
        c.Step(f.service,scratch);CHECK(!c.initial_resources_ready());CHECK_EQ(c.Step(f.service,scratch),Admission::Ready);
        CHECK_EQ(c.Cancel(),Code::Ok);f.Reclaim();CHECK_EQ(f.producer.SourceCount(),3u);}
}

TEST_CASE(real_sd_cancel_every_phase_including_pinned_completion) {
    for(unsigned turns=0;turns<25;++turns) {
        Fixture f;f.File("Samples/a.wav");const auto initial=ResourcePaths().UsedBytes();
        auto bytes=Tune();MemoryReader reader(bytes);Document doc;Workspace work;Pattern pattern;AdmissionScratch scratch;
        CHECK_EQ(Validate(ReaderOwner::Pin(reader),doc,work),Result::Ok);
        {Count count;Candidate c;CHECK_EQ(c.Begin(doc,{101,{}},work,pattern,scratch),Result::Ok);
            c.Step(f.service,scratch);c.Step(f.service,scratch);
            for(unsigned i=0;i<turns;++i)f.service.Service();
            f.Reclaim(); // A completed, unconsumed source is already pinned.
            const auto result=c.Cancel();CHECK(result==Code::Busy || result==Code::Ok);
            if(result==Code::Busy){CHECK(!c.CanDestroy());CHECK(!c.initial_resources_ready());CHECK_EQ(f.Finish(),Status::Released);CHECK_EQ(c.Cancel(),Code::Ok);}
            CHECK(c.CanDestroy());CHECK(f.service.CanDestroy());f.Reclaim();CHECK_EQ(ResourcePaths().UsedBytes(),initial);}
    }
}

TEST_CASE(real_sd_foreign_contention_checked_tickets_and_no_double_advancement) {
    Fixture f;f.File("Samples/a.wav");f.File("Samples/b.wav");SourceLease lease;
    CHECK(f.producer.BeginPrepareAsync("Samples/b.wav"));
    CHECK_EQ(f.service.Acquire("Samples/a.wav",lease),Status::Pending);
    CHECK_EQ(f.service.CancelFor(&f.service),Status::Released);
    CHECK_EQ(f.service.Service(),Status::Released);CHECK(FakePatternSd::reads.empty());
    for(unsigned i=0;i<200 && !f.producer.PreparationDone();++i)f.producer.PreparationStep();
    CHECK_EQ(f.producer.PreparationResult(),InstrumentAvailability::Ready);
    CHECK_EQ(f.service.Acquire("Samples/a.wav",lease),Status::Pending); // Do not replace an unacknowledged result.
    CHECK(f.producer.AcknowledgePreparation());CHECK_EQ(f.service.Acquire("Samples/a.wav",lease),Status::Pending);
    auto before=FakePatternSd::reads.size();f.producer.PreparationStep();f.producer.CancelPreparation();
    CHECK_EQ(FakePatternSd::reads.size(),before);CHECK(!f.producer.BeginPrepareAsync("Samples/b.wav"));CHECK(!f.producer.PreparationDone());
    SdSourceService other(f.producer);SourceLease blocked;
    int foreign_request=0;
    CHECK_EQ(f.service.AcquireFor(&foreign_request,"Samples/b.wav",blocked),Status::Pending);
    CHECK_EQ(f.service.CancelFor(&foreign_request),Status::Released);
    CHECK_EQ(other.Acquire("Samples/a.wav",blocked),Status::Pending);CHECK_EQ(other.CancelFor(&other),Status::Released);
    CHECK_EQ(f.Finish(),Status::Ready);f.Reclaim(); // Completion pinned before adapter Acquire.
    CHECK_EQ(f.service.Acquire("Samples/a.wav",lease),Status::Ready);lease.Reset();
    uint32_t old=0;CHECK(f.producer.BeginOwnedPreparation("Samples/b.wav",old));Producer::CandidatePin pin;
    for(unsigned i=0;i<200 && f.producer.OwnedPreparationStatus(old)==Producer::PreparationStatus::Pending;++i)f.producer.StepOwnedPreparation(old);
    CHECK(f.producer.TakeOwnedPreparation(old,pin));CHECK(f.producer.ReleaseCandidatePin(pin));
    uint32_t fresh=0;CHECK(f.producer.BeginOwnedPreparation("Samples/a.wav",fresh));CHECK(fresh!=old);
    CHECK(!f.producer.CancelOwnedPreparation(old));f.producer.StepOwnedPreparation(old);CHECK_EQ(f.producer.OwnedPreparationStatus(old),Producer::PreparationStatus::Stale);
    CHECK(f.producer.TakeOwnedPreparation(fresh,pin={}));CHECK(f.producer.ReleaseCandidatePin(pin));f.Reclaim();
}

TEST_CASE(real_sd_quarantine_is_not_success_and_candidate_cannot_be_destroyed) {
    Fixture f;f.File("Samples/a.wav");auto bytes=Tune();MemoryReader reader(bytes);Document doc;Workspace work;Pattern pattern;AdmissionScratch scratch;
    CHECK_EQ(Validate(ReaderOwner::Pin(reader),doc,work),Result::Ok);Candidate c;CHECK_EQ(c.Begin(doc,{101,{}},work,pattern,scratch),Result::Ok);
    c.Step(f.service,scratch);c.Step(f.service,scratch);f.service.Service();CHECK_EQ(c.Cancel(),Code::Busy);
    FakePatternSd::fail_close=true;CHECK_EQ(f.service.Service(),Status::Quarantined);CHECK_EQ(c.Cancel(),Code::Io);
    CHECK_EQ(c.cleanup_status(),Status::Quarantined);CHECK(!c.CanDestroy());CHECK(!f.service.CanDestroy());CHECK(!f.producer.Reset());
    CHECK(!f.producer.BeginPrepareAsync("Samples/b.wav"));CHECK_EQ(c.Discard(),Code::Io);CHECK_EQ(c.Begin(doc,{101,{}},work,pattern,scratch),Result::Backing);
    CHECK_EQ(c.Step(f.service,scratch),Admission::Cancelled);CHECK(!c.CanDestroy());
    FakePatternSd::fail_close=false;CHECK_EQ(f.service.Service(),Status::Released);CHECK_EQ(c.Cancel(),Code::Ok);CHECK(c.CanDestroy());f.Reclaim();
}

TEST_CASE(real_sd_distinct_failures_preserve_active_source_and_candidate_rollback) {
    for(unsigned fault=0;fault<4;++fault) {
        auto bytes=Tune();MemoryReader reader(bytes);Document doc;Workspace work;Pattern pattern;AdmissionScratch scratch;
        Fixture f;f.File("Samples/a.wav");if(fault!=0)f.File("Samples/b.wav",fault==1?48000:44100);
        CHECK_EQ(Validate(ReaderOwner::Pin(reader),doc,work),Result::Ok);
        Candidate c;CHECK_EQ(c.Begin(doc,{101,{}},work,pattern,scratch),Result::Ok);
        for(unsigned i=0;i<200 && !c.Find(1)->source;++i){c.Step(f.service,scratch);f.service.Service();}
        CHECK(c.Find(1)->source);SourceLease active;CHECK(c.Find(1)->source.Clone(active));
        if(fault==2)FakePatternSd::short_read_path="Samples/b.wav";
        if(fault==3)FakePatternSd::fail_seek_path="Samples/b.wav";
        for(unsigned i=0;i<200 && c.state()==Admission::Progress;++i){c.Step(f.service,scratch);f.service.Service();}
        CHECK(c.state()==Admission::Blocked || c.state()==Admission::Io);
        CHECK_EQ(c.Find(2)->source_status,fault==0?Status::Missing:fault==1?Status::Unsupported:Status::Io);
        CHECK(!c.Find(1)->source);Pcm16MixedSampleView view;const char* path=nullptr;CHECK(f.service.Access(active.token(),view,path));
        CHECK_EQ(c.Cancel(),Code::Ok);active.Reset();f.Reclaim();CHECK_EQ(f.producer.SourceCount(),3u);
    }
}

TEST_CASE(real_sd_source_and_path_capacity_no_eviction_and_cycles) {
    Fixture f;f.File("Samples/a.wav");f.File("Samples/b.wav");std::array<SourceLease,13> owners;
    for(unsigned i=0;i<owners.size();++i){const auto path="Samples/cap"+std::to_string(i)+".wav";f.File(path.c_str());
        CHECK_EQ(f.service.Acquire(path.c_str(),owners[i]),Status::Pending);CHECK_EQ(f.Finish(),Status::Ready);CHECK_EQ(f.service.Acquire(path.c_str(),owners[i]),Status::Ready);}
    SourceLease rejected;CHECK_EQ(f.service.Acquire("Samples/a.wav",rejected),Status::Pending);CHECK_EQ(f.service.Acquire("Samples/a.wav",rejected),Status::Capacity);
    f.Reclaim();CHECK_EQ(f.producer.SourceCount(),16u);for(auto& owner:owners)owner.Reset();f.Reclaim();
    {std::array<ResourcePathRef,256> fill;for(unsigned i=0;i<253;++i){const auto name="fill/"+std::to_string(i);CHECK(fill[i].Assign(name.c_str()));}
        CHECK_EQ(f.service.Acquire("Samples/a.wav",rejected),Status::PathCapacity);}
    const auto initial=ResourcePaths().UsedBytes();
    {Count count;for(unsigned i=0;i<80;++i){CHECK_EQ(f.service.Acquire("Samples/a.wav",rejected),Status::Pending);
        if(i%2){CHECK_EQ(f.Finish(),Status::Ready);CHECK_EQ(f.service.Acquire("Samples/a.wav",rejected),Status::Ready);rejected.Reset();}
        else {f.service.Service();CHECK_EQ(f.service.CancelFor(&f.service),Status::Pending);CHECK_EQ(f.Finish(),Status::Released);CHECK_EQ(f.service.CancelFor(&f.service),Status::Released);}
        f.Reclaim();CHECK_EQ(ResourcePaths().UsedBytes(),initial);CHECK(f.service.CanDestroy());}}
}

TEST_CASE(real_sd_adapter_yields_to_pattern_refills_and_ticket_exhaustion_is_terminal) {
    Fixture f;f.File("Samples/a.wav");f.File("Samples/b.wav");Pcm16MixedSampleView view;
    CHECK_EQ(f.producer.PreparePath("Samples/a.wav",view),InstrumentAvailability::Ready);
    SourceLease active,pending;CHECK_EQ(f.service.Acquire("Samples/a.wav",active),Status::Ready);
    StreamedVoiceSnapshot voice{};voice.sample=view.streaming;voice.generation=1;voice.step=kSourceFrameStepUnity;voice.position=view.streaming->PrefixFrames();
    f.producer.PublishVoice(0,voice);CHECK(f.producer.RefillNeeded());
    CHECK_EQ(f.service.Acquire("Samples/b.wav",pending),Status::Pending);
    auto reads=FakePatternSd::reads.size();CHECK_EQ(f.service.Service(),Status::Pending);CHECK_EQ(FakePatternSd::reads.size(),reads);
    for(unsigned i=0;i<4 && f.producer.RefillNeeded();++i)CHECK(f.producer.Service());
    CHECK(!f.producer.RefillNeeded());CHECK_EQ(f.Finish(),Status::Ready);
    CHECK_EQ(f.service.Acquire("Samples/b.wav",pending),Status::Ready);
    f.producer.PublishVoice(0,{});active.Reset();pending.Reset();f.Reclaim();
    SdSampleStreamingTestAccess::ExhaustTickets(f.producer);
    CHECK_EQ(f.service.Acquire("Samples/a.wav",pending),Status::GenerationExhausted);
    CHECK(f.producer.Reset());DevelopmentWavInspection inspection{};f.producer.Prepare(true,pcm,inspection);
    CHECK_EQ(f.service.Acquire("Samples/a.wav",pending),Status::GenerationExhausted);
}
}
int main(int argc,char** argv){const int result=test::RunAll();std::ofstream report(argc>1?argv[1]:"build/btt-source-adapter/adapter-allocations.csv");
    report<<"scope,cpp_heap_allocations\nadmission_adapter_and_real_producer_excluding_fake_sd_bookkeeping,"<<allocations<<'\n';
    std::ofstream layout(std::string(argc>1?argv[1]:"build/btt-source-adapter/adapter-allocations.csv")+".layout.txt");
    layout<<"Host ABI, not target memory; shared PCM/path storage excluded.\nSdSourceService="<<sizeof(SdSourceService)
        <<"\nCandidate="<<sizeof(Candidate)<<"\nSourceLease="<<sizeof(SourceLease)<<"\nProducer="<<sizeof(Producer)<<'\n';return result;}

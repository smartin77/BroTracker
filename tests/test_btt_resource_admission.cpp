#include "test_framework.h"
#include "btt_pattern_layout.h"
#include "backing.h"
#include "resource_admission.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>

static size_t allocations=0;
void* operator new(size_t n){if(auto* p=std::malloc(n?n:1)){++allocations;return p;}throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void operator delete[](void* p,size_t)noexcept{std::free(p);}

namespace {
using namespace btt;
using namespace btt::prototype;
using BroTracker::ResourcePathRef;
using BroTracker::ResourcePaths;
namespace fixture=btt_layout;
struct Disk {
    struct Handle {int index=-1;};
    struct File {char path[96]{};bool exists=false;unsigned opens=0;uint32_t length=0;std::array<uint8_t,65536> bytes{};};
    std::array<File,3> files{};
    bool Open(const Handle& h)const{return h.index>=0;}
    bool Create(Handle& h,const char* path){for(unsigned i=0;i<files.size();++i)if(!files[i].exists){auto& f=files[i];std::strcpy(f.path,path);f.exists=true;f.opens=1;f.length=0;h.index=int(i);return true;}return false;}
    bool ReadOpen(Handle& h,const char* path,uint32_t& length){for(unsigned i=0;i<files.size();++i)if(files[i].exists&&!std::strcmp(files[i].path,path)){h.index=int(i);++files[i].opens;length=files[i].length;return true;}return false;}
    bool Read(Handle& h,uint32_t at,void* p,unsigned n){if(!Open(h))return false;auto& f=files[h.index];if(at>f.length||n>f.length-at)return false;std::memcpy(p,f.bytes.data()+at,n);return true;}
    bool Write(Handle& h,uint32_t at,const void* p,unsigned n){if(!Open(h))return false;auto& f=files[h.index];if(at>f.bytes.size()||n>f.bytes.size()-at)return false;std::memcpy(f.bytes.data()+at,p,n);f.length=std::max<uint32_t>(f.length,at+n);return true;}
    bool Resize(Handle& h,uint32_t n){if(!Open(h)||n>files[h.index].bytes.size())return false;files[h.index].length=n;return true;}
    bool Sync(Handle& h){return Open(h);}
    bool Close(Handle& h){if(Open(h)){--files[h.index].opens;h={};}return true;}
    bool Remove(const char* path){for(auto& f:files)if(f.exists&&!std::strcmp(f.path,path)){if(f.opens)return false;f.exists=false;return true;}return false;}
};
using Pool=Backings<Disk>;
static Disk disk; // Fake disk payloads are fixture storage, not prototype state.
alignas(32) static unsigned char memory[65536];
struct Sources final:SourceService {
    struct Slot {ResourcePathRef path;uint32_t generation=0,version=0;unsigned refs=0;};
    std::array<Slot,16> slots{};
    unsigned limit=16,acquires=0,created=0,fail_at=0;
    uint32_t version=1,next=1;
    SourceStatus failure=SourceStatus::Io;
    SourceStatus Acquire(const char* path,SourceLease& output)noexcept override {
        if(++acquires==fail_at)return failure;
        if(std::strstr(path,"missing"))return SourceStatus::Missing;
        for(unsigned i=0;i<limit;++i){auto& s=slots[i];if(s.refs&&s.version==version&&!std::strcmp(s.path.Get(),path)){
            if(s.refs==65535)return SourceStatus::Capacity;
            ++s.refs;output=SourceLease::Adopt(*this,{s.generation,i});return SourceStatus::Ready;
        }}
        for(unsigned i=0;i<limit;++i)if(!slots[i].refs){auto& s=slots[i];if(!s.path.Assign(path))return SourceStatus::PathCapacity;
            s.generation=next++;s.version=version;s.refs=1;++created;output=SourceLease::Adopt(*this,{s.generation,i});return SourceStatus::Ready;}
        return SourceStatus::Capacity;
    }
    bool Retain(SourceToken t)noexcept override {if(t.slot>=slots.size())return false;auto& s=slots[t.slot];if(!s.refs||s.generation!=t.generation||s.refs==65535)return false;++s.refs;return true;}
    void Release(SourceToken t)noexcept override {CHECK(t.slot<slots.size());auto& s=slots[t.slot];CHECK(s.refs&&s.generation==t.generation);if(!--s.refs)s.path.Reset();}
    unsigned Live()const{unsigned n=0;for(const auto& s:slots)n+=s.refs!=0;return n;}
    unsigned Pins()const{unsigned n=0;for(const auto& s:slots)n+=s.refs;return n;}
};
struct Spec {uint32_t id;const char* path;int root=-1,fine=-1;unsigned type=1,base=0,encoding=6;};
std::vector<uint8_t> Fixture(const std::vector<Spec>& definitions,const std::vector<uint32_t>& required) {
    std::vector<uint8_t> tune,order,pattern;
    fixture::Tlv(tune,1,4,std::vector<uint8_t>(16,1));fixture::Tlv(tune,2,5,{'R','e','s'});
    fixture::Scalar(tune,3,4,12500);fixture::Scalar(tune,4,1,8);fixture::Scalar(tune,5,4,100001);fixture::Scalar(tune,6,4,1001);
    std::vector<uint8_t> ids;fixture::Put(ids,101,4);fixture::Tlv(order,1,6,ids);
    fixture::Pattern p;p.Clear(2);unsigned i=0;
    for(auto id:required){fixture::Cell c;c.note=60;c.instrument=id;c.fx={17,1024,65535};c.values={0,1,255};p.Put(i++,c);}
    CHECK(fixture::Encode(p,3,pattern));
    std::vector<fixture::Section> sections{{"TUNE",0,tune},{"ORDR",0,order}};
    for(const auto& d:definitions){std::vector<uint8_t> inst;fixture::Scalar(inst,1,2,d.type);
        if(d.path){std::vector<uint8_t> path{uint8_t(d.base)};for(const char* s=d.path;*s;++s)path.push_back(uint8_t(*s));fixture::Tlv(inst,2,d.encoding,path,false);}
        if(d.root>=0)fixture::Scalar(inst,3,2,unsigned(d.root),false);if(d.fine>=0)fixture::Scalar(inst,4,2,unsigned(d.fine),false);
        fixture::Tlv(inst,999,99,{0,42,255},false);sections.push_back({"INST",d.id,inst});}
    sections.push_back({"PATT",101,pattern});sections.push_back({"XTRA",1,std::vector<uint8_t>(4096,0x5a),0});return fixture::Envelope(sections);
}
std::filesystem::path output;
std::ofstream measurements;
struct Harness {
    Sources sources;
    Arena arena;
    Object<Pool> pool;
    Object<Document> old_doc,new_doc;
    Object<Candidate> old,next;
    Object<Workspace> work;
    Object<Pattern> pattern;
    Object<AdmissionScratch> scratch;
    Pool::Token old_token{},new_token{};
    Harness(){CHECK_EQ(ResourcePaths().UsedBytes(),0u);CHECK_EQ(arena.Init(memory,sizeof(memory)),Code::Ok);
        CHECK_EQ(arena.New(pool,disk),Code::Ok);CHECK_EQ(arena.New(old_doc),Code::Ok);CHECK_EQ(arena.New(new_doc),Code::Ok);
        CHECK_EQ(arena.New(old),Code::Ok);CHECK_EQ(arena.New(next),Code::Ok);CHECK_EQ(arena.New(work),Code::Ok);
        CHECK_EQ(arena.New(pattern),Code::Ok);CHECK_EQ(arena.New(scratch),Code::Ok);}
    ~Harness(){CHECK_EQ(old->Discard(),Code::Ok);CHECK_EQ(next->Discard(),Code::Ok);CHECK_EQ(sources.Live(),0u);
        old_doc->backing.reset();new_doc->backing.reset();if(old_token.generation)CHECK_EQ(pool->Discard(old_token),Code::Ok);if(new_token.generation)CHECK_EQ(pool->Discard(new_token),Code::Ok);
        CHECK_EQ(arena.Reset(),Code::Ok);CHECK_EQ(ResourcePaths().UsedBytes(),0u);CHECK_EQ(ResourcePaths().UniqueCount(),0u);}
    void Load(const std::vector<uint8_t>& bytes,bool original=false){Pool::Output writer;auto& token=original?old_token:new_token;
        CHECK_EQ(pool->Create(writer,token),Code::Ok);for(unsigned at=0;at<bytes.size();){auto n=std::min(512u,unsigned(bytes.size())-at);CHECK_EQ(writer.write(at,bytes.data()+at,n),n);at+=n;}
        ReaderOwner owner;CHECK_EQ(writer.Finish(owner),Code::Ok);CHECK_EQ(Validate(owner,original?*old_doc:*new_doc,*work),Result::Ok);}
    Result Begin(bool original=false){return (original?old:next)->Begin(original?*old_doc:*new_doc,{101,{}},*work,*pattern,*scratch);}
    Admission Run(bool original=false){auto* c=(original?old:next).get();Admission result=c->state();for(unsigned i=0;i<200 && (result==Admission::Scanned||result==Admission::Progress);++i)result=c->Step(sources,*scratch);return result;}
};
void EqualBacking(const Reader& reader,const std::vector<uint8_t>& bytes){CHECK_EQ(reader.size(),bytes.size());uint8_t block[512];
    for(unsigned at=0;at<bytes.size();){auto n=std::min(512u,unsigned(bytes.size())-at);CHECK_EQ(reader.read(at,block,n),n);CHECK(!std::memcmp(block,bytes.data()+at,n));at+=n;}}
struct PathFill {
    std::array<ResourcePathRef,256> refs;
    unsigned Records(){unsigned count=0;for(unsigned i=0;i<refs.size();++i){char name[32];std::snprintf(name,sizeof(name),"fill/%03u.wav",i);if(!refs[i].Assign(name))break;++count;}return count;}
};

TEST_CASE(resource_shared_paths_saved_values_and_initial_requirements) {
    auto bytes=Fixture({{11,"Samples/shared.wav"},{500,"Samples/shared.wav",72,25},{42,"Samples/unused.wav"},{300,"Samples/other.wav",0,199}}, {11,500});
    const auto before=allocations;Harness h;h.Load(bytes);CHECK_EQ(h.Begin(),Result::Ok);CHECK_EQ(h.next->count(),4u);
    CHECK_EQ(h.next->initial_pattern(),101u);
    CHECK_EQ(ResourcePaths().UniqueCount(),0u);CHECK_EQ(h.sources.acquires,0u);
    std::memset(h.scratch->path,'x',sizeof(h.scratch->path));CHECK_EQ(h.Run(),Admission::Ready);
    const auto* a=h.next->Find(11);const auto* b=h.next->Find(500);CHECK(a&&b);
    CHECK_EQ(std::strcmp(a->path.Get(),"Samples/shared.wav"),0);CHECK_EQ(a->path.Id(),b->path.Id());CHECK(a->source.token()==b->source.token());
    CHECK_EQ(a->effective_root,60);CHECK_EQ(a->effective_fine,0);CHECK_EQ(a->saved.present,0);CHECK_EQ(b->effective_root,72);CHECK_EQ(b->effective_fine,25);
    CHECK_EQ(h.sources.acquires,2u);CHECK_EQ(h.sources.created,1u);CHECK_EQ(ResourcePaths().UniqueCount(),1u);
    CHECK(!h.next->Find(42)->path.Get());CHECK_EQ(h.next->Find(300)->saved.root,0);CHECK_EQ(h.next->Find(300)->saved.fine,199);
    CHECK_EQ(h.next->Find(300)->settings,SettingStatus::UnsupportedValue);EqualBacking(*h.next->backing(),bytes);
    CHECK_EQ(h.next->Cancel(),Code::Ok);CHECK_EQ(ResourcePaths().UsedBytes(),0u);CHECK(h.next->backing());CHECK_EQ(h.next->count(),4u);
    CHECK_EQ(allocations,before);measurements<<"setup_scan_prepare_cancel_including_owners,"<<allocations-before<<','<<h.arena.used()<<'\n';
}

TEST_CASE(resource_missing_invalid_unsupported_and_independent_defaults) {
    for(const Spec& spec:std::vector<Spec>{{11,"Samples/missing.wav"},{11,"../bad.wav"},{11,"Samples/ok.wav",0,0},
        {11,"Samples/ok.wav",60,199},{11,"Samples/ok.wav",-1,25},{11,"Samples/ok.wav",72,-1},{11,"Samples/ok.wav",-1,-1,1,1},
        {11,"Samples/ok.wav",-1,-1,1,0,99},{11,nullptr,-1,-1,99}}){
        auto bytes=Fixture({spec},{11});const auto before=allocations;Harness h;h.Load(bytes);CHECK_EQ(h.Begin(),Result::Ok);auto outcome=h.Run();auto* d=h.next->At(0);
        if(spec.root==-1&&spec.fine==25){CHECK_EQ(outcome,Admission::Ready);CHECK_EQ(d->effective_root,60);CHECK_EQ(d->effective_fine,25);}
        else if(spec.root==72&&spec.fine==-1){CHECK_EQ(outcome,Admission::Ready);CHECK_EQ(d->effective_root,72);CHECK_EQ(d->effective_fine,0);}
        else {CHECK_EQ(outcome,Admission::Blocked);CHECK_EQ(ResourcePaths().UsedBytes(),0u);CHECK_EQ(h.sources.Live(),0u);}
        if(std::strstr(spec.path?spec.path:"","missing"))CHECK_EQ(d->source_status,SourceStatus::Missing);
        if(spec.root==0)CHECK_EQ(d->effective_root,0);if(spec.fine==199)CHECK_EQ(d->effective_fine,199);
        EqualBacking(*h.next->backing(),bytes);CHECK_EQ(allocations,before);
    }
    // Explicit unknown parameter encoding and an overlong valid UTF-8 reference
    // are saved, not normalized or truncated to fit the runtime path pool.
    std::string long_path(256,'a');auto bytes=Fixture({{11,long_path.c_str()}},{11});Harness h;h.Load(bytes);CHECK_EQ(h.Begin(),Result::Ok);
    CHECK_EQ(h.next->At(0)->path_status,PathStatus::TooLong);CHECK_EQ(h.Run(),Admission::Blocked);EqualBacking(*h.next->backing(),bytes);
}

TEST_CASE(resource_path_record_byte_exhaustion_and_fragmentation) {
    auto bytes=Fixture({{11,"Samples/new.wav"}},{11});
    {Harness h;h.Load(bytes);CHECK_EQ(h.Begin(),Result::Ok);PathFill fill;CHECK_EQ(fill.Records(),256u);
        CHECK_EQ(h.Run(),Admission::Blocked);CHECK_EQ(h.next->At(0)->path_status,PathStatus::Capacity);CHECK(h.next->backing());}
    {Harness h;h.Load(bytes);CHECK_EQ(h.Begin(),Result::Ok);PathFill fill;
        for(unsigned i=0;i<32;++i){char path[256];std::memset(path,'a',255);path[0]=char('A'+i/26);path[1]=char('a'+i%26);path[255]=0;CHECK(fill.refs[i].Assign(path));}
        CHECK_EQ(ResourcePaths().UsedBytes(),8192u);CHECK_EQ(h.Run(),Admission::Blocked);CHECK_EQ(h.next->At(0)->path_status,PathStatus::Capacity);}
    // First-fit allocator cannot satisfy a long path from disjoint short holes.
    {std::array<ResourcePathRef,128> refs;char path[128];
        for(unsigned i=0;i<128;++i){std::memset(path,'a',63);path[0]=char('A'+i/26);path[1]=char('a'+i%26);path[63]=0;CHECK(refs[i].Assign(path));}
        for(unsigned i=0;i<128;i+=2)refs[i].Reset();CHECK_EQ(ResourcePaths().UsedBytes(),4096u);
        std::memset(path,'b',127);path[127]=0;ResourcePathRef longer;CHECK(!longer.Assign(path));
        refs[1].Reset();CHECK(longer.Assign(path));CHECK_EQ(std::strlen(longer.Get()),127u);}
    CHECK_EQ(ResourcePaths().UsedBytes(),0u);
}

TEST_CASE(resource_source_exhaustion_compatible_pins_and_versions) {
    auto old=Fixture({{11,"Samples/old.wav"}},{11});auto next=Fixture({{42,"Samples/new.wav"}},{42});
    const auto before=allocations;Harness h;h.Load(old,true);h.Load(next);h.sources.limit=1;CHECK_EQ(h.Begin(true),Result::Ok);CHECK_EQ(h.Run(true),Admission::Ready);
    SourceLease voice;CHECK(h.old->At(0)->source.Clone(voice));const auto token=voice.token();
    CHECK_EQ(h.Begin(),Result::Ok);CHECK_EQ(h.Run(),Admission::Blocked);CHECK_EQ(h.next->At(0)->source_status,SourceStatus::Capacity);
    CHECK_EQ(h.sources.Live(),1u);CHECK_EQ(h.sources.Pins(),2u);CHECK(voice.token()==token);CHECK_EQ(std::strcmp(h.old->At(0)->path.Get(),"Samples/old.wav"),0);
    h.sources.limit=2;CHECK_EQ(h.Begin(),Result::Ok);CHECK_EQ(h.Run(),Admission::Ready);CHECK_EQ(h.sources.Live(),2u);
    CHECK_EQ(h.next->Cancel(),Code::Ok);CHECK_EQ(h.old->Cancel(),Code::Ok);CHECK_EQ(h.sources.Live(),1u);voice.Reset();CHECK_EQ(h.sources.Live(),0u);
    CHECK(!h.sources.Retain(token));
    CHECK_EQ(h.Begin(true),Result::Ok);CHECK_EQ(h.Run(true),Admission::Ready);const auto current=h.old->At(0)->source.token();
    ++h.sources.version;SourceLease updated;CHECK_EQ(h.sources.Acquire("Samples/old.wav",updated),SourceStatus::Ready);CHECK(!(updated.token()==current));CHECK_EQ(h.sources.Live(),2u);
    updated.Reset();CHECK_EQ(h.sources.Live(),1u);CHECK_EQ(allocations,before);measurements<<"source_capacity_versions_and_voice_pins,"<<allocations-before<<','<<h.arena.used()<<'\n';
}

TEST_CASE(resource_cancel_and_failure_after_every_acquisition) {
    auto old=Fixture({{11,"Samples/active.wav"}},{11});auto bytes=Fixture({{42,"Samples/A.wav"},{500,"Samples/B.wav"}},{42,500});
    for(unsigned stop=0;stop<=4;++stop){const auto before=allocations;Harness h;h.Load(old,true);h.Load(bytes);CHECK_EQ(h.Begin(true),Result::Ok);CHECK_EQ(h.Run(true),Admission::Ready);
        const auto used=ResourcePaths().UsedBytes(),pins=h.sources.Pins();CHECK_EQ(h.Begin(),Result::Ok);
        for(unsigned i=0;i<stop;++i)CHECK_EQ(h.next->Step(h.sources,*h.scratch),Admission::Progress);
        CHECK_EQ(h.next->Cancel(),Code::Ok);CHECK_EQ(ResourcePaths().UsedBytes(),used);CHECK_EQ(h.sources.Pins(),pins);CHECK_EQ(h.sources.Live(),1u);
        CHECK_EQ(std::strcmp(h.old->At(0)->path.Get(),"Samples/active.wav"),0);EqualBacking(*h.next->backing(),bytes);CHECK_EQ(allocations,before);}
    for(unsigned stop=0;stop<4;++stop){Harness h;h.Load(old,true);h.Load(bytes);CHECK_EQ(h.Begin(true),Result::Ok);CHECK_EQ(h.Run(true),Admission::Ready);
        const auto used=ResourcePaths().UsedBytes(),pins=h.sources.Pins();CHECK_EQ(h.Begin(),Result::Ok);
        for(unsigned i=0;i<stop;++i)CHECK_EQ(h.next->Step(h.sources,*h.scratch),Admission::Progress);
        {PathFill fill;if(stop%2==0)(void)fill.Records();else {h.sources.fail_at=h.sources.acquires+1;h.sources.failure=SourceStatus::Capacity;}
            CHECK_EQ(h.next->Step(h.sources,*h.scratch),Admission::Blocked);}
        CHECK_EQ(ResourcePaths().UsedBytes(),used);CHECK_EQ(h.sources.Pins(),pins);CHECK_EQ(h.sources.Live(),1u);EqualBacking(*h.old->backing(),old);}
}

TEST_CASE(resource_shared_path_survives_full_pool_and_io_rollback) {
    auto old=Fixture({{11,"Samples/shared.wav"}},{11});
    auto shared=Fixture({{42,"Samples/shared.wav"},{500,"Samples/new.wav"}},{42,500});
    const auto before=allocations;Harness h;h.Load(old,true);h.Load(shared);CHECK_EQ(h.Begin(true),Result::Ok);CHECK_EQ(h.Run(true),Admission::Ready);
    const auto pins=h.sources.Pins();CHECK_EQ(h.Begin(),Result::Ok);
    {PathFill fill;(void)fill.Records();CHECK_EQ(ResourcePaths().UniqueCount(),256u);
        CHECK_EQ(h.next->Step(h.sources,*h.scratch),Admission::Progress);CHECK_EQ(h.next->At(0)->path.Id(),h.old->At(0)->path.Id());
        CHECK_EQ(h.next->Step(h.sources,*h.scratch),Admission::Progress);CHECK_EQ(h.sources.Pins(),pins+1);
        CHECK_EQ(h.next->Step(h.sources,*h.scratch),Admission::Blocked);}
    CHECK_EQ(h.sources.Pins(),pins);CHECK_EQ(ResourcePaths().UniqueCount(),1u);
    for(unsigned failing_source=1;failing_source<=2;++failing_source) {
        CHECK_EQ(h.Begin(),Result::Ok);h.sources.fail_at=h.sources.acquires+failing_source;h.sources.failure=SourceStatus::Io;
        CHECK_EQ(h.Run(),Admission::Io);CHECK_EQ(h.sources.Pins(),pins);CHECK_EQ(ResourcePaths().UniqueCount(),1u);
        EqualBacking(*h.next->backing(),shared);
    }
    CHECK_EQ(allocations,before);
}

TEST_CASE(resource_unknown_settings_and_unresolved_initial_context) {
    // Field encoding 0 is an unknown optional encoding, not absence.
    auto opaque=Fixture({{11,"Samples/opaque.wav",-1,-1,1,0,0}},{11});
    {Harness h;h.Load(opaque);CHECK_EQ(h.Begin(),Result::Ok);CHECK(h.next->At(0)->saved.resource_present);
        CHECK_EQ(h.next->At(0)->path_status,PathStatus::UnsupportedEncoding);CHECK_EQ(h.Run(),Admission::Blocked);EqualBacking(*h.next->backing(),opaque);}
    auto unresolved=Fixture({{11,"Samples/a.wav"}},{99});
    {Harness h;h.Load(unresolved);CHECK_EQ(h.Begin(),Result::Ok);CHECK(h.next->unresolved_requirements());CHECK_EQ(h.Run(),Admission::Blocked);CHECK_EQ(h.sources.acquires,0u);}
    auto incoming=Fixture({{11,"Samples/a.wav"}},{0});
    {Harness h;h.Load(incoming);CHECK_EQ(h.Begin(),Result::Ok);CHECK(h.next->unresolved_requirements());CHECK_EQ(h.Run(),Admission::Blocked);
        InitialRequirements requirements{101,{}};requirements.incoming_instruments[0]=11;
        CHECK_EQ(h.next->Begin(*h.new_doc,requirements,*h.work,*h.pattern,*h.scratch),Result::Ok);CHECK_EQ(h.Run(),Admission::Ready);}
}

TEST_CASE(resource_stopped_handoff_rollback_old_release_and_cycles) {
    auto old=Fixture({{11,"Samples/old.wav"}},{11});auto bytes=Fixture({{42,"Samples/new.wav"}},{42});
    const auto before=allocations;Harness h;h.Load(old,true);h.Load(bytes);CHECK_EQ(h.Begin(true),Result::Ok);CHECK_EQ(h.Run(true),Admission::Ready);
    StoppedExchange exchange;CHECK_EQ(exchange.Commit(*h.old,false),Code::Busy);CHECK_EQ(exchange.Commit(*h.old,true),Code::Ok);
    CHECK_EQ(h.old->Cancel(),Code::Busy);CHECK_EQ(h.arena.Destroy(h.old),Code::Busy);CHECK_EQ(h.Begin(),Result::Ok);CHECK_EQ(h.Run(),Admission::Ready);
    SourceLease voice;CHECK(h.old->At(0)->source.Clone(voice));CHECK_EQ(exchange.Commit(*h.next,true),Code::Ok);CHECK(exchange.previous()==h.old.get());
    CHECK_EQ(exchange.Rollback(true),Code::Ok);CHECK(exchange.current()==h.old.get());CHECK_EQ(exchange.Rollback(true),Code::Ok);
    CHECK_EQ(exchange.ReleasePrevious(true),Code::Ok);CHECK_EQ(h.old->count(),0u);CHECK_EQ(h.sources.Live(),2u);
    voice.Reset();CHECK_EQ(h.sources.Live(),1u);CHECK_EQ(exchange.Detach(true),Code::Ok);CHECK_EQ(h.next->Discard(),Code::Ok);
    const auto base=h.new_doc->backing.use_count();
    for(unsigned cycle=0;cycle<100;++cycle){CHECK_EQ(h.Begin(),Result::Ok);CHECK_EQ(h.Run(),Admission::Ready);CHECK_EQ(h.next->Cancel(),Code::Ok);
        CHECK_EQ(h.sources.Live(),0u);CHECK_EQ(ResourcePaths().UsedBytes(),0u);CHECK_EQ(h.next->Discard(),Code::Ok);CHECK_EQ(h.new_doc->backing.use_count(),base);}
    CHECK_EQ(allocations,before);measurements<<"handoff_rollback_release_100_cycles,"<<allocations-before<<','<<h.arena.used()<<'\n';
}

TEST_CASE(resource_saved_backing_recovery_and_full_teardown_accounting) {
    const auto bytes=Fixture({{11,"Samples/intent.wav",0,199}},{11});
    {Harness h;h.Load(bytes);CHECK_EQ(h.Begin(),Result::Ok);CHECK_EQ(h.Run(),Admission::Blocked);
        h.new_doc->backing.reset();CHECK(h.next->backing());EqualBacking(*h.next->backing(),bytes);
        CHECK_EQ(Validate(h.next->backing(),*h.new_doc,*h.work),Result::Ok);
        Pool::Output writer;Pool::Token output_token;CHECK_EQ(h.pool->Create(writer,output_token),Code::Ok);
        CHECK_EQ(Rewrite(*h.new_doc,writer,*h.work),Result::Ok);ReaderOwner rewritten;CHECK_EQ(writer.Finish(rewritten),Code::Ok);
        EqualBacking(*rewritten,bytes);rewritten.reset();CHECK_EQ(h.pool->Discard(output_token),Code::Ok);}
    std::vector<Spec> specs;for(unsigned i=0;i<99;++i)specs.push_back({100+i,i?"Samples/unused.wav":"Samples/new.wav"});
    const auto many=Fixture(specs,{100});const auto old=Fixture({{11,"Samples/active.wav"}},{11});
    const auto before=allocations;size_t peak=0;
    {Harness h;h.Load(old,true);h.Load(many);CHECK_EQ(h.Begin(true),Result::Ok);CHECK_EQ(h.Run(true),Admission::Ready);
        CHECK_EQ(h.Begin(),Result::Ok);CHECK_EQ(h.next->count(),99u);CHECK_EQ(h.Run(),Admission::Ready);
        CHECK_EQ(h.sources.acquires,2u);CHECK_EQ(h.sources.Live(),2u);CHECK_EQ(ResourcePaths().UniqueCount(),2u);peak=h.arena.used();}
    CHECK_EQ(ResourcePaths().UsedBytes(),0u);CHECK_EQ(allocations,before);
    measurements<<"complete_setup_and_destructor_teardown_99_definitions,"<<allocations-before<<','<<peak<<'\n';
}
}
int main(int argc,char** argv){output=argc>1?argv[1]:"build/btt-resource-admission";std::filesystem::create_directories(output);
    measurements.open(output/"allocations.csv");measurements<<"operation,cpp_heap_allocations,simultaneous_arena_used\n";const int result=test::RunAll();
    std::ofstream memory_report(output/"host-memory.txt");memory_report<<"Host ABI only; fixture/fake disk bytes excluded.\nSavedInstrument="<<sizeof(SavedInstrument)<<"\nDefinition="<<sizeof(Definition)
        <<"\nCandidate="<<sizeof(Candidate)<<"\nTwo candidates="<<2*sizeof(Candidate)<<"\nAdmissionScratch="<<sizeof(AdmissionScratch)<<"\nSourceLease="<<sizeof(SourceLease)
        <<"\nSourceToken="<<sizeof(SourceToken)<<"\nStoppedExchange="<<sizeof(StoppedExchange)<<"\nFake Sources metadata="<<sizeof(Sources)<<"\nArena metadata="<<sizeof(Arena)
        <<"\nComplete host harness context (arena metadata, fake source metadata, handles)="<<sizeof(Harness)
        <<"\nDocument="<<sizeof(Document)<<"\nPattern="<<sizeof(Pattern)<<"\nWorkspace="<<sizeof(Workspace)<<"\nGlobal real ResourcePathPool="<<sizeof(BroTracker::ResourcePathPool)
        <<"\nNo second path pool, audio ring or PCM prefix cache allocated. Source PCM preparation/handoff is simulated.\n";return result;}

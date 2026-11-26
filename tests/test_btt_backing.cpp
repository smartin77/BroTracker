#include "test_framework.h"
#include "btt_pattern_layout.h"
#include "backing.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>

static size_t allocations=0;
void* operator new(size_t n) {if(auto* p=std::malloc(n?n:1)){++allocations;return p;}throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
void operator delete[](void* p,size_t) noexcept {std::free(p);}

namespace {
using namespace btt;
using namespace btt::prototype;
namespace fixture=btt_layout;
enum Op {CreateOp,OpenOp,SeekOp,ReadOp,WriteOp,ResizeOp,SyncOp,CloseOp,RemoveOp,OpCount};
struct FakeBackend {
    struct Handle {int index=-1;bool write=false;};
    struct File {char path[96]{};bool exists=false;unsigned opens=0;uint32_t length=0;std::array<uint8_t,131072> bytes{};};
    std::array<File,3> files{};
    std::array<unsigned,OpCount> calls{};
    Op fault=OpCount;unsigned nth=0;
    bool external_read=false,external_write=false;
    unsigned leases=0,max_read=0;
    void Reset() {files={};calls={};fault=OpCount;nth=0;external_read=external_write=false;leases=max_read=0;}
    void Fault(Op op,unsigned at=1) {calls={};fault=op;nth=at;}
    bool Fail(Op op) {return ++calls[op]==nth && op==fault;}
    struct Lease {
        FakeBackend& b;bool held;
        Lease(FakeBackend& backend,bool write):b(backend),held(!b.external_write && (!write || !b.external_read)) {if(held)++b.leases;}
        ~Lease(){if(held)--b.leases;}
        explicit operator bool()const{return held;}
    };
    bool Open(const Handle& h) const {return h.index>=0;}
    bool Create(Handle& h,const char* path) {
        Lease lease(*this,true);if(!lease || Fail(CreateOp) || Open(h))return false;
        for(const auto& f:files)if(f.exists && !std::strcmp(f.path,path))return false;
        for(unsigned i=0;i<files.size();++i)if(!files[i].exists) {
            auto& f=files[i];f.exists=true;f.length=0;f.opens=1;std::strcpy(f.path,path);h={int(i),true};return true;
        }return false;
    }
    bool ReadOpen(Handle& h,const char* path,uint32_t& length) {
        Lease lease(*this,false);if(!lease || Fail(OpenOp) || Open(h))return false;
        for(unsigned i=0;i<files.size();++i)if(files[i].exists && !std::strcmp(files[i].path,path)) {
            ++files[i].opens;h={int(i),false};length=files[i].length;return true;
        }return false;
    }
    bool Read(Handle& h,uint32_t at,void* p,unsigned n) {
        Lease lease(*this,false);max_read=std::max(max_read,n);
        if(!lease || !Open(h) || Fail(SeekOp))return false;
        auto& f=files[h.index];if(n>512 || at>f.length || n>f.length-at)return false;
        const bool fail=Fail(ReadOp);std::memcpy(p,f.bytes.data()+at,fail && n?n-1:n);return !fail;
    }
    bool Write(Handle& h,uint32_t at,const void* p,unsigned n) {
        Lease lease(*this,true);
        if(!lease || !Open(h) || !h.write || Fail(SeekOp))return false;
        auto& f=files[h.index];if(n>512 || at>f.bytes.size() || n>f.bytes.size()-at)return false;
        const bool fail=Fail(WriteOp);const auto take=fail && n?n-1:n;
        std::memcpy(f.bytes.data()+at,p,take);f.length=std::max<uint32_t>(f.length,at+take);return !fail;
    }
    bool Resize(Handle& h,uint32_t n) {
        Lease lease(*this,true);if(!lease || !Open(h) || Fail(ResizeOp) || n>files[h.index].bytes.size())return false;
        files[h.index].length=n;return true;
    }
    bool Sync(Handle& h) {Lease lease(*this,true);return lease && Open(h) && !Fail(SyncOp);}
    bool Close(Handle& h) {
        if(!Open(h))return true;
        Lease lease(*this,true);if(!lease || Fail(CloseOp))return false;
        --files[h.index].opens;h={};return true;
    }
    bool Remove(const char* path) {
        Lease lease(*this,true);if(!lease || Fail(RemoveOp))return false;
        for(auto& f:files)if(f.exists && !std::strcmp(f.path,path)) {if(f.opens)return false;f.exists=false;return true;}return false;
    }
};
using Pool=Backings<FakeBackend>;
static FakeBackend backend; // Fixture/fake disk bytes, never firmware/prototype resident RAM.
alignas(32) static unsigned char memory[65536];
std::filesystem::path output;
std::ofstream metrics;
struct Harness {
    Arena arena;
    Object<Pool> pool;
    Object<Document> active,candidate,copy;
    Object<Workspace> work;
    Object<Pattern> pattern;
    Harness() {
        backend.Reset();CHECK_EQ(arena.Init(memory,sizeof(memory)),Code::Ok);
        CHECK_EQ(arena.New(pool,backend),Code::Ok);
        CHECK_EQ(arena.New(active),Code::Ok);CHECK_EQ(arena.New(candidate),Code::Ok);CHECK_EQ(arena.New(copy),Code::Ok);
        CHECK_EQ(arena.New(work),Code::Ok);CHECK_EQ(arena.New(pattern),Code::Ok);
    }
    ~Harness() {
        backend.fault=OpCount;backend.external_write=backend.external_read=false;
        CHECK_EQ(arena.Destroy(active),Code::Ok);CHECK_EQ(arena.Destroy(candidate),Code::Ok);CHECK_EQ(arena.Destroy(copy),Code::Ok);
        // Retry quarantined closes explicitly; keep on-disk bytes unless Discard
        // was requested by the test. No automatic recovery/promotion is implied.
        CHECK(pool->CanDestroy());CHECK_EQ(arena.Reset(),Code::Ok);CHECK_EQ(backend.leases,0u);
    }
};
std::vector<uint8_t> Fixture(unsigned codec=3,bool root=true,bool fine=true,unsigned optional=60000,unsigned bpm=12500) {
    std::vector<uint8_t> tune,inst,order,ord,patt;
    fixture::Tlv(tune,1,4,std::vector<uint8_t>(16,1));fixture::Tlv(tune,2,5,{'B','o','u','n','d','e','d'});
    fixture::Scalar(tune,3,4,bpm);fixture::Scalar(tune,4,1,8);fixture::Scalar(tune,5,4,8000);fixture::Scalar(tune,6,4,10000);
    fixture::Tlv(tune,500,99,{3,0,1},false);
    for(auto id:{17,9001,17})fixture::Put(order,id,4);fixture::Tlv(ord,1,6,order);
    fixture::Scalar(inst,1,2,1);fixture::Tlv(inst,2,6,{0,'m','i','s','s','i','n','g','.','w','a','v'},false);
    if(root)fixture::Scalar(inst,3,2,0,false);if(fine)fixture::Scalar(inst,4,2,199,false);
    fixture::Pattern p;p.Clear(64);fixture::Cell cell;cell.fx={17,uint16_t(codec>=3?65535:255),42};cell.values={0,255,0};p.Put(1,cell);
    cell.note=60;cell.instrument=7999;p.Put(2,cell);CHECK(fixture::Encode(p,codec,patt));
    return fixture::Envelope({{"TUNE",0,tune},{"ORDR",0,ord},{"INST",7001,inst},{"PATT",17,patt},{"PATT",9001,patt},
        {"XTRA",5,std::vector<uint8_t>(optional,0x5a),0}});
}
Code Put(Harness& h,const std::vector<uint8_t>& bytes,Pool::Token& token,ReaderOwner& owner) {
    Pool::Output writer;auto result=h.pool->Create(writer,token);if(result!=Code::Ok)return result;
    for(unsigned at=0;at<bytes.size();) {
        const auto n=std::min(512u,unsigned(bytes.size())-at);
        if(writer.write(at,bytes.data()+at,n)!=n) {writer.Cancel();return Code::Io;}at+=n;
    }
    return writer.Finish(owner);
}
bool Equals(const Reader& r,const std::vector<uint8_t>& bytes) {
    if(r.size()!=bytes.size())return false;uint8_t b[512];
    for(unsigned at=0;at<bytes.size();) {auto n=std::min(512u,unsigned(bytes.size())-at);if(r.read(at,b,n)!=n || std::memcmp(b,bytes.data()+at,n))return false;at+=n;}return true;
}
unsigned SavedTuning(const Document& d,unsigned tag) {
    const auto* e=Find(d,"INST",7001);CHECK(e);if(!e)return UINT32_MAX;
    for(unsigned at=e->offset;at<e->offset+e->length;) {
        uint8_t h[8],v[2];if(d.backing->read(at,h,8)!=8)return UINT32_MAX;
        if(fixture::Get(h,2)==tag) {if(d.backing->read(at+8,v,2)!=2)return UINT32_MAX;return fixture::Get(v,2);}
        at+=8+fixture::Get(h+4,4);
    }return UINT32_MAX;
}
struct SaveStore final:Store {
    Pool::Output& writer;
    ReaderOwner old,sealed,current;
    bool fail_promote=false;
    SaveStore(Pool::Output& w,ReaderOwner source):writer(w),old(source),current(source){}
    bool finish() override {return writer.Finish(sealed)==Code::Ok;}
    ReaderOwner reopen() override {return sealed;}
    bool promote() override {if(fail_promote)return false;current=sealed;return true;}
};

TEST_CASE(backing_owner_lifecycle_exhaustion_and_stale_generations) {
    const auto fixture=Fixture();const auto before=allocations;
    Harness h;ReaderOwner owner;Pool::Token first;
    CHECK_EQ(Put(h,fixture,first,owner),Code::Ok);CHECK_EQ(Validate(owner,*h.active,*h.work),Result::Ok);
    *h.copy=*h.active;owner.reset();h.active->backing.reset();CHECK(h.copy->backing);CHECK(Equals(*h.copy->backing,fixture));
    ReaderOwner moved=std::move(h.copy->backing);CHECK(!h.copy->backing);CHECK(moved);
    CHECK_EQ(h.pool->Discard(first),Code::Busy);Pool::Output alias;CHECK_EQ(h.pool->OpenWriter(first,alias),Code::Busy);
    Pool::Output a,b,c;Pool::Token ta,tb,tc;
    CHECK_EQ(h.pool->Create(a,ta),Code::Ok);CHECK_EQ(h.pool->Create(b,tb),Code::Ok);
    CHECK_EQ(h.pool->Discard(ta),Code::Busy);
    Pool::Output moved_writer=std::move(a);CHECK(!a.identity());CHECK(moved_writer.identity());a=std::move(moved_writer);
    CHECK_EQ(h.pool->Create(c,tc),Code::Capacity);CHECK(Equals(*moved,fixture));
    ReaderOwner unchanged=moved;CHECK_EQ(h.pool->Acquire(tc,unchanged),Code::Stale);CHECK(Equals(*unchanged,fixture));unchanged.reset();
    CHECK_EQ(a.Cancel(),Code::Ok);CHECK_EQ(b.Cancel(),Code::Ok);
    CHECK_EQ(h.pool->Discard(ta),Code::Ok);CHECK_EQ(h.pool->Discard(tb),Code::Ok);
    moved.reset();CHECK_EQ(h.pool->Discard(first),Code::Ok);
    CHECK_EQ(Put(h,fixture,tc,owner),Code::Ok);CHECK(first.generation!=tc.generation);uint8_t byte;
    CHECK_EQ(h.pool->Read(first,0,&byte,1),0u);CHECK_EQ(h.pool->Acquire(first,unchanged),Code::Stale);
    CHECK_EQ(h.pool->OpenWriter(first,c),Code::Stale);CHECK_EQ(h.pool->Discard(first),Code::Stale);
    owner.reset();CHECK_EQ(h.pool->Discard(tc),Code::Ok);CHECK_EQ(allocations,before);
    metrics<<"owner_create_copy_move_release_exhaustion,"<<allocations-before<<'\n';
}

TEST_CASE(backing_arena_lifetime_alignment_exhaustion_and_reuse) {
    {
        Arena records;alignas(32) unsigned char small[64]{};Object<uint8_t> value;
        CHECK_EQ(records.Init(small,sizeof(small)),Code::Ok);
        for(unsigned i=0;i<16;++i)CHECK_EQ(records.New(value),Code::Ok);
        CHECK_EQ(records.New(value),Code::Capacity);CHECK(value.get());
        struct alignas(64) Wide {uint8_t byte=0;};Object<Wide> wide;
        CHECK_EQ(records.New(wide),Code::Invalid);CHECK_EQ(records.Reset(),Code::Ok);CHECK(!value.get());
    }
    const auto before=allocations;Harness h;
    Object<Pattern> second,third,too_large;CHECK_EQ(h.arena.New(second),Code::Ok);CHECK_EQ(h.arena.New(third),Code::Ok);
    while(h.arena.New(too_large)==Code::Ok) {} // Finite fixed record/byte capacities.
    const auto used=h.arena.used();Object<Pattern> failed;CHECK_EQ(h.arena.New(failed),Code::Capacity);CHECK(!failed.get());CHECK_EQ(h.arena.used(),used);
    CHECK_EQ(reinterpret_cast<uintptr_t>(h.pattern.get())%alignof(Pattern),0u);
    CHECK_EQ(reinterpret_cast<uintptr_t>(h.work.get())%alignof(Workspace),0u);
    CHECK_EQ(h.arena.Init(memory+1,sizeof(memory)-1),Code::Busy);
    const auto bytes=Fixture(); // Fixture allocation is explicitly outside the zero-allocation assertion below.
    Pool::Token token;ReaderOwner owner;const auto operation_before=allocations;
    CHECK_EQ(Put(h,bytes,token,owner),Code::Ok);CHECK_EQ(Validate(owner,*h.active,*h.work),Result::Ok);owner.reset();
    CHECK_EQ(h.arena.Destroy(h.pool),Code::Busy);CHECK_EQ(h.arena.Reset(),Code::Busy);CHECK(h.active->accepted);
    h.active->backing.reset();CHECK_EQ(h.pool->Discard(token),Code::Ok);
    CHECK_EQ(allocations,operation_before);
    CHECK_EQ(h.arena.Destroy(second),Code::Ok);CHECK(!second.get());
    // Harness owns the documents; destroy them before resetting the pool.
    CHECK_EQ(h.arena.Destroy(h.active),Code::Ok);CHECK_EQ(h.arena.Destroy(h.candidate),Code::Ok);CHECK_EQ(h.arena.Destroy(h.copy),Code::Ok);
    CHECK_EQ(h.arena.Reset(),Code::Ok);CHECK(!third.get());
    CHECK_EQ(h.arena.Init(memory+1,sizeof(memory)-1),Code::Invalid);CHECK_EQ(h.arena.Init(memory,sizeof(memory)),Code::Ok);
    CHECK_EQ(h.arena.New(h.pool,backend),Code::Ok);CHECK_EQ(h.arena.New(h.active),Code::Ok);
    CHECK_EQ(h.arena.New(h.candidate),Code::Ok);CHECK_EQ(h.arena.New(h.copy),Code::Ok);
    CHECK_EQ(h.arena.New(h.work),Code::Ok);CHECK_EQ(h.arena.New(h.pattern),Code::Ok);
    ReaderOwner stale;CHECK_EQ(h.pool->Acquire(token,stale),Code::Stale);CHECK_EQ(allocations,operation_before);
    Pool::Token replacement;CHECK_EQ(Put(h,bytes,replacement,owner),Code::Ok);
    CHECK_EQ(replacement.generation,token.generation);CHECK(replacement.domain!=token.domain);
    CHECK_EQ(h.pool->Acquire(token,stale),Code::Stale);owner.reset();CHECK_EQ(h.pool->Discard(replacement),Code::Ok);
    metrics<<"arena_ownership_operations,"<<allocations-operation_before<<" # fixture allocations excluded="<<operation_before-before<<'\n';
}

TEST_CASE(backing_bounded_foundation_rewrite_save_preserves_data) {
    for(unsigned codec=1;codec<=4;++codec)for(bool root:{false,true})for(bool fine:{false,true}) {
        auto bytes=Fixture(codec,root,fine);
        const auto expected_edit=Fixture(codec,true,true,60000,15000);
        const auto before=allocations;Harness h;ReaderOwner owner;Pool::Token source,target;
        CHECK_EQ(Put(h,bytes,source,owner),Code::Ok);CHECK_EQ(Validate(owner,*h.active,*h.work),Result::Ok);
        CHECK(h.active->status.unsupported_values);CHECK(h.active->status.unresolved_instruments);
        CHECK_EQ(Materialize(*h.active,9001,*h.pattern,*h.work),Result::Ok);CHECK(h.pattern->ready);
        CHECK_EQ(h.pattern->At(1).note,255);CHECK_EQ(h.pattern->At(1).fx[2],42);CHECK_EQ(h.pattern->At(1).values[0],0);
        CHECK_EQ(h.pattern->At(1).fx[1],codec>=3?65535:255);
        Pool::Output writer;CHECK_EQ(h.pool->Create(writer,target),Code::Ok);
        {
            SaveStore store(writer,owner);CHECK_EQ(Save(*h.active,writer,store,*h.candidate,*h.work),Result::Ok);
            if(root && fine)CHECK(Equals(*h.candidate->backing,bytes));
            CHECK_EQ(h.candidate->bpm,12500u);
            CHECK_EQ(SavedTuning(*h.candidate,3),root?0u:60u);CHECK_EQ(SavedTuning(*h.candidate,4),fine?199u:0u);
        }
        // A second no-edit save must be stable even if defaults were inserted.
        Pool::Output stable;Pool::Token last;ReaderOwner last_owner;
        CHECK_EQ(h.pool->Create(stable,last),Code::Ok);CHECK_EQ(Rewrite(*h.candidate,stable,*h.work),Result::Ok);CHECK_EQ(stable.Finish(last_owner),Code::Ok);
        uint8_t a[512],b[512];CHECK_EQ(last_owner->size(),h.candidate->backing->size());
        for(unsigned at=0;at<last_owner->size();) {auto n=std::min<uint32_t>(512,last_owner->size()-at);
            CHECK_EQ(last_owner->read(at,a,n),n);CHECK_EQ(h.candidate->backing->read(at,b,n),n);CHECK(!std::memcmp(a,b,n));at+=n;}
        last_owner.reset();CHECK_EQ(h.pool->Discard(last),Code::Ok);
        // BPM edit preserves every unrelated byte through the existing exact comparison Save path.
        Pool::Output edited;CHECK_EQ(h.pool->Create(edited,last),Code::Ok);
        {SaveStore store(edited,owner);CHECK_EQ(Save(*h.active,edited,store,*h.copy,*h.work,15000),Result::Ok);CHECK_EQ(h.copy->bpm,15000u);}
        if(root && fine)CHECK(Equals(*h.copy->backing,expected_edit));
        CHECK(Equals(*owner,bytes));CHECK_EQ(backend.leases,0u);CHECK(backend.max_read<=512);CHECK_EQ(allocations,before);
        metrics<<"bounded_save_codec_"<<codec<<"_root_"<<root<<"_fine_"<<fine<<','<<allocations-before<<'\n';
        h.copy->backing.reset();CHECK_EQ(h.pool->Discard(last),Code::Ok);
        h.candidate->backing.reset();CHECK_EQ(h.pool->Discard(target),Code::Ok);
        h.active->backing.reset();owner.reset();CHECK_EQ(h.pool->Discard(source),Code::Ok);
    }
}

TEST_CASE(backing_alias_arbitration_and_close_quarantine) {
    auto bytes=Fixture();const auto before=allocations;Harness h;ReaderOwner owner;Pool::Token token;
    CHECK_EQ(Put(h,bytes,token,owner),Code::Ok);CHECK_EQ(Validate(owner,*h.active,*h.work),Result::Ok);
    struct AlternateWriter:Writer {
        const void* id;unsigned calls=0;
        explicit AlternateWriter(const void* p):id(p){}
        const void* identity()const override{return id;}
        unsigned write(uint32_t,const void*,unsigned)override{++calls;return 0;}
        bool resize(uint32_t)override{++calls;return false;}
    } alias(h.pool->Identity(token));
    CHECK_EQ(Rewrite(*h.active,alias,*h.work),Result::Backing);CHECK_EQ(alias.calls,0u);
    backend.external_write=true;CHECK_EQ(Materialize(*h.active,17,*h.pattern,*h.work),Result::Io);CHECK(!h.pattern->ready);
    backend.external_write=false;CHECK_EQ(Materialize(*h.active,17,*h.pattern,*h.work),Result::Ok);
    owner.reset();backend.Fault(CloseOp);h.active->backing.reset();
    CHECK_EQ(h.pool->Inspect(token),Code::Io);CHECK(!h.pool->CanDestroy());CHECK_EQ(h.arena.Destroy(h.pool),Code::Busy);
    ReaderOwner failed;CHECK_EQ(h.pool->Acquire(token,failed),Code::Io);
    backend.fault=OpCount;CHECK_EQ(h.pool->Discard(token),Code::Ok);CHECK(h.pool->CanDestroy());CHECK_EQ(allocations,before);
}

TEST_CASE(backing_injected_io_failures_preserve_active_and_previous) {
    auto bytes=Fixture();
    for(Op op:{CreateOp,OpenOp,SeekOp,ReadOp,WriteOp,ResizeOp,SyncOp,CloseOp,RemoveOp}) {
        Harness h;ReaderOwner owner;Pool::Token source,target;CHECK_EQ(Put(h,bytes,source,owner),Code::Ok);
        CHECK_EQ(Validate(owner,*h.active,*h.work),Result::Ok);const auto before=allocations;
        Pool::Output writer;
        if(op==CreateOp) {
            backend.Fault(op);CHECK_EQ(h.pool->Create(writer,target),Code::Io);
        } else {
            CHECK_EQ(h.pool->Create(writer,target),Code::Ok);
            if(op==ReadOp) {
                backend.Fault(op);CHECK_EQ(Validate(owner,*h.candidate,*h.work),Result::Io);CHECK(!h.candidate->accepted);
                h.candidate->backing.reset();CHECK_EQ(writer.Cancel(),Code::Ok);
            } else if(op==RemoveOp) {
                CHECK_EQ(writer.Cancel(),Code::Ok);backend.Fault(op);CHECK_EQ(h.pool->Discard(target),Code::Io);
            } else {
                backend.Fault(op);
                {SaveStore store(writer,owner);const auto result=Save(*h.active,writer,store,*h.candidate,*h.work,15000);
                    CHECK(result!=Result::Ok);CHECK(!h.candidate->accepted);CHECK(store.current.get()==owner.get());}
                // Rewrite failure leaves the temporary writer for caller cleanup.
                if(writer.identity())(void)writer.Cancel();
            }
            backend.fault=OpCount;h.candidate->backing.reset();
            CHECK_EQ(h.pool->Discard(target),Code::Ok);
        }
        backend.fault=OpCount;CHECK(h.active->accepted);CHECK(Equals(*owner,bytes));CHECK_EQ(allocations,before);
        metrics<<"fault_"<<unsigned(op)<<','<<allocations-before<<'\n';
        h.active->backing.reset();owner.reset();CHECK_EQ(h.pool->Discard(source),Code::Ok);
    }
    // Successful candidate validation followed by failed promotion preserves old.
    Harness h;ReaderOwner owner;Pool::Token source,target;CHECK_EQ(Put(h,bytes,source,owner),Code::Ok);
    CHECK_EQ(Validate(owner,*h.active,*h.work),Result::Ok);Pool::Output writer;CHECK_EQ(h.pool->Create(writer,target),Code::Ok);
    {SaveStore store(writer,owner);store.fail_promote=true;CHECK_EQ(Save(*h.active,writer,store,*h.candidate,*h.work,15000),Result::Io);
        CHECK(store.current.get()==owner.get());CHECK(!h.candidate->accepted);CHECK(Equals(*store.current,bytes));}
    h.candidate->backing.reset();CHECK_EQ(h.pool->Discard(target),Code::Ok);h.active->backing.reset();owner.reset();CHECK_EQ(h.pool->Discard(source),Code::Ok);
}

TEST_CASE(backing_every_save_io_position_and_candidate_validation_failure) {
    const auto bytes=Fixture(3,true,true,8);
    std::array<unsigned,OpCount> counts{};
    {
        Harness h;ReaderOwner owner;Pool::Token source,target;
        CHECK_EQ(Put(h,bytes,source,owner),Code::Ok);CHECK_EQ(Validate(owner,*h.active,*h.work),Result::Ok);
        Pool::Output writer;CHECK_EQ(h.pool->Create(writer,target),Code::Ok);backend.calls={};
        {SaveStore store(writer,owner);CHECK_EQ(Save(*h.active,writer,store,*h.candidate,*h.work,15000),Result::Ok);counts=backend.calls;}
        h.candidate->backing.reset();CHECK_EQ(h.pool->Discard(target),Code::Ok);h.active->backing.reset();owner.reset();CHECK_EQ(h.pool->Discard(source),Code::Ok);
    }
    unsigned injected=0;
    for(Op op:{SeekOp,ReadOp,WriteOp,ResizeOp,SyncOp,CloseOp,OpenOp})for(unsigned call=1;call<=counts[op];++call) {
        Harness h;ReaderOwner owner;Pool::Token source,target;
        CHECK_EQ(Put(h,bytes,source,owner),Code::Ok);CHECK_EQ(Validate(owner,*h.active,*h.work),Result::Ok);
        Pool::Output writer;CHECK_EQ(h.pool->Create(writer,target),Code::Ok);backend.Fault(op,call);const auto before=allocations;
        {SaveStore store(writer,owner);CHECK(Save(*h.active,writer,store,*h.candidate,*h.work,15000)!=Result::Ok);
            CHECK(!h.candidate->accepted);CHECK(store.current.get()==owner.get());}
        backend.fault=OpCount;if(writer.identity())CHECK_EQ(writer.Cancel(),Code::Ok);
        h.candidate->backing.reset();CHECK_EQ(h.pool->Discard(target),Code::Ok);CHECK(Equals(*owner,bytes));
        h.active->backing.reset();owner.reset();CHECK_EQ(h.pool->Discard(source),Code::Ok);CHECK_EQ(allocations,before);++injected;
    }
    metrics<<"all_save_io_positions,"<<0<<" # injected="<<injected<<'\n';
}
}
int main(int argc,char** argv) {
    output=argc>1?argv[1]:"build/btt-backing-prototype";std::filesystem::create_directories(output);
    metrics.open(output/"allocations.csv");metrics<<"operation,cpp_heap_allocations_including_owner_creation\n";
    const auto result=test::RunAll();
    std::ofstream report(output/"host-memory.txt");
    report<<"Host ABI; not Teensy admission.\nReaderOwner="<<sizeof(ReaderOwner)<<"\nDocument="<<sizeof(Document)
        <<"\nArena metadata="<<sizeof(Arena)<<"\nFake-backed Pool="<<sizeof(Pool)<<"\nOutput="<<sizeof(Pool::Output)
        <<"\nWorkspace="<<sizeof(Workspace)<<"\nPattern="<<sizeof(Pattern)<<"\nTwo patterns="<<2*sizeof(Pattern)
        <<"\nArena raw storage=65536 (aligned 32), 16 object records; pool has 3 slots.\n"
        <<"Measured scopes include arena initialization, fixed backing/control creation, copy/move/release, Validate/Materialize/Rewrite/Save.\n"
        <<"Fixture vectors, fake disk byte arrays, framework registration and report I/O are outside these scopes.\n"
        <<"Fake disk is static test-only storage; no complete BTT payload is resident in the prototype owner/arena.\n";
    return result;
}

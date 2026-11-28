#include "test_framework.h"
#include "btt_pattern_layout.h" // Fixture encoding only; never used for validation.
#include "foundation.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <new>
#include <cstddef>
#ifdef _WIN32
#include <share.h>
#include <io.h>
#else
#include <unistd.h>
#endif

static size_t allocations=0;
void* operator new(size_t n) {if(auto p=std::malloc(n?n:1)){++allocations;return p;}throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete(void* p,size_t) noexcept {::operator delete(p);}
void operator delete[](void* p,size_t) noexcept {::operator delete(p);}

namespace {
using namespace btt;
// At most 15 logical bytes per cell plus a four-byte dimensions/readiness
// header. This fails if an ABI-padded Cell array is reintroduced.
static_assert(sizeof(Pattern)<=512*15+4,"Decoded pattern exceeds compact storage budget");
static_assert(2*sizeof(Pattern)<=15368,"Two decoded buffers exceed the layout experiment budget");
static_assert(alignof(Pattern)>=alignof(uint32_t),"Pattern must retain natural integer alignment");
static_assert(offsetof(Pattern,instruments)%alignof(uint32_t)==0,"Instrument IDs must be aligned");
static_assert(offsetof(Pattern,fx)%alignof(uint16_t)==0,"FX IDs must be aligned");
namespace fixture=btt_layout;
std::filesystem::path root;
std::ofstream measurements;
size_t fixture_buffers=0,fixture_buffer_bound=0;
struct File final:Reader {
    // Tests own the fixture files. Windows denies competing write opens while
    // backing is live; POSIX test runs require exclusive ownership by the test.
    std::FILE* handle=nullptr;uint32_t length=0;
    mutable uint64_t calls=0,bytes=0,fail=0;
    mutable unsigned maximum=0;
    uint32_t begin=0,end=UINT32_MAX;
    explicit File(const std::filesystem::path& path) {
#ifdef _WIN32
        handle=_fsopen(path.string().c_str(),"rb",_SH_DENYWR);
#else
        handle=std::fopen(path.string().c_str(),"rb");
#endif
        if(handle) {std::setvbuf(handle,nullptr,_IONBF,0);std::fseek(handle,0,SEEK_END);length=uint32_t(std::ftell(handle));}
    }
    ~File(){if(handle)std::fclose(handle);}
    uint32_t size() const override {return length;}
    const void* identity() const override {return this;}
    unsigned read(uint32_t at,void* p,unsigned n) const override {
        ++calls;maximum=std::max(maximum,n);bytes+=n;
        if(!handle || n>512 || at<begin || at>end || n>end-at || at>length || n>length-at)return 0;
        if(std::fseek(handle,at,SEEK_SET))return 0;
        return unsigned(std::fread(p,1,fail==calls && n?n-1:n,handle));
    }
    void reset() const {calls=bytes=fail=maximum=0;}
};
struct Output final:Writer {
    std::FILE* handle;uint64_t calls=0,fail=0;uint32_t capacity=1048576;
    const void* alias=nullptr;bool resize_fail=false;
    explicit Output(const std::filesystem::path& path):handle(std::fopen(path.string().c_str(),"w+b")) {
        if(handle)std::setvbuf(handle,nullptr,_IONBF,0);
    }
    ~Output(){if(handle)std::fclose(handle);}
    const void* identity() const override {return alias?alias:this;}
    unsigned write(uint32_t at,const void* p,unsigned n) override {
        ++calls;if(!handle || n>512 || at>capacity || n>capacity-at || std::fseek(handle,at,SEEK_SET))return 0;
        return unsigned(std::fwrite(p,1,fail==calls && n?n-1:n,handle));
    }
    bool resize(uint32_t n) override {
        if(!handle || resize_fail || n>capacity)return false;
#ifdef _WIN32
        return !_chsize_s(_fileno(handle),n);
#else
        return !ftruncate(fileno(handle),n);
#endif
    }
    bool close() {
        if(!handle)return false;
        const bool flushed=std::fflush(handle)==0;const bool closed=std::fclose(handle)==0;handle=nullptr;return flushed&&closed;
    }
};
std::vector<fixture::Section> Sections(unsigned codec=3,unsigned optional=60000,unsigned patterns=2,
                                       bool root_present=true,bool fine_present=true) {
    std::vector<uint8_t> tune,order,inst,payload;
    fixture::Tlv(tune,1,4,std::vector<uint8_t>(16,1));fixture::Tlv(tune,2,5,{'T','u','n','e'});
    fixture::Scalar(tune,3,4,12500);fixture::Scalar(tune,4,1,8);
    fixture::Scalar(tune,5,4,8000);fixture::Scalar(tune,6,4,10000);
    fixture::Tlv(tune,500,97,{1,0,255},false); // Unknown encoding/tag survives.
    for(auto id:{17,9001,17})fixture::Put(order,id,4);
    std::vector<uint8_t> ord;fixture::Tlv(ord,1,6,order);
    fixture::Scalar(inst,1,2,1);std::vector<uint8_t> path{0};
    for(auto c:std::string("Samples/missing.wav"))path.push_back(uint8_t(c));
    fixture::Tlv(inst,2,6,path,false);
    if(root_present)fixture::Scalar(inst,3,2,0,false); // Explicit unsupported, never defaulted.
    if(fine_present)fixture::Scalar(inst,4,2,199,false);
    fixture::Tlv(inst,600,6,{9,8,7},false);
    fixture::Pattern p;p.Clear(64);fixture::Cell cell;cell.fx={17,codec>=3?uint16_t(65535):uint16_t(255),42};cell.values={0,255,0};p.Put(1,cell);
    cell.note=60;cell.instrument=7001;p.Put(2,cell);
    cell.instrument=7999;p.Put(3,cell); // Structurally valid unresolved reference below counter.
    CHECK(fixture::Encode(p,codec,payload));
    std::vector<fixture::Section> s{{"TUNE",0,tune},{"ORDR",0,ord},{"INST",7001,inst}};
    for(unsigned i=0;i<patterns;++i)s.push_back({"PATT",i==0?17u:i==1?9001u:100u+i,payload});
    s.push_back({"XTRA",5,std::vector<uint8_t>(optional,0x5a),0});
    return s;
}
std::shared_ptr<File> Fixture(const std::vector<fixture::Section>& sections,const char* name="source.btt") {
    const auto bytes=fixture::Envelope(sections);
    size_t section_buffers=sections.capacity()*sizeof(fixture::Section);
    for(const auto& s:sections)section_buffers+=s.bytes.capacity();
    fixture_buffers=section_buffers+bytes.capacity();
    // Conservative bound for overlap of old/new envelope vector allocations.
    fixture_buffer_bound=section_buffers+2*bytes.capacity();
    CHECK(fixture::Write((root/name).string().c_str(),bytes));
    return std::make_shared<File>(root/name);
}
unsigned Tag(const std::vector<uint8_t>& b,unsigned tag) {
    for(unsigned at=0;at+8<=b.size();at+=8+fixture::Get(b.data()+at+4,4))if(fixture::Get(b.data()+at,2)==tag)return at;
    return unsigned(b.size());
}
void PatchTag(std::vector<uint8_t>& b,unsigned tag,uint32_t value,unsigned width) {fixture::Patch(b,Tag(b,tag)+8,value,width);}
void Recrc(std::vector<uint8_t>& b) {
    auto c=fixture::CrcStep(0xffffffffu,b.data(),20);c=fixture::CrcStep(c,b.data()+24,unsigned(b.size()-24));fixture::Patch(b,20,c^0xffffffffu,4);
}
bool Same(const Reader& a,const Reader& b) {
    if(a.size()!=b.size())return false;uint8_t x[512],y[512];
    for(uint32_t at=0;at<a.size();) {auto n=std::min(512u,a.size()-at);if(a.read(at,x,n)!=n || b.read(at,y,n)!=n || std::memcmp(x,y,n))return false;at+=n;}return true;
}
struct FakeStore final:Store {
    Output& output;std::filesystem::path path;
    std::shared_ptr<const Reader> previous,current,candidate;
    bool fail_finish=false,fail_reopen=false,fail_promote=false,corrupt=false,wrong_valid=false;
    uint64_t fail_candidate_read=0;
    explicit FakeStore(Output& o,std::filesystem::path p,std::shared_ptr<const Reader> old):output(o),path(std::move(p)),previous(old),current(old){}
    bool finish() override {
        const bool ok=output.close();if(fail_finish)return false;
        if(corrupt) {auto* f=std::fopen(path.string().c_str(),"r+b");std::fseek(f,-1,SEEK_END);std::fputc(0,f);std::fclose(f);}
        if(wrong_valid) {auto s=Sections();PatchTag(s[0].bytes,3,18000,4);fixture::Write(path.string().c_str(),fixture::Envelope(s));}
        return ok;
    }
    ReaderOwner reopen() override {
        if(fail_reopen)return {};
        auto file=std::make_shared<File>(path);file->fail=fail_candidate_read;candidate=file;return candidate;
    }
    bool promote() override {if(fail_promote)return false;current=candidate;return true;}
};

TEST_CASE(foundation_compact_pattern_storage_and_defaults) {
    Pattern p;
    auto empty=[](const Cell& c) {
        CHECK_EQ(c.note,255);CHECK_EQ(c.timing,0);CHECK_EQ(c.instrument,0u);
        CHECK_EQ(c.fx,(std::array<uint16_t,3>{}));CHECK_EQ(c.values,(std::array<uint8_t,3>{}));
    };
    CHECK_EQ(p.rows,0);CHECK_EQ(p.channels,0);CHECK(!p.ready);
    for(unsigned i=0;i<512;++i)empty(p.At(i));
    auto value=[](unsigned i) {
        Cell c;c.note=uint8_t(i);c.timing=uint8_t(i>>8);c.instrument=UINT32_MAX-i;
        c.fx={uint16_t(i),uint16_t(65535-i),uint16_t(1024+i)};
        c.values={uint8_t(i),0,uint8_t(255-i)};return c;
    };
    for(unsigned i=0;i<512;++i)p.Put(i,value(i));
    for(unsigned i=0;i<512;++i) {
        const auto actual=p.At(i),expected=value(i);
        CHECK_EQ(actual.note,expected.note);CHECK_EQ(actual.timing,expected.timing);
        CHECK_EQ(actual.instrument,expected.instrument);CHECK_EQ(actual.fx,expected.fx);CHECK_EQ(actual.values,expected.values);
    }
    p.ready=true;p.Clear(32,8);
    CHECK_EQ(p.rows,32);CHECK_EQ(p.channels,8);CHECK(!p.ready);
    for(unsigned i=0;i<512;++i)empty(p.At(i));
}

TEST_CASE(foundation_roundtrip_edit_defaults_and_owned_backing) {
    for(unsigned codec=1;codec<=4;++codec) {
        auto sections=Sections(codec);auto source=Fixture(sections);Document d;Workspace w;
        std::filesystem::copy_file(root/"source.btt",root/("reference-codec-"+std::to_string(codec)+".btt"),std::filesystem::copy_options::overwrite_existing);
        CHECK_EQ(Validate(source,d,w),Result::Ok);CHECK(d.accepted);CHECK(d.status.unsupported_values);
        CHECK(d.status.resources_unchecked);CHECK(d.status.unresolved_instruments);CHECK(!d.status.unresolved_patterns);
        source.reset();CHECK(d.backing.use_count()==1); // Document owns backing, not naked offsets.
        {
            Output out(root/"no-edit.btt");const auto before=allocations;
            CHECK_EQ(Rewrite(d,out,w),Result::Ok);CHECK_EQ(allocations,before);CHECK(out.close());
            File copy(root/"no-edit.btt");CHECK(Same(*d.backing,copy));
        }
        {
            Output out(root/"edit.btt");CHECK_EQ(Rewrite(d,out,w,15000),Result::Ok);CHECK(out.close());
            Document reopened;CHECK_EQ(Validate(std::make_shared<File>(root/"edit.btt"),reopened,w),Result::Ok);CHECK_EQ(reopened.bpm,15000u);
            PatchTag(sections[0].bytes,3,15000,4);auto expected=Fixture(sections,"expected.btt");CHECK(Same(*expected,*reopened.backing));
            Pattern p;p.Put(511,{60,1,7001,{1,2,3},{4,5,6}});p.ready=true;
            CHECK_EQ(Materialize(reopened,9001,p,w),Result::Ok);CHECK(p.ready);CHECK_EQ(p.At(1).note,255);
            CHECK_EQ(p.At(1).fx[1],codec>=3?65535:255);CHECK_EQ(p.At(1).values[0],0);CHECK_EQ(p.At(1).fx[2],42);
            CHECK_EQ(p.At(511).note,255);CHECK_EQ(p.At(511).timing,0);CHECK_EQ(p.At(511).instrument,0u);
            CHECK_EQ(p.At(511).fx,(std::array<uint16_t,3>{}));CHECK_EQ(p.At(511).values,(std::array<uint8_t,3>{}));
        }
    }
    for(bool root_present:{false,true})for(bool fine_present:{false,true}) {
        auto sections=Sections(3,3,2,root_present,fine_present);auto source=Fixture(sections);Document d;Workspace w;
        CHECK_EQ(Validate(source,d,w),Result::Ok);Output out(root/"defaults.btt");CHECK_EQ(Rewrite(d,out,w),Result::Ok);CHECK(out.close());
        if(!root_present)fixture::Scalar(sections[2].bytes,3,2,60,false);
        if(!fine_present)fixture::Scalar(sections[2].bytes,4,2,0,false);
        auto expected=Fixture(sections,"defaults-expected.btt");auto actual=std::make_shared<File>(root/"defaults.btt");CHECK(Same(*expected,*actual));
        Document reopened;CHECK_EQ(Validate(actual,reopened,w),Result::Ok);
    }
    // Explicit effective defaults also roundtrip byte-for-byte, independently
    // of the unsupported-value fixtures above.
    auto s=Sections();PatchTag(s[2].bytes,3,60,2);PatchTag(s[2].bytes,4,0,2);
    auto source=Fixture(s,"explicit-defaults.btt");Document d;Workspace w;
    CHECK_EQ(Validate(source,d,w),Result::Ok);Output out(root/"explicit-defaults-copy.btt");
    CHECK_EQ(Rewrite(d,out,w),Result::Ok);CHECK(out.close());File copy(root/"explicit-defaults-copy.btt");CHECK(Same(*source,copy));
}

TEST_CASE(foundation_bounded_growth_and_direct_access) {
    std::ofstream fixture_memory(root/"fixture-memory.csv");
    fixture_memory<<"patterns,optional_bytes,fixture_cpp_allocations,live_fixture_buffer_capacities,fixture_buffer_peak_upper_bound\n";
    for(unsigned patterns:{2u,32u})for(unsigned optional:{8u,65536u}) {
        const auto fixture_before=allocations;
        auto source=Fixture(Sections(3,optional,patterns));
        const auto fixture_allocs=allocations-fixture_before;
        fixture_memory<<patterns<<','<<optional<<','<<fixture_allocs<<','<<fixture_buffers<<','<<fixture_buffer_bound<<'\n';
        Document d;Workspace w;Pattern p;
        const auto before=allocations;source->reset();w.logical_fields=0;
        CHECK_EQ(Validate(source,d,w),Result::Ok);CHECK_EQ(allocations,before);
        measurements<<"validate,"<<source->size()<<','<<patterns<<','<<optional<<','<<source->calls<<','<<w.logical_fields<<','<<source->maximum<<','<<allocations-before<<'\n';
        const auto* e=Find(d,"PATT",9001);CHECK(e);source->begin=e->offset;source->end=e->offset+e->length;
        source->reset();w.logical_fields=0;
        CHECK_EQ(Materialize(d,9001,p,w),Result::Ok);CHECK_EQ(allocations,before);CHECK(p.ready);
        CHECK(source->calls<50); // 512 cells, not 512 backing reads.
        measurements<<"materialize,"<<source->size()<<','<<patterns<<','<<optional<<','<<source->calls<<','<<w.logical_fields<<','<<source->maximum<<','<<allocations-before<<'\n';
        source->begin=0;source->end=source->size();source->reset();w.logical_fields=0;
        Output out(root/"large-copy.btt");const auto rewrite_before=allocations;
        CHECK_EQ(Rewrite(d,out,w),Result::Ok);CHECK_EQ(allocations,rewrite_before);CHECK(out.close());
        measurements<<"rewrite,"<<source->size()<<','<<patterns<<','<<optional<<','<<source->calls<<','<<w.logical_fields<<','<<source->maximum<<','<<allocations-rewrite_before<<'\n';
        File copied(root/"large-copy.btt");CHECK(Same(*source,copied));CHECK(source->maximum<=512);
        const auto name="growth-"+std::to_string(patterns)+"-"+std::to_string(optional)+".btt";
        std::filesystem::copy_file(root/"source.btt",root/name,std::filesystem::copy_options::overwrite_existing);
    }
}

TEST_CASE(foundation_schema_rejections_and_unresolved_rules) {
    auto reject=[](std::vector<fixture::Section> s,Result expected=Result::Invalid) {
        auto file=Fixture(s);Document candidate;Workspace w;
        CHECK_EQ(Validate(file,candidate,w),expected);CHECK(!candidate.accepted);
    };
    for(unsigned kind=0;kind<30;++kind) {
        auto s=Sections();auto expected=Result::Invalid;
        auto& tune=s[0].bytes;auto& order=s[1].bytes;auto& inst=s[2].bytes;auto& patt=s[3].bytes;
        switch(kind) {
        case 0:s[4].id=s[3].id;break;
        case 1:fixture::Scalar(tune,3,4,13000);break;
        case 2:fixture::Tlv(tune,500,6,{},false);break;
        case 3:fixture::Patch(patt,4,UINT32_MAX,4);break;
        case 4:patt.pop_back();break;
        case 5:PatchTag(tune,3,0,4);break;
        case 6:PatchTag(tune,5,7001,4);break;
        case 7:PatchTag(tune,6,9001,4);break;
        case 8:fixture::Patch(order,8,0,4);break;
        case 9:fixture::Patch(order,8,10000,4);break;
        case 10:fixture::Patch(patt,Tag(patt,2)+8+2,8000,4);break;
        case 11:patt[Tag(patt,2)+8]=0;break;
        case 12:patt[Tag(patt,2)+8+1]=2;break;
        case 13:patt[Tag(patt,2)+8+8]=1;break; // ID zero, operand nonzero.
        case 14:PatchTag(patt,3,99,2);expected=Result::Unsupported;break;
        case 15:s.push_back({"NEED",0,{1},1});expected=Result::Unsupported;break;
        case 16:fixture::Tlv(tune,700,6,{1},true);expected=Result::Unsupported;break;
        case 17:tune[3]=3;expected=Result::Unsupported;break;
        case 18:s.back().flags=2;expected=Result::Unsupported;break;
        case 19:tune[2]=99;expected=Result::Unsupported;break;
        case 20:tune[2]=6;break;
        case 21:tune[3]=0;break;
        case 22:for(unsigned i=8;i<24;++i)tune[i]=0;break;
        case 23:tune[Tag(tune,2)+8]=0xff;break;
        case 24:fixture::Patch(inst,Tag(inst,2)+4,0,4);break;
        case 25:inst[Tag(inst,2)+9]=0;break;
        case 26:s[3].id=0;break;
        case 27:s[0].id=1;break;
        case 28:s.erase(s.begin()+1);break;
        case 29:PatchTag(patt,1,65,2);expected=Result::Capacity;break;
        }
        reject(std::move(s),expected);
    }
    for(unsigned kind=0;kind<9;++kind) {
        auto b=fixture::Envelope(Sections());auto expected=Result::Invalid;
        switch(kind) {
        case 0:b.pop_back();break;
        case 1:fixture::Patch(b,40,32,4);Recrc(b);break;
        case 2:b.back()^=1;Recrc(b);break; // Section CRC independently checked.
        case 3:b[20]^=1;break;
        case 4:fixture::Patch(b,24,1,4);Recrc(b);expected=Result::Unsupported;break;
        case 5:fixture::Patch(b,4,2,2);Recrc(b);expected=Result::Unsupported;break;
        case 6:fixture::Patch(b,28,1,4);Recrc(b);break;
        case 7:fixture::Patch(b,44,UINT32_MAX,4);Recrc(b);break;
        case 8:fixture::Patch(b,14,23,2);Recrc(b);break;
        }
        CHECK(fixture::Write((root/"bad-envelope.btt").string().c_str(),b));auto f=std::make_shared<File>(root/"bad-envelope.btt");Document d;Workspace w;
        CHECK_EQ(Validate(f,d,w),expected);CHECK(!d.accepted);
    }
    auto s=Sections();fixture::Patch(s[1].bytes,8,9999,4);PatchTag(s[0].bytes,3,1,4);
    s[2].bytes[Tag(s[2].bytes,3)+2]=99; // Present uninterpretable optional tuning is retained.
    auto f=Fixture(s);Document d;Workspace w;CHECK_EQ(Validate(f,d,w),Result::Ok);CHECK(d.status.unresolved_patterns);CHECK(d.status.unsupported_values);
    Output out(root/"unsupported-optional.btt");CHECK_EQ(Rewrite(d,out,w),Result::Ok);CHECK(out.close());File copy(root/"unsupported-optional.btt");CHECK(Same(*f,copy));
    // Later minor accepted and retained, provided required schema is understood.
    f.reset();d={};auto b=fixture::Envelope(Sections());fixture::Patch(b,6,7,2);Recrc(b);
    CHECK(fixture::Write((root/"later.btt").string().c_str(),b));auto later=std::make_shared<File>(root/"later.btt");CHECK_EQ(Validate(later,d,w),Result::Ok);
    Output later_out(root/"later-copy.btt");CHECK_EQ(Rewrite(d,later_out,w),Result::Ok);CHECK(later_out.close());File later_copy(root/"later-copy.btt");CHECK(Same(*later,later_copy));
}

TEST_CASE(foundation_sparse_structural_validation) {
    for(unsigned codec:{2u,4u})for(unsigned kind=0;kind<5;++kind) {
        auto s=Sections(codec);auto& p=s[3].bytes;const auto at=Tag(p,2)+8;const auto stride=fixture::Stride(codec);
        switch(kind) {
        case 0:fixture::Patch(p,at,513,2);break;
        case 1:fixture::Patch(p,at+2,512,2);break;
        case 2:fixture::Patch(p,at+2+stride+2,1,2);break;
        case 3:fixture::Patch(p,at+2+stride+2,0,2);break;
        case 4:for(unsigned j=0;j<stride;++j)p[at+4+j]=j==0?255:0;break;
        }
        auto f=Fixture(s);Document d;Workspace w;CHECK_EQ(Validate(f,d,w),Result::Invalid);CHECK(!d.accepted);
    }
}

TEST_CASE(foundation_schema_capacity_boundaries) {
    auto reject=[](const std::vector<fixture::Section>& sections) {
        auto source=Fixture(sections);Document d;Workspace w;CHECK_EQ(Validate(source,d,w),Result::Capacity);CHECK(!d.accepted);
    };
    reject(Sections(3,8,33));
    {
        auto s=Sections(3,65537);reject(s);
    }
    {
        auto s=Sections(3,8);for(unsigned tag=1000;tag<1251;++tag)fixture::Tlv(s[0].bytes,tag,6,{},false);
        reject(s); // 7 initial + 251 = 258 TLVs.
    }
    {
        auto s=Sections(3,8,2,false,false);
        // Exactly 256 instrument fields fit validation; inserting the two
        // required-on-save defaults exceeds the provisional TLV budget.
        for(unsigned tag=1000;tag<1253;++tag)fixture::Tlv(s[2].bytes,tag,6,{},false);
        auto source=Fixture(s);Document d;Workspace w;CHECK_EQ(Validate(source,d,w),Result::Ok);
        Output out(root/"defaults-capacity.btt");CHECK_EQ(Rewrite(d,out,w),Result::Capacity);
    }
    {
        auto s=Sections(3,8);for(unsigned i=0;i<252;++i)s.push_back({"MORE",i,{},0});reject(s);
    }
    {
        auto s=Sections(3,8);for(unsigned i=0;i<16;++i)s.push_back({"MORE",i,std::vector<uint8_t>(65536,0x33),0});reject(s);
    }
}

TEST_CASE(foundation_large_optional_tlv_and_raw_resource_preservation) {
    auto s=Sections(3,8);fixture::Tlv(s[0].bytes,1000,99,std::vector<uint8_t>(60000,0x61),false);
    // Valid UTF-8 saved resource, unsupported base and invalid runtime traversal.
    auto& inst=s[2].bytes;auto at=Tag(inst,2)+8;inst[at]=99;inst[at+1]='.';inst[at+2]='.';inst[at+3]='/';
    auto source=Fixture(s,"large-optional-tlv.btt");Document d;Workspace w;
    auto before=allocations;CHECK_EQ(Validate(source,d,w),Result::Ok);CHECK_EQ(allocations,before);
    Output out(root/"large-optional-tlv-copy.btt");before=allocations;
    CHECK_EQ(Rewrite(d,out,w,13500),Result::Ok);CHECK_EQ(allocations,before);CHECK(out.close());
    PatchTag(s[0].bytes,3,13500,4);auto expected=Fixture(s,"large-optional-tlv-expected.btt");
    auto copy=std::make_shared<File>(root/"large-optional-tlv-copy.btt");CHECK(Same(*expected,*copy));Document reopened;
    CHECK_EQ(Validate(copy,reopened,w),Result::Ok);
}

TEST_CASE(foundation_changed_backing_blocks_save) {
    // Deliberately violate Reader immutability to exercise the defensive saved
    // fingerprint check, even when the substituted file has valid new CRCs.
    struct Changed final:Reader {
        std::shared_ptr<const Reader> original,replacement;bool changed=false;
        uint32_t size() const override {return (changed?replacement:original)->size();}
        const void* identity() const override {return this;}
        unsigned read(uint32_t at,void* p,unsigned n) const override {return (changed?replacement:original)->read(at,p,n);}
    };
    auto source=std::make_shared<Changed>();auto sections=Sections();
    source->original=Fixture(sections,"stable.btt");PatchTag(sections[0].bytes,3,19000,4);
    source->replacement=Fixture(sections,"substituted.btt");Document d;Workspace w;
    CHECK_EQ(Validate(source,d,w),Result::Ok);source->changed=true;
    Output out(root/"changed-backing.btt");CHECK_EQ(Rewrite(d,out,w),Result::Backing);CHECK_EQ(out.calls,0u);
    CHECK_EQ(d.bpm,12500u);source->changed=false;CHECK_EQ(Rewrite(d,out,w),Result::Ok);
}

TEST_CASE(foundation_capacity_and_every_short_read_write) {
    auto source=Fixture(Sections(3,8));Document d;Workspace w;
    CHECK_EQ(Validate(source,d,w),Result::Ok);const auto validation_calls=source->calls;
    for(unsigned kind=0;kind<5;++kind) {
        Limits limits;if(kind==0)limits.entries=5;if(kind==1)limits.patterns=1;if(kind==2)limits.instruments=0;if(kind==3)limits.orders=2;if(kind==4)limits.cells=256;
        Document candidate;CHECK_EQ(Validate(source,candidate,w,limits),Result::Capacity);CHECK(!candidate.accepted);
    }
    for(uint64_t call=1;call<=validation_calls;++call) {
        source->reset();source->fail=call;Document candidate;CHECK_EQ(Validate(source,candidate,w),Result::Io);CHECK(!candidate.accepted);
    }
    source->reset();Pattern p;CHECK_EQ(Materialize(d,9001,p,w),Result::Ok);const auto materialize_calls=source->calls;
    for(uint64_t call=1;call<=materialize_calls;++call) {
        source->reset();source->fail=call;p.ready=true;CHECK_EQ(Materialize(d,9001,p,w),Result::Io);CHECK(!p.ready);
    }
    source->reset();CHECK_EQ(Materialize(d,9001,p,w,256),Result::Capacity);CHECK(!p.ready);
    uint64_t rewrite_calls,write_calls;
    {Output out(root/"fault.btt");source->reset();CHECK_EQ(Rewrite(d,out,w,13000),Result::Ok);rewrite_calls=source->calls;write_calls=out.calls;}
    for(uint64_t call=1;call<=rewrite_calls;++call) {
        source->reset();source->fail=call;Output out(root/"fault.btt");CHECK_EQ(Rewrite(d,out,w,13000),Result::Io);
    }
    source->reset();
    for(uint64_t call=1;call<=write_calls;++call) {
        Output out(root/"fault.btt");out.fail=call;CHECK_EQ(Rewrite(d,out,w,13000),Result::Io);
    }
    {Output out(root/"fault.btt");out.resize_fail=true;CHECK_EQ(Rewrite(d,out,w),Result::Io);}
    {Output out(root/"fault.btt");out.capacity=64;CHECK_EQ(Rewrite(d,out,w),Result::Io);}
    {Output out(root/"fault.btt");out.alias=source->identity();CHECK_EQ(Rewrite(d,out,w),Result::Backing);CHECK_EQ(out.calls,0u);}
    measurements<<"# injected validation reads="<<validation_calls<<", materialization reads="<<materialize_calls
        <<", rewrite reads="<<rewrite_calls<<", rewrite writes="<<write_calls<<'\n';
}

TEST_CASE(foundation_fake_store_preserves_previous_on_failure) {
    auto source=Fixture(Sections());Document d;Workspace w;CHECK_EQ(Validate(source,d,w),Result::Ok);
    const auto validation_calls=source->calls;
    for(unsigned failure=0;failure<9;++failure) {
        Output out(root/"candidate.btt");FakeStore store(out,root/"candidate.btt",source);Document reopened;
        store.fail_finish=failure==0;store.fail_reopen=failure==1;store.fail_promote=failure==2;store.corrupt=failure==3;
        store.wrong_valid=failure==4;if(failure==5)out.fail=10;
        if(failure==6)store.fail_candidate_read=1;
        if(failure==7)store.fail_candidate_read=validation_calls+1; // Comparison read after successful candidate validation.
        const auto before=allocations;
        auto result=Save(d,out,store,reopened,w,14000);
        const auto save_allocs=allocations-before;
        if(failure<8) {CHECK(result!=Result::Ok);CHECK(store.current==source);CHECK(store.previous==source);}
        else {CHECK_EQ(result,Result::Ok);CHECK(store.current!=source);CHECK(store.previous==source);CHECK_EQ(reopened.bpm,14000u);}
        measurements<<"# fake Save failure stage="<<failure<<", backend-inclusive C++ allocations="<<save_allocs<<'\n';
        CHECK(d.accepted);CHECK_EQ(d.bpm,12500u);
        Document previous;CHECK_EQ(Validate(store.previous,previous,w),Result::Ok);CHECK_EQ(previous.bpm,12500u);
    }
}
}
int main(int argc,char** argv) {
    if(argc!=2 || !*argv[1]) {std::cerr<<"Usage: "<<argv[0]<<" <artifact-directory>\n";return 2;}
    root=argv[1];std::filesystem::create_directories(root);
    measurements.open(root/"measurements.csv");measurements<<"operation,file_bytes,patterns,optional_bytes,backing_read_calls,logical_field_accesses,max_read,cpp_allocations\n";
    const int result=test::RunAll();
    std::ofstream memory(root/"memory.txt");
    memory<<"Host ABI bytes; fixed capacities, independent of file size within provisional limits.\n"
        <<"Document="<<sizeof(Document)<<" (256 Entry index + shared backing owner + scalar state)\n"
        <<"Entry="<<sizeof(Entry)<<"; Workspace="<<sizeof(Workspace)<<" (512 block + 512 tag IDs + 8 spans + counter)\n"
        <<"Pattern="<<sizeof(Pattern)<<"; Cell="<<sizeof(Cell)<<"; File="<<sizeof(File)<<"\n"
        <<"One decoded pattern buffer="<<sizeof(Pattern)<<"; two decoded pattern buffers="<<2*sizeof(Pattern)<<"\n"
        <<"Pattern cell arrays=7680 logical bytes; naturally aligned SoA, no retained Cell padding.\n"
        <<"Peak live caller-owned validation/rewrite buffers and state="<<sizeof(Document)+sizeof(Workspace)<<"\n"
        <<"Peak live caller-owned materialization buffers and state="<<sizeof(Document)+sizeof(Workspace)+sizeof(Pattern)<<"\n"
        <<"Peak live Save caller state + comparison array="<<2*sizeof(Document)+sizeof(Workspace)+512<<"\n"
        <<"Validation/rewrite live explicit state = one Document + one Workspace; zero decoded patterns.\n"
        <<"Materialization adds one caller-owned Pattern; never a pattern bank or duplicate tune.\n"
        <<"Save adds one caller-owned candidate Document and CompareWriter's 512-byte stack buffer.\n"
        <<"Stack: no recursion/VLAs/alloca; local headers <=32 bytes, cursor scalars, one 20-byte Cell.\n"
        <<"Workspace/Document/Pattern are caller-owned (test cases place them on host stack).\n"
        <<"Stack call frames/compiler spills not measured as a high-water mark; FILE CRT internal state excluded.\n"
        <<"C++ new/new[] counted around Validate, Materialize, Rewrite: zero. shared_ptr control blocks/file opens outside counts.\n"
        <<"fixture-memory.csv records live fixture vector capacities and a conservative vector-buffer peak bound.\n"
        <<"Fixture figures exclude reporting/path/FILE/allocator overhead and generator scalar stack buffers.\n"
        <<"Fake Save backend-inclusive heap allocation counts reported separately in measurements.csv.\n"
        <<"Peak figures above cover named resident buffers/state, not compiler stack frames or total process RSS.\n"
        <<"Fixture generation uses vectors/full envelope and reports use streams outside operation counts.\n"
        <<"Growth matrix: 2/32 stored patterns x 8/65536 optional bytes, identical resident types.\n"
        <<"Fake-store tests cover software failure ordering only, not FAT or power-loss recovery.\n"
        <<"No Teensy admission, real SD timing/deadlines, playback, resource preparation or hardware claims.\n";
    return result;
}

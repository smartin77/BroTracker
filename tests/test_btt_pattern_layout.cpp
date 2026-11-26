#include "test_framework.h"
#include "btt_pattern_layout.h"
#include "bringup/instrument_definition.h"
#include "bringup/working_session.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <new>

// Count C++ heap allocations around only index/decode operations, not fixtures,
// stdio setup, report formatting or the test framework. C runtime I/O is excluded.
static std::size_t allocation_count=0;
void* operator new(std::size_t n) {
    if(auto* p=std::malloc(n?n:1)){++allocation_count;return p;}throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}

namespace {
using namespace btt_layout;
std::filesystem::path output;
Pattern Fixture(unsigned rows,unsigned density) {
    Pattern p;p.Clear(rows);
    for(unsigned i=0;i<rows*8;++i) {
        if(!density || (density==1 && i%32!=1))continue;
        Cell c;c.fx={17,200,255};c.values={0,static_cast<std::uint8_t>(i),255};
        if(density==2 || i!=1){c.note=60;c.instrument=7001;}
        p.Put(i,c);
    }
    return p;
}
std::vector<Section> Sections(const std::vector<std::uint8_t>& pattern) {
    std::vector<std::uint8_t> tune,order,instrument,first;
    Tlv(tune,1,4,std::vector<std::uint8_t>(16,1));Tlv(tune,2,5,{'M','e','a','s','u','r','e'});
    Scalar(tune,3,4,12500);Scalar(tune,4,1,8);Scalar(tune,5,4,7002);Scalar(tune,6,4,9002);
    for(auto id:{17,9001,17})Put(order,id,4);
    std::vector<std::uint8_t> order_section;Tlv(order_section,1,6,order);
    Scalar(instrument,1,2,1);std::vector<std::uint8_t> resource{0};
    const char* path="Samples/missing.wav";
    for(const char* at=path;*at;++at)resource.push_back(static_cast<std::uint8_t>(*at));
    Tlv(instrument,2,6,resource,false);Scalar(instrument,3,2,60,false);Scalar(instrument,4,2,0,false);
    Encode(Fixture(32,0),3,first);
    return {{"TUNE",0,tune},{"ORDR",0,order_section},{"INST",7001,instrument},
        {"PATT",17,first},{"XTRA",5,std::vector<std::uint8_t>(60000,0x5a),0},{"PATT",9001,pattern}};
}
std::string Save(const char* name,const std::vector<std::uint8_t>& payload) {
    const auto path=(output/name).string();CHECK(Write(path.c_str(),Envelope(Sections(payload))));return path;
}
bool Equal(const Pattern& a,const Pattern& b) {
    if(a.rows!=b.rows || a.channels!=b.channels)return false;
    for(unsigned i=0;i<a.rows*8;++i)if(!(a.At(i)==b.At(i)))return false;
    return true;
}
TEST_CASE(pattern_layout_matrix_and_measurements) {
    std::ofstream csv(output/"measurements.csv");
    csv<<"rows,density,codec,pattern_section_bytes,file_bytes,read_bytes,read_calls,max_read,cpp_allocations,mean_host_decode_us\n";
    volatile std::uint64_t checksum=0;
    for(unsigned rows:{32u,64u})for(unsigned density=0;density<3;++density)for(unsigned codec=1;codec<=4;++codec) {
        const auto original=Fixture(rows,density);std::vector<std::uint8_t> encoded;
        CHECK(Encode(original,codec,encoded));
        const auto count=density==0?0:density==1?rows*8/32:rows*8;
        CHECK_EQ(encoded.size(),28u+(Sparse(codec)?2+count*(Stride(codec)+2):rows*8*Stride(codec)));
        const auto name="pattern_"+std::to_string(rows)+"_"+std::to_string(density)+"_"+std::to_string(codec)+".btt";
        const auto path=Save(name.c_str(),encoded);
        Reader reader(path.c_str());Index index;Workspace workspace;Pattern decoded;
        const auto before_index=allocation_count;
        const bool indexed=ReadIndex(reader,index,workspace);
        CHECK_EQ(allocation_count,before_index);CHECK(indexed);CHECK(WholeCrc(reader,workspace));
        const auto* entry=Find(index,"PATT",9001);CHECK(entry!=nullptr);if(!entry)continue;
        // Forbid every access outside the selected section, including the earlier pattern.
        reader.Window(entry->offset,entry->length);
        const auto before=allocation_count;
        const bool ok=Decode(reader,*entry,workspace,decoded);
        const auto allocations=allocation_count-before;
        CHECK(ok);CHECK_EQ(allocations,0u);CHECK(Equal(original,decoded));CHECK(reader.max_read<=512);
        const auto bytes=reader.bytes,reads=reader.reads;const auto max_read=reader.max_read;
        const auto start=std::chrono::steady_clock::now();
        for(unsigned repeat=0;repeat<100;++repeat) {
            CHECK(Decode(reader,*entry,workspace,decoded));checksum+=decoded.fx[1][2]+decoded.rows;
        }
        const auto micros=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/100;
        csv<<rows<<','<<density<<','<<codec<<','<<encoded.size()<<','<<reader.size<<','<<bytes<<','<<reads<<','
           <<max_read<<','<<allocations<<','<<micros<<'\n';
    }
    CHECK(checksum>0);CHECK(csv.good());
    std::ofstream memory(output/"memory.txt");
    memory<<"All sizes in bytes; host ABI, not Teensy admission evidence.\n"
        <<"sizeof(Pattern)="<<sizeof(Pattern)<<"\ntwo Pattern buffers="<<2*sizeof(Pattern)
        <<"\nactive decoded cell payload 32x8="<<256*15<<"\nactive decoded cell payload 64x8="<<512*15
        <<"\nsizeof(Workspace)="<<sizeof(Workspace)<<" (512-byte CRC block + 17-byte record)"
        <<"\nsizeof(Cell)="<<sizeof(Cell)<<" (temporary ABI-padded cell)"
        <<"\nsizeof(Index)="<<sizeof(Index)<<"\nmax directory wire bytes="<<256*24
        <<"\nsizeof(Reader)="<<sizeof(Reader)<<" (excludes C runtime FILE state)"
        <<"\nmax resident uint32 order="<<1024*4<<"\nfixture directory wire bytes="<<6*24
        <<"\nsizeof(current InstrumentDefinition)="<<sizeof(BroTracker::InstrumentDefinition)
        <<"\nsizeof(current InstrumentDefinitions)="<<sizeof(BroTracker::InstrumentDefinitions)
        <<"\nsizeof(current ResourcePathPool)="<<sizeof(BroTracker::ResourcePathPool)
        <<"\nsizeof(current RealtimePattern)="<<sizeof(BroTracker::RealtimePattern)
        <<"\nsizeof(current WorkingSession)="<<sizeof(BroTracker::WorkingSession)
        <<"\nThese current runtime definitions are not a lossless BTT saved-data model."
        <<"\nDecoder scratch also uses scalar locals and one Cell; sizeof(Workspace) is not a total stack high-water measurement."
        <<"\nIndex/decode C++ heap allocation checks: zero; fixture generation uses vectors outside measured access.\n";
    CHECK(memory.good());
}
TEST_CASE(pattern_layout_direct_access_and_wide_unknown_effects) {
    auto p=Fixture(64,1);auto c=p.At(1);c.fx={0,1024,65535};c.values={0,0,255};p.Put(1,c);
    std::vector<std::uint8_t> payload;CHECK(!Encode(p,1,payload));CHECK(!Encode(p,2,payload));
    for(unsigned codec:{3u,4u}) {
        CHECK(Encode(p,codec,payload));const auto path=Save("wide_unknown.btt",payload);
        Reader r(path.c_str());Index index;Workspace w;Pattern decoded;
        CHECK(ReadIndex(r,index,w));
        const auto* order=Find(index,"ORDR",0);const auto* tune=Find(index,"TUNE",0);
        const auto* instrument=Find(index,"INST",7001);
        CHECK(order && tune && instrument);
        if(!order || !tune || !instrument)return;
        CHECK(CheckCrc(r,*tune,w));CHECK(CheckCrc(r,*instrument,w));
        CHECK(r.Read(order->offset+8,w.block.data(),12));
        CHECK_EQ(Get(w.block.data(),4),17u);CHECK_EQ(Get(w.block.data()+4,4),9001u);CHECK_EQ(Get(w.block.data()+8,4),17u);
        // Definitions and tune metadata can be read without decoding a pattern.
        for(unsigned id:{9001u,17u,9001u}) {
            r.Window(0,r.size);const auto* e=Find(index,"PATT",id);CHECK(e);if(!e)return;
            r.Window(e->offset,e->length);CHECK(Decode(r,*e,w,decoded));
            if(id==9001) {CHECK(Equal(decoded,p));CHECK_EQ(decoded.fx[1][1],1024u);CHECK_EQ(decoded.values[1][1],0u);CHECK_EQ(decoded.fx[1][0],0u);}
        }
        std::vector<std::uint8_t> rewritten;
        CHECK(Encode(decoded,codec,rewritten));CHECK_EQ(rewritten,payload);
    }
}
TEST_CASE(pattern_layout_rejects_malformed_and_capacity_failures) {
    for(unsigned codec=1;codec<=4;++codec) {
        std::vector<std::uint8_t> good;CHECK(Encode(Fixture(64,1),codec,good));
        auto attempt=[&](const std::vector<std::uint8_t>& bytes,unsigned capacity=512) {
            const auto path=Save("invalid_pattern.btt",bytes);Reader r(path.c_str());Index index;Workspace w;
            const auto sentinel=Fixture(32,2);auto out=sentinel;
            CHECK(ReadIndex(r,index,w));const auto* e=Find(index,"PATT",9001);CHECK(e);if(!e)return;
            CHECK(!Decode(r,*e,w,out,capacity));CHECK(Equal(out,sentinel));
        };
        attempt(good,256);
        auto bad=good;bad.pop_back();attempt(bad);
        bad=good;Patch(bad,8,65,2);attempt(bad); // row capacity
        bad=good;Patch(bad,static_cast<unsigned>(bad.size())-2,5,2);attempt(bad); // unknown codec
        bad=good;Patch(bad,14,UINT32_MAX,4);attempt(bad); // cells TLV length overflow
        bad=good;bad[18+(Sparse(codec)?4:0)]=0;attempt(bad); // invalid note
        bad=good;bad[18+(Sparse(codec)?4:0)+1]=2;attempt(bad); // invalid OFF timing
        bad=good;bad[18+(Sparse(codec)?4:0)+6]=0;
        if(Wide(codec))bad[18+(Sparse(codec)?4:0)+7]=0;
        bad[18+(Sparse(codec)?4:0)+6+(Wide(codec)?2:1)]=1;attempt(bad); // empty FX with nonzero value
        if(Sparse(codec)) {
            bad=good;Patch(bad,18,513,2);attempt(bad);
            bad=good;Patch(bad,20,512,2);attempt(bad);
            bad=good;Patch(bad,20+Stride(codec)+2,1,2);attempt(bad); // duplicate indices
        }
        const auto valid=Envelope(Sections(good));
        const auto path=(output/"invalid_envelope.btt").string();
        for(unsigned kind=0;kind<5;++kind) {
            auto broken=valid;
            if(kind==0)broken.pop_back();
            if(kind==1)Patch(broken,32+5*24+8,32,4); // overlapping selected section
            if(kind==2)Patch(broken,32+5*24+4,17,4); // duplicate pattern ID
            if(kind==3)broken.back()^=1; // payload CRC damage
            CHECK(Write(path.c_str(),broken));Reader r(path.c_str());Index index;Workspace w;
            if(kind<3)CHECK(!ReadIndex(r,index,w));
            if(kind==3) {CHECK(ReadIndex(r,index,w));CHECK(!WholeCrc(r,w));Pattern out;CHECK(!Decode(r,*Find(index,"PATT",9001),w,out));}
            if(kind==4)CHECK(!ReadIndex(r,index,w,5));
        }
    }
}
}
int main(int argc,char** argv) {
    output=argc>1?argv[1]:"build/btt-pattern-layout";
    std::filesystem::create_directories(output);
    return test::RunAll();
}

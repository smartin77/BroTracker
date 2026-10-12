#include "test_framework.h"
#include "core/audio/wav_buffer_loader.h"
#include <vector>
#include <algorithm>
using namespace BroTracker;
namespace {
using Bytes=std::vector<std::uint8_t>;
void Put32(Bytes& b,std::size_t i,std::uint32_t n) { for(unsigned k=0;k<4;++k) b[i+k]=n>>(8*k); }
void Chunk(Bytes& b,const char* id,const Bytes& payload) {
    const auto i=b.size(); b.insert(b.end(),id,id+4); b.resize(i+8); Put32(b,i+4,payload.size());
    b.insert(b.end(),payload.begin(),payload.end()); if(payload.size()%2) b.push_back(0);
}
Bytes Format() { return {1,0,1,0,0x44,0xac,0,0,0x88,0x58,1,0,2,0,16,0}; }
Bytes Wave(const Bytes& fmt=Format(),const Bytes& pcm={0,0,0xff,0x7f,0,0x80,0xff,0xff},bool extras=false) {
    Bytes b={'R','I','F','F',0,0,0,0,'W','A','V','E'};
    if(extras) Chunk(b,"JUNK",{1,2,3});
    Chunk(b,"fmt ",fmt); Chunk(b,"data",pcm); if(extras) Chunk(b,"LIST",{4}); Put32(b,4,b.size()-8); return b;
}
struct Reader : WavBufferReader {
    Bytes bytes; std::uint64_t at=0; bool size_fail=false,seek_fail=false;
    std::uint64_t short_at=UINT64_MAX;
    explicit Reader(Bytes b):bytes(std::move(b)) {}
    bool Size(std::uint64_t& n) noexcept override { n=bytes.size(); return !size_fail; }
    bool Seek(std::uint64_t n) noexcept override { at=n; return !seek_fail && n<=bytes.size(); }
    std::size_t Read(std::uint8_t* out,std::size_t n) noexcept override {
        n=std::min<std::size_t>(n,bytes.size()-at); if(at>=short_at && n) --n;
        std::copy_n(bytes.data()+at,n,out); at+=n; return n;
    }
};
void Reject(Bytes bytes,WavBufferStatus expected) {
    Reader reader(std::move(bytes)); std::int16_t out[4]={7,7,7,7}; Pcm16MonoView metadata{out,99,123};
    CHECK_EQ(LoadWavToBuffer(reader,out,4,metadata),expected);
    CHECK_EQ(metadata.frame_count,99); CHECK_EQ(metadata.sample_rate_hz,123); CHECK_EQ(metadata.data,out);
    for(auto x:out) CHECK_EQ(x,7);
}
}
TEST_CASE(WavBufferExactPcmMetadataCapacityAndExtraChunks) {
    for(bool extras:{false,true}) {
        Reader reader(Wave(Format(),{0,0,0xff,0x7f,0,0x80,0xff,0xff},extras));
        std::int16_t out[5]={9,9,9,9,9}; Pcm16MonoView metadata;
        CHECK_EQ(LoadWavToBuffer(reader,out,4,metadata),WavBufferStatus::Success);
        CHECK_EQ(out[0],0); CHECK_EQ(out[1],32767); CHECK_EQ(out[2],-32768); CHECK_EQ(out[3],-1); CHECK_EQ(out[4],9);
        CHECK_EQ(metadata.data,out); CHECK_EQ(metadata.frame_count,4); CHECK_EQ(metadata.sample_rate_hz,44100);
        Reader too_small(reader.bytes); metadata={out,99,123};
        CHECK_EQ(LoadWavToBuffer(too_small,out,3,metadata),WavBufferStatus::InsufficientCapacity); CHECK_EQ(metadata.frame_count,99);
    }
    Bytes reversed={'R','I','F','F',0,0,0,0,'W','A','V','E'};
    Chunk(reversed,"data",{1,0}); Chunk(reversed,"fmt ",Format()); Put32(reversed,4,reversed.size()-8);
    Reader order(reversed); std::int16_t one=0; Pcm16MonoView view;
    CHECK_EQ(LoadWavToBuffer(order,&one,1,view),WavBufferStatus::Success); CHECK_EQ(one,1);
    auto fmt=Format(); fmt.insert(fmt.end(),{0,0}); Reader extended(Wave(fmt)); std::int16_t out[4]; Pcm16MonoView metadata;
    CHECK_EQ(LoadWavToBuffer(extended,out,4,metadata),WavBufferStatus::Success);
}
TEST_CASE(WavBufferMalformedChunksAndUnsupportedFormats) {
    for(unsigned mode=0;mode<12;++mode) {
        auto bytes=Wave(); auto status=WavBufferStatus::InvalidChunk;
        if(mode==0) { bytes.pop_back(); status=WavBufferStatus::Truncated; }
        if(mode==1) Put32(bytes,16,UINT32_MAX);
        if(mode==2) { Chunk(bytes,"data",{0,0}); Put32(bytes,4,bytes.size()-8); status=WavBufferStatus::DuplicateChunk; }
        if(mode==3) { Chunk(bytes,"fmt ",Format()); Put32(bytes,4,bytes.size()-8); status=WavBufferStatus::DuplicateChunk; }
        if(mode==4) { bytes=Wave(Format(),{}); status=WavBufferStatus::InvalidData; }
        if(mode==5) { bytes=Wave(Format(),{0}); status=WavBufferStatus::InvalidData; }
        if(mode==6) { bytes[20]=3; status=WavBufferStatus::UnsupportedFormat; }
        if(mode==7) { bytes[22]=2; status=WavBufferStatus::UnsupportedFormat; }
        if(mode==8) { bytes[24]=0; status=WavBufferStatus::UnsupportedFormat; }
        if(mode==9) { bytes[34]=8; status=WavBufferStatus::UnsupportedFormat; }
        if(mode==10) { bytes[32]=4; status=WavBufferStatus::InvalidFormat; }
        if(mode==11) { bytes[28]=0; status=WavBufferStatus::InvalidFormat; }
        Reject(bytes,status);
    }
    Reject(Wave(Bytes(17)),WavBufferStatus::InvalidFormat);
    auto odd=Wave(); Chunk(odd,"JUNK",{1}); odd.pop_back(); Put32(odd,4,odd.size()-8); Reject(odd,WavBufferStatus::InvalidChunk);
    auto missing=Wave(); missing.resize(36); Put32(missing,4,28); Reject(missing,WavBufferStatus::MissingChunk);
    auto oversized=Wave(); Put32(oversized,4,UINT32_MAX); Reject(oversized,WavBufferStatus::Truncated);
    auto invalid_end=Wave(); Put32(invalid_end,4,0); Reject(invalid_end,WavBufferStatus::InvalidContainer);
    Reject(Bytes(11),WavBufferStatus::Truncated);
    auto extension=Format(); extension.insert(extension.end(),{1,0}); Reject(Wave(extension),WavBufferStatus::InvalidFormat);
    auto bad=Wave(); bad[0]='X'; Reject(bad,WavBufferStatus::InvalidContainer);
}
TEST_CASE(WavBufferIoFailureMetadataAndPartialCopyContract) {
    std::int16_t out[200]; std::fill_n(out,200,9); Pcm16MonoView metadata{out,99,123};
    Reader reader(Wave()); reader.short_at=0;
    CHECK_EQ(LoadWavToBuffer(reader,out,200,metadata),WavBufferStatus::IoError); CHECK_EQ(out[0],9);
    reader=Reader(Wave()); reader.size_fail=true;
    CHECK_EQ(LoadWavToBuffer(reader,out,200,metadata),WavBufferStatus::IoError);
    reader=Reader(Wave()); reader.seek_fail=true;
    CHECK_EQ(LoadWavToBuffer(reader,out,200,metadata),WavBufferStatus::IoError);
    reader=Reader(Wave(Format(),Bytes(300,0))); reader.short_at=44+256;
    CHECK_EQ(LoadWavToBuffer(reader,out,200,metadata),WavBufferStatus::IoError);
    CHECK_EQ(out[0],0); CHECK_EQ(out[127],0); CHECK_EQ(out[128],9);
    CHECK_EQ(metadata.frame_count,99); CHECK_EQ(metadata.sample_rate_hz,123);
    CHECK_EQ(LoadWavToBuffer(reader,nullptr,4,metadata),WavBufferStatus::InvalidDestination);
    CHECK_EQ(LoadWavToBuffer(reader,out,UINT64_MAX,metadata),WavBufferStatus::InvalidCapacity);
}

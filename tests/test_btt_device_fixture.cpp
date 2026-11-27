#include "test_framework.h"
#include "device_fixture.h"
#include "foundation.h"
#include "core/audio/wav_buffer_loader.h"
#include <algorithm>
#include <cstring>

namespace {
using namespace btt;
constexpr const char* path="BroTracker/.btt-prototype/device-diagnostic.wav";
struct MemoryReader:Reader,ReaderControl {
    const device::Fixture& data;unsigned refs=0;mutable unsigned max_read=0;
    explicit MemoryReader(const device::Fixture& d):data(d){}
    uint32_t size()const override{return data.size;}
    const void* identity()const override{return this;}
    unsigned read(uint32_t at,void* p,unsigned n)const override {
        if(at>data.size || n>data.size-at)return 0;max_read=std::max(max_read,n);std::memcpy(p,data.bytes+at,n);return n;
    }
    void retain()noexcept override{++refs;}void release()noexcept override{--refs;}
    const Reader* reader()const noexcept override{return this;}uint32_t count()const noexcept override{return refs;}
};
struct MemoryWriter:Writer {
    device::Fixture& data;
    explicit MemoryWriter(device::Fixture& d):data(d){data.size=0;}
    const void* identity()const override{return this;}
    unsigned write(uint32_t at,const void* p,unsigned n)override {
        if(at>sizeof(data.bytes) || n>sizeof(data.bytes)-at)return 0;
        std::memcpy(data.bytes+at,p,n);data.size=std::max<unsigned>(data.size,at+n);return n;
    }
    bool resize(uint32_t n)override {if(n>sizeof(data.bytes))return false;data.size=n;return true;}
};
TEST_CASE(device_fixture_roundtrip_edit_defaults_and_unknown_bytes) {
    for(bool absent:{false,true}) {
        device::Fixture source,expected,rewritten,again;CHECK(source.Build(path,absent?device::Tuning::Absent:device::Tuning::Explicit));
        CHECK(again.Build(path,absent?device::Tuning::Absent:device::Tuning::Explicit));
        CHECK_EQ(source.size,again.size);CHECK(!std::memcmp(source.bytes,again.bytes,source.size));
        MemoryReader reader(source);Document document;Workspace work;Pattern pattern;
        CHECK_EQ(Validate(ReaderOwner::Pin(reader),document,work),Result::Ok);CHECK(document.status.unsupported_values);
        for(unsigned id:{101u,303u}) {CHECK_EQ(Materialize(document,id,pattern,work),Result::Ok);
            CHECK_EQ(pattern.At(0).note,id==101?60:72);CHECK_EQ(pattern.At(0).instrument,11u);
            CHECK((pattern.At(0).fx==std::array<uint16_t,3>{17,1024,65535}));CHECK_EQ(pattern.At(0).values[0],0u);
            CHECK_EQ(pattern.At(0).values[2],255u);CHECK_EQ(pattern.At(2).note,255u);CHECK_EQ(pattern.At(2).instrument,0u);}
        const auto* order=Find(document,"ORDR",0);CHECK(order);CHECK_EQ(order->length,20u);
        CHECK_EQ(source.bytes[order->offset+8],101u);CHECK_EQ(source.bytes[order->offset+12],303u&255u);
        CHECK_EQ(source.bytes[order->offset+13],1u);CHECK_EQ(source.bytes[order->offset+16],101u);
        for(unsigned bpm:{12500u,15000u}) {
            MemoryWriter writer(rewritten);CHECK_EQ(Rewrite(document,writer,work,bpm==12500?0:bpm),Result::Ok);
            CHECK(expected.Build(path,absent?device::Tuning::Resolved:device::Tuning::Explicit,bpm));
            CHECK_EQ(rewritten.size,expected.size);CHECK(!std::memcmp(rewritten.bytes,expected.bytes,expected.size));
            if(!absent && bpm==12500){CHECK_EQ(rewritten.size,source.size);CHECK(!std::memcmp(rewritten.bytes,source.bytes,source.size));}
            MemoryReader reopened(rewritten);Document verified;CHECK_EQ(Validate(ReaderOwner::Pin(reopened),verified,work),Result::Ok);CHECK_EQ(verified.bpm,bpm);
            for(unsigned id:{11u,22u,33u}) {SavedInstrument value;CHECK_EQ(InspectInstrument(verified,id,value,work),Result::Ok);
                CHECK_EQ(value.root,id==33?0:id==22?72:60);CHECK_EQ(value.fine,id==33?199:absent?(id==11?25:0):(id==11?0:25));CHECK_EQ(value.present&3,3u);}
        }
        CHECK(reader.max_read<=512);
    }
}
TEST_CASE(device_native_wav_fixture_has_real_supported_geometry) {
    struct WavReader:BroTracker::WavBufferReader {
        uint64_t at=0;
        bool Size(uint64_t& n)noexcept override{n=device::kWavBytes;return true;}
        bool Seek(uint64_t offset)noexcept override{at=offset;return at<=device::kWavBytes;}
        size_t Read(uint8_t* p,size_t n)noexcept override{n=std::min<size_t>(n,device::kWavBytes-at);for(size_t i=0;i<n;++i)p[i]=device::WavByte(at++);return n;}
    } reader;
    BroTracker::WavMetadata metadata;
    CHECK_EQ(BroTracker::InspectWavMetadata(reader,metadata),BroTracker::WavBufferStatus::Success);
    CHECK_EQ(metadata.sample_rate,44100u);CHECK_EQ(metadata.channels,1u);CHECK_EQ(metadata.bits,16u);
    CHECK_EQ(metadata.frames,device::kWavFrames);CHECK_EQ(metadata.data_offset,44u);CHECK_EQ(metadata.file_bytes,device::kWavBytes);
}
}

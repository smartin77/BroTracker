#pragma once
// Deterministic, bounded native-C++ diagnostic fixtures. No filesystem or heap.
#include <cstdint>
#include <cstring>

namespace btt::device {
enum class Tuning { Explicit, Absent, Resolved };
struct Fixture {
    uint8_t bytes[2048]{};
    unsigned size=0;
    bool good=true;
    void Put(uint32_t value,unsigned width) noexcept {
        if(width>4 || size>sizeof(bytes)-width){good=false;return;}
        for(unsigned i=0;i<width;++i)bytes[size++]=uint8_t(value>>(8*i));
    }
    void Raw(const void* data,unsigned length) noexcept {
        if(size>sizeof(bytes) || length>sizeof(bytes)-size){good=false;return;}
        std::memcpy(bytes+size,data,length);size+=length;
    }
    void At(unsigned offset,uint32_t value,unsigned width) noexcept {
        if(offset>size || width>size-offset){good=false;return;}
        for(unsigned i=0;i<width;++i)bytes[offset+i]=uint8_t(value>>(8*i));
    }
    void Tag(unsigned id,unsigned encoding,unsigned length,bool required=true) noexcept {
        Put(id,2);Put(encoding,1);Put(required?1:0,1);Put(length,4);
    }
    void Scalar(unsigned id,unsigned encoding,uint32_t value,unsigned width,bool required=true) noexcept {
        Tag(id,encoding,width,required);Put(value,width);
    }
    static uint32_t Crc(const uint8_t* data,unsigned count,unsigned skip=UINT32_MAX) noexcept {
        uint32_t c=UINT32_MAX;
        for(unsigned i=0;i<count;++i){if(i>=skip && i-skip<4)continue;c^=data[i];for(unsigned j=0;j<8;++j)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}
        return c^UINT32_MAX;
    }
    void Unknown() noexcept {Tag(777,99,4,false);Put(0x00ff2a00,4);}
    bool Build(const char* wav,Tuning tuning=Tuning::Explicit,uint32_t bpm=12500) noexcept {
        size=0;good=true;
        if(!wav || std::strlen(wav)>255){good=false;return false;}
        // Eight sections; fixed16 cells, two distinct stable IDs and repeated order.
        for(unsigned i=0;i<32+8*24;++i)Put(0,1);
        const char* types[]={"TUNE","ORDR","INST","INST","INST","PATT","PATT","XTRA"};
        const uint32_t ids[]={0,0,11,22,33,101,303,7};
        for(unsigned section=0;section<8;++section) {
            const auto start=size;
            if(section==0) {
                Tag(1,4,16);for(unsigned i=0;i<16;++i)Put(i+1,1);
                Tag(2,5,8);Raw("BTT DIAG",8);Scalar(3,3,bpm,4);Scalar(4,1,2,1);
                Scalar(5,3,100,4);Scalar(6,3,1000,4);Unknown();
            } else if(section==1) {Tag(1,6,12);Put(101,4);Put(303,4);Put(101,4);}
            else if(section<5) {
                const unsigned instrument=section-2;
                Scalar(1,2,1,2);Tag(2,6,unsigned(std::strlen(wav))+1,false);Put(0,1);Raw(wav,std::strlen(wav));
                const bool partial=tuning!=Tuning::Explicit;
                if(instrument!=0 || !partial)Scalar(3,2,instrument==2?0:instrument==1?72:60,2,false);
                if(instrument!=1 || !partial)Scalar(4,2,instrument==2?199:partial?25:instrument==1?25:0,2,false);
                Unknown(); // Missing defaults must be appended after original opaque TLVs.
                if(tuning==Tuning::Resolved && instrument==0)Scalar(3,2,60,2,false);
                if(tuning==Tuning::Resolved && instrument==1)Scalar(4,2,0,2,false);
            } else if(section<7) {
                Scalar(1,2,2,2);Tag(2,6,4*15);
                for(unsigned i=0;i<4;++i){Put(i==2?255:60+i+(section==6?12:0),1);Put(0,1);Put(i==2?0:i%2?22:11,4);
                    Put(17,2);Put(0,1);Put(1024,2);Put(1,1);Put(65535,2);Put(255,1);}
                Scalar(3,2,3,2);
            } else {Put(0x13579bdf,4);Put(0x00ff4200,4);Put(0x12345678,4);}
            const auto directory=32+section*24;
            std::memcpy(bytes+directory,types[section],4);At(directory+4,ids[section],4);At(directory+8,start,4);
            At(directory+12,size-start,4);At(directory+16,Crc(bytes+start,size-start),4);At(directory+20,section==7?0:1,4);
        }
        std::memcpy(bytes,"BTT1",4);At(4,1,2);At(8,32,4);At(12,8,2);At(14,24,2);At(16,size,4);At(20,Crc(bytes,size,20),4);
        return good;
    }
};
constexpr unsigned kWavFrames=3072,kWavBytes=44+kWavFrames*2;
inline uint8_t WavByte(unsigned offset) noexcept {
    const uint8_t header[44]={'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,
        0x44,0xac,0,0,0x88,0x58,1,0,2,0,16,0,'d','a','t','a',0,0,0,0};
    if(offset>=4 && offset<8)return uint8_t((kWavBytes-8)>>(8*(offset-4)));
    if(offset>=40 && offset<44)return uint8_t((kWavFrames*2)>>(8*(offset-40)));
    if(offset<44)return header[offset];
    const auto word=uint16_t(int(((offset-44)/2*137)%60001)-30000);
    return uint8_t(word>>(8*((offset-44)%2)));
}
}

#pragma once

// Host-only format experiment. Not linked into the application or firmware.
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace btt_layout {
constexpr unsigned kCells = 64 * 8, kEntries = 256;
struct Cell {
    std::uint8_t note = 255, timing = 0;
    std::uint32_t instrument = 0;
    std::array<std::uint16_t, 3> fx{};
    std::array<std::uint8_t, 3> values{};
    bool operator==(const Cell& b) const {
        return note == b.note && timing == b.timing && instrument == b.instrument &&
            fx == b.fx && values == b.values;
    }
};
// SoA avoids depending on Cell's ABI padding. Always reserves the 64x8 capacity.
struct Pattern {
    std::array<std::uint8_t, kCells> notes{}, timing{};
    std::array<std::uint32_t, kCells> instruments{};
    std::array<std::array<std::uint16_t, 3>, kCells> fx{};
    std::array<std::array<std::uint8_t, 3>, kCells> values{};
    std::uint16_t rows = 0;
    std::uint8_t channels = 8;
    void Clear(unsigned r) {
        rows = static_cast<std::uint16_t>(r); channels = 8;
        notes.fill(255); timing.fill(0); instruments.fill(0); fx.fill({}); values.fill({});
    }
    Cell At(unsigned i) const { return {notes[i], timing[i], instruments[i], fx[i], values[i]}; }
    void Put(unsigned i, const Cell& c) {
        notes[i] = c.note; timing[i] = c.timing; instruments[i] = c.instrument;
        fx[i] = c.fx; values[i] = c.values;
    }
};
inline std::uint32_t Get(const std::uint8_t* p, unsigned n) {
    std::uint32_t v = 0;
    for (unsigned i = 0; i < n; ++i) v |= std::uint32_t(p[i]) << (8 * i);
    return v;
}
inline void Put(std::vector<std::uint8_t>& b, std::uint32_t v, unsigned n) {
    for (unsigned i = 0; i < n; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
inline void Patch(std::vector<std::uint8_t>& b, unsigned at, std::uint32_t v, unsigned n) {
    for (unsigned i = 0; i < n; ++i) b.at(at+i) = static_cast<std::uint8_t>(v >> (8*i));
}
inline std::uint32_t CrcStep(std::uint32_t c, const std::uint8_t* p, unsigned n) {
    for (unsigned i = 0; i < n; ++i) {
        c ^= p[i];
        for (unsigned bit = 0; bit < 8; ++bit) c = (c >> 1) ^ (0xedb88320u & (0u-(c&1u)));
    }
    return c;
}
inline std::uint32_t Crc(const std::vector<std::uint8_t>& b) {
    return CrcStep(0xffffffffu, b.data(), static_cast<unsigned>(b.size())) ^ 0xffffffffu;
}
inline void Tlv(std::vector<std::uint8_t>& b, unsigned tag, unsigned encoding,
                const std::vector<std::uint8_t>& v, bool required = true) {
    Put(b,tag,2); Put(b,encoding,1); Put(b,required,1); Put(b,static_cast<unsigned>(v.size()),4);
    b.insert(b.end(),v.begin(),v.end());
}
inline void Scalar(std::vector<std::uint8_t>& b, unsigned tag, unsigned width,
                   std::uint32_t v, bool required = true) {
    std::vector<std::uint8_t> bytes; Put(bytes,v,width);
    Tlv(b,tag,width==1?1:width==2?2:3,bytes,required);
}
// Experimental selectors: 1 fixed8, 2 sparse8, 3 fixed16, 4 sparse16.
inline bool Wide(unsigned codec) { return codec >= 3; }
inline bool Sparse(unsigned codec) { return codec == 2 || codec == 4; }
inline unsigned Stride(unsigned codec) { return Wide(codec)?15:12; }
inline bool Encode(const Pattern& p, unsigned codec, std::vector<std::uint8_t>& out) {
    if (codec<1 || codec>4 || !p.rows || p.rows>64 || p.channels!=8) return false;
    std::vector<std::uint8_t> cells;
    unsigned count=0;
    if (Sparse(codec)) Put(cells,0,2);
    for (unsigned i=0;i<p.rows*8;++i) {
        const auto c=p.At(i);
        if (Sparse(codec) && c==Cell{}) continue;
        if (Sparse(codec)) {Put(cells,i,2); ++count;}
        Put(cells,c.note,1); Put(cells,c.timing,1); Put(cells,c.instrument,4);
        for (unsigned f=0;f<3;++f) {
            if (!Wide(codec) && c.fx[f]>255) return false;
            Put(cells,c.fx[f],Wide(codec)?2:1); Put(cells,c.values[f],1);
        }
    }
    if (Sparse(codec)) Patch(cells,0,count,2);
    out.clear(); Scalar(out,1,2,p.rows); Tlv(out,2,6,cells); Scalar(out,3,2,codec);
    return true;
}
struct Entry {
    char type[4]{};
    std::uint32_t id=0, offset=0, length=0, crc=0, flags=0;
};
static_assert(sizeof(Entry)==24,"Measured directory representation");
struct Index { std::array<Entry,kEntries> entries{}; unsigned count=0; };
struct Workspace {
    std::array<std::uint8_t,512> block{};
    std::array<std::uint8_t,17> record{};
};
// Each read is bounded; no vector/file-sized storage in reader/index/decoder.
class Reader {
public:
    explicit Reader(const char* path) : file_(std::fopen(path,"rb")) {
        if (file_) {
            std::setvbuf(file_,nullptr,_IONBF,0);
            std::fseek(file_,0,SEEK_END); size=static_cast<std::uint32_t>(std::ftell(file_));
        }
    }
    ~Reader(){if(file_)std::fclose(file_);}
    Reader(const Reader&)=delete;
    bool Read(std::uint32_t at, void* p, unsigned n) {
        if (!file_ || n>512 || at>size || n>size-at || at<allowed_begin ||
            at>allowed_end || n>allowed_end-at) return false;
        ++reads; bytes+=n; max_read=std::max(max_read,n);
        return !std::fseek(file_,static_cast<long>(at),SEEK_SET) && std::fread(p,1,n,file_)==n;
    }
    void Window(std::uint32_t at, std::uint32_t n) {
        allowed_begin=at; allowed_end=at+n; reads=0; bytes=0; max_read=0;
    }
    std::uint32_t size=0, allowed_begin=0, allowed_end=UINT32_MAX;
    std::uint64_t bytes=0, reads=0;
    unsigned max_read=0;
private:
    std::FILE* file_=nullptr;
};
inline bool ReadIndex(Reader& r, Index& index, Workspace& w, unsigned capacity=kEntries) {
    if (!r.Read(0,w.block.data(),32))return false;
    const auto* h=w.block.data();
    const auto count=Get(h+12,2);
    if (std::memcmp(h,"BTT1",4) || Get(h+4,2)!=1 || Get(h+6,2) || Get(h+8,4)!=32 ||
        Get(h+14,2)!=24 || Get(h+16,4)!=r.size || Get(h+24,4) || Get(h+28,4) ||
        !count || count>capacity || count>kEntries || r.size>1048576 || r.size<32+count*24)return false;
    std::uint32_t end=32+count*24;
    for(unsigned i=0;i<count;++i) {
        if(!r.Read(32+i*24,w.block.data(),24))return false;
        auto& e=index.entries[i];const auto* b=w.block.data();
        std::memcpy(e.type,b,4);e.id=Get(b+4,4);e.offset=Get(b+8,4);
        e.length=Get(b+12,4);e.crc=Get(b+16,4);e.flags=Get(b+20,4);
        if(e.offset!=end || e.length>65536 || end>r.size || e.length>r.size-end || e.flags>1)return false;
        for(unsigned j=0;j<i;++j)if(e.id==index.entries[j].id && !std::memcmp(e.type,index.entries[j].type,4))return false;
        end+=e.length;
    }
    if(end!=r.size)return false;
    index.count=count;return true;
}
inline const Entry* Find(const Index& index, const char* type, unsigned id) {
    for(unsigned i=0;i<index.count;++i) {
        const auto& e=index.entries[i];if(e.id==id && !std::memcmp(e.type,type,4))return &e;
    }
    return nullptr;
}
inline bool CheckCrc(Reader& r,const Entry& e,Workspace& w) {
    std::uint32_t crc=0xffffffffu;
    for(unsigned at=0;at<e.length;) {
        const auto n=std::min(512u,e.length-at);
        if(!r.Read(e.offset+at,w.block.data(),n))return false;
        crc=CrcStep(crc,w.block.data(),n);at+=n;
    }
    return (crc^0xffffffffu)==e.crc;
}
inline bool WholeCrc(Reader& r,Workspace& w) {
    std::uint32_t crc=0xffffffffu, expected=0;
    for(unsigned at=0;at<r.size;) {
        const auto n=std::min(512u,r.size-at);
        if(!r.Read(at,w.block.data(),n))return false;
        for(unsigned i=0;i<n;++i) {
            if(at+i>=20 && at+i<24) { expected |= std::uint32_t(w.block[i])<<(8*(at+i-20));continue; }
            crc=CrcStep(crc,w.block.data()+i,1);
        }
        at+=n;
    }
    return (crc^0xffffffffu)==expected;
}
inline bool ReadCell(Reader& r,unsigned at,unsigned codec,Workspace& w,Cell& c) {
    if(!r.Read(at,w.record.data(),Stride(codec)))return false;
    const auto* b=w.record.data();c.note=b[0];c.timing=b[1];c.instrument=Get(b+2,4);
    if(!((c.note>=24 && c.note<=127) || c.note>=254) || c.timing>1)return false;
    unsigned cursor=6;
    for(unsigned f=0;f<3;++f) {
        c.fx[f]=static_cast<std::uint16_t>(Get(b+cursor,Wide(codec)?2:1));cursor+=Wide(codec)?2:1;
        c.values[f]=b[cursor++];if(!c.fx[f] && c.values[f])return false;
    }
    return true;
}
inline bool Decode(Reader& r,const Entry& e,Workspace& w,Pattern& out,unsigned cell_capacity=kCells) {
    if(std::memcmp(e.type,"PATT",4) || !CheckCrc(r,e,w))return false;
    unsigned at=0,rows=0,codec=0,cell_at=0,cell_bytes=0;
    bool seen[4]{};
    while(at<e.length) {
        if(e.length-at<8 || !r.Read(e.offset+at,w.block.data(),8))return false;
        const unsigned tag=Get(w.block.data(),2), encoding=w.block[2], flags=w.block[3], n=Get(w.block.data()+4,4);
        at+=8;if(n>e.length-at || flags!=1 || tag<1 || tag>3 || seen[tag])return false;
        seen[tag]=true;
        if(tag==2) {if(encoding!=6)return false;cell_at=e.offset+at;cell_bytes=n;}
        else {
            if(encoding!=2 || n!=2 || !r.Read(e.offset+at,w.block.data(),2))return false;
            if(tag==1)rows=Get(w.block.data(),2);else codec=Get(w.block.data(),2);
        }
        at+=n;
    }
    if(!seen[1] || !seen[2] || !seen[3] || !rows || rows>64 || codec<1 || codec>4 || rows*8>cell_capacity)return false;
    const auto stride=Stride(codec);unsigned count=rows*8,base=cell_at;
    if(Sparse(codec)) {
        if(cell_bytes<2 || !r.Read(base,w.block.data(),2))return false;
        count=Get(w.block.data(),2);base+=2;
        if(count>rows*8 || cell_bytes!=2+count*(stride+2))return false;
    }else if(cell_bytes!=count*stride)return false;
    // First pass validates without mutating output. Caller owns a candidate buffer.
    for(unsigned pass=0;pass<2;++pass) {
        if(pass)out.Clear(rows);
        unsigned cursor=base,previous=0;
        for(unsigned i=0;i<count;++i) {
            unsigned position=i;
            if(Sparse(codec)) {
                if(!r.Read(cursor,w.block.data(),2))return false;
                position=Get(w.block.data(),2);cursor+=2;
                if(position>=rows*8 || (i && position<=previous))return false;
                previous=position;
            }
            Cell cell;
            if(!ReadCell(r,cursor,codec,w,cell) || (Sparse(codec) && cell==Cell{}))return false;
            cursor+=stride;if(pass)out.Put(position,cell);
        }
    }
    return true;
}
struct Section { const char* type; unsigned id; std::vector<std::uint8_t> bytes; unsigned flags=1; };
inline std::vector<std::uint8_t> Envelope(const std::vector<Section>& sections) {
    std::vector<std::uint8_t> b(32,0);
    std::memcpy(b.data(),"BTT1",4);Patch(b,4,1,2);Patch(b,8,32,4);
    Patch(b,12,static_cast<unsigned>(sections.size()),2);Patch(b,14,24,2);
    unsigned offset=32+static_cast<unsigned>(sections.size())*24;
    for(const auto& s:sections) {
        b.insert(b.end(),s.type,s.type+4);Put(b,s.id,4);Put(b,offset,4);
        Put(b,static_cast<unsigned>(s.bytes.size()),4);Put(b,Crc(s.bytes),4);Put(b,s.flags,4);
        offset+=static_cast<unsigned>(s.bytes.size());
    }
    for(const auto& s:sections)b.insert(b.end(),s.bytes.begin(),s.bytes.end());
    Patch(b,16,static_cast<unsigned>(b.size()),4);
    auto crc=CrcStep(0xffffffffu,b.data(),20);crc=CrcStep(crc,b.data()+24,static_cast<unsigned>(b.size())-24);
    Patch(b,20,crc^0xffffffffu,4);return b;
}
inline bool Write(const char* path,const std::vector<std::uint8_t>& b) {
    auto* f=std::fopen(path,"wb");if(!f)return false;
    const bool ok=std::fwrite(b.data(),1,b.size(),f)==b.size();return std::fclose(f)==0 && ok;
}
}

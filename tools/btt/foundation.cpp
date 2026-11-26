#include "foundation.h"
#include <algorithm>
#include <cstring>

namespace btt {
namespace {
uint32_t LE(const uint8_t* p,unsigned n) { uint32_t v=0; for(unsigned i=0;i<n;++i)v|=uint32_t(p[i])<<(i*8); return v; }
void Put(uint8_t* p,uint32_t v,unsigned n) { for(unsigned i=0;i<n;++i)p[i]=uint8_t(v>>(8*i)); }
uint32_t Step(uint32_t c,const uint8_t* p,unsigned n) {
    for(unsigned i=0;i<n;++i) { c^=p[i]; for(unsigned b=0;b<8;++b)c=(c>>1)^(0xedb88320u&(0u-(c&1u))); } return c;
}
bool Type(const Entry& e,const char* t) { return !std::memcmp(e.type,t,4); }
bool Known(const Entry& e) { return Type(e,"TUNE")||Type(e,"ORDR")||Type(e,"PATT")||Type(e,"INST"); }
bool Read(const Reader& r,uint32_t at,void* p,unsigned n) {
    return n<=512 && at<=r.size() && n<=r.size()-at && r.read(at,p,n)==n;
}
// A section-limited block cursor. Logical accesses never seek/read per cell.
class Cursor {
    const Reader& r; Workspace& w; uint32_t at,end,base=0; unsigned available=0;
public:
    Cursor(const Reader& reader,Workspace& work,uint32_t begin,uint32_t n):r(reader),w(work),at(begin),end(begin+n){}
    bool bytes(uint8_t* p,unsigned n) {
        ++w.logical_fields;
        if(n>end-at)return false;
        while(n) {
            if(!available || at<base || at>=base+available) {
                base=at; available=std::min<uint32_t>(512u,end-at);
                if(!Read(r,base,w.block.data(),available))return false;
            }
            const auto take=std::min<uint32_t>(n,base+available-at);
            std::memcpy(p,w.block.data()+at-base,take); at+=take; p+=take; n-=take;
        } return true;
    }
};
Result Crc(const Reader& r,uint32_t at,uint32_t length,Workspace& w,uint32_t& value,bool whole=false) {
    uint32_t c=0xffffffffu;
    while(length) {
        const auto n=std::min<uint32_t>(512u,length);
        if(!Read(r,at,w.block.data(),n))return Result::Io;
        if(whole && at==0) { c=Step(c,w.block.data(),20); c=Step(c,w.block.data()+24,n-24); }
        else c=Step(c,w.block.data(),n);
        at+=n; length-=n;
    } value=c^0xffffffffu; return Result::Ok;
}
Result Check(const Reader& r,const Entry& e,Workspace& w) {
    uint32_t crc=0; const auto result=Crc(r,e.offset,e.length,w,crc);
    return result!=Result::Ok?result:crc==e.crc?Result::Ok:Result::Invalid;
}
Result Utf8(const Reader& r,Span s,Workspace& w) {
    Cursor c(r,w,s.offset,s.length); unsigned left=0; uint32_t value=0,minimum=0;
    for(uint32_t i=0;i<s.length;++i) {
        uint8_t b; if(!c.bytes(&b,1))return Result::Io;
        if(left) {
            if((b&0xc0)!=0x80)return Result::Invalid;
            value=(value<<6)|(b&63);
            if(!--left && (value<minimum || value>0x10ffff || (value>=0xd800 && value<=0xdfff)))return Result::Invalid;
        } else if(b<128) { if(!b)return Result::Invalid; }
        else if(b>=0xc2 && b<=0xdf) {left=1;value=b&31;minimum=128;}
        else if(b>=0xe0 && b<=0xef) {left=2;value=b&15;minimum=2048;}
        else if(b>=0xf0 && b<=0xf4) {left=3;value=b&7;minimum=65536;}
        else return Result::Invalid;
    } return left?Result::Invalid:Result::Ok;
}
unsigned Encoding(const Entry& e,unsigned tag) {
    if(Type(e,"TUNE")) { constexpr unsigned a[]={0,4,5,3,1,3,3}; return tag<7?a[tag]:0; }
    if(Type(e,"PATT")) { constexpr unsigned a[]={0,2,6,2}; return tag<4?a[tag]:0; }
    if(Type(e,"ORDR"))return tag==1?6:0;
    if(Type(e,"INST")) { constexpr unsigned a[]={0,2,6,2,2,2,5,5};return tag<8?a[tag]:0; }
    return 0;
}
unsigned Required(const Entry& e) { return Type(e,"TUNE")?6:Type(e,"PATT")?3:1; }
// Complete TLV framing/duplicates, including unknown tags. Only seven known spans
// survive this scan; optional payloads stay in immutable backing.
Result Fields(const Reader& r,const Entry& e,Workspace& w,Status& status) {
    w.fields={};w.field_count=0; unsigned count=0; uint32_t at=e.offset,end=at+e.length;
    while(at<end) {
        uint8_t h[8]; if(end-at<8)return Result::Invalid;
        if(!Read(r,at,h,8))return Result::Io;
        ++w.logical_fields;
        const unsigned tag=LE(h,2),encoding=h[2],flags=h[3];const uint32_t n=LE(h+4,4); at+=8;
        if(n>end-at)return Result::Invalid;
        if(count==256)return Result::Capacity;
        for(unsigned i=0;i<count;++i)if(w.tags[i]==tag)return Result::Invalid;
        w.tags[count++]=uint16_t(tag);
        if(flags&~1u)return Result::Unsupported;
        const auto expected=Encoding(e,tag); const bool required=tag && tag<=Required(e);
        if(required && flags!=1)return Result::Invalid;
        if(!expected && flags)return Result::Unsupported;
        if(expected && encoding!=expected) {
            if(encoding>=1 && encoding<=6)return Result::Invalid;
            if(required || flags)return Result::Unsupported;
            status.unsupported_values=true;
        }
        if(encoding>=1 && encoding<=4) {
            constexpr unsigned sizes[]={0,1,2,4,16};if(n!=sizes[encoding])return Result::Invalid;
        }
        if(encoding==5) { auto v=Utf8(r,{at,n,uint8_t(encoding),true},w);if(v!=Result::Ok)return v; }
        if(expected)w.fields[tag]={at,n,uint8_t(encoding),true};
        at+=n;
    }
    for(unsigned tag=1;tag<=Required(e);++tag)if(!w.fields[tag].present)return Result::Invalid;
    w.field_count=count;
    return Result::Ok;
}
Result Scalar(const Reader& r,Span s,uint32_t& value) {
    uint8_t b[4];if(s.length>4)return Result::Invalid;
    if(!Read(r,s.offset,b,s.length))return Result::Io;
    value=LE(b,s.length);return Result::Ok;
}
Result Cells(const Document& d,const Entry& e,Workspace& w,Status& status,Pattern* out,unsigned capacity) {
    const auto& r=*d.backing;
    auto result=Fields(r,e,w,status);if(result!=Result::Ok)return result;
    uint32_t rows,codec;
    if((result=Scalar(r,w.fields[1],rows))!=Result::Ok)return result;
    if((result=Scalar(r,w.fields[3],codec))!=Result::Ok)return result;
    if(!rows)return Result::Invalid;
    if(rows>64 || rows*d.channels>capacity)return Result::Capacity;
    if(codec<1 || codec>4)return Result::Unsupported;
    const bool sparse=codec==2 || codec==4,wide=codec>=3;
    const unsigned stride=wide?15:12,total=rows*d.channels; unsigned count=total;
    const auto payload=w.fields[2];Cursor cursor(r,w,payload.offset,payload.length);
    uint8_t b[17];
    if(sparse) { if(payload.length<2)return Result::Invalid; if(!cursor.bytes(b,2))return Result::Io;count=LE(b,2); }
    if(count>total || payload.length!=(sparse?2u:0u)+count*(stride+(sparse?2:0)))return Result::Invalid;
    if(out)out->Clear(rows,d.channels);
    unsigned previous=0;
    for(unsigned i=0;i<count;++i) {
        unsigned position=i;
        if(sparse) {
            if(!cursor.bytes(b,2))return Result::Io;
            position=LE(b,2);
            if(position>=total || (i && position<=previous))return Result::Invalid;
            previous=position;
        }
        if(!cursor.bytes(b,stride))return Result::Io;
        Cell c;c.note=b[0];c.timing=b[1];c.instrument=LE(b+2,4);
        if(!((c.note>=24 && c.note<=127)||c.note>=254)||c.timing>1)return Result::Invalid;
        if(c.instrument>=d.next_instrument)return Result::Invalid;
        if(c.instrument && !Find(d,"INST",c.instrument))status.unresolved_instruments=true;
        unsigned at=6;bool nondefault=c.note!=255 || c.timing || c.instrument;
        for(unsigned f=0;f<3;++f) {
            c.fx[f]=uint16_t(LE(b+at,wide?2:1));at+=wide?2:1;c.values[f]=b[at++];
            if(!c.fx[f] && c.values[f])return Result::Invalid;
            if(c.fx[f]) {status.unsupported_values=true;nondefault=true;}
        }
        if(sparse && !nondefault)return Result::Invalid;
        if(out)out->Put(position,c);
    }
    if(out)out->ready=true;
    return Result::Ok;
}
}

const Entry* Find(const Document& d,const char* type,uint32_t id) {
    for(unsigned i=0;i<d.count;++i)if(d.entries[i].id==id && Type(d.entries[i],type))return &d.entries[i];
    return nullptr;
}
Result Validate(ReaderOwner source,Document& d,Workspace& w,Limits limits) {
    d.accepted=false;d.backing=std::move(source);d.count=0;d.status={};
    if(!d.backing || !d.backing->identity())return Result::Backing;
    const auto& r=*d.backing;uint8_t h[32];
    if(r.size()<32)return Result::Invalid;
    if(r.size()>1048576)return Result::Capacity;
    if(!Read(r,0,h,32))return Result::Io;
    if(std::memcmp(h,"BTT1",4))return Result::Invalid;
    if(LE(h+4,2)!=1 || LE(h+24,4))return Result::Unsupported;
    const unsigned count=LE(h+12,2);d.minor=uint16_t(LE(h+6,2));
    d.saved_bytes=r.size();d.whole_crc=LE(h+20,4);
    if(LE(h+8,4)!=32 || LE(h+14,2)!=24 || LE(h+16,4)!=r.size() || LE(h+28,4) || !count)return Result::Invalid;
    if(count>256 || count>limits.entries)return Result::Capacity;
    uint32_t crc;auto result=Crc(r,0,r.size(),w,crc,true);if(result!=Result::Ok)return result;
    if(crc!=LE(h+20,4))return Result::Invalid;
    uint32_t end=32+count*24;unsigned patterns=0,instruments=0,tunes=0,orders=0;
    for(unsigned i=0;i<count;++i) {
        uint8_t b[24];if(!Read(r,32+i*24,b,24))return Result::Io;
        auto& e=d.entries[i];std::memcpy(e.type,b,4);e.id=LE(b+4,4);e.offset=LE(b+8,4);e.length=LE(b+12,4);e.crc=LE(b+16,4);e.flags=LE(b+20,4);
        if(e.offset!=end || end>r.size() || e.length>r.size()-end)return Result::Invalid;
        if(e.length>65536)return Result::Capacity;
        if(e.flags&~1u)return Result::Unsupported;
        if(Known(e)) {
            if(e.flags!=1)return Result::Invalid;
            if(Type(e,"TUNE")) {if(e.id)return Result::Invalid;++tunes;}
            if(Type(e,"ORDR")) {if(e.id)return Result::Invalid;++orders;}
            if(Type(e,"PATT")) {if(!e.id)return Result::Invalid;++patterns;}
            if(Type(e,"INST")) {if(!e.id)return Result::Invalid;++instruments;}
        } else if(e.flags)return Result::Unsupported;
        for(unsigned j=0;j<i;++j)if(e.id==d.entries[j].id && !std::memcmp(e.type,d.entries[j].type,4))return Result::Invalid;
        end+=e.length;++d.count;
        if((result=Check(r,e,w))!=Result::Ok)return result;
    }
    if(end!=r.size() || tunes!=1 || orders!=1 || !patterns)return Result::Invalid;
    if(patterns>limits.patterns || instruments>limits.instruments)return Result::Capacity;
    const auto* tune=Find(d,"TUNE",0);
    if((result=Fields(r,*tune,w,d.status))!=Result::Ok)return result;
    uint8_t id[16];if(!Read(r,w.fields[1].offset,id,16))return Result::Io;
    bool nonzero=false;for(auto b:id)nonzero|=b!=0;
    if(!nonzero || !w.fields[2].length || w.fields[2].length>255)return Result::Invalid;
    if((result=Scalar(r,w.fields[3],d.bpm))!=Result::Ok)return result;
    uint32_t channels;
    if((result=Scalar(r,w.fields[4],channels))!=Result::Ok)return result;
    d.channels=channels;
    if((result=Scalar(r,w.fields[5],d.next_instrument))!=Result::Ok)return result;
    if((result=Scalar(r,w.fields[6],d.next_pattern))!=Result::Ok)return result;
    if(!d.bpm || !channels || !d.next_instrument || !d.next_pattern)return Result::Invalid;
    if(channels>8)return Result::Capacity;
    if(d.bpm<3200 || d.bpm>30000)d.status.unsupported_values=true;
    for(unsigned i=0;i<d.count;++i) {
        const auto& e=d.entries[i];
        if(Type(e,"PATT")) {
            if(e.id>=d.next_pattern)return Result::Invalid;
            if((result=Cells(d,e,w,d.status,nullptr,limits.cells))!=Result::Ok)return result;
        } else if(Type(e,"ORDR")) {
            if((result=Fields(r,e,w,d.status))!=Result::Ok)return result;
            const auto s=w.fields[1];if(s.length%4)return Result::Invalid;
            if(s.length/4>limits.orders)return Result::Capacity;
            Cursor cursor(r,w,s.offset,s.length);uint8_t b[4];
            for(unsigned j=0;j<s.length/4;++j) {
                if(!cursor.bytes(b,4))return Result::Io;
                const auto ref=LE(b,4);
                if(!ref || ref>=d.next_pattern)return Result::Invalid;
                if(!Find(d,"PATT",ref))d.status.unresolved_patterns=true;
            }
        } else if(Type(e,"INST")) {
            if(e.id>=d.next_instrument)return Result::Invalid;
            if((result=Fields(r,e,w,d.status))!=Result::Ok)return result;
            uint32_t type;if((result=Scalar(r,w.fields[1],type))!=Result::Ok)return result;
            if(type<1 || type>3)d.status.unsupported_values=true;
            const auto resource=w.fields[2];
            if(resource.present) {
                d.status.resources_unchecked=true;
                if(resource.encoding==6) {
                    if(!resource.length)return Result::Invalid;
                    if((result=Utf8(r,{resource.offset+1,resource.length-1,5,true},w))!=Result::Ok)return result;
                }
            }
            for(unsigned tag=3;tag<=5;++tag)if(w.fields[tag].present && w.fields[tag].encoding==2) {
                uint32_t v;if((result=Scalar(r,w.fields[tag],v))!=Result::Ok)return result;
                if((tag==3 && (type!=1 || v<24 || v>127)) || (tag==4 && (type!=1 || v>99)) ||
                   (tag==5 && (type!=2 || v<1 || v>16)))d.status.unsupported_values=true;
            }
        }
    }
    d.accepted=true;return Result::Ok;
}

Result Materialize(const Document& d,uint32_t id,Pattern& p,Workspace& w,unsigned capacity) {
    p.ready=false;
    if(!d.accepted || !d.backing)return Result::Backing;
    const auto* e=Find(d,"PATT",id);if(!e)return Result::Invalid;
    auto result=Check(*d.backing,*e,w);if(result!=Result::Ok)return result;
    auto status=d.status;return Cells(d,*e,w,status,&p,std::min(512u,capacity));
}

namespace {
// Build one section at a time, copying opaque data through the one work block.
// This sink supports a dry CRC/length pass, then exact bounded writes.
struct Sink {
    Writer* writer; uint32_t at,crc=0xffffffffu;
    bool bytes(const uint8_t* b,unsigned n) {
        if(writer && writer->write(at,b,n)!=n)return false;
        crc=Step(crc,b,n);at+=n;return true;
    }
};
Result Copy(const Reader& r,uint32_t at,uint32_t n,Workspace& w,Sink& sink) {
    while(n) {
        const auto take=std::min<uint32_t>(512u,n);
        if(!Read(r,at,w.block.data(),take))return Result::Io;
        if(!sink.bytes(w.block.data(),take))return Result::Io;
        at+=take;n-=take;
    }
    return Result::Ok;
}
Result Emit(const Document& d,const Entry& e,Workspace& w,uint32_t bpm,Sink& sink) {
    auto& r=*d.backing;
    if(!Type(e,"INST") && !(Type(e,"TUNE") && bpm))return Copy(r,e.offset,e.length,w,sink);
    Status status;auto result=Fields(r,e,w,status);if(result!=Result::Ok)return result;
    if(Type(e,"TUNE")) {
        const auto s=w.fields[3];
        if((result=Copy(r,e.offset,s.offset-e.offset,w,sink))!=Result::Ok)return result;
        uint8_t b[4];Put(b,bpm,4);if(!sink.bytes(b,4))return Result::Io;
        return Copy(r,s.offset+4,e.offset+e.length-s.offset-4,w,sink);
    }
    uint32_t type;if((result=Scalar(r,w.fields[1],type))!=Result::Ok)return result;
    const bool root=!w.fields[3].present,fine=!w.fields[4].present;
    if(type==1 && w.field_count+unsigned(root)+unsigned(fine)>256)return Result::Capacity;
    if((result=Copy(r,e.offset,e.length,w,sink))!=Result::Ok)return result;
    if(type==1)for(unsigned tag=3;tag<=4;++tag)if(tag==3?root:fine) {
        uint8_t b[10]{};Put(b,tag,2);b[2]=2;Put(b+4,2,4);Put(b+8,tag==3?60:0,2);
        if(!sink.bytes(b,10))return Result::Io;
    }
    return Result::Ok;
}
void Wire(const Entry& e,uint8_t* b) {
    std::memcpy(b,e.type,4);Put(b+4,e.id,4);Put(b+8,e.offset,4);Put(b+12,e.length,4);Put(b+16,e.crc,4);Put(b+20,e.flags,4);
}
}
Result Rewrite(const Document& d,Writer& output,Workspace& w,uint32_t bpm) {
    if(!d.accepted || !d.backing || !output.identity() || output.identity()==d.backing->identity())return Result::Backing;
    // Check immutable backing integrity again before touching temporary output.
    uint8_t h[32];if(!Read(*d.backing,0,h,32))return Result::Io;
    if(d.backing->size()!=d.saved_bytes || LE(h+20,4)!=d.whole_crc)return Result::Backing;
    uint32_t crc;auto result=Crc(*d.backing,0,d.backing->size(),w,crc,true);
    if(result!=Result::Ok)return result;
    if(crc!=d.whole_crc)return Result::Backing;
    uint32_t end=32+d.count*24;
    // Directory is generated from one dry section at a time; no duplicate index.
    for(unsigned i=0;i<d.count;++i) {
        const auto& e=d.entries[i];Sink dry{nullptr,end};
        if((result=Emit(d,e,w,bpm,dry))!=Result::Ok)return result;
        Entry changed=e;changed.offset=end;changed.length=dry.at-end;changed.crc=dry.crc^0xffffffffu;
        if(changed.length>65536 || dry.at>1048576)return Result::Capacity;
        uint8_t b[24];Wire(changed,b);if(output.write(32+i*24,b,24)!=24)return Result::Io;
        end=dry.at;
    }
    Put(h+16,end,4);Put(h+20,0,4);
    if(!output.resize(end))return Result::Io;
    // Whole CRC includes the new directory. Recompute its deterministic entries.
    uint32_t whole=Step(0xffffffffu,h,20);whole=Step(whole,h+24,8);
    uint32_t at=32+d.count*24;
    for(unsigned i=0;i<d.count;++i) {
        const auto& e=d.entries[i];Sink dry{nullptr,at};
        if((result=Emit(d,e,w,bpm,dry))!=Result::Ok)return result;
        Entry changed=e;changed.offset=at;changed.length=dry.at-at;changed.crc=dry.crc^0xffffffffu;
        uint8_t b[24];Wire(changed,b);whole=Step(whole,b,24);at=dry.at;
    }
    Sink sink{&output,32+d.count*24};sink.crc=whole;
    for(unsigned i=0;i<d.count;++i)if((result=Emit(d,d.entries[i],w,bpm,sink))!=Result::Ok)return result;
    Put(h+20,sink.crc^0xffffffffu,4);
    if(output.write(0,h,32)!=32)return Result::Io;
    return Result::Ok;
}
namespace {
class CompareWriter final : public Writer {
    const Reader& reader;
public:
    explicit CompareWriter(const Reader& r):reader(r){}
    const void* identity() const override {return this;}
    bool resize(uint32_t n) override {return n==reader.size();}
    unsigned write(uint32_t at,const void* p,unsigned n) override {
        uint8_t b[512];
        return Read(reader,at,b,n) && !std::memcmp(p,b,n)?n:0;
    }
};
}
Result Save(const Document& d,Writer& output,Store& store,Document& candidate,Workspace& w,uint32_t bpm) {
    if(&d==&candidate)return Result::Backing;
    candidate.accepted=false;
    auto result=Rewrite(d,output,w,bpm);if(result!=Result::Ok)return result;
    if(!store.finish())return Result::Io;
    auto backing=store.reopen();
    if(!backing || backing->identity()==d.backing->identity())return Result::Backing;
    if((result=Validate(std::move(backing),candidate,w))!=Result::Ok)return result;
    CompareWriter compare(*candidate.backing);
    if((result=Rewrite(d,compare,w,bpm))!=Result::Ok) {candidate.accepted=false;return result;}
    if(!store.promote()) {candidate.accepted=false;return Result::Io;}
    return Result::Ok;
}
}

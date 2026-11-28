#include "export.h"
#include "bringup/device_facts.h"
#include <cstdio>
#include <algorithm>
#include <cstring>
namespace btt {
using namespace BroTracker;
const char* Export::Error(char* out,uint32_t id,const char* reason,const char* nonce) noexcept {
    std::snprintf(out,kBttExportLine,"BTBTT1 EXPORTERROR 1 %lu %s %s\n",(unsigned long)id,reason,nonce);return out;
}
const char* Export::Command(const BttExportRequest& r,ReaderOwner source,uint32_t generation,
    uint32_t crc,const char* boot,bool ready,uint32_t now,char* out) noexcept {
    const auto error=[&](const char* reason){return Error(out,r.id,reason,r.nonce);};
    if(r.verb==BttExportVerb::Begin) {
        if(Active())return error("busy");
        if(r.id<=high_)return error("stale-request");
        high_=r.id;
        if(!ready || !ValidDeviceBoot(r.nonce) || (*boot && !ValidDeviceBoot(boot)))return error("not-settled");
        if(!source || !generation || source->size()<32 || source->size()>kBttExportMax)return error("no-source");
        std::strcpy(nonce_,r.nonce);source_=std::move(source);id_=r.id;generation_=generation;crc_=crc;length_=source_->size();offset_=0;rolling_=0xffffffffu;at_=now;
        std::snprintf(out,kBttExportLine,"BTBTT1 EXPORTBEGIN 1 %lu %lu %lu %lu %s %s\n",
            (unsigned long)id_,(unsigned long)generation_,(unsigned long)length_,(unsigned long)crc_,*boot?boot:"unavailable",nonce_);return out;
    }
    if(!Active() || r.id!=id_ || std::strcmp(r.nonce,nonce_) || (r.generation!=generation_ && !(r.verb==BttExportVerb::Cancel && !r.generation)))return error("identity");
    if(r.verb==BttExportVerb::Cancel) {
        // Offset is diagnostic only: a lost DATA reply leaves peers at different offsets.
        Disconnect();std::snprintf(out,kBttExportLine,"BTBTT1 EXPORTCANCELLED 1 %lu %lu %s\n",(unsigned long)id_,(unsigned long)generation_,nonce_);return out;
    }
    if(!ready || now-at_>=kBttExportTimeout){Disconnect();return error("not-settled-or-timeout");}
    if(r.offset!=offset_){Disconnect();return error("offset");}
    if(r.verb==BttExportVerb::End) {
        if(offset_!=length_ || (rolling_^0xffffffffu)!=crc_){Disconnect();return error("integrity");}
        Disconnect();std::snprintf(out,kBttExportLine,"BTBTT1 EXPORTDONE 1 %lu %lu %lu %lu %s\n",
            (unsigned long)id_,(unsigned long)generation_,(unsigned long)length_,(unsigned long)crc_,nonce_);return out;
    }
    if(offset_==length_){Disconnect();return error("past-end");}
    uint8_t bytes[kBttExportChunk];const auto n=std::min<uint32_t>(kBttExportChunk,length_-offset_);
    if(source_->size()!=length_ || source_->read(offset_,bytes,n)!=n){Disconnect();return error("read");}
    for(unsigned i=0;i<n;++i)if(offset_+i<20 || offset_+i>=24)rolling_=BttExportCrc(rolling_,bytes+i,1);
    const unsigned header=std::snprintf(out,kBttExportLine,"BTBTT1 EXPORTDATA 1 %lu %lu %lu %lu %lu ",
        (unsigned long)id_,(unsigned long)generation_,(unsigned long)offset_,(unsigned long)n,(unsigned long)(BttExportCrc(0xffffffffu,bytes,n)^0xffffffffu));
    constexpr char hex[]="0123456789abcdef";
    for(unsigned i=0;i<n;++i){out[header+2*i]=hex[bytes[i]>>4];out[header+2*i+1]=hex[bytes[i]&15];}
    std::snprintf(out+header+2*n,kBttExportLine-header-2*n," %s\n",nonce_);offset_+=n;at_=now;return out;
}
bool Export::Poll(uint32_t now,bool ready,char* out) noexcept {
    if(!Active() || (ready && now-at_<kBttExportTimeout))return false;
    Disconnect();Error(out,id_,ready?"timeout":"not-settled",nonce_);return true;
}
}

#pragma once

#include "pattern_snapshot.h"
#include "resource_path.h"
#include <algorithm>
#include <cstdio>

namespace BroTracker {

constexpr char kWaveformCapability[] = "BTWAVE1 CAP 3";
constexpr char kWaveformRangeCapability[] = "BTWAVE1 RANGECAP 1";
constexpr unsigned kWaveformPlayingBytes = 64;
constexpr unsigned kWaveformIdleBytes = 768;
constexpr unsigned kWaveformPlayingIntervalMs = 25;
constexpr unsigned kWaveformLineCapacity = 2*kWaveformIdleBytes+40;

// One hex DATA line and one credit per chunk. Budget maximum-width decimal
// fields and allow turnaround/headroom beyond the firmware's minimum interval.
inline std::uint64_t WaveformPlayingEstimateMs(std::uint64_t pcm_bytes) {
    constexpr unsigned wire_bytes_per_second=6000;
    constexpr unsigned turnaround_ms=32;
    const auto credits=(pcm_bytes+kWaveformPlayingBytes-1)/kWaveformPlayingBytes;
    const auto wire_bytes=2*pcm_bytes+credits*80;
    return std::max(credits*turnaround_ms,(wire_bytes*1000+wire_bytes_per_second-1)/wire_bytes_per_second);
}

struct WaveformSource {
    std::uint32_t token=0, frames=0, rate=0, channels=0, bits=0;
    unsigned Stride() const { return channels*(bits/8); }
    std::uint64_t Bytes() const { return std::uint64_t(frames)*Stride(); }
};

inline bool ParseWaveformGet(std::string_view line,std::uint32_t& id,std::uint32_t& slot,
    char (&path)[kSamplePathMaximum+1]) {
    const auto split=line.rfind(' ');
    std::uint32_t fields[3];
    if(split==line.npos || !SnapshotFields(line.data(),split,"BTWAVE1 GET ",fields,3) ||
       fields[0]!=1 || !fields[1] || fields[2]>=99 ||
       !DecodePathToken(line.substr(split+1),path,sizeof(path),true) || !ValidSamplePath(path)) return false;
    id=fields[1];slot=fields[2];return true;
}

inline unsigned WaveformByteBudget(bool playing,std::uint32_t elapsed_ms) {
    if(!playing)return kWaveformIdleBytes;
    return elapsed_ms>=kWaveformPlayingIntervalMs?kWaveformPlayingBytes:0;
}

inline unsigned WaveformReadBytes(const WaveformSource& source,std::uint32_t offset,unsigned limit) {
    if(!source.Stride() || offset%source.Stride() || offset>=source.Bytes()) return 0;
    return static_cast<unsigned>(std::min<std::uint64_t>(limit-limit%source.Stride(),source.Bytes()-offset));
}

// One foreground-only transfer and at most one outstanding response credit.
// A fresh GET replaces the previous job, and NEXT must match its exact cursor.
struct WaveformPull {
    std::uint32_t id=0, token=0, cursor=0, slot=0;
    enum class Reply { None, Metadata, Data, Error };
    Reply pending=Reply::None;
    bool first_credit=false;
    unsigned limit=kWaveformIdleBytes;
    void Begin(std::uint32_t request,std::uint32_t instrument,std::uint32_t source) {
        *this={request,source,0,instrument,source?Reply::Metadata:Reply::Error,true};
    }
    bool Next(std::uint32_t request,std::uint32_t offset,bool resume=false) {
        if(!id || request!=id || pending!=Reply::None || !token ||
           (resume?!first_credit:offset!=cursor)) return false;
        first_credit=false;cursor=offset;limit=kWaveformIdleBytes;pending=Reply::Data;return true;
    }
    bool Range(std::uint32_t request,std::uint32_t offset,unsigned bytes,const WaveformSource& source) {
        if(!id || request!=id || pending!=Reply::None || !token || source.token!=token ||
           !bytes || bytes>kWaveformIdleBytes || WaveformReadBytes(source,offset,bytes)!=bytes)
            return false;
        first_credit=false;cursor=offset;limit=bytes;pending=Reply::Data;return true;
    }
    void Cancel(std::uint32_t request) { if(id==request)*this={}; }
    void Sent(unsigned bytes=0) {
        if(pending==Reply::Error) { *this={};return; }
        cursor+=bytes;pending=Reply::None;
    }
};

inline std::size_t FormatWaveformData(char (&line)[kWaveformLineCapacity],std::uint32_t id,
    std::uint32_t offset,const std::uint8_t* pcm,unsigned bytes) {
    if(!bytes || bytes>kWaveformIdleBytes) return 0;
    auto at=std::size_t(std::snprintf(line,sizeof(line),"BTWAVE1 DATA 1 %lu %lu ",
        (unsigned long)id,(unsigned long)offset));
    constexpr char hex[]="0123456789abcdef";
    for(unsigned i=0;i<bytes;++i) {line[at++]=hex[pcm[i]>>4];line[at++]=hex[pcm[i]&15];}
    line[at++]='\n';line[at]='\0';return at;
}

inline bool ParseWaveformData(std::string_view line,std::uint32_t& id,std::uint32_t& offset,
    std::uint8_t (&pcm)[kWaveformIdleBytes],unsigned& bytes) {
    const auto split=line.rfind(' ');
    std::uint32_t fields[3];
    if(split==line.npos || !SnapshotFields(line.data(),split,"BTWAVE1 DATA ",fields,3) ||
       fields[0]!=1 || !fields[1]) return false;
    const auto encoded=line.substr(split+1);
    if(encoded.empty() || encoded.size()%2 || encoded.size()>2*sizeof(pcm))return false;
    const auto digit=[](char c) {return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
    for(unsigned i=0;i<encoded.size()/2;++i) {
        const auto high=digit(encoded[2*i]),low=digit(encoded[2*i+1]);
        if(high<0 || low<0)return false;
        pcm[i]=static_cast<std::uint8_t>(16*high+low);
    }
    id=fields[1];offset=fields[2];bytes=static_cast<unsigned>(encoded.size()/2);return true;
}

} // namespace BroTracker

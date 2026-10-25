#pragma once
#include "pattern_snapshot.h"
namespace BroTracker {
inline bool ParseVolumeState(const char* p,std::size_t n,std::uint32_t (&out)[4]) noexcept {
    constexpr char prefix[]="BTVOL1 STATE 1 ";
    if(!p || n>=96 || n<=sizeof(prefix)-1 || std::memcmp(p,prefix,sizeof(prefix)-1))return false;
    std::uint32_t f[4]{};std::size_t at=sizeof(prefix)-1;
    for(unsigned i=0;i<4;++i) {
        const auto start=at;
        while(at<n && p[at]>='0' && p[at]<='9') {const unsigned d=p[at++]-'0';if(f[i]>(UINT32_MAX-d)/10)return false;f[i]=f[i]*10+d;}
        if(at==start || (i==3?at!=n:at==n || p[at++]!=' '))return false;
    }
    if(f[2]>100 || f[3]>65536 || (f[1] && f[1]<=f[0]))return false;
    for(unsigned i=0;i<4;++i)out[i]=f[i];return true;
}
struct MasterVolumeMessage {std::uint32_t id=0;unsigned percent=100;};
inline bool ParseMasterVolume(const char* p,std::size_t n,MasterVolumeMessage& out) noexcept {
    std::uint32_t f[2]{};
    if(!SnapshotFields(p,n,"BTVOL1 SET 1 ",f,2) || !f[0] || f[1]>100)return false;
    out={f[0],unsigned(f[1])};return true;
}
}

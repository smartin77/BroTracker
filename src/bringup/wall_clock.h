#pragma once
#include "pattern_snapshot.h"
#include <algorithm>
#include <string_view>

namespace BroTracker {
constexpr char kWallClockCapability[]="BTCLOCK1 CAP 1";
constexpr std::uint32_t kClockMinimum=1577836800; // 2020-01-01 UTC
constexpr std::uint32_t kClockMaximum=4102444799; // 2099-12-31 UTC
constexpr std::uint32_t kClockExchangeMs=5000;
constexpr int kClockOffsetLimit=14*60;
enum class ClockQuality : unsigned { Unknown, Retained, Host, Synchronized };

inline bool ParseClockFields(std::string_view line,const char* prefix,
                             std::uint32_t* fields,std::size_t count) {
    return SnapshotFields(line.data(),line.size(),prefix,fields,count) && fields[0]==1;
}
inline bool PlausibleClock(std::uint32_t utc) {
    return utc>=kClockMinimum && utc<=kClockMaximum;
}
inline std::uint32_t FilesystemCalendarSeconds(std::uint32_t utc,int offset) {
    const auto shifted=std::int64_t(utc)+offset*60;
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(shifted,0,UINT32_MAX));
}

class WallClockDevice {
public:
    void NewConnection() {
        ResetConnection();
        if(generation_==UINT32_MAX)return;
        ++generation_;
        connected_=true;
    }
    void ResetConnection() { connected_=false;request_=last_=0; }
    std::uint32_t Generation() const { return generation_; }
    ClockQuality Quality(std::uint32_t utc) const {
        if(!PlausibleClock(utc))return ClockQuality::Unknown;
        return quality_==ClockQuality::Unknown?ClockQuality::Retained:quality_;
    }
    int Offset() const { return offset_; }
    bool Query(std::uint32_t id,std::uint32_t now) {
        if(!connected_ || !id || id<=last_)return false;
        request_=last_=id;issued_=now;return true;
    }
    enum class Result { Stale, Invalid, Applied, Kept };
    Result Set(std::uint32_t id,std::uint32_t generation,std::uint32_t utc,unsigned quality,
               unsigned encoded_offset,std::uint32_t now) {
        if(!connected_ || !request_ || id!=request_ || generation!=generation_ ||
           now-issued_>=kClockExchangeMs)return Result::Stale;
        request_=0;
        if(!PlausibleClock(utc) || quality<unsigned(ClockQuality::Host) ||
           quality>unsigned(ClockQuality::Synchronized) || encoded_offset>2*kClockOffsetLimit)
            return Result::Invalid;
        offset_=int(encoded_offset)-kClockOffsetLimit;
        if(quality<unsigned(quality_))return Result::Kept;
        quality_=static_cast<ClockQuality>(quality);
        return Result::Applied;
    }
private:
    std::uint32_t generation_=0,request_=0,last_=0,issued_=0;
    int offset_=0;
    ClockQuality quality_=ClockQuality::Unknown;
    bool connected_=false;
};
}

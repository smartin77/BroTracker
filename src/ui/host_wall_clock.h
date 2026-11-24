#pragma once
#include "bringup/wall_clock.h"
#include <chrono>
#include <ctime>
#ifdef __linux__
#include <sys/timex.h>
#endif

namespace BroTracker {
struct OsClockSample {
    std::int64_t milliseconds=0;
    int offset=0;
    ClockQuality quality=ClockQuality::Host;
    bool valid=false;
};
inline OsClockSample ReadOsClock() {
    const auto now=std::chrono::system_clock::now();
    const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    if(ms/1000<kClockMinimum || ms/1000>kClockMaximum)return {};
    const auto seconds=static_cast<std::time_t>(ms/1000);
    std::tm local{};
    int offset=0;
#ifdef _WIN32
    if(localtime_s(&local,&seconds))return {};
    auto calendar=local;
    const auto as_utc=_mkgmtime64(&calendar);
    if(as_utc<0)return {};
    offset=int((as_utc-seconds)/60);
#else
    if(!localtime_r(&seconds,&local))return {};
    offset=int(local.tm_gmtoff/60);
#endif
    if(offset < -kClockOffsetLimit || offset>kClockOffsetLimit)return {};
    auto quality=ClockQuality::Host;
#ifdef __linux__
    struct timex state{};
    const auto status=adjtimex(&state); // modes=0 is a read-only kernel query.
    if(status>=0 && status!=TIME_ERROR && !(state.status&STA_UNSYNC))
        quality=ClockQuality::Synchronized;
#endif
    return {ms,offset,quality,true};
}

class HostWallClock {
public:
    void Disconnect() { phase_=Phase::Idle; supported_=false; have_baseline_=false; attempted_=false; }
    void Advertise() { supported_=true; }
    bool NeedsSet() const { return phase_==Phase::Set; }
    template<class Reader>
    std::size_t Command(char* line,std::size_t capacity,std::uint32_t now,Reader read) {
        if(!supported_)return 0;
        if(phase_!=Phase::Idle && now-at_>=kClockExchangeMs)phase_=Phase::Idle;
        if(phase_==Phase::Query || phase_==Phase::Ack)return 0;
        if(phase_==Phase::Idle) {
            if(attempted_ && now-polled_<(have_baseline_?2000u:10000u))return 0;
            attempted_=true;polled_=now;
            const auto sample=read();
            if(have_baseline_ && !ClockChanged(sample,now))return 0;
            if(sequence_==UINT32_MAX)return 0;
            const auto size=std::snprintf(line,capacity,"BTCLOCK1 GET 1 %lu\n",(unsigned long)(sequence_+1));
            if(size<=0 || std::size_t(size)>=capacity)return 0;
            ++sequence_;phase_=Phase::Query;at_=now;
            return size;
        }
        // Sample again at submission, after the round trip and scheduler wait.
        const auto sample=read();
        if(!sample.valid) { phase_=Phase::Idle; return 0; }
        const auto size=std::snprintf(line,capacity,"BTCLOCK1 SET 1 %lu %lu %lu %u %u\n",
            (unsigned long)sequence_,(unsigned long)generation_,(unsigned long)(sample.milliseconds/1000),
            unsigned(sample.quality),unsigned(sample.offset+kClockOffsetLimit));
        if(size<=0 || std::size_t(size)>=capacity)return 0;
        submitted_=sample;submitted_at_=now;
        phase_=Phase::Ack;at_=now;
        return size;
    }
    bool Response(std::string_view line) {
        std::uint32_t fields[5];
        const auto parse=[&](const char* prefix) {
            return ParseClockFields(line,prefix,fields,5) &&
                fields[1]==sequence_ && fields[2] && fields[4]<=3;
        };
        if(phase_==Phase::Query && parse("BTCLOCK1 STATE ")) {
            generation_=fields[2];phase_=Phase::Set;return true;
        }
        if(phase_==Phase::Ack && (parse("BTCLOCK1 ACK ") || parse("BTCLOCK1 KEPT ")) &&
           fields[2]==generation_) {
            baseline_=submitted_;baseline_at_=submitted_at_;have_baseline_=true;
            phase_=Phase::Idle;return true;
        }
        return false;
    }
private:
    enum class Phase { Idle, Query, Set, Ack };
    bool ClockChanged(const OsClockSample& sample,std::uint32_t now) const {
        if(!sample.valid)return false;
        const auto correction=sample.milliseconds-baseline_.milliseconds-std::uint32_t(now-baseline_at_);
        return correction>=2000 || correction<=-2000 || sample.offset!=baseline_.offset ||
            sample.quality!=baseline_.quality;
    }
    OsClockSample baseline_{},submitted_{};
    std::uint32_t sequence_=0,generation_=0,at_=0,polled_=0,baseline_at_=0,submitted_at_=0;
    Phase phase_=Phase::Idle;
    bool supported_=false,have_baseline_=false,attempted_=false;
};
}

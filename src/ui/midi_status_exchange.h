#pragma once
// Local, versioned observation exchange. No route or transport commands.
#include "midi_health.h"
#include <sstream>
#include <string>
#include <filesystem>
#include <cstring>
namespace BroTracker {
struct MidiRouteObservation {
    std::uint64_t pid=0,start=0,at_ms=0;
    std::string boot,state,detail,usb_error,visibility;
    unsigned subscription=0;
};
inline bool ParseMidiRoute(const std::string& text,MidiRouteObservation& out) {
    if(text.size()>4096)return false;
    std::istringstream lines(text);std::string header,extra;MidiRouteObservation v;
    if(!std::getline(lines,header))return false;
    std::istringstream fields(header);std::string magic;unsigned version=0;
    if(!(fields>>magic>>version>>v.pid>>v.start>>v.boot>>v.at_ms>>v.state>>v.subscription) ||
        fields>>extra || magic!="BTMIDIROUTE" || version!=1 || !v.pid || v.pid>2147483647 ||
        !v.start || v.boot.size()!=36 || v.subscription>1 ||
        (v.state!="missing" && v.state!="ambiguous" && v.state!="connecting" && v.state!="connected" && v.state!="unavailable") ||
        (v.state=="connected" && !v.subscription))return false;
    for(auto item:{std::pair<const char*,std::string*>{"detail ",&v.detail},{"usb ",&v.usb_error},{"visibility ",&v.visibility}}) {
        std::string line;if(!std::getline(lines,line) || line.compare(0,std::strlen(item.first),item.first))return false;
        *item.second=line.substr(std::strlen(item.first));
        if(item.second->find_first_of("\r\t")!=std::string::npos)return false;
    }
    if(std::getline(lines,extra))return false;
    out=v;return true;
}
}

#ifdef __linux__
#include "host_logging.h"
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
namespace BroTracker {
class MidiStatusExchange {
    std::filesystem::path directory_;
    std::string boot_,start_,last_log_;
    int lock_=-1;std::uint64_t last_=0;bool ticked_=false;
    FILE* log_=nullptr;
    static std::string Read(const std::filesystem::path& path,unsigned limit=4096) {
        const int fd=open(path.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
        if(fd<0)return {};
        struct stat s{};std::string out;
        if(!fstat(fd,&s) && S_ISREG(s.st_mode) && s.st_uid==getuid() && s.st_size<=limit) {
            char bytes[4097];const auto n=read(fd,bytes,limit+1);
            if(n>=0 && unsigned(n)<=limit)out.assign(bytes,n);
        }
        close(fd);return out;
    }
    static std::string SystemRead(const std::filesystem::path& path) {
        FILE* f=std::fopen(path.c_str(),"r");if(!f)return {};
        char b[4096];const auto n=std::fread(b,1,sizeof(b),f);std::fclose(f);return std::string(b,n);
    }
    static std::string Start(const std::string& stat) {
        auto p=stat.rfind(") ");if(p==std::string::npos)return {};
        std::istringstream in(stat.substr(p+2));std::string token;
        for(unsigned i=0;i<=19;++i) {if(!(in>>token) || (!i && (token=="Z" || token=="X")))return {};}
        return token;
    }
    static std::string Token(const std::string& text) {std::istringstream in(text);std::string v;in>>v;return v;}
    static std::string OwnerStart(std::uint64_t pid) {
        const auto id=std::to_string(pid);
        // Thread-group leader: same kernel start time as /proc/PID/stat.
        // QEMU user mode synthesizes zero start times for its own /proc/PID/stat.
        return Start(SystemRead("/proc/"+id+"/task/"+id+"/stat"));
    }
    bool Publish(const HostMidiHealth& h,const char* transport,std::uint64_t now) {
        if(lock_<0)return false;
        std::ostringstream out;
        out<<"BTMIDIHEALTH 1 "<<getpid()<<' '<<start_<<' '<<boot_<<' '<<now<<' '
           <<unsigned(h.connected)<<' '<<unsigned(h.supported)<<' '<<unsigned(h.known)<<' '
           <<unsigned(h.configured)<<' '<<unsigned(h.fault)<<' '<<unsigned(h.cleanup)<<' '
           <<h.reason<<' '<<h.age_ms<<' '<<transport<<'\n';
        const auto temp=directory_/("btx-midi-health.next."+std::to_string(getpid()));
        const auto bytes=out.str();const int fd=open(temp.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);
        if(fd<0)return false;
        const bool ok=write(fd,bytes.data(),bytes.size())==ssize_t(bytes.size());close(fd);
        const bool installed=ok && !rename(temp.c_str(),(directory_/"btx-midi-health.state").c_str());
        if(!installed)unlink(temp.c_str());
        return installed;
    }
public:
    std::string route="H4 route: keeper status unavailable; launch MIDI Connect",usb;
    static std::uint64_t Now() {timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return std::uint64_t(t.tv_sec)*1000+t.tv_nsec/1000000;}
    explicit MidiStatusExchange(std::filesystem::path directory,FILE* log=nullptr):directory_(directory),log_(log) {
        boot_=Token(SystemRead("/proc/sys/kernel/random/boot_id"));
        start_=OwnerStart(getpid());
        if(directory_.empty() || boot_.empty() || start_.empty())return;
        std::error_code e;std::filesystem::create_directories(directory_,e);if(e)return;
        lock_=open((directory_/"btx-midi-health.lock").c_str(),O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
        if(lock_>=0 && flock(lock_,LOCK_EX|LOCK_NB)) {close(lock_);lock_=-1;}
    }
    MidiStatusExchange(const MidiStatusExchange&)=delete;
    ~MidiStatusExchange() {
        if(lock_>=0) {unlink((directory_/"btx-midi-health.state").c_str());close(lock_);}
    }
    void Tick(const HostMidiHealth& health,const char* transport,std::uint64_t now) {
        if(directory_.empty())return;
        if(ticked_ && now-last_<1000)return;ticked_=true;last_=now;
        const bool published=Publish(health,transport,now);
        MidiRouteObservation v;
        route="H4 route: status missing; check MIDI Connect";usb.clear();
        const auto text=Read(directory_/"midi-connect-status.btx");
        if(!text.empty()) {
            if(!ParseMidiRoute(text,v))route="H4 route: keeper status invalid; delivery unverified";
            else if(v.boot!=boot_ || OwnerStart(v.pid)!=std::to_string(v.start) ||
                    v.at_ms>now || now-v.at_ms>20000)route="H4 route: keeper stopped/stale; delivery unverified";
            else {
                route="H4 route: "+std::string(v.subscription?"subscription present":v.state)+"; delivery unverified";
                if(v.visibility.find("reduced")==0)route+="; reduced USB visibility";
                if(!v.usb_error.empty())usb="Route USB error: "+v.usb_error.substr(0,160);
            }
        }
        if(!published)route+="; BTX health publisher unavailable";
        const auto summary=route+" "+usb;
        if(log_ && summary!=last_log_)HostLog(log_,HostLogLevel::Basic,"MIDI exchange",summary.c_str());
        last_log_=summary;
    }
};
}
#endif

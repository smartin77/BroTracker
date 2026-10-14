#include "host_logging.h"
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <mutex>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
#endif
namespace BroTracker {
namespace {
std::atomic<HostLogMode> mode{HostLogMode::Basic};
std::mutex lock;
struct Last { FILE* file=nullptr; char source[64]{}; char message[512]{}; } last[16];
unsigned replacement=0;
bool Contains(std::string_view s,std::string_view needle) { return s.find(needle)!=s.npos; }
std::filesystem::path Archive(const std::filesystem::path& p,unsigned i) {
    return p.parent_path()/(p.stem().wstring()+L"("+std::to_wstring(i)+L")"+p.extension().wstring());
}
bool Utc(std::time_t time,std::tm& out) {
#ifdef _WIN32
    return gmtime_s(&out,&time)==0;
#else
    return gmtime_r(&time,&out)!=nullptr;
#endif
}
}
void ConfigureHostLogging(HostLogMode value) noexcept {
    std::lock_guard<std::mutex> guard(lock); mode=value;
    for(auto& entry:last) { entry={}; }
    replacement=0;
}
bool HostDebugLogging() noexcept { return mode==HostLogMode::Debug; }
HostLogLevel ClassifyHostDiagnostic(std::string_view source,std::string_view message) noexcept {
    for(auto token:{"error","Error","fault","disabled","Invalid"})
        if(Contains(source,token)) return HostLogLevel::Basic;
    // Explicit payload filtering precedes general lifecycle/error classification.
    for(auto token:{" CELL "," POS "," PERF ","local draft changed:","flow running="})
        if(Contains(message,token)) return HostLogLevel::Debug;
    for(auto token:{"error","ERROR","failed","failure","fault","rejected","REJECTED","invalid","Invalid",
        "ignored","suppressed","confirmed","mismatch","Mismatch","Device identities reset","Draft not applied","unavailable","disabled","timeout","denied","disconnected","recover","uncertain",
        "waiting","synchronized"," APPLIED "," SUPERSEDED ","connected:","opened:","opened identified","routing started","STATE PLAYING","STATE IDLE",
        "STATE PAUSED","STATE ERROR","STARTED","STOPPED","CONTINUED","BTTEST1 PAUSED","waiting:"})
        if(Contains(message,token)) return HostLogLevel::Basic;
    if(source=="Terminal" || source=="Configuration" || source=="Logging" || source=="Transport input") return HostLogLevel::Basic;
    return HostLogLevel::Debug;
}
bool FormatHostTimestamp(std::int64_t ms,int offset,std::uint64_t mono,char* out,std::size_t cap) noexcept {
    if(!out || !cap || ms<0 || ms>253402300799999LL || offset < -1440 || offset >1440) return false;
    std::tm utc{},local{};
    const auto seconds=static_cast<std::time_t>(ms/1000);
    if(static_cast<std::int64_t>(seconds)!=ms/1000 || !Utc(seconds,utc) || !Utc(seconds+offset*60,local)) return false;
    const unsigned absolute=static_cast<unsigned>(offset<0 ? -offset : offset);
    const int n=std::snprintf(out,cap,"utc=%04d-%02d-%02dT%02d:%02d:%02d.%03dZ local=%04d-%02d-%02dT%02d:%02d:%02d.%03d%c%02u:%02u mono_ms=%llu",
        utc.tm_year+1900,utc.tm_mon+1,utc.tm_mday,utc.tm_hour,utc.tm_min,utc.tm_sec,int(ms%1000),
        local.tm_year+1900,local.tm_mon+1,local.tm_mday,local.tm_hour,local.tm_min,local.tm_sec,int(ms%1000),
        offset<0?'-':'+',absolute/60,absolute%60,static_cast<unsigned long long>(mono));
    return n>=0 && static_cast<std::size_t>(n)<cap;
}
void HostLog(FILE* file,HostLogLevel level,const char* source,const char* message) noexcept {
    if(!file || !source || !message || (level==HostLogLevel::Debug && !HostDebugLogging())) return;
    std::lock_guard<std::mutex> guard(lock);
    // Suppress unchanged waiting/error/state observations; transitions replace them.
    if(level==HostLogLevel::Basic) {
        Last* entry=nullptr;
        for(auto& value:last) if(value.file==file && !std::strcmp(value.source,source)) {entry=&value; break;}
        const char* comparison=message;
        if(!std::strncmp(message,"[host_ms=",9)) {const auto end=std::strstr(message,"] "); if(end) comparison=end+2;}
        const bool repetitive=Contains(comparison,"waiting") || Contains(comparison,"error") || Contains(comparison,"failed") || Contains(comparison,"STATE ") || Contains(comparison,"ERROR") || Contains(comparison,"fault");
        if(entry && repetitive && !std::strcmp(entry->message,comparison)) return;
        if(!entry) entry=&last[replacement++%16];
        entry->file=file; std::snprintf(entry->source,sizeof(entry->source),"%s",source);
        std::snprintf(entry->message,sizeof(entry->message),"%s",comparison);
    }
    const auto now=std::chrono::system_clock::now();
    const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    const auto mono=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const auto time=std::chrono::system_clock::to_time_t(now); std::tm local{},utc{};
#ifdef _WIN32
    localtime_s(&local,&time);
#else
    localtime_r(&time,&local);
#endif
    int offset=0;
    if(Utc(time,utc)) {utc.tm_isdst=local.tm_isdst; offset=int(std::difftime(std::mktime(&local),std::mktime(&utc))/60);}
    char timestamp[192];
    if(!FormatHostTimestamp(ms,offset,static_cast<std::uint64_t>(mono),timestamp,sizeof(timestamp))) std::strcpy(timestamp,"timestamp unavailable");
    std::fprintf(file,"[%s] %s: %s\n",timestamp,source,message); std::fflush(file);
}
void HostLogf(FILE* f,HostLogLevel level,const char* source,const char* format,...) noexcept {
    if(!f || (level==HostLogLevel::Debug && !HostDebugLogging())) return;
    char text[2048]; va_list args; va_start(args,format); const int n=std::vsnprintf(text,sizeof(text),format,args); va_end(args);
    if(n<0 || static_cast<std::size_t>(n)>=sizeof(text)) {HostLog(f,HostLogLevel::Basic,"Logging","Diagnostic exceeded bounded format buffer; omitted"); return;}
    HostLog(f,level,source,text);
}
HostLogOpenPlan PrepareHostLog(const std::filesystem::path& path,const HostConfig& config,bool append) noexcept {
    if(append) return {true,"BROTRACKER_APPEND_LOG=1: append enabled; startup rotation skipped"};
    if(config.logging_mode!=HostLogMode::Debug || !config.preserve_last) return {};
    const auto failed=HostLogOpenPlan{true,"Startup rotation failed; preserving current log with append fallback. Check log directory permissions/archive types"};
    try {
        if(config.preserve_last>20 || !path.is_absolute()) return failed;
        // Preflight prevents directories/symlinks being overwritten or followed.
        for(unsigned i=0;i<=20;++i) {
            const auto item=i?Archive(path,i):path;
            const auto status=std::filesystem::symlink_status(item);
            if(std::filesystem::exists(status) && !std::filesystem::is_regular_file(status)) {
                if(i==0) return {true,"Current log is not a regular file; using stderr without touching it. Check log path",true};
                return failed;
            }
        }
        if(!std::filesystem::exists(path)) {
            for(unsigned i=config.preserve_last+1;i<=20;++i) std::filesystem::remove(Archive(path,i));
            return {false,"No current log to rotate; existing archives retained within limit"};
        }
        // Descending same-directory moves retain file identity and timestamps.
        // All pruning occurs before moving current; a failure therefore leaves
        // unarchived current available for the append fallback.
        std::filesystem::remove(Archive(path,config.preserve_last));
        for(unsigned i=config.preserve_last+1;i<=20;++i) std::filesystem::remove(Archive(path,i));
        for(unsigned i=config.preserve_last;i>1;--i) {
            const auto from=Archive(path,i-1);
            if(std::filesystem::exists(from)) std::filesystem::rename(from,Archive(path,i));
        }
        // Last potentially failing file operation. Open a new current only on
        // successful return; no timestamp rewriting or copy fallback.
        std::filesystem::rename(path,Archive(path,1));
        return {false,"Debug startup rotation completed"};
    } catch(...) {return failed;}
}
void LogHostStartup(FILE* file,const char* platform,const HostConfigResolution& config,
    const std::filesystem::path& path,const HostLogOpenPlan& plan) noexcept {
    HostLogf(file,HostLogLevel::Basic,"Terminal","BTX application start; platform=%s; build=%s %s; device firmware build identity unavailable",platform,__DATE__,__TIME__);
    HostLogf(file,HostLogLevel::Basic,"Configuration","%s; path=%s; new_pattern_length=%u",HostConfigDiagnostic(config.status),path.u8string().c_str(),config.config.new_pattern_length);
    HostLogf(file,HostLogLevel::Basic,"Logging","mode=%s preserve_last=%u; %s; timestamps describe host observation, not physical audio timing",
        HostDebugLogging()?"debug":"basic",config.config.preserve_last,plan.diagnostic);
}
void LogHostSystem(FILE* file) noexcept {
    if(!HostDebugLogging()) return;
#ifdef _WIN32
    SYSTEM_INFO info{}; GetNativeSystemInfo(&info);
    MEMORYSTATUSEX memory{}; memory.dwLength=sizeof(memory);
    const bool have_memory=GlobalMemoryStatusEx(&memory)!=0;
    OSVERSIONINFOW version{}; version.dwOSVersionInfoSize=sizeof(version);
    using Version=LONG(WINAPI*)(OSVERSIONINFOW*);
    const auto address=GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion");
    Version fn=nullptr;
    // Windows GetProcAddress supplies an ABI-compatible code pointer; copying
    // its representation avoids a compiler-specific incompatible-function cast.
    static_assert(sizeof(fn)==sizeof(address),"Windows function pointer sizes must agree");
    std::memcpy(&fn,&address,sizeof(fn));
    if(fn && fn(&version)==0) HostLogf(file,HostLogLevel::Debug,"Host system","Windows %lu.%lu build %lu",version.dwMajorVersion,version.dwMinorVersion,version.dwBuildNumber);
    else HostLog(file,HostLogLevel::Debug,"Host system","Windows version unavailable");
    HostLogf(file,HostLogLevel::Debug,"Host system","native_architecture=%u process_bits=%u CPU_count=%lu",info.wProcessorArchitecture,unsigned(sizeof(void*)*8),info.dwNumberOfProcessors);
    if(have_memory) HostLogf(file,HostLogLevel::Debug,"Host system","physical_memory_total=%llu available=%llu bytes",memory.ullTotalPhys,memory.ullAvailPhys);
    else HostLog(file,HostLogLevel::Debug,"Host system","Physical memory information unavailable");
#else
    utsname info{};
    if(uname(&info)==0) HostLogf(file,HostLogLevel::Debug,"Host system","OS=%s release=%s architecture=%s process_bits=%u",info.sysname,info.release,info.machine,unsigned(sizeof(void*)*8));
    else HostLog(file,HostLogLevel::Debug,"Host system","OS/architecture information unavailable");
    const long cpus=sysconf(_SC_NPROCESSORS_ONLN),pages=sysconf(_SC_PHYS_PAGES),page_size=sysconf(_SC_PAGESIZE);
    HostLogf(file,HostLogLevel::Debug,"Host system","CPU_count=%ld (negative means unavailable)",cpus);
    if(pages>0 && page_size>0) HostLogf(file,HostLogLevel::Debug,"Host system","physical_memory_total=%llu bytes; available memory not queried",static_cast<unsigned long long>(pages)*page_size);
    else HostLog(file,HostLogLevel::Debug,"Host system","Physical memory information unavailable");
#endif
}
}

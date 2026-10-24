#pragma once
#include <sd_access.h>
#include "bringup/pattern_file_store.h"
namespace BroTracker {
// Same initialized SD/SdFat device and D0046 guard as WAV preparation/probes.
// FsFile avoids the heap-allocated SDFile wrapper in SD.open(). Main context only.
class SdPatternFiles {
public:
    bool ready=false;
    bool Acquire(bool write) {return ready && (write?SdAccess::AcquireWrite():SdAccess::AcquireRead());}
    void Release(bool write) {if(write) SdAccess::ReleaseWrite(); else SdAccess::ReleaseRead();}
    bool Exists(const char* path) {return SD.sdfs.exists(path);}
    bool RegularFile(const char* path) {
        auto f=SD.sdfs.open(path,O_RDONLY);if(!f) return false;
        const bool regular=!f.isDirectory();const bool closed=f.close();return regular && closed;
    }
    bool Directory(const char* path) {
        auto f=SD.sdfs.open(path,O_RDONLY);
        if(f) {const bool dir=f.isDirectory();const bool closed=f.close();return dir && closed;}
        return SD.sdfs.mkdir(path);
    }
    PatternFileResult Read(const char* path,std::uint8_t* bytes,std::size_t capacity,std::size_t& size) {
        auto f=SD.sdfs.open(path,O_RDONLY);
        if(!f) return PatternFileResult::Io;
        const auto length=f.size();
        if(f.isDirectory() || length>capacity || length<kPatternFileHeaderSize) {f.close();return PatternFileResult::Invalid;}
        const bool read=f.read(bytes,static_cast<std::size_t>(length))==static_cast<int>(length);
        const bool closed=f.close();
        if(!read || !closed) return PatternFileResult::Io;
        size=static_cast<std::size_t>(length); return PatternFileResult::Ok;
    }
    bool WriteNew(const char* path,const std::uint8_t* bytes,std::size_t size) {
        auto f=SD.sdfs.open(path,O_WRONLY|O_CREAT|O_EXCL);
        if(!f) return false;
        const bool written=f.write(bytes,size)==size;
        const bool synced=f.sync();const bool closed=f.close();
        return written && synced && closed;
    }
    bool Verify(const char* path,const std::uint8_t* bytes,std::size_t size) {
        auto f=SD.sdfs.open(path,O_RDONLY); if(!f) return false;
        bool ok=!f.isDirectory() && f.size()==size; std::uint8_t block[64];
        for(std::size_t at=0;ok && at<size;at+=sizeof(block)) {
            const auto n=size-at<sizeof(block)?size-at:sizeof(block);
            ok=f.read(block,n)==static_cast<int>(n) && !std::memcmp(block,bytes+at,n);
        }
        const bool closed=f.close();return ok && closed;
    }
    bool Rename(const char* from,const char* to) {return !Exists(to) && SD.sdfs.rename(from,to);}
    bool Remove(const char* path) {return SD.sdfs.remove(path);}
};
}

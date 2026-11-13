#pragma once
#include <sd_access.h>
#include "bringup/pattern_file_store.h"
#include "bringup/wav_directory.h"
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
    bool NextPatternName(std::uint32_t& offset,char* name,std::size_t capacity,bool& end) {
        return NextDirectoryName("/BroTracker/development",offset,name,capacity,end);
    }
    WavFileFacts ReadWavFacts(const char* directory,const char* name) {
        char path[322];std::snprintf(path,sizeof(path),"%s%s%s",directory,std::strcmp(directory,"/")?"/":"",name);
        WavFileFacts facts;facts.known=true;
        // The caller owns the shared read lease. FsFile keeps the existing
        // allocation-free directory adapter; validation is the playback validator.
        class Reader final:public WavBufferReader {
        public:
            FsFile file;
            bool Size(std::uint64_t& n)noexcept override {n=file.size();return true;}
            bool Seek(std::uint64_t n)noexcept override{return n<=UINT32_MAX && file.seekSet(n);}
            std::size_t Read(std::uint8_t* data,std::size_t n)noexcept override {const int read=file.read(data,n);return read<0?0:static_cast<std::size_t>(read);}
        } reader;
        if(!reader.file.open(path,O_RDONLY))return facts;
        if(reader.file.isDirectory()){reader.file.close();return facts;}
        WavMetadata metadata;facts.status=InspectWavMetadata(reader,metadata);
        facts.format=metadata.format;facts.channels=metadata.channels;facts.bits=metadata.bits;facts.sample_rate=metadata.sample_rate;
        if(!reader.file.close())facts.status=WavBufferStatus::IoError;
        return facts;
    }
    WavDirectoryResult ReadDirectoryStatus(const char* path) {
        auto f=SD.sdfs.open(path,O_RDONLY);
        if(!f)return SD.sdfs.sdErrorCode()?WavDirectoryResult::ReadError:Exists(path)?WavDirectoryResult::ReadError:WavDirectoryResult::Missing;
        const bool directory=f.isDirectory();const bool closed=f.close();
        return !closed?WavDirectoryResult::ReadError:directory?WavDirectoryResult::Ok:WavDirectoryResult::NotDirectory;
    }
    bool NextDirectoryName(const char* directory,std::uint32_t& offset,char* name,std::size_t capacity,bool& end) {
        bool is_directory=false;const bool ok=NextDirectoryEntry(directory,offset,name,capacity,end,is_directory);
        if(is_directory)name[0]=0;return ok;
    }
    bool NextDirectoryEntry(const char* directory,std::uint32_t& offset,char* name,std::size_t capacity,bool& end,bool& is_directory) {
        auto dir=SD.sdfs.open(directory,O_RDONLY);
        if(!dir || !dir.isDirectory() || !dir.seekSet(offset)) {if(dir)dir.close();return false;}
        FsFile file;end=!file.openNext(&dir,O_RDONLY);
        if(end) {const bool ok=!dir.getError();return dir.close() && ok;}
        const auto position=dir.curPosition();
        name[0]=0;
        is_directory=file.isDirectory();
        const auto n=file.getName(name,capacity);
        if(!n || n>=capacity-1)name[0]=0;
        const bool closed=file.close();const bool dir_closed=dir.close();
        if(position>UINT32_MAX || position<=offset)return false;
        offset=static_cast<std::uint32_t>(position);return closed && dir_closed;
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

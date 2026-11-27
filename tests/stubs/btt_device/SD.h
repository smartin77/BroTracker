#pragma once
// Complete positional file fake for the diagnostic entry-point simulator only.
// Other fake-SD targets keep their established isolated headers.
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>
constexpr int O_RDONLY=0,O_WRONLY=1,O_CREAT=2,O_EXCL=4,O_RDWR=8,FILE_READ=0,FILE_WRITE=1,BUILTIN_SDCARD=254;
namespace DeviceSd {
struct Entry {bool directory=false;std::vector<uint8_t> bytes;unsigned opens=0;};
inline std::map<std::string,Entry> files;
inline unsigned begin_calls=0,open_calls=0,write_calls=0,created=0,removed=0;
inline unsigned rejected_seeks=0;
inline uint64_t rejected_offset=0,rejected_size=0;
inline bool fail_write=false;
inline std::string fail_write_path,fail_close_path;
inline std::string fail_read_path,fail_sync_path,fail_remove_path;
// Optional actual SdFat failure shape: inner close shuts the file even when
// sync fails, while FsBaseFile retains its pointer/isOpen() result on failure.
inline bool close_failure_closes_inner=false;
inline std::string Path(const char* p){return p[0]=='/'?std::string(p):"/"+std::string(p);}
}
class FsFile {
    DeviceSd::Entry* entry_=nullptr;size_t at_=0;bool writable_=false,inner_closed_=false;std::string path_;
public:
    FsFile()=default;
    FsFile(const FsFile&)=delete;
    FsFile(FsFile&& other)noexcept{*this=std::move(other);}
    FsFile& operator=(FsFile&& other)noexcept{entry_=other.entry_;at_=other.at_;writable_=other.writable_;inner_closed_=other.inner_closed_;path_=std::move(other.path_);other.entry_=nullptr;return *this;}
    ~FsFile(){if(entry_)(void)close();}
    bool isOpen()const{return entry_!=nullptr;}
    explicit operator bool()const{return isOpen();}
    bool open(const char* p,int flags){
        ++DeviceSd::open_calls;if(entry_ && !close())return false;path_=DeviceSd::Path(p);
        auto found=DeviceSd::files.find(path_);
        if((flags&O_EXCL) && found!=DeviceSd::files.end())return false;
        if(found==DeviceSd::files.end()){
            if(!(flags&O_CREAT))return false;
            found=DeviceSd::files.emplace(path_,DeviceSd::Entry{}).first;++DeviceSd::created;
        }
        entry_=&found->second;++entry_->opens;at_=0;inner_closed_=false;writable_=(flags&(O_WRONLY|O_RDWR))!=0;return true;
    }
    bool isDirectory()const{return entry_ && entry_->directory;}
    size_t size()const{return entry_?entry_->bytes.size():0;}
    // Installed SdFat FAT and exFAT reject seeks past logical EOF even O_RDWR.
    bool seekSet(uint64_t n){if(!entry_ || inner_closed_ || n>size()){
        ++DeviceSd::rejected_seeks;DeviceSd::rejected_offset=n;DeviceSd::rejected_size=size();return false;
    }at_=size_t(n);return true;}
    bool seek(uint32_t n){return seekSet(n);}
    uint32_t curPosition()const{return at_;}uint32_t position()const{return at_;}
    int available()const{return int(size()-std::min(size(),at_));}
    unsigned getError()const{return 0;}
    int read(void* p,size_t n){if(!entry_ || path_==DeviceSd::fail_read_path)return -1;n=std::min(n,size()-std::min(size(),at_));if(n)std::memcpy(p,entry_->bytes.data()+at_,n);at_+=n;return int(n);}
    size_t write(const void* p,size_t n){++DeviceSd::write_calls;if(!entry_ || !writable_ || DeviceSd::fail_write || path_==DeviceSd::fail_write_path)return 0;
        if(at_+n>entry_->bytes.size())entry_->bytes.resize(at_+n);std::memcpy(entry_->bytes.data()+at_,p,n);at_+=n;return n;}
    bool truncate(uint32_t n){if(!entry_ || !writable_ || n>size())return false;entry_->bytes.resize(n);at_=n;return true;}
    bool sync(){return entry_!=nullptr && path_!=DeviceSd::fail_sync_path;}
    bool close(){
        if(!entry_)return false;
        if(inner_closed_){entry_=nullptr;return true;}
        if(!DeviceSd::fail_close_path.empty() && path_==DeviceSd::fail_close_path){
            if(DeviceSd::close_failure_closes_inner){--entry_->opens;inner_closed_=true;writable_=false;}
            return false;
        }
        --entry_->opens;entry_=nullptr;return true;
    }
    void flush(){}void print(const char*){}void println(const char*){}void println(){}
    FsFile openNextFile(){return {};}
};
using File=FsFile;
struct DeviceFs {
    bool exists(const char* p){return DeviceSd::files.count(DeviceSd::Path(p));}
    bool mkdir(const char* p,bool=true){const auto name=DeviceSd::Path(p);if(exists(p))return DeviceSd::files[name].directory;DeviceSd::files[name].directory=true;return true;}
    bool remove(const char* p){++DeviceSd::removed;if(DeviceSd::Path(p)==DeviceSd::fail_remove_path)return false;return DeviceSd::files.erase(DeviceSd::Path(p));}
};
struct DeviceSD {
    DeviceFs sdfs;
    bool begin(int){++DeviceSd::begin_calls;return true;}
    File open(const char* path,int flags){File file;file.open(path,flags);return file;}
};
inline DeviceSD SD;

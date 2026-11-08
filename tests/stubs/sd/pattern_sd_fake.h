#pragma once
#include <map>
#include <string>
#include <vector>
#include <cstring>
constexpr int O_RDONLY=0,O_WRONLY=1,O_CREAT=2,O_EXCL=4;
namespace FakePatternSd {
struct Entry {bool directory=false;std::vector<std::uint8_t> bytes;};
inline std::map<std::string,Entry> files;
inline bool short_write=false,short_read=false,fail_sync=false,fail_close=false,fail_open=false,corrupt_read=false,fail_mkdir=false,fail_remove=false;
inline unsigned rename_calls=0,fail_rename_mask=0;
inline void (*on_io)()=nullptr;
struct IoCall {std::string path;std::uint64_t offset;std::size_t count;};
inline std::vector<IoCall> seeks,reads;
inline std::string fail_seek_path,negative_read_path,short_read_path;
inline void (*on_seek)()=nullptr;
inline void Reset(){files.clear();short_write=short_read=fail_sync=fail_close=fail_open=corrupt_read=fail_mkdir=fail_remove=false;rename_calls=fail_rename_mask=0;on_io=nullptr;
    seeks.clear();reads.clear();fail_seek_path.clear();negative_read_path.clear();short_read_path.clear();on_seek=nullptr;}
}
class FsFile {
    FakePatternSd::Entry* entry_=nullptr;std::size_t at_=0;std::string path_;
public:
    FsFile()=default;explicit FsFile(FakePatternSd::Entry* e,std::string path={}):entry_(e),path_(std::move(path)){}
    bool open(const char* path,int flags) {
        close();
        const auto it=FakePatternSd::files.find(path);
        if(FakePatternSd::fail_open || flags!=O_RDONLY || it==FakePatternSd::files.end())return false;
        entry_=&it->second;path_=path;at_=0;return true;
    }
    explicit operator bool() const {return entry_!=nullptr;}
    bool isDirectory() const {return entry_->directory;}
    std::size_t size() const {return entry_->bytes.size();}
    int read(void* p,std::size_t n){
        FakePatternSd::reads.push_back({path_,at_,n});
        if(FakePatternSd::on_io)FakePatternSd::on_io();
        if(!entry_ || (!FakePatternSd::negative_read_path.empty() && path_==FakePatternSd::negative_read_path))return -1;
        n=std::min(n,at_<entry_->bytes.size()?entry_->bytes.size()-at_:0);
        if((FakePatternSd::short_read || (!FakePatternSd::short_read_path.empty() && path_==FakePatternSd::short_read_path)) && n)--n;
        if(n)std::memcpy(p,entry_->bytes.data()+at_,n);
        at_+=n;if(FakePatternSd::corrupt_read && n)static_cast<std::uint8_t*>(p)[0]^=1;return static_cast<int>(n);
    }
    std::size_t write(const void* p,std::size_t n){if(FakePatternSd::on_io)FakePatternSd::on_io();if(FakePatternSd::short_write && n)--n;const auto* b=static_cast<const std::uint8_t*>(p);entry_->bytes.assign(b,b+n);return n;}
    bool seekSet(std::uint64_t n){
        FakePatternSd::seeks.push_back({path_,n,0});
        if(FakePatternSd::on_seek)FakePatternSd::on_seek();
        if(!entry_ || (!FakePatternSd::fail_seek_path.empty() && path_==FakePatternSd::fail_seek_path))return false;
        if(entry_->directory ? n%32!=0 : n>entry_->bytes.size())return false;
        at_=static_cast<std::size_t>(n);return true;
    }
    std::uint32_t curPosition()const{return static_cast<std::uint32_t>(at_);}
    unsigned getError()const{return 0;}
    bool openNext(FsFile* dir,int) {
        unsigned index=0;
        for(auto& item:FakePatternSd::files) {
            std::string prefix=dir->path_;if(prefix.back()!='/')prefix+='/';
            if(item.first.rfind(prefix,0)!=0 || item.first.substr(prefix.size()).find('/')!=std::string::npos)continue;
            if(index++<dir->at_/32)continue;
            dir->at_+=32;entry_=&item.second;path_=item.first;return true;
        }return false;
    }
    std::size_t getName(char* out,std::size_t cap){const auto name=path_.substr(path_.find_last_of('/')+1);if(cap){std::strncpy(out,name.c_str(),cap-1);out[cap-1]=0;}return name.size();}
    bool sync(){return !FakePatternSd::fail_sync;}
    bool close(){entry_=nullptr;return !FakePatternSd::fail_close;}
};
struct FakePatternFs {
    bool exists(const char* p){return FakePatternSd::files.count(p)!=0;}
    FsFile open(const char* p,int flags){
        if(FakePatternSd::fail_open)return {};
        auto it=FakePatternSd::files.find(p);
        if((flags&O_EXCL) && it!=FakePatternSd::files.end())return {};
        if(it==FakePatternSd::files.end()) {if(!(flags&O_CREAT))return {};it=FakePatternSd::files.emplace(p,FakePatternSd::Entry{}).first;}
        return FsFile(&it->second,p);
    }
    bool mkdir(const char* p){if(FakePatternSd::fail_mkdir)return false;FakePatternSd::files[p].directory=true;return true;}
    bool remove(const char* p){if(FakePatternSd::fail_remove)return false;return FakePatternSd::files.erase(p)!=0;}
    bool rename(const char* a,const char* b){++FakePatternSd::rename_calls;if((FakePatternSd::fail_rename_mask&(1u<<FakePatternSd::rename_calls)) || exists(b) || !exists(a))return false;FakePatternSd::files[b]=FakePatternSd::files[a];FakePatternSd::files.erase(a);return true;}
};

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
inline void Reset(){files.clear();short_write=short_read=fail_sync=fail_close=fail_open=corrupt_read=fail_mkdir=fail_remove=false;rename_calls=fail_rename_mask=0;on_io=nullptr;}
}
class FsFile {
    FakePatternSd::Entry* entry_=nullptr;std::size_t at_=0;
public:
    FsFile()=default;explicit FsFile(FakePatternSd::Entry* e):entry_(e){}
    explicit operator bool() const {return entry_!=nullptr;}
    bool isDirectory() const {return entry_->directory;}
    std::size_t size() const {return entry_->bytes.size();}
    int read(void* p,std::size_t n){if(FakePatternSd::on_io)FakePatternSd::on_io();n=std::min(n,entry_->bytes.size()-at_);if(FakePatternSd::short_read && n)--n;std::memcpy(p,entry_->bytes.data()+at_,n);at_+=n;if(FakePatternSd::corrupt_read && n)static_cast<std::uint8_t*>(p)[0]^=1;return static_cast<int>(n);}
    std::size_t write(const void* p,std::size_t n){if(FakePatternSd::on_io)FakePatternSd::on_io();if(FakePatternSd::short_write && n)--n;const auto* b=static_cast<const std::uint8_t*>(p);entry_->bytes.assign(b,b+n);return n;}
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
        return FsFile(&it->second);
    }
    bool mkdir(const char* p){if(FakePatternSd::fail_mkdir)return false;FakePatternSd::files[p].directory=true;return true;}
    bool remove(const char* p){if(FakePatternSd::fail_remove)return false;return FakePatternSd::files.erase(p)!=0;}
    bool rename(const char* a,const char* b){++FakePatternSd::rename_calls;if((FakePatternSd::fail_rename_mask&(1u<<FakePatternSd::rename_calls)) || exists(b) || !exists(a))return false;FakePatternSd::files[b]=FakePatternSd::files[a];FakePatternSd::files.erase(a);return true;}
};

#pragma once
// Private immutable-backing SD adapter for developer loading and diagnostics.
#include "backing.h"
#include <sd_access.h>

namespace btt::prototype {
class FsBackend {
    template<class> friend class Backings;
    friend class FsSave;
    bool ready_;
    class Lease {
        bool write_,held_;
    public:
        explicit Lease(bool write,bool ready) noexcept:write_(write),held_(ready && (write?BroTracker::SdAccess::AcquireWrite():BroTracker::SdAccess::AcquireRead())){}
        ~Lease() {if(held_) {if(write_)BroTracker::SdAccess::ReleaseWrite();else BroTracker::SdAccess::ReleaseRead();}}
        explicit operator bool() const noexcept {return held_;}
    };
    using Handle=FsFile;
    bool Open(const Handle& h) const noexcept {return h.isOpen();}
    // SdFat FAT/exFAT seekSet cannot create holes. Extend only by actual bounded
    // writes from logical EOF; preallocation alone does not grow valid length.
    BTT_FLASHMEM("fs_grow") bool Grow(Handle& h,uint32_t length) noexcept {
        const auto current=h.size();
        if(current>=length)return true;
        if(!h.seekSet(current))return false;
        const uint8_t zeros[128]{};
        uint32_t remaining=length-static_cast<uint32_t>(current);
        while(remaining) {
            const auto n=std::min<uint32_t>(remaining,sizeof(zeros));
            if(h.write(zeros,n)!=n || h.getError())return false;
            remaining-=n;
        }
        return h.size()==length;
    }
    bool Create(Handle& h,const char* path) noexcept {
        Lease lease(true,ready_);
        return lease && !h.isOpen() && h.open(path,O_RDWR|O_CREAT|O_EXCL);
    }
    bool ReadOpen(Handle& h,const char* path,uint32_t& length) noexcept {
        Lease lease(false,ready_);
        if(!lease || h.isOpen() || !h.open(path,O_RDONLY))return false;
        const auto bytes=h.size();
        if(h.isDirectory() || bytes>1048576)return false; // Registry performs checked cleanup.
        length=static_cast<uint32_t>(bytes);return true;
    }
    bool Read(Handle& h,uint32_t at,void* p,unsigned n) noexcept {
        Lease lease(false,ready_);
        return lease && h.isOpen() && n<=512 && at<=h.size() && n<=h.size()-at &&
            h.seekSet(at) && h.read(p,n)==static_cast<int>(n) && !h.getError();
    }
    bool Write(Handle& h,uint32_t at,const void* p,unsigned n) noexcept {
        Lease lease(true,ready_);
        return lease && h.isOpen() && n<=512 && at<=1048576 && n<=1048576-at &&
            Grow(h,at) && h.seekSet(at) && h.write(p,n)==n && !h.getError();
    }
    BTT_FLASHMEM("fs_resize") bool Resize(Handle& h,uint32_t n) noexcept {
        Lease lease(true,ready_);
        if(!lease || !h.isOpen() || n>1048576)return false;
        if(n<=h.size())return h.truncate(n);
        return Grow(h,n);
    }
    bool Sync(Handle& h) noexcept {Lease lease(true,ready_);return lease && h.isOpen() && h.sync();}
    bool Close(Handle& h) noexcept {
        if(!h.isOpen())return true;
        Lease lease(true,ready_);return lease && h.close();
    }
    bool Remove(const char* path) noexcept {Lease lease(true,ready_);return lease && SD.sdfs.remove(path);}
public:
    explicit FsBackend(bool ready=false) noexcept:ready_(ready){}
};
using FsBackings=Backings<FsBackend>;
}

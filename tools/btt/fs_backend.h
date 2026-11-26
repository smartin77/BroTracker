#pragma once
// Developer-only adapter; include/instantiate only in isolated ARM diagnostics.
#include "backing.h"
#include <sd_access.h>

namespace btt::prototype {
class FsBackend {
    template<class> friend class Backings;
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
            h.seekSet(at) && h.write(p,n)==n && !h.getError();
    }
    bool Resize(Handle& h,uint32_t n) noexcept {
        Lease lease(true,ready_);
        if(!lease || !h.isOpen() || n>1048576)return false;
        if(n<=h.size())return h.truncate(n);
        const uint8_t zero=0;
        return h.seekSet(n-1) && h.write(&zero,1)==1 && !h.getError();
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

#pragma once
// Developer-only bounded object arena. Main/foreground owner, no ISR access.
#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace btt::prototype {
enum class Code { Ok, Invalid, Capacity, Busy, Stale, Io };
class Arena;
template<class T> class Object {
    friend class Arena;
    Arena* arena_=nullptr;
    unsigned slot_=0;
    uint32_t generation_=0;
public:
    T* get() const noexcept;
    T* operator->() const noexcept {return get();}
    T& operator*() const noexcept {return *get();}
};
class Arena {
    struct Record {
        void* object=nullptr;
        void (*destroy)(void*) noexcept=nullptr;
        bool (*can_destroy)(const void*) noexcept=nullptr;
        uint32_t generation=0;
    };
    std::array<Record,16> records_{};
    unsigned char* bytes_=nullptr;
    size_t capacity_=0,used_=0;
    uint32_t next_=1;
    template<class T,class=void> struct Guard {
        static bool Check(const void*) noexcept {return true;}
    };
    template<class T> struct Guard<T,std::void_t<decltype(std::declval<const T&>().CanDestroy())>> {
        static bool Check(const void* p) noexcept {return static_cast<const T*>(p)->CanDestroy();}
    };
public:
    Arena() noexcept = default;
    Arena(const Arena&)=delete;
    Arena& operator=(const Arena&)=delete;
    // Arena metadata and its storage must outlive all Object handles. Destroy
    // explicitly; no implicit reset can invalidate pinned backing owners.
    Code Init(void* bytes,size_t capacity) noexcept {
        for(const auto& r:records_)if(r.object)return Code::Busy;
        if(!bytes || reinterpret_cast<uintptr_t>(bytes)%32 || !capacity)return Code::Invalid;
        bytes_=static_cast<unsigned char*>(bytes);capacity_=capacity;used_=0;return Code::Ok;
    }
    template<class T,class... Args> Code New(Object<T>& output,Args&&... args) noexcept {
        static_assert(std::is_nothrow_constructible<T,Args...>::value,"Arena construction must be fallible before entering New, or noexcept");
        static_assert(std::is_nothrow_destructible<T>::value,"Arena destruction must not throw");
        if(!bytes_ || alignof(T)>32)return Code::Invalid;
        if(!next_)return Code::Capacity; // Generations never wrap/revalidate stale handles.
        unsigned index=0;while(index<records_.size() && records_[index].object)++index;
        if(index==records_.size())return Code::Capacity;
        const auto address=reinterpret_cast<uintptr_t>(bytes_)+used_;
        const size_t pad=(alignof(T)-address%alignof(T))%alignof(T);
        if(pad>capacity_-used_ || sizeof(T)>capacity_-used_-pad)return Code::Capacity;
        auto* object=new(bytes_+used_+pad) T(std::forward<Args>(args)...);
        auto& r=records_[index];r={object,[](void* p) noexcept {static_cast<T*>(p)->~T();},&Guard<T>::Check,next_++};
        used_+=pad+sizeof(T);
        Object<T> candidate;candidate.arena_=this;candidate.slot_=index;candidate.generation_=r.generation;
        output=candidate;return Code::Ok;
    }
    template<class T> Code Destroy(Object<T> object) noexcept {
        if(object.arena_!=this || !Get(object.slot_,object.generation_))return Code::Stale;
        auto& r=records_[object.slot_];
        if(!r.can_destroy(r.object))return Code::Busy;
        r.destroy(r.object);r.object=nullptr;return Code::Ok;
    }
    Code Reset() noexcept {
        for(const auto& r:records_)if(r.object && !r.can_destroy(r.object))return Code::Busy;
        for(size_t i=records_.size();i;--i) {
            auto& r=records_[i-1];if(r.object) {r.destroy(r.object);r.object=nullptr;}
        }
        used_=0;return Code::Ok;
    }
    void* Get(unsigned index,uint32_t generation) const noexcept {
        return index<records_.size() && records_[index].generation==generation?records_[index].object:nullptr;
    }
    size_t used() const noexcept {return used_;}
};
template<class T> T* Object<T>::get() const noexcept {
    return arena_?static_cast<T*>(arena_->Get(slot_,generation_)):nullptr;
}
}

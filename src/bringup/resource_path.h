#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string_view>
namespace BroTracker {
constexpr unsigned kSamplePathMaximum=255;
constexpr unsigned kPathProtocolCapacity=640;
constexpr unsigned kResourcePathBytes=8192,kResourcePathEntries=256;
// Printable conservative FAT names, including ordinary internal spaces. Reject
// traversal, empty components, control bytes, reserved punctuation and FAT aliases.
inline bool ValidSamplePath(std::string_view path,bool absolute=false) noexcept {
    if(path.empty() || path.size()>kSamplePathMaximum)return false;
    if(absolute){if(path=="/")return true;if(path.front()!='/')return false;path.remove_prefix(1);}
    else if(path.front()=='/')return false;
    while(!path.empty()) {
        const auto slash=path.find('/');const auto part=path.substr(0,slash);
        if(part.empty() || part=="." || part==".." || part.front()==' ' || part.back()==' ' || part.back()=='.')return false;
        for(const unsigned char c:part)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c==' '||c=='.'||c=='_'||c=='-'))return false;
        if(slash==path.npos)return true;
        path.remove_prefix(slash+1);if(path.empty())return false;
    }
    return false;
}
// Versioned protocol paths are single ASCII tokens: @ followed by hex bytes.
// Legacy paths remain raw tokens, frozen at 63 bytes without whitespace.
inline bool EncodePathToken(const char* path,char* out,std::size_t capacity) noexcept {
    if(!path)return false;const auto n=std::strlen(path);
    if(n>kSamplePathMaximum || capacity<2*n+2)return false;
    constexpr char hex[]="0123456789abcdef";out[0]='@';
    for(unsigned i=0;i<n;++i){const auto c=static_cast<unsigned char>(path[i]);out[1+2*i]=hex[c>>4];out[2+2*i]=hex[c&15];}
    out[1+2*n]=0;return true;
}
inline bool DecodePathToken(std::string_view token,char* out,std::size_t capacity,bool encoded) noexcept {
    if(!encoded){if(token.empty()||token.size()>63||token.size()>=capacity)return false;for(char c:token)if(c<=32||c=='@')return false;std::memcpy(out,token.data(),token.size());out[token.size()]=0;return true;}
    if(token.empty()||token.front()!='@'||token.size()%2!=1)return false;
    const auto n=(token.size()-1)/2;if(!n||n>kSamplePathMaximum||n>=capacity)return false;
    const auto digit=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};
    for(unsigned i=0;i<n;++i){const auto a=digit(token[1+2*i]),b=digit(token[2+2*i]);if(a<0||b<0||!(a*16+b))return false;out[i]=char(a*16+b);}
    out[n]=0;return true;
}
class ResourcePathPool {
    struct Entry{std::uint16_t offset=0,length=0,refs=0,reserved=0;};
public:
    using Id=std::uint16_t;
    Id Find(const char* path)const noexcept {if(!path)return 0;for(unsigned i=0;i<kResourcePathEntries;++i)if(entries_[i].refs&&!std::strcmp(bytes_+entries_[i].offset,path))return i+1;return 0;}
    const char* Resolve(Id id)const noexcept{return id&&id<=kResourcePathEntries&&entries_[id-1].refs?bytes_+entries_[id-1].offset:nullptr;}
    unsigned UsedBytes()const noexcept {unsigned n=0;for(const auto& e:entries_)if(e.refs)n+=e.length;return n;}
    unsigned UniqueCount()const noexcept {unsigned n=0;for(const auto& e:entries_)if(e.refs)++n;return n;}
    bool CanAcquire(const char* path)const noexcept {if(!path)return true;if(Find(path))return true;unsigned offset=0,slot=0;return Space(path,offset,slot);}
    Id Acquire(const char* path) noexcept {
        if(!path)return 0;if(const auto id=Find(path)){++entries_[id-1].refs;return id;}
        unsigned offset=0,slot=0;if(!Space(path,offset,slot))return 0;
        const auto length=std::strlen(path)+1;std::memcpy(bytes_+offset,path,length);
        entries_[slot]={std::uint16_t(offset),std::uint16_t(length),1,0};return slot+1;
    }
    void Retain(Id id)noexcept {if(id)++entries_[id-1].refs;}
    void Release(Id id)noexcept {if(id&&!--entries_[id-1].refs)entries_[id-1]={};}
private:
    bool Space(const char* path,unsigned& offset,unsigned& slot)const noexcept {
        if(!ValidSamplePath(path))return false;const auto length=std::strlen(path)+1;
        slot=0;while(slot<kResourcePathEntries&&entries_[slot].refs)++slot;if(slot==kResourcePathEntries)return false;
        offset=0;
        while(offset+length<=kResourcePathBytes){unsigned next=offset;for(const auto& e:entries_)if(e.refs&&offset<e.offset+e.length&&offset+length>e.offset)if(e.offset+e.length>next)next=e.offset+e.length;if(next==offset)return true;offset=next;}
        return false;
    }
    char bytes_[kResourcePathBytes]{};Entry entries_[kResourcePathEntries]{};
};
inline ResourcePathPool& ResourcePaths(){static ResourcePathPool pool;return pool;}
// Two-byte owning identity. Copies retain the same immutable path; no per-copy
// path allocation. All lifetime/lookup work is foreground, never audio callback.
class ResourcePathRef {
public:
    ResourcePathRef()=default;
    ResourcePathRef(const ResourcePathRef& r):id_(r.id_){ResourcePaths().Retain(id_);}
    ~ResourcePathRef(){ResourcePaths().Release(id_);}
    ResourcePathRef& operator=(const ResourcePathRef& r){if(this!=&r){ResourcePaths().Retain(r.id_);ResourcePaths().Release(id_);id_=r.id_;}return *this;}
    bool Assign(const char* path){const auto next=ResourcePaths().Acquire(path);if(path&&!next)return false;ResourcePaths().Release(id_);id_=next;return true;}
    void Reset(){ResourcePaths().Release(id_);id_=0;}
    const char* Get()const{return ResourcePaths().Resolve(id_);}
    ResourcePathPool::Id Id()const{return id_;}
private:ResourcePathPool::Id id_=0;
};
static_assert(sizeof(ResourcePathRef)==2,"Compact resource identity");
}

#pragma once
#include "pattern_directory.h"
#include "core/audio/wav_buffer_loader.h"
namespace BroTracker {
constexpr unsigned kWavNameMaximum=44;
constexpr const char* kWavDirectory="/BroTracker/Samples";
constexpr const char* kWavResourcePrefix="BroTracker/Samples/";
inline bool CompatibleWavName(std::string_view name) noexcept {
    if(name.size()<5 || name.size()>kWavNameMaximum)return false;
    const auto extension=name.substr(name.size()-4);
    if(extension!=".wav" && extension!=".WAV")return false;
    name.remove_suffix(4);return !name.empty() && name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string_view::npos;
}
inline bool BuildWavResource(std::string_view name,char* path,std::size_t capacity) noexcept {
    if(!CompatibleWavName(name) || capacity<20+name.size())return false;
    std::memcpy(path,kWavResourcePrefix,19);std::memcpy(path+19,name.data(),name.size());path[19+name.size()]=0;return true;
}
using WavDirectoryPage=PatternDirectoryPage;
template<class Backend> PatternFileResult ReadWavDirectoryPage(Backend& b,std::uint32_t offset,WavDirectoryPage& output) noexcept {
    if(offset%32 || !b.Acquire(false))return PatternFileResult::Busy;
    struct Release {Backend& b;~Release(){b.Release(false);}} release{b};
    WavDirectoryPage page;page.next=offset;
    for(unsigned scanned=0;scanned<64 && page.count<kPatternListPageSize;++scanned) {
        char name[kWavNameMaximum+2]{};bool end=false;
        if(!b.NextDirectoryName(kWavDirectory,page.next,name,sizeof(name),end))return PatternFileResult::Io;
        if(end){page.done=true;break;}
        if(CompatibleWavName(name))std::strcpy(page.names[page.count++],name);
    }
    output=page;return PatternFileResult::Ok;
}
}

namespace BroTracker {
constexpr unsigned kWavDirectoryMaximum=63;
enum class WavDirectoryResult : unsigned {Ok,Missing,ReadError,NotDirectory,Busy,InvalidPath};
enum class WavEntryKind : unsigned {File,Directory};
struct WavFileFacts {
    WavBufferStatus status=WavBufferStatus::IoError;
    std::uint32_t sample_rate=0;std::uint16_t format=0,channels=0,bits=0;
    bool known=false;
};
inline bool WavFilename(std::string_view name) noexcept {
    if(name.size()<4)return false;
    const auto ext=name.substr(name.size()-4);
    return ext[0]=='.' && (ext[1]=='w' || ext[1]=='W') && (ext[2]=='a' || ext[2]=='A') && (ext[3]=='v' || ext[3]=='V');
}
struct WavDirectoryEntry {char name[256]{};WavEntryKind kind=WavEntryKind::File;WavFileFacts facts{};};
struct WavBrowsePage {
    char directory[64]{};WavDirectoryEntry entries[kPatternListPageSize]{};
    unsigned count=0,scanned=0,fallback=0;std::uint32_t next=0;bool done=false;
};
struct WavOpenRequest {unsigned version=2;std::uint32_t request=0,offset=0;bool fallback=false;char directory[64]{};};
inline bool ValidWavDirectory(std::string_view path) noexcept {
    if(path=="/")return true;
    if(path.size()<2 || path.size()>kWavDirectoryMaximum || path.front()!='/' || path.back()=='/' || path.find("..")!=path.npos || path.find("//")!=path.npos)return false;
    for(const auto c:path)if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='/' || c=='.' || c=='_' || c=='-'))return false;
    return true;
}
inline void ParentWavDirectory(char* path) noexcept {
    auto* slash=std::strrchr(path,'/');if(!slash || slash==path){path[0]='/';path[1]=0;}else *slash=0;
}
inline bool WavEntryName(std::string_view name) noexcept {
    if(name.empty() || name.size()>255 || name=="." || name=="..")return false;
    for(const unsigned char c:name)if(c<32 || c==127 || c=='/' || c=='\\')return false;
    return true;
}
inline bool BuildWavResourceAt(std::string_view directory,std::string_view name,char* output,std::size_t capacity) noexcept {
    if(!output || !ValidWavDirectory(directory) || !CompatibleWavName(name))return false;
    const auto prefix=directory=="/"?std::string_view{}:directory.substr(1);
    const auto size=prefix.size()+(prefix.empty()?0:1)+name.size();if(size>63 || capacity<=size)return false;
    std::size_t at=0;if(!prefix.empty()){std::memcpy(output,prefix.data(),prefix.size());at=prefix.size();output[at++]='/';}
    std::memcpy(output+at,name.data(),name.size());output[at+name.size()]=0;return true;
}
inline bool ParseWavOpen(const char* line,std::size_t length,WavOpenRequest& output) noexcept {
    if(!line || length>=128)return false;
    std::size_t split=length;while(split && line[split-1]!=' ')--split;
    if(!split || length-split>63 || !ValidWavDirectory(std::string_view(line+split,length-split)))return false;
    std::uint32_t f[4]{};
    if(!SnapshotFields(line,split-1,"BTSTORE1 WAVOPEN ",f,4) || (f[0]!=2 && f[0]!=3) || !f[1] || f[2]%32 || f[3]>1 || (f[3] && f[2]))return false;
    WavOpenRequest value;value.version=f[0];value.request=f[1];value.offset=f[2];value.fallback=f[3];std::memcpy(value.directory,line+split,length-split);output=value;return true;
}
template<class Backend> WavDirectoryResult ReadWavBrowsePage(Backend& b,const WavOpenRequest& request,WavBrowsePage& output) noexcept {
    if(!ValidWavDirectory(request.directory) || request.offset%32 || (request.fallback && request.offset))return WavDirectoryResult::InvalidPath;
    if(!b.Acquire(false))return WavDirectoryResult::Busy;
    struct Release {Backend& b;~Release(){b.Release(false);}} release{b};
    char directory[64];std::strcpy(directory,request.directory);unsigned fallback=0;
    for(unsigned parent=0;parent<33;++parent) {
        WavBrowsePage page;page.next=request.offset;std::strcpy(page.directory,directory);page.fallback=fallback;
        auto result=b.ReadDirectoryStatus(directory);
        if(result==WavDirectoryResult::Ok) {
            while(page.scanned<64 && page.count<kPatternListPageSize) {
                char name[257]{};bool end=false,is_directory=false;
                if(!b.NextDirectoryEntry(directory,page.next,name,sizeof(name),end,is_directory)){result=WavDirectoryResult::ReadError;break;}
                if(end){page.done=true;break;}++page.scanned;
                if((request.version<3 && std::strlen(name)>63) || !WavEntryName(name) || (!is_directory && !(request.version>=3?WavFilename(name):CompatibleWavName(name))))continue;
                auto& entry=page.entries[page.count++];std::strcpy(entry.name,name);entry.kind=is_directory?WavEntryKind::Directory:WavEntryKind::File;
                if(!is_directory && request.version>=3) {
                    entry.facts=b.ReadWavFacts(directory,name);
                    break; // At most one bounded WAV metadata inspection per foreground page.
                }
            }
        }
        if(result==WavDirectoryResult::Ok){output=page;return result;}
        if(!request.fallback || !std::strcmp(directory,"/"))return result;
        if(!fallback)fallback=result==WavDirectoryResult::Missing?1:2;
        ParentWavDirectory(directory);
    }
    return WavDirectoryResult::ReadError;
}
}

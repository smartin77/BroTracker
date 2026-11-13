#pragma once
#include "pattern_directory.h"
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

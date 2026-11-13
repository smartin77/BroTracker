#pragma once
#include "pattern_file_store.h"
namespace BroTracker {
constexpr unsigned kPatternListPageSize=8;
struct PatternDirectoryPage {
    char names[kPatternListPageSize][64]{};
    unsigned count=0;std::uint32_t next=0;bool done=false;
};
inline bool CompatiblePatternName(std::string_view name) noexcept {
    if(!DevelopmentHasExtension(name))return false;
    name.remove_suffix(6);return ValidDevelopmentStem(name);
}
// A bounded stopped-only page, under the existing SD read lease. Never creates files.
// Directory offsets are opaque FAT directory byte positions, not pattern IDs.
template<class Backend> PatternFileResult ReadPatternDirectoryPage(Backend& backend,std::uint32_t offset,PatternDirectoryPage& output) noexcept {
    if(offset%32 || !backend.Acquire(false))return PatternFileResult::Busy;
    struct Release {Backend& b;~Release(){b.Release(false);}} release{backend};
    PatternDirectoryPage page;page.next=offset;std::uint8_t bytes[kPatternFileCapacity];
    for(unsigned scanned=0;scanned<64 && page.count<kPatternListPageSize;++scanned) {
        char name[kDevelopmentNameMaximum+2]{};bool end=false;
        if(!backend.NextPatternName(page.next,name,sizeof(name),end))return PatternFileResult::Io;
        if(end){page.done=true;break;}
        if(!CompatiblePatternName(name))continue;
        char path[kDevelopmentPathCapacity];if(!BuildDevelopmentPath(name,path,sizeof(path)))continue;
        std::size_t size=0;
        if(backend.Read(path,bytes,sizeof(bytes),size)!=PatternFileResult::Ok ||
           ValidatePatternFile(PatternFileKind::Development,bytes,size)!=PatternFileResult::Ok)continue;
        std::memcpy(page.names[page.count++],name,std::strlen(name)+1);
    }
    output=page;return PatternFileResult::Ok;
}
}

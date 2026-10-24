#pragma once
#include "core/tempo.h"
#include <cstring>
#include <string_view>
namespace BroTracker {
// Normal LOAD is a filename stem, independent of provisioning definitions.
constexpr unsigned kDevelopmentStemMaximum=24;
constexpr char kDevelopmentDirectory[]="/BroTracker/development/";
constexpr unsigned kDevelopmentPathCapacity=sizeof(kDevelopmentDirectory)-1+kDevelopmentStemMaximum+sizeof(".btdev");
inline bool ValidDevelopmentStem(std::string_view stem) noexcept {
    if(stem.empty() || stem.size()>kDevelopmentStemMaximum)return false;
    for(char c:stem)if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-'))return false;
    return true;
}
constexpr unsigned kDevelopmentNameMaximum=kDevelopmentStemMaximum+6;
inline bool DevelopmentHasExtension(std::string_view name) noexcept {
    return name.size()>=6 && name.substr(name.size()-6)==".btdev";
}
inline bool ValidDevelopmentSelection(std::string_view name) noexcept {
    if(name=="none")return true; // Reserved before any suffix normalization.
    if(DevelopmentHasExtension(name))name.remove_suffix(6);
    return ValidDevelopmentStem(name);
}
struct DevelopmentPattern {
    char stem[kDevelopmentNameMaximum+1]="none"; // Validated original selector, not a numeric enum.
    bool IsNone() const noexcept {return !std::strcmp(stem,"none");}
    bool Assign(std::string_view value) noexcept {
        if(!ValidDevelopmentSelection(value))return false;
        std::memcpy(stem,value.data(),value.size());stem[value.size()]=0;return true;
    }
};
inline const char* DevelopmentPatternName(const DevelopmentPattern& value) noexcept {return value.stem;}
inline bool BuildDevelopmentPath(std::string_view name,char* output,unsigned capacity) noexcept {
    DevelopmentPattern selected;if(!selected.Assign(name) || selected.IsNone())return false;
    const auto prefix=sizeof(kDevelopmentDirectory)-1;
    const bool extension=DevelopmentHasExtension(name);
    const auto needed=prefix+name.size()+1+(extension?0:6);
    if(!output || capacity<needed)return false;
    std::memcpy(output,kDevelopmentDirectory,prefix);std::memcpy(output+prefix,name.data(),name.size());
    if(extension)output[prefix+name.size()]=0;
    else std::memcpy(output+prefix+name.size(),".btdev",sizeof(".btdev"));return true;
}
// Provisioning-only numbered definitions/paths below; normal loading never uses them.
inline const char* DevelopmentPatternPath(unsigned n) noexcept {
    constexpr const char* paths[]={nullptr,"/BroTracker/development/patern1.btdev","/BroTracker/development/patern2.btdev",
        "/BroTracker/development/patern3.btdev","/BroTracker/development/patern4.btdev"};
    return n>=1 && n<=4?paths[n]:nullptr;
}
inline const char* DevelopmentCandidatePath(unsigned n) noexcept {
    constexpr const char* paths[]={nullptr,"/BroTracker/development/patern1.btdev.candidate","/BroTracker/development/patern2.btdev.candidate",
        "/BroTracker/development/patern3.btdev.candidate","/BroTracker/development/patern4.btdev.candidate"};
    return n>=1 && n<=4?paths[n]:nullptr;
}
constexpr unsigned DevelopmentPatternTempo(unsigned n) noexcept {return n?15500:kDefaultTempo;}
}

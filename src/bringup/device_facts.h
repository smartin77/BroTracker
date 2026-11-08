#pragma once
#include "audio_output_selection.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace BroTracker {
enum class AudioInitialization { Uninitialized, Initialized, Failed };
struct DeviceFacts {
    char board[11]{}; // Existing USB serial, not a second board-ID algorithm.
    char boot[33]{};  // Lowercase hex nonce; empty means explicitly unavailable.
    AudioOutputChoice route=AudioOutputChoice::Default;
    AudioInitialization audio=AudioInitialization::Uninitialized;
};
constexpr std::size_t kDeviceFactsLineCapacity=96; // Including LF and NUL.
inline bool ValidDeviceBoard(std::string_view value) noexcept {
    if(value.empty() || value.size()>10)return false;
    for(char c:value)if(c<'0' || c>'9')return false;
    return true;
}
inline bool ValidDeviceBoot(std::string_view value) noexcept {
    if(value.size()!=32)return false;
    bool nonzero=false;
    for(char c:value) {
        if(!((c>='0' && c<='9') || (c>='a' && c<='f')))return false;
        nonzero|=c!='0';
    }
    return nonzero;
}
inline bool ParseDeviceFacts(std::string_view line,DeviceFacts& output) noexcept {
    if(line.size()>=kDeviceFactsLineCapacity)return false;
    std::array<std::string_view,7> words{};
    for(unsigned i=0;i<words.size();++i) {
        const auto space=line.find(' ');
        if(i==words.size()-1) {if(space!=line.npos)return false;words[i]=line;}
        else {if(space==line.npos || !space)return false;words[i]=line.substr(0,space);line.remove_prefix(space+1);}
    }
    if(words[0]!="BTDEVICE1" || words[1]!="FACTS" || words[2]!="1")return false;
    DeviceFacts next;
    if(words[3]!="unavailable") {
        if(!ValidDeviceBoard(words[3]))return false;
        std::memcpy(next.board,words[3].data(),words[3].size());
    }
    if(words[4]!="unavailable") {
        if(!ValidDeviceBoot(words[4]))return false;
        std::memcpy(next.boot,words[4].data(),words[4].size());
    }
    if(words[5]=="uninitialized")next.audio=AudioInitialization::Uninitialized;
    else if(words[5]=="initialized")next.audio=AudioInitialization::Initialized;
    else if(words[5]=="failed")next.audio=AudioInitialization::Failed;
    else return false;
    if(words[6]=="default")next.route=AudioOutputChoice::Default;
    else if(words[6]=="bridge")next.route=AudioOutputChoice::Bridge;
    else if(words[6]=="pt8211")next.route=AudioOutputChoice::Pt8211;
    else if(words[6]=="audio_shield")next.route=AudioOutputChoice::AudioShield;
    else return false;
    if((next.audio==AudioInitialization::Uninitialized)!=(next.route==AudioOutputChoice::Default))return false;
    output=next;return true;
}
inline std::size_t FormatDeviceFacts(const DeviceFacts& facts,char* out,std::size_t capacity) noexcept {
    if(!out || !capacity)return 0;
    const int n=std::snprintf(out,capacity,"BTDEVICE1 FACTS 1 %s %s %s %s\n",
        *facts.board?facts.board:"unavailable",*facts.boot?facts.boot:"unavailable",
        facts.audio==AudioInitialization::Uninitialized?"uninitialized":
        facts.audio==AudioInitialization::Initialized?"initialized":"failed",AudioOutputName(facts.route));
    if(n<0 || static_cast<std::size_t>(n)>=capacity) {out[0]=0;return 0;}
    return static_cast<std::size_t>(n);
}
enum class BootComparison { Unknown, WrongBoard, SameBoot, NewBoot };
inline BootComparison CompareDeviceBoot(const DeviceFacts& before,const DeviceFacts& after) noexcept {
    if(!*before.board || !*after.board)return BootComparison::Unknown;
    if(std::strcmp(before.board,after.board))return BootComparison::WrongBoard;
    if(!*before.boot || !*after.boot)return BootComparison::Unknown;
    return std::strcmp(before.boot,after.boot)?BootComparison::NewBoot:BootComparison::SameBoot;
}
}

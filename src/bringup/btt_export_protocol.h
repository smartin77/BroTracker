#pragma once
#include <cstdint>
#include "../../tools/btt/code_placement.h"
#include <array>
#include <string_view>
namespace BroTracker {
constexpr unsigned kBttExportChunk=64,kBttExportLine=256;
constexpr uint32_t kBttExportMax=1048576,kBttExportTimeout=15000;
inline uint32_t BttExportCrc(uint32_t c,const uint8_t* p,unsigned n) noexcept {
    for(unsigned i=0;i<n;++i){c^=p[i];for(unsigned b=0;b<8;++b)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return c;
}
BTT_FLASHMEM("export_words") inline unsigned BttExportWords(std::string_view line,std::array<std::string_view,10>& words) noexcept {
    unsigned n=0;
    while(!line.empty()) {if(n==words.size())return 0;const auto at=line.find(' ');
        words[n++]=line.substr(0,at);if(words[n-1].empty())return 0;
        if(at==line.npos)return n;
        line.remove_prefix(at+1);if(line.empty())return 0;}
    return 0;
}
BTT_FLASHMEM("export_number") inline bool BttExportNumber(std::string_view word,uint32_t& value) noexcept {
    value=0;if(word.empty())return false;
    for(char c:word){if(c<'0'||c>'9'||value>(UINT32_MAX-unsigned(c-'0'))/10)return false;value=value*10+unsigned(c-'0');}return true;
}
enum class BttExportVerb { Begin, Next, End, Cancel };
struct BttExportRequest {
    BttExportVerb verb=BttExportVerb::Begin;uint32_t id=0,generation=0,offset=0;
    char nonce[33]{};
};
BTT_FLASHMEM("export_parse") inline bool ParseBttExportRequest(std::string_view line,BttExportRequest& out) noexcept {
    std::array<std::string_view,10> w{};const auto n=BttExportWords(line,w);
    if((n!=5 && n!=7) || w[0]!="BTBTT1" || w[2]!="1")return false;
    BttExportRequest r;
    if(w[1]=="EXPORT" && n==5)r.verb=BttExportVerb::Begin;
    else if(n==7 && w[1]=="EXPORTNEXT")r.verb=BttExportVerb::Next;
    else if(n==7 && w[1]=="EXPORTEND")r.verb=BttExportVerb::End;
    else if(n==7 && w[1]=="EXPORTCANCEL")r.verb=BttExportVerb::Cancel;
    else return false;
    if(!BttExportNumber(w[3],r.id) || !r.id)return false;
    if(n==7 && (!BttExportNumber(w[4],r.generation) || !BttExportNumber(w[5],r.offset)))return false;
    const auto token=w[n-1];if(token.size()!=32)return false;
    bool nonzero=false;
    for(unsigned i=0;i<32;++i){const char c=token[i];if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;r.nonce[i]=c;nonzero|=c!='0';}
    if(!nonzero)return false;
    out=r;return true;
}
static_assert(227<=kBttExportLine,"Maximal export DATA reply must fit framing/FIFO");
}

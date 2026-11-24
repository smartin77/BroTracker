#pragma once
#include <string_view>

namespace BroTracker {
constexpr unsigned kSessionLineageSize=32;
inline bool ValidSessionLineage(std::string_view id) {
    if(id.size()!=kSessionLineageSize)return false;
    bool nonzero=false;
    for(char c:id) {
        if(!((c>='0' && c<='9') || (c>='a' && c<='f')))return false;
        nonzero|=c!='0';
    }
    return nonzero;
}
}

#pragma once
#include <cstddef>
#include <string_view>
// Host warning locales: lower-case language, optional two-letter REGION.
// Canonicalize legacy en/en_us and lower-case region spellings without paths.
inline bool NormalizeWarningLocale(std::string_view value,char* output,std::size_t capacity) noexcept {
    if(value.empty() || value=="en")value="en_US";
    if(value.size()+1>capacity)return false;
    const auto separator=value.find('_');
    const auto language=value.substr(0,separator);
    if(language.size()<2 || language.size()>3)return false;
    for(char c:language)if(c<'a' || c>'z')return false;
    if(separator!=value.npos) {
        const auto region=value.substr(separator+1);
        if(region.size()!=2)return false;
        for(char c:region)if(!((c>='A' && c<='Z') || (c>='a' && c<='z')))return false;
    }
    for(std::size_t i=0;i<value.size();++i) {
        const char c=value[i];output[i]=(separator!=value.npos && i>separator && c>='a' && c<='z')?char(c-'a'+'A'):c;
    }
    output[value.size()]=0;return true;
}

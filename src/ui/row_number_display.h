#pragma once
#include <cstdio>
#include <string>
namespace BroTracker {
enum class RowNumbering { D1, D0, H1, H0 };
inline const char* RowNumberingName(RowNumbering mode) noexcept {
    switch(mode) {case RowNumbering::D0:return "d0";case RowNumbering::H1:return "h1";
        case RowNumbering::H0:return "h0";default:return "d1";}
}
inline std::string FormatRowNumber(unsigned row,RowNumbering mode) {
    const unsigned value=row+((mode==RowNumbering::D1 || mode==RowNumbering::H1)?1:0);
    char text[16];std::snprintf(text,sizeof(text),mode==RowNumbering::H0 || mode==RowNumbering::H1?"%02X":"%02u",value);
    return text;
}
}

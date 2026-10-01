#pragma once
#include <algorithm>
#include <cwctype>
#include <string>
inline bool IsTeensyIdentity(std::wstring id)
{
    std::transform(id.begin(), id.end(), id.begin(), [](wchar_t c) { return std::towupper(c); });
    const std::wstring prefix = L"USB\\VID_16C0&PID_048A";
    return id.compare(0, prefix.size(), prefix) == 0 &&
        (id.size() == prefix.size() || id[prefix.size()] == L'&' || id[prefix.size()] == L'\\');
}

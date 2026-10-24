#pragma once
namespace BroTracker {
enum class DevelopmentPattern { None, Theme, Midi };
inline const char* DevelopmentPatternName(DevelopmentPattern value) noexcept {
    return value==DevelopmentPattern::Midi?"midi":value==DevelopmentPattern::Theme?"theme":"none";
}
}

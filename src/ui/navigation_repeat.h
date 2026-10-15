#pragma once
#include "local_pattern_editor.h"
#include <cstdint>

namespace NavigationRepeatTiming {
constexpr std::uint32_t first_ms=350, accelerate_ms=1000, fastest_ms=2000;
constexpr std::uint32_t slow_ms=100, medium_ms=60, fast_ms=40;
}

// Input owners handle physical releases/modifiers; this timer owns no platform state.
class NavigationRepeat {
public:
    void Cancel() noexcept { action_=EditorAction::None; }
    bool Active() const noexcept { return action_!=EditorAction::None; }
    void Begin(EditorAction action,std::uint32_t now,LocalPatternEditor& editor) noexcept {
        action_=action; started_=last_=now; repeated_=false; editor.Apply(action_);
    }
    void Tick(std::uint32_t now,LocalPatternEditor& editor) noexcept {
        if(!Active()) return;
        const auto held=std::uint32_t(now-started_);
        const auto interval=!repeated_ ? NavigationRepeatTiming::first_ms :
            held>=NavigationRepeatTiming::fastest_ms ? NavigationRepeatTiming::fast_ms :
            held>=NavigationRepeatTiming::accelerate_ms ? NavigationRepeatTiming::medium_ms :
            NavigationRepeatTiming::slow_ms;
        if(std::uint32_t(now-last_)<interval) return;
        last_=now; repeated_=true; editor.Apply(action_);
    }
private:
    EditorAction action_=EditorAction::None;
    std::uint32_t started_=0,last_=0;
    bool repeated_=false;
};

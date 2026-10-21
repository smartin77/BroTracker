#pragma once
#include <cstdint>
// Application-owned repeat; one step per Tick after a stall, never a backlog.
class TempoRepeat {
public:
    int Begin(int direction,std::uint32_t now) noexcept { direction_=direction; began_=now; last_=now+300; return direction*5; }
    void Cancel() noexcept { direction_=0; }
    int Tick(std::uint32_t now) noexcept {
        if(!direction_ || now-began_<400 || now-last_<100) return 0;
        last_=now; const auto held=now-began_;
        return direction_*(held>=3000?100:held>=1500?25:5);
    }
private:
    int direction_=0; std::uint32_t began_=0,last_=0;
};

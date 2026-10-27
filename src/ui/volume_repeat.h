#pragma once
#include <cstdint>
class VolumeRepeat {
public:
    void Cancel() noexcept {direction_=0;blocked_=up_ || down_;}
    int Edge(bool up,bool down,std::uint32_t now) noexcept {
        if(up==up_ && down==down_)return 0;
        up_=up;down_=down;
        if(!up && !down){blocked_=false;direction_=0;return 0;}
        if(blocked_ || up==down){Cancel();return 0;}
        direction_=up?1:-1;last_=now;repeated_=false;return direction_;
    }
    int Tick(std::uint32_t now) noexcept {
        if(!direction_ || blocked_)return 0;
        if(now-last_<(repeated_?100u:400u))return 0;
        last_=now;repeated_=true;return direction_;
    }
    bool Held() const noexcept {return up_ || down_;}
private:
    bool up_=false,down_=false,blocked_=false,repeated_=false;
    int direction_=0;std::uint32_t last_=0;
};

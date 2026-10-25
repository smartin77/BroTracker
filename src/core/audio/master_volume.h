#pragma once
#include <cstdint>
#include <cstddef>
namespace BroTracker {
// Single audio owner. 0..100 linear percent; unity is the pre-existing output.
class MasterVolume {
public:
    void Configure(std::uint32_t rate) noexcept {length_=(rate+99)/100;if(!length_)length_=1;}
    bool Set(unsigned percent) noexcept {
        if(percent>100)return false;
        if(percent==percent_)return true;
        percent_=percent;from_=current_;target_=(percent*65536u+50)/100;position_=0;return true;
    }
    unsigned Percent() const noexcept {return percent_;}
    std::uint32_t Current() const noexcept {return current_;}
    void Process(std::int16_t* pcm,std::size_t count) noexcept {
        for(std::size_t i=0;i<count;++i) {
            if(position_<length_) {++position_;current_=std::uint32_t(std::int64_t(from_)+(std::int64_t(target_)-from_)*position_/length_);}
            const auto value=std::int64_t(pcm[i])*current_;
            pcm[i]=std::int16_t((value+(value>=0?32768:-32768))/65536);
        }
    }
private:
    unsigned percent_=100;std::uint32_t current_=65536,target_=65536,from_=65536,length_=441,position_=441;
};
}

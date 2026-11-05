#pragma once
#include <cstdint>
// Allow normal Teensy boot/enumeration only at initial host startup. This is a
// presentation delay, never an input/readiness override or a blocking sleep.
constexpr std::uint32_t kInitialTeensyDiscoveryGraceMs=5000;
class HostWarningPresentation {
public:
    enum class Overlay { None, Disconnected, SdUnavailable };
    explicit HostWarningPresentation(std::uint32_t startup_at):startup_at_(startup_at) {}
    void ObserveConnection(bool connected) {connected_once_=connected_once_ || connected;}
    Overlay Select(bool connected,bool sd_unavailable,std::uint32_t now) {
        ObserveConnection(connected);
        if(connected)return sd_unavailable?Overlay::SdUnavailable:Overlay::None;
        if(!connected_once_ && std::uint32_t(now-startup_at_)<kInitialTeensyDiscoveryGraceMs)return Overlay::None;
        return Overlay::Disconnected;
    }
private:
    std::uint32_t startup_at_;
    bool connected_once_=false;
};

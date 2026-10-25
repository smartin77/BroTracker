#pragma once
#include <cstdint>
namespace BroTracker {
// Main-context readiness rendezvous, not a retry of an SD operation.
class StorageReadyWait {
public:
    static constexpr std::uint32_t TimeoutMs=5000;
    enum class Result { Waiting, Ready, Timeout };
    bool Begin(std::uint32_t now) noexcept {if(active_)return false;active_=true;began_=now;return true;}
    void Cancel() noexcept {active_=false;}
    bool Active() const noexcept {return active_;}
    Result Poll(std::uint32_t now,bool ready) const noexcept {
        if(now-began_>=TimeoutMs)return Result::Timeout;
        return ready?Result::Ready:Result::Waiting;
    }
private: bool active_=false;std::uint32_t began_=0;
};
}

#pragma once
#include <string_view>
namespace BroTracker {
enum class AudioOutputChoice { Default, Bridge, Pt8211, AudioShield };
inline const char* AudioOutputName(AudioOutputChoice value) noexcept {
    return value==AudioOutputChoice::AudioShield ? "audio_shield" : value==AudioOutputChoice::Pt8211 ? "pt8211" : value==AudioOutputChoice::Bridge ? "bridge" : "default";
}
inline bool ParseAudioOutputRequest(std::string_view line, AudioOutputChoice& value) noexcept {
    if(line=="BTAUDIO1 SET 1 bridge") value=AudioOutputChoice::Bridge;
    else if(line=="BTAUDIO1 SET 1 pt8211") value=AudioOutputChoice::Pt8211;
    else if(line=="BTAUDIO1 SET 1 audio_shield") value=AudioOutputChoice::AudioShield;
    else if(line=="BTAUDIO1 SET 1 default") value=AudioOutputChoice::Default;
    else return false;
    return true;
}
// Driver lifecycle policy: first selection locks for this boot; same-route requests
// are idempotent, including reconnect. Never reconstruct a live DMA output.
class AudioOutputSelection {
public:
    AudioOutputChoice Active() const noexcept { return active_; }
    bool CanSelect(AudioOutputChoice requested, AudioOutputChoice fallback) const noexcept {
        if(requested==AudioOutputChoice::Default) return true;
        const auto next=requested;
        (void)fallback;
        return active_==AudioOutputChoice::Default || active_==next;
    }
    void Commit(AudioOutputChoice requested, AudioOutputChoice fallback) noexcept {
        if(requested==AudioOutputChoice::Default && active_!=AudioOutputChoice::Default) return;
        if(CanSelect(requested,fallback)) active_=requested==AudioOutputChoice::Default ? fallback : requested;
    }
private:
    AudioOutputChoice active_=AudioOutputChoice::Default;
};
}

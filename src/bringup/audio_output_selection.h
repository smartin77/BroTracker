#pragma once
#include <string_view>
namespace BroTracker {
enum class AudioOutputChoice { Default, Bridge, Pt8211, AudioShield };
inline const char* AudioOutputName(AudioOutputChoice value) noexcept {
    return value==AudioOutputChoice::AudioShield ? "audio_shield" : value==AudioOutputChoice::Pt8211 ? "pt8211" : value==AudioOutputChoice::Bridge ? "bridge" : "default";
}
inline bool ParseAudioOutputName(std::string_view name, AudioOutputChoice& value) noexcept {
    if(name=="bridge") value=AudioOutputChoice::Bridge;
    else if(name=="pt8211") value=AudioOutputChoice::Pt8211;
    else if(name=="audio_shield") value=AudioOutputChoice::AudioShield;
    else if(name=="default") value=AudioOutputChoice::Default;
    else return false;
    return true;
}
inline bool ParseAudioOutputRequest(std::string_view line, AudioOutputChoice& value) noexcept {
    constexpr std::string_view prefix="BTAUDIO1 SET 1 ";
    return line.substr(0,prefix.size())==prefix && ParseAudioOutputName(line.substr(prefix.size()),value);
}
inline bool ParseAudioOutputRequestV2(std::string_view line, AudioOutputChoice& value, bool& swap) noexcept {
    constexpr std::string_view prefix="BTAUDIO1 SET 2 ";
    if(line.substr(0,prefix.size())!=prefix) return false;
    line.remove_prefix(prefix.size());
    const auto space=line.find(' ');
    if(space==line.npos) return false;
    const auto order=line.substr(space+1);
    if(order!="0" && order!="1") return false;
    if(!ParseAudioOutputName(line.substr(0,space),value)) return false;
    swap=order=="1";
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

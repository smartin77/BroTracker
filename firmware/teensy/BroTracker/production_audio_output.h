#pragma once
#include <Audio.h>
#include <new>
#include <type_traits>
#include "audio_output_config.h"
#include "../../../src/bringup/audio_output_selection.h"
#include "../../../src/bringup/device_facts.h"

namespace BroTracker {
#if defined(BROTRACKER_PATTERN_VARIABLE_RATE) || defined(BROTRACKER_DIAGNOSTIC_SYNTH_LEFT)
constexpr unsigned kProductionRightSourceChannel=1;
#else
constexpr unsigned kProductionRightSourceChannel=0;
#endif
// Acquire a complete frame pair before the audio owner can render or publish.
template<class Allocate,class Release>
auto AllocateAudioPair(Allocate allocate,Release release) noexcept {
    using Block=decltype(allocate());
    struct Pair { Block left,right; };
    auto left=allocate();
    auto right=left ? allocate() : nullptr;
    if(left && !right) { release(left); left=nullptr; }
    return Pair{left,right};
}
// Foreground, boot-only activation. Placement new uses fixed static storage;
// live outputs are never destroyed, replaced or reinitialized.
class ProductionAudioOutput {
public:
    AudioOutputChoice Active() const noexcept { return selection_.Active(); }
    bool Ready() const noexcept { return Active()!=AudioOutputChoice::Default && !error_; }
    AudioInitialization Initialization() const noexcept {
        return Active()==AudioOutputChoice::Default ? AudioInitialization::Uninitialized :
            error_ ? AudioInitialization::Failed : AudioInitialization::Initialized;
    }
    DeviceFacts Facts(DeviceFacts identity) const noexcept {
        identity.route=Active();identity.audio=Initialization();return identity;
    }
    const char* Select(AudioOutputChoice request, AudioStream& source, AudioOutputUSB& usb);

private:
    AudioOutputSelection selection_;
    using Hardware=typename std::aligned_union<0,AudioOutputPT8211,AudioOutputMQS,AudioOutputI2S>::type;
    using Patch=typename std::aligned_storage<sizeof(AudioConnection),alignof(AudioConnection)>::type;
    Hardware hardware_;
    Patch patches_[3];
    AudioControlSGTL5000 codec_; // Constructor does no I2C; used only for Shield.
    const char* error_=nullptr; // Sticky boot failure; same/default never retries.
};
}

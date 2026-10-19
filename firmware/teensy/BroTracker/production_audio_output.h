#pragma once
#include <Audio.h>
#include <new>
#include <type_traits>
#include "audio_output_config.h"
#include "../../../src/bringup/audio_output_selection.h"

namespace BroTracker {
// Foreground, boot-only activation. Placement new uses fixed static storage;
// live outputs are never destroyed, replaced or reinitialized.
class ProductionAudioOutput {
public:
    AudioOutputChoice Active() const noexcept { return selection_.Active(); }
    const char* Select(AudioOutputChoice request, AudioStream& source, AudioOutputUSB& usb);

private:
    AudioOutputSelection selection_;
    using Hardware=typename std::aligned_union<0,AudioOutputPT8211,AudioOutputMQS>::type;
    using Patch=typename std::aligned_storage<sizeof(AudioConnection),alignof(AudioConnection)>::type;
    Hardware hardware_;
    Patch patches_[3];
};
}

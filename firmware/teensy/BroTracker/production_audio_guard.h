#pragma once
#include <cstdint>
namespace BroTracker {
struct AudioActivationMask {std::uint32_t primask, basepri;};
AudioActivationMask BeginAudioActivation();
void EndAudioActivation(AudioActivationMask saved);
}

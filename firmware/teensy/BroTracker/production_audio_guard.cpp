#include "production_audio_guard.h"
namespace BroTracker {
AudioActivationMask BeginAudioActivation() {
    AudioActivationMask saved;
    asm volatile("mrs %0, primask\n\tmrs %1, basepri\n\tcpsid i\n\tmsr basepri_max, %2\n\tdsb\n\tisb"
        : "=&r"(saved.primask), "=&r"(saved.basepri) : "r"(16u) : "memory");
    return saved;
}
void EndAudioActivation(AudioActivationMask saved) {
    asm volatile("msr primask, %0\n\tmsr basepri, %1\n\tdsb\n\tisb"
        :: "r"(saved.primask), "r"(saved.basepri) : "memory");
}
}

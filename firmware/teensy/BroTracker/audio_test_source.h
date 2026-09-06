#pragma once

#include <Audio.h>
#include <scheduler.h>

namespace BroTracker
{
    // Minimal deterministic test-tone AudioStream source.
    //
    // Generates a fixed-frequency square wave so the native Teensy audio
    // path (this source -> AudioMixer4 -> AudioOutputMQS/AudioOutputUSB)
    // can be verified on real hardware without depending on tracker
    // playback, synthesis or sample streaming.
    //
    // update() is called by the Teensy Audio Library once per audio block
    // and is the audio processing boundary described in
    // docs/TEENSY_AUDIO_ARCHITECTURE.md and docs/SCHEDULER.md. It advances
    // the shared BroTracker Scheduler by the number of samples processed.
    class AudioTestSource : public AudioStream
    {
    public:
        explicit AudioTestSource(Scheduler& scheduler);

        virtual void update() override;

    private:
        Scheduler& scheduler_;
        uint32_t sample_index_ = 0;
    };
}

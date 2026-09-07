#pragma once

#include <Audio.h>

#include "sample.h"

namespace BroTracker
{
    // Minimal AudioStream sample player.
    //
    // Plays a loaded Sample once, start to end, with no looping and no
    // sample-rate conversion: the sample's sample_rate_hz is expected to
    // match the audio engine's rate for now. Playback at a different pitch
    // (e.g. a 48 kHz sample through a 44.1 kHz engine) can be added later
    // without changing this interface.
    class SamplePlayer : public AudioStream
    {
    public:
        SamplePlayer();

        virtual void update() override;

        // Sample must outlive the player, or be cleared via SetSample(nullptr)
        // before it is freed. Does not take ownership.
        void SetSample(const Sample* sample);

        // Restart playback from the first frame.
        void Play();

        bool IsPlaying() const { return playing_; }

    private:
        const Sample* sample_ = nullptr;
        std::uint32_t position_ = 0;
        bool playing_ = false;
    };
}

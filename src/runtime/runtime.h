#pragma once

#include "core_state.h"
#include "midi.h"
#include "playback_engine.h"
#include "scheduler.h"
#include "storage.h"

namespace BroTracker
{
    class Runtime
    {
    public:
        bool Initialize();
        void RunOnce();

    private:
        CoreState core_state_;
        Scheduler scheduler_;
        PlaybackEngine playback_engine_;
        Storage storage_;
        Midi midi_;
    };
}
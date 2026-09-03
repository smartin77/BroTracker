#include "runtime.h"

#include "core/logger.h"
#include "platform_services.h"

namespace BroTracker
{
    bool Runtime::Initialize()
    {
        LogInfo("BroTracker starting.");

        if (!core_state_.Initialize())
            return false;
        LogInfo("Core initialized.");

        if (!scheduler_.Initialize())
            return false;
        LogInfo("Scheduler initialized.");

        if (!playback_engine_.Initialize())
            return false;
        LogInfo("Playback engine initialized.");

        if (!storage_.Initialize())
            return false;
        LogInfo("Storage initialized.");

        if (!midi_.Initialize())
            return false;
        LogInfo("MIDI initialized.");

        LogInfo("BroTracker ready.");
        IndicateInitializationComplete();
        return true;
    }

    void Runtime::RunOnce()
    {
        scheduler_.AdvanceSamples(0);  // No audio processed in host test harness yet
        playback_engine_.Process();
        core_state_.Update();
    }
}
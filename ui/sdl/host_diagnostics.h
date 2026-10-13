#pragma once
#include <SDL.h>
#include "ui/host_logging.h"
inline void LogSdlHostDiagnostics(FILE* file) {
    if(!BroTracker::HostDebugLogging()) return;
    SDL_version runtime{}; SDL_GetVersion(&runtime);
    BroTracker::HostLogf(file,BroTracker::HostLogLevel::Debug,"SDL","compiled=%u.%u.%u runtime=%u.%u.%u video=%s audio=%s",
        SDL_MAJOR_VERSION,SDL_MINOR_VERSION,SDL_PATCHLEVEL,runtime.major,runtime.minor,runtime.patch,
        SDL_GetCurrentVideoDriver()?SDL_GetCurrentVideoDriver():"unavailable",
        SDL_GetCurrentAudioDriver()?SDL_GetCurrentAudioDriver():"unavailable (not SDL audio)");
    for(int i=0;i<SDL_GetNumVideoDrivers() && i<16;++i)
        BroTracker::HostLogf(file,BroTracker::HostLogLevel::Debug,"SDL","available_video_backend=%s",SDL_GetVideoDriver(i));
    for(int i=0;i<SDL_GetNumAudioDrivers() && i<16;++i)
        BroTracker::HostLogf(file,BroTracker::HostLogLevel::Debug,"SDL","available_audio_backend=%s",SDL_GetAudioDriver(i));
}

#pragma once
#include <SDL.h>
#include "ui/host_config.h"

// SDL resolves the loaded executable, independent of the launch working directory.
inline std::filesystem::path RuntimeConfigPath() {
    char* base=SDL_GetBasePath();
    const auto path=BroTracker::ExecutableConfigPath(base?std::filesystem::u8path(base):std::filesystem::path{});
    SDL_free(base);
    return path;
}

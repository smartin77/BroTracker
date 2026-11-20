#pragma once
#include <SDL.h>
#include <limits>
#include "ui/bringup_controls.h"

// The host supplies the existing framebuffer coordinate mapping.
template<class MapPoint>
bool HandleFileBrowserInput(const SDL_Event& event,BringUpControls& controls,std::uint32_t now,MapPoint map) {
    if(!controls.FileListNavigationAvailable())return false;
    if(event.type==SDL_KEYDOWN && !(event.key.keysym.mod&(KMOD_CTRL|KMOD_SHIFT|KMOD_ALT|KMOD_GUI))) {
        if(event.key.keysym.sym==SDLK_HOME || event.key.keysym.sym==SDLK_END) {
            controls.NavigateFileList(event.key.keysym.sym==SDLK_HOME?
                std::numeric_limits<std::int64_t>::min():std::numeric_limits<std::int64_t>::max());
            return true;
        }
    }
    if(event.type==SDL_MOUSEWHEEL && event.wheel.y) {
        const std::int64_t steps=event.wheel.y;
        controls.NavigateFileList(event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?steps:-steps);
        return true;
    }
    if(event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_LEFT) {
        int x,y;
        if(map(event.button.x,event.button.y,x,y))controls.ClickFileList(x,y,event.button.clicks==2,now);
        return true;
    }
    return false;
}

#pragma once
#include <SDL.h>
#include "ui/bringup_controls.h"
inline BringUpAction MapWindowsAction(const SDL_Event& event) {
    if (event.type == SDL_QUIT) return BringUpAction::Exit;
    if (event.type != SDL_KEYDOWN || event.key.repeat) return BringUpAction::None;
    const auto key = event.key.keysym.sym;
    const auto modifiers = event.key.keysym.mod;
    if (key == SDLK_x && (modifiers & KMOD_CTRL)) return BringUpAction::Exit;
    if (modifiers & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) return BringUpAction::None;
    if (key == SDLK_SPACE) return BringUpAction::Start;
    if (key == SDLK_RETURN) return BringUpAction::Stop;
    return BringUpAction::None;
}

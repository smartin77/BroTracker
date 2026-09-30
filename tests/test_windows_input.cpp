#define SDL_MAIN_HANDLED
#include "ui/windows/input.h"
#include "ui/windows/teensy_identity.h"
#include <cstdio>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s\n", #x); return 1; } } while (0)
int main() {
    SDL_Event e{}; e.type = SDL_KEYDOWN;
    e.key.keysym.sym = SDLK_SPACE; CHECK(MapWindowsAction(e) == BringUpAction::Start);
    e.key.repeat = 1; CHECK(MapWindowsAction(e) == BringUpAction::None); e.key.repeat = 0;
    e.key.keysym.sym = SDLK_RETURN; CHECK(MapWindowsAction(e) == BringUpAction::Stop);
    e.key.keysym.sym = SDLK_x; CHECK(MapWindowsAction(e) == BringUpAction::None);
    for (auto mod : {KMOD_LCTRL, KMOD_RCTRL}) {
        e.key.keysym.mod = mod; CHECK(MapWindowsAction(e) == BringUpAction::Exit);
    }
    e.key.repeat = 1; CHECK(MapWindowsAction(e) == BringUpAction::None);
    e = {}; e.type = SDL_QUIT; CHECK(MapWindowsAction(e) == BringUpAction::Exit);
    for (auto key : {SDLK_ESCAPE, SDLK_VOLUMEUP, SDLK_VOLUMEDOWN, SDLK_a}) {
        e = {}; e.type = SDL_KEYDOWN; e.key.keysym.sym = key;
        CHECK(MapWindowsAction(e) == BringUpAction::None);
    }
    for (auto type : {SDL_KEYUP, SDL_CONTROLLERBUTTONDOWN, SDL_JOYBUTTONDOWN, SDL_CONTROLLERAXISMOTION}) {
        e = {}; e.type = type; CHECK(MapWindowsAction(e) == BringUpAction::None);
    }
    CHECK(IsTeensyIdentity(L"USB\\VID_16C0&PID_048A&MI_00\\instance"));
    CHECK(IsTeensyIdentity(L"usb\\vid_16c0&pid_048a&mi_00\\other"));
    CHECK(!IsTeensyIdentity(L"USB\\VID_16C0&PID_048AB&MI_00"));
    CHECK(!IsTeensyIdentity(L"USB\\VID_1234&PID_048A&MI_00"));
    CHECK(!IsTeensyIdentity(L"BTHENUM\\USB\\VID_16C0&PID_048A"));
    std::puts("Windows keyboard mapping and Teensy identity checks passed");
}

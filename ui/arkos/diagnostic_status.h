#pragma once
// Diagnostic-only local status display. No serial controls or audio ownership.
#include <chrono>
#include <fstream>
#include <string>
#include "ui/text_renderer.h"
#include "ui/sdl/sdl_display_backend.h"
inline std::string DiagnosticStatusText(const char* path) {
    if(!path) return {};
    static std::string cached,previous;
    static uint32_t last=0;
    const auto now=SDL_GetTicks();
    if(previous!=path || now-last>=500) {
        char text[1024]{};
        std::ifstream file(path); file.read(text,sizeof(text)-1);
        cached=text;previous=path;last=now;
    }
    return cached;
}
inline int RunDiagnosticStatus(const char* path) {
    if(!LoadUiFont("assets/fonts/brotracker.btf")) return 3;
    Framebuffer frame(SCREEN_WIDTH,SCREEN_HEIGHT);
    SdlDisplayBackend display("BroTracker offline diagnostics");
    if(!display.Initialize(frame.Width(),frame.Height())) return 3;
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER|SDL_INIT_JOYSTICK);
    const auto began=SDL_GetTicks();
    while(SDL_GetTicks()-began<900000) {
        SDL_Event event;
        while(SDL_PollEvent(&event))
            if(event.type==SDL_QUIT || event.type==SDL_KEYDOWN || event.type==SDL_JOYBUTTONDOWN || event.type==SDL_CONTROLLERBUTTONDOWN) return 0;
        frame.Clear(Color{0,0,0});
        DrawWrappedFixedText(frame,8,8,frame.Width()-16,frame.Height()-16,
            DiagnosticStatusText(path),Color{255,255,255});
        if(!display.Present(frame)) return 3;
        SDL_Delay(100);
    }
    return 0;
}

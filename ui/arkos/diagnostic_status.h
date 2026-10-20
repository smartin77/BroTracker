#pragma once
// Diagnostic-only local status display. No serial controls or audio ownership.
#include <SDL.h>
#include <chrono>
#include <fstream>
#include <string>
#include <cstdlib>
#include <csignal>
#include <unistd.h>
#include <array>
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
// Only explicit diagnostic callers use these helpers. Notify our actual parent only.
inline bool DiagnosticExitEvent(const SDL_Event& e) {
    return e.type==SDL_QUIT || (e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_ESCAPE) ||
        (e.type==SDL_CONTROLLERBUTTONDOWN &&
         (e.cbutton.button==SDL_CONTROLLER_BUTTON_B || e.cbutton.button==SDL_CONTROLLER_BUTTON_BACK));
}
inline void RequestDiagnosticCancel() {
    const char* value=std::getenv("BROTRACKER_DIAGNOSTIC_RUNNER_PID");
    if(!value) return;
    char* end=nullptr; const long pid=std::strtol(value,&end,10);
    if(end && *end==0 && pid>1 && pid==getppid()) kill(pid,SIGUSR1);
}
inline void DrawDiagnosticStatus(Framebuffer& frame,const char* path) {
    // Fixed 3x integer glyphs at 640x480, independent of error/path length.
    static Framebuffer text(213,160);
    text.Clear(Color{0,0,0});
    DrawFixedText(text,4,4,"B: EXIT   Back / Escape",Color{255,255,0});
    const auto message=DiagnosticStatusText(path);
    int y=20; size_t start=0;
    while(start<message.size() && y<156) {
        const auto end=message.find('\n',start);
        const auto line=message.substr(start,end==std::string::npos?end:end-start);
        y+=DrawWrappedFixedText(text,4,y,205,156-y,line,Color{255,255,255});
        if(line.empty()) y+=12;
        if(end==std::string::npos) break;
        start=end+1;
    }
    for(unsigned y1=0;y1<text.Height();++y1) for(unsigned x=0;x<text.Width();++x)
        for(int dy=0;dy<3;++dy) for(int dx=0;dx<3;++dx)
            frame.SetPixel(x*3+dx,y1*3+dy,text.PixelData()[y1*text.Width()+x]);
}
inline int RunDiagnosticStatus(const char* path) {
    if(!LoadUiFont("assets/fonts/brotracker.btf")) return 3;
    Framebuffer frame(640,480);
    SdlDisplayBackend display("BroTracker offline diagnostics");
    if(!display.Initialize(frame.Width(),frame.Height())) { RequestDiagnosticCancel(); return 3; }
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER|SDL_INIT_JOYSTICK);
    std::array<SDL_GameController*,8> controllers{};
    auto open_controller=[&](int index) {
        if(!SDL_IsGameController(index)) return;
        for(auto& c:controllers) if(!c) { c=SDL_GameControllerOpen(index); break; }
    };
    for(int i=0;i<SDL_NumJoysticks();++i) open_controller(i);
    int code=0;
    bool done=false;
    const auto began=SDL_GetTicks();
    while(!done && SDL_GetTicks()-began<900000) {
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            if(DiagnosticExitEvent(event)) { RequestDiagnosticCancel(); done=true; }
            if(event.type==SDL_CONTROLLERDEVICEADDED) open_controller(event.cdevice.which);
            if(event.type==SDL_CONTROLLERDEVICEREMOVED) for(auto& c:controllers)
                if(c && !SDL_GameControllerGetAttached(c)) { SDL_GameControllerClose(c); c=nullptr; }
        }
        if(done) break;
        frame.Clear(Color{0,0,0}); DrawDiagnosticStatus(frame,path);
        if(!display.Present(frame)) { RequestDiagnosticCancel(); code=3; break; }
        SDL_Delay(100);
    }
    for(auto c:controllers) if(c) SDL_GameControllerClose(c);
    return code;
}

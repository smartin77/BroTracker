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
// Diagnostic-only input gate. Drain launch events and observe a released interval
// before accepting controller buttons; Escape/window-close remain explicit exits.
class DiagnosticCancelInput {
    bool armed_=false, released_=false;
    Uint32 released_at_=0;
public:
    void Reset() { armed_=false; released_=false; }
    void ObserveReleased(bool released,Uint32 now) {
        if(armed_) return;
        if(!released) { released_=false; return; }
        if(!released_) { released_=true; released_at_=now; }
        else if(now-released_at_>=250) armed_=true;
    }
    bool Accept(const SDL_Event& e,bool known_controller) {
        if(e.type==SDL_CONTROLLERDEVICEADDED || e.type==SDL_CONTROLLERDEVICEREMAPPED) Reset();
        return e.type==SDL_QUIT || (e.type==SDL_KEYDOWN && !e.key.repeat && e.key.keysym.sym==SDLK_ESCAPE) ||
            (armed_ && known_controller && e.type==SDL_CONTROLLERBUTTONDOWN &&
             (e.cbutton.button==SDL_CONTROLLER_BUTTON_B || e.cbutton.button==SDL_CONTROLLER_BUTTON_BACK));
    }
};
inline bool DiagnosticButtonsReleased() {
    int count=0; const auto* keys=SDL_GetKeyboardState(&count);
    for(int i=0;i<count;++i) if(keys[i]) return false;
    for(int i=0;i<SDL_NumJoysticks();++i) {
        auto* c=SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i));
        if(c) for(int b=0;b<SDL_CONTROLLER_BUTTON_MAX;++b)
            if(SDL_GameControllerGetButton(c,static_cast<SDL_GameControllerButton>(b))) return false;
    }
    return true;
}
inline std::string DiagnosticJsonString(const std::string& value) {
    std::string result="\"";
    for(unsigned char c:value) {
        if(c=='"' || c=='\\') result+='\\';
        result+=c<32?' ':char(c);
    }
    return result+'"';
}
inline void RequestDiagnosticCancel(const char* view,const SDL_Event& e,const char* status_path) {
    const char* value=std::getenv("BROTRACKER_DIAGNOSTIC_RUNNER_PID");
    if(!value) return;
    char* end=nullptr; const long pid=std::strtol(value,&end,10);
    if(!end || *end!=0 || pid<=1 || pid!=getppid()) return;
    const char* reason=e.type==SDL_QUIT?"user_window_close":e.type==SDL_KEYDOWN?"escape":
        e.cbutton.button==SDL_CONTROLLER_BUTTON_B?"controller_b":"controller_back";
    const char* kind=e.type==SDL_KEYDOWN?"key":e.type==SDL_CONTROLLERBUTTONDOWN?"button":"window";
    const int identifier=e.type==SDL_KEYDOWN?int(e.key.keysym.sym):e.type==SDL_CONTROLLERBUTTONDOWN?int(e.cbutton.button):0;
    const char* directory=std::getenv("BROTRACKER_DIAGNOSTIC_CANCEL_DIR");
    if(directory) {
        const std::string path=std::string(directory)+"/cancel-"+std::to_string(getpid())+".json";
        const std::string pending=path+".pending";
        char phase[160]{};
        if(status_path) { std::ifstream status(status_path); status.getline(phase,sizeof(phase)); }
        std::ofstream file(pending);
        file<<"{\"producer_pid\":"<<getpid()<<",\"view\":"<<DiagnosticJsonString(view)
            <<",\"phase\":"<<DiagnosticJsonString(phase)<<",\"reason\":"<<DiagnosticJsonString(reason)
            <<",\"sdl_event_type\":"<<e.type<<",\"input_kind\":"<<DiagnosticJsonString(kind)
            <<",\"input_id\":"<<identifier<<",\"controller_instance\":"
            <<(e.type==SDL_CONTROLLERBUTTONDOWN?int(e.cbutton.which):-1)<<"}\n";
        file.close();
        if(file) std::rename(pending.c_str(),path.c_str());
    }
    kill(pid,SIGUSR1); // Even a source-record I/O failure must not trap the user.
}
inline volatile sig_atomic_t diagnostic_view_shutdown=0;
inline void DiagnosticViewStop(int) { diagnostic_view_shutdown=1; }
class DiagnosticViewShutdown {
    struct sigaction previous_{};
public:
    DiagnosticViewShutdown() {
        diagnostic_view_shutdown=0;
        struct sigaction action{};action.sa_handler=DiagnosticViewStop;sigemptyset(&action.sa_mask);
        sigaction(SIGTERM,&action,&previous_);
    }
    ~DiagnosticViewShutdown() { sigaction(SIGTERM,&previous_,nullptr); }
};
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
    // Install before video initialization so even an early owned shutdown is safe.
    DiagnosticViewShutdown shutdown;
    SDL_SetHintWithPriority(SDL_HINT_NO_SIGNAL_HANDLERS,"1",SDL_HINT_OVERRIDE);
    if(!LoadUiFont("assets/fonts/brotracker.btf")) return 3;
    Framebuffer frame(640,480);
    SdlDisplayBackend display("BroTracker offline diagnostics");
    if(!display.Initialize(frame.Width(),frame.Height())) return 3;
    DiagnosticCancelInput cancel_input;
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
    while(!done && !diagnostic_view_shutdown && SDL_GetTicks()-began<900000) {
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            if(diagnostic_view_shutdown) break;
            const bool known=event.type==SDL_CONTROLLERBUTTONDOWN && SDL_GameControllerFromInstanceID(event.cbutton.which);
            if(cancel_input.Accept(event,known)) { RequestDiagnosticCancel("status",event,path); done=true; break; }
            if(event.type==SDL_CONTROLLERDEVICEADDED) open_controller(event.cdevice.which);
            if(event.type==SDL_CONTROLLERDEVICEREMOVED) for(auto& c:controllers)
                if(c && !SDL_GameControllerGetAttached(c)) { SDL_GameControllerClose(c); c=nullptr; }
        }
        if(done || diagnostic_view_shutdown) break;
        cancel_input.ObserveReleased(DiagnosticButtonsReleased(),SDL_GetTicks());
        frame.Clear(Color{0,0,0}); DrawDiagnosticStatus(frame,path);
        if(!display.Present(frame)) { code=3; break; }
        SDL_Delay(100);
    }
    for(auto c:controllers) if(c) SDL_GameControllerClose(c);
    return code;
}

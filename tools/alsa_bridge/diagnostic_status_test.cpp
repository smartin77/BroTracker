#include "ui/arkos/diagnostic_status.h"
#include <cassert>
#include <sys/wait.h>
#include <thread>
#include <iostream>
static volatile sig_atomic_t cancelled=0;
static void cancel_handler(int) { cancelled=1; }
int main(int argc,char** argv) {
    assert(argc==2);
    SDL_Event e{};
    e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_ESCAPE;assert(DiagnosticExitEvent(e));
    e.key.keysym.sym=SDLK_SPACE;assert(!DiagnosticExitEvent(e));
    e.type=SDL_QUIT;assert(DiagnosticExitEvent(e));
    e.type=SDL_CONTROLLERBUTTONDOWN;
    for(auto button:{SDL_CONTROLLER_BUTTON_B,SDL_CONTROLLER_BUTTON_BACK}) {
        e.cbutton.button=button;assert(DiagnosticExitEvent(e));
    }
    e.cbutton.button=SDL_CONTROLLER_BUTTON_A;assert(!DiagnosticExitEvent(e));
    // Only notify the owning parent, including when the status window closes.
    signal(SIGUSR1,cancel_handler);
    const auto owner=std::to_string(getpid());
    setenv("BROTRACKER_DIAGNOSTIC_RUNNER_PID",owner.c_str(),1);
    const auto child=fork();assert(child>=0);
    if(child==0) { RequestDiagnosticCancel(); _exit(0); }
    int status=0;while(waitpid(child,&status,0)<0 && errno==EINTR) {}
    assert(WIFEXITED(status) && WEXITSTATUS(status)==0 && cancelled);
    unsetenv("BROTRACKER_DIAGNOSTIC_RUNNER_PID");
    assert(LoadUiFont("assets/fonts/brotracker.btf"));
    const std::string path=argv[1];
    { std::ofstream f(path);f<<"FAILED\nReadable short error\nDetails in logs.\n"; }
    Framebuffer frame(640,480);frame.Clear(Color{0,0,0});DrawDiagnosticStatus(frame,path.c_str());
    bool yellow=false;
    for(int y=0;y<480;y+=3) for(int x=0;x<639;x+=3) {
        const auto p=frame.PixelData()[y*640+x];yellow=yellow||(p.r==255 && p.g==255 && p.b==0);
        for(int dy=0;dy<3;++dy) for(int dx=0;dx<3;++dx) {
            const auto q=frame.PixelData()[(y+dy)*640+x+dx];assert(p.r==q.r && p.g==q.g && p.b==q.b);
        }
    }
    assert(yellow);
    // Real dummy-SDL window: a close event terminates it rather than hiding it.
    setenv("SDL_VIDEODRIVER","dummy",1);setenv("SDL_AUDIODRIVER","dummy",1);
    std::thread closer([]{SDL_Delay(300);SDL_Event quit{};quit.type=SDL_QUIT;SDL_PushEvent(&quit);});
    const int result=RunDiagnosticStatus(path.c_str());closer.join();assert(result==0);
    std::cout<<"Diagnostic cancel events, parent notification, fixed 3x text and window close passed\n";
}

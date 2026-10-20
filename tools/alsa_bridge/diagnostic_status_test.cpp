#include "ui/arkos/diagnostic_status.h"
#include <cassert>
#include <sys/wait.h>
#include <thread>
#include <iostream>
static volatile sig_atomic_t cancelled=0;
static void cancel_handler(int) { cancelled=1; }
int main(int argc,char** argv) {
    assert(argc==2);
    DiagnosticCancelInput gate;
    SDL_Event e{};
    e.type=SDL_CONTROLLERBUTTONDOWN;e.cbutton.button=SDL_CONTROLLER_BUTTON_B;
    assert(!gate.Accept(e,true)); // Launch/held B must not cancel.
    gate.ObserveReleased(false,0);gate.ObserveReleased(false,1000);assert(!gate.Accept(e,true));
    gate.ObserveReleased(true,1100);gate.ObserveReleased(true,1349);assert(!gate.Accept(e,true));
    gate.ObserveReleased(true,1350);assert(gate.Accept(e,true));
    assert(!gate.Accept(e,false)); // Unopened/unknown controller cannot claim B.
    for(int button=0;button<SDL_CONTROLLER_BUTTON_MAX;++button) {
        e.cbutton.button=button;
        assert(gate.Accept(e,true)==(button==SDL_CONTROLLER_BUTTON_B || button==SDL_CONTROLLER_BUTTON_BACK));
    }
    e.type=SDL_CONTROLLERDEVICEADDED;assert(!gate.Accept(e,true));
    e.type=SDL_CONTROLLERBUTTONDOWN;e.cbutton.button=SDL_CONTROLLER_BUTTON_BACK;
    assert(!gate.Accept(e,true)); // New controller must release before arming.
    gate.ObserveReleased(true,2000);gate.ObserveReleased(true,2250);assert(gate.Accept(e,true));
    e.type=SDL_CONTROLLERDEVICEREMOVED;assert(!gate.Accept(e,true));
    gate.Reset();
    e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_ESCAPE;e.key.repeat=0;assert(gate.Accept(e,false));
    e.key.repeat=1;assert(!gate.Accept(e,false));
    e.key.repeat=0;e.key.keysym.sym=SDLK_RETURN;assert(!gate.Accept(e,false));
    e.type=SDL_QUIT;assert(gate.Accept(e,false));
    // Only notify the owning parent, including when the status window closes.
    signal(SIGUSR1,cancel_handler);
    const auto owner=std::to_string(getpid());
    setenv("BROTRACKER_DIAGNOSTIC_RUNNER_PID",owner.c_str(),1);
    const std::string evidence=std::string(argv[1]).substr(0,std::string(argv[1]).find_last_of('/'));
    setenv("BROTRACKER_DIAGNOSTIC_CANCEL_DIR",evidence.c_str(),1);
    { std::ofstream f(argv[1]);f<<"PREPARING source test\n"; }
    const auto child=fork();assert(child>=0);
    if(child==0) { RequestDiagnosticCancel("status",e,argv[1]); _exit(0); }
    int status=0;while(waitpid(child,&status,0)<0 && errno==EINTR) {}
    assert(WIFEXITED(status) && WEXITSTATUS(status)==0 && cancelled);
    {
        std::ifstream f(evidence+"/cancel-"+std::to_string(child)+".json");
        const std::string source((std::istreambuf_iterator<char>(f)),{});
        assert(source.find("\"view\":\"status\"")!=std::string::npos);
        assert(source.find("\"reason\":\"user_window_close\"")!=std::string::npos);
        assert(source.find("PREPARING source test")!=std::string::npos);
    }
    unsetenv("BROTRACKER_DIAGNOSTIC_RUNNER_PID");
    unsetenv("BROTRACKER_DIAGNOSTIC_CANCEL_DIR");
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

#include "ui/arkos/offline_click_mode.h"
#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
struct FakeSerial {
    LivePlaybackView view;
    bool playing=false,pending=false,ready=true,paused=false;
    int starts=0,stops=0;
    FakeSerial() {
        BroTracker::DevicePatternSnapshot s; s.version=2; s.tempo_hundredths=12500;
        s.pattern.active_rows=1;s.pattern.active_channels=1;
        view.device_pattern=s; view.channel_gains=BroTracker::HostGainState{};
        view.fresh=true; view.position.valid=true; view.position.running=true;
    }
    bool Connected(){return true;} bool Playing(){return playing;} bool Paused(){return paused;}
    bool AudioOutputConfirmed(){return true;} const char* ActiveAudioOutput(){return "bridge";}
    bool Start(){++starts;pending=true;return true;} bool StartPending(){return pending;}
    bool TransportPending(){return pending;} bool DiagnosticReleaseReady(){return ready;}
    std::optional<LivePlaybackView> PlaybackView(uint32_t){return view;}
};
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
void command(const std::string& dir,const char* text){std::ofstream(dir+"/command.txt")<<text;}
int main(int argc,char** argv){
    require(argc==2,"test output dir required");
    const auto base=std::filesystem::path(argv[1]);std::filesystem::create_directories(base);
    auto make=[&](){std::string p=(base/"synthetic-mode-XXXXXX").string();require(mkdtemp(&p[0]),"mkdtemp");return p;};
    FakeSerial serial; const auto began=OfflineMonotonicNs();
    OfflineClickMode disabled(nullptr);disabled.Tick(serial,0,began);require(serial.starts==0,"normal startup unchanged");
    const auto dir=make();OfflineClickMode mode(dir.c_str());
    mode.Tick(serial,0,began+100000000);require(std::filesystem::exists(dir+"/workload.json"),"readback before start");
    command(dir,"START\n");mode.Tick(serial,0,began+200000000);require(serial.starts==1,"one START");
    serial.playing=true;serial.pending=false;serial.view.position.tick=1;
    mode.Tick(serial,0,began+300000000);require(!mode.Exit(),"first telemetry is not ready");
    serial.view.position.tick=2;mode.Tick(serial,0,began+400000000);
    command(dir,"RELEASE\n");serial.ready=false;mode.Tick(serial,0,began+500000000);
    require(!mode.Exit(),"partial command must drain before release");
    serial.ready=true;mode.Tick(serial,0,began+600000000);require(mode.ReleaseRequested(),"runner controls release");
    require(serial.stops==0,"diagnostic release sends no STOP");
    // Publish only after the caller has synchronously closed its transport.
    mode.SerialReleased(began+700000000);
    std::ifstream f(dir+"/status.json");std::string text((std::istreambuf_iterator<char>(f)),{});
    require(text.find("\"phase\":\"released\"")!=std::string::npos,"release status");
    const auto early=make();OfflineClickMode bad(early.c_str());bad.SerialReleased();require(bad.Failed(),"premature exit fails");
    const auto invalid=make();OfflineClickMode wrong(invalid.c_str());command(invalid,"RELEASE\n");
    FakeSerial untouched;wrong.Tick(untouched,0,began+800000000);require(wrong.Failed(),"premature release fails");
    const auto timeout=make();OfflineClickMode timed(timeout.c_str());timed.Tick(untouched,0,began+211000000000ULL);
    require(timed.Failed(),"watchdog bounds abandoned setup");
    const auto existing=make();OfflineClickMode no_replace(existing.c_str());untouched.playing=true;
    no_replace.Tick(untouched,0,began+900000000);require(no_replace.Failed()&&untouched.starts==0,"existing playback not restarted");
    const auto unavailable=make()+"/missing";OfflineClickMode io(unavailable.c_str());untouched.playing=false;
    io.Tick(untouched,0,began+1000000000);require(io.Failed(),"status/workload IO fails explicitly");
    std::cout<<"Offline BTX lifecycle checks passed; no physical serial/audio.\n";
}

#include "ui/bringup_controls.h"
#include <stdexcept>
#include <string>
#include <cstring>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (0)
struct FakeTransport : SerialTransport {
    bool online = true;
    bool hold_write = false;
    unsigned write_limit=UINT32_MAX;
    std::string incoming, outgoing;
    bool Open(FILE*, const char*) override { return online; }
    void Close() override {}
    bool Healthy() override { return online; }
    int Read(char* data, unsigned size) override {
        unsigned count = std::min(size, static_cast<unsigned>(incoming.size()));
        std::memcpy(data, incoming.data(), count); incoming.erase(0, count); return count;
    }
    int Write(const char* data, unsigned size) override { if (hold_write) return 0; size=std::min(size,write_limit); outgoing.append(data, size); return size; }
    const char* Error() const override { return "fake disconnect"; }
    void Expect(const char* value) { if(outgoing != value) std::fprintf(stderr,"expected [%s], received [%s]\n",value,outgoing.c_str()); CHECK(outgoing == value); outgoing.clear(); }
};
std::unique_ptr<SerialTransport> MakeSerialTransport() { return std::make_unique<FakeTransport>(); }
struct Test {
    FakeTransport* port = new FakeTransport;
    BringUpSerial serial{nullptr, std::unique_ptr<SerialTransport>(port)};
    BringUpControls controls{serial, nullptr};
    unsigned now = 0;
    void Tick() { controls.Tick(now++); }
    void Reply(const char* reply) { port->incoming = reply; Tick(); }
    void Action(BringUpAction action) { controls.Request(action, "test", now); }
    void Ready(const char* state = "BTTEST1 STATE DONE\n") {
        Tick(); port->Expect("BTTEST1 HELLO\n"); Reply(state); CHECK(serial.Connected());
    }
    void Send(const char* value) { Tick(); Tick(); port->Expect(value); }
};
int main() {
    try {
        { Test t;std::string events;
          t.serial.SetTransportObserver([](void* context,const char* event,std::uint32_t){*static_cast<std::string*>(context)+=std::string(event)+",";},&events);
          t.Ready("BTTEST1 STATE IDLE\nBTPATTERN1 TRANSPORTCAP 1\n");events.clear();
          CHECK(t.serial.Start());t.Send("BTTEST1 START\n");CHECK(events.empty());
          t.Reply("BTTEST1 STARTED\n");CHECK(events=="START,");
          CHECK(t.serial.Pause());t.Send("BTTEST1 PAUSE\n");t.Reply("BTTEST1 PAUSED\n");
          CHECK(t.serial.Continue());t.Send("BTTEST1 CONTINUE\n");t.Reply("BTTEST1 CONTINUED\n");
          CHECK(t.serial.Stop());t.Send("BTTEST1 STOP\n");t.Reply("BTTEST1 STOPPED\n");
          CHECK(events=="START,PAUSE,CONTINUE,STOP,");
          // Ignore unsolicited PAUSE replies; no fabricated recording transition.
          t.Reply("BTTEST1 PAUSED\n");CHECK(events=="START,PAUSE,CONTINUE,STOP,");
        }

        { Test t;t.Ready("BTTEST1 STATE PLAYING\nBTMUTE1 CAP 1\nBTMUTE1 STATE 1 0 1 0\n");
          t.port->write_limit=3;CHECK(t.serial.ToggleChannelMute(0));
          t.Tick();t.Tick();CHECK(!t.port->outgoing.empty());
          t.Action(BringUpAction::Stop);
          for(unsigned n=0;n<32;++n)t.Tick();
          t.port->Expect("BTMUTE1 SET 1 1 1\nBTTEST1 STOP\n");
        }

        { Test t;t.Ready("BTTEST1 STATE IDLE\nBTMUTE1 CAP 1\nBTMUTE1 STATE 1 0 1 0\n");
          CHECK(t.serial.MuteAvailable());CHECK(t.serial.ToggleChannelMute(0));CHECK(t.serial.ToggleChannelMute(1));
          CHECK(t.serial.PlaybackView(t.now)->mute_mask==0);t.Send("BTMUTE1 SET 1 1 3\n");
          t.Reply("BTMUTE1 SUBMITTED 1 1\nBTMUTE1 APPLIED 1 1\nBTMUTE1 STATE 1 1 2 3\n");
          CHECK(t.serial.PlaybackView(t.now)->mute_mask==3);CHECK(t.serial.ToggleChannelMute(7));
          t.port->online=false;t.Tick();t.port->online=true;t.now+=1001;
          t.Tick();t.port->Expect("BTTEST1 HELLO\n");
          t.Reply("BTTEST1 STATE IDLE\nBTMUTE1 CAP 1\nBTMUTE1 STATE 1 1 2 3\n");
          t.Tick();t.port->Expect("");CHECK(t.serial.PlaybackView(t.now)->mute_mask==3);
        }
        { Test t;t.Ready("BTTEST1 STATE IDLE\n");CHECK(!t.serial.MuteAvailable());CHECK(!t.serial.ToggleChannelMute(0)); }
        for(unsigned channel:{1u,12u,16u}) {
            Test t; t.serial.SetMidiOutputChannel(channel);
            t.Ready("BTTEST1 STATE IDLE\nBTMIDI1 CAP 1\n");
            CHECK(!t.serial.Start()); CHECK(!t.serial.Playing());
            const auto command="BTMIDI1 SET 1 "+std::to_string(channel)+"\n";
            t.Tick(); t.port->Expect(command.c_str());
            const auto ack="BTMIDI1 SELECTED 1 "+std::to_string(channel)+"\n";
            t.Reply(ack.c_str()); CHECK(std::strstr(t.serial.MidiStatus(),"acknowledged"));
            t.Tick(); t.port->Expect(""); CHECK(!t.serial.Playing());
            t.port->online=false; t.Tick(); t.port->online=true; t.now+=1001;
            t.Tick(); t.port->Expect("BTTEST1 HELLO\n");
            t.Reply("BTTEST1 STATE IDLE\nBTMIDI1 CAP 1\n");
            t.Tick(); t.port->Expect(command.c_str()); t.Reply(ack.c_str());
            t.Tick(); t.port->Expect(""); CHECK(!t.serial.Playing()); CHECK(t.serial.Start());
        }
        { Test t; t.Ready("BTTEST1 STATE PLAYING\nBTMIDI1 CAP 1\n");
          t.Tick(); t.port->Expect("BTMIDI1 SET 1 12\n"); t.Reply("BTMIDI1 SELECTED 1 12\n");
          for(const auto output:{"BTMIDI1 OUTPUT 1 1 1 1 5\n", "BTMIDI1 OUTPUT 1 0 1 1 6\n"}) {
              t.Reply(output); CHECK(t.serial.Playing());
              CHECK(std::strstr(t.serial.MidiStatus(),"cleanup uncertain"));
              CHECK(std::strstr(t.serial.Status(),"playing"));
          }
          t.Reply("BTMIDI1 OUTPUT 1 1 0 0 0\n");
          CHECK(std::strstr(t.serial.MidiStatus(),"external delivery unknown"));
          t.Reply("BTMIDI1 OUTPUT 1 1 0 0 99\n"); // Malformed status cannot erase last health.
          CHECK(std::strstr(t.serial.MidiStatus(),"external delivery unknown"));
          t.Reply("BTTEST1 STATE ERROR\n"); CHECK(!t.serial.Playing());
          CHECK(std::strstr(t.serial.Status(),"playback ERROR"));
        }
        for(const auto response:{"BTMIDI1 ERROR 1 restart-required\n","BTMIDI1 ERROR 1 invalid-channel\n","BTMIDI1 SELECTED 1 11\n","BTTEST1 ERROR command\n"}) {
            Test t; t.Ready("BTTEST1 STATE IDLE\nBTMIDI1 CAP 1\n");
            t.Tick(); t.port->Expect("BTMIDI1 SET 1 12\n"); t.Reply(response);
            CHECK(!t.serial.Start()); CHECK(std::strstr(t.serial.Status(),"MIDI setup failed"));
            t.Tick(); t.port->Expect("");
        }
        { Test t; t.Ready("BTTEST1 STATE IDLE\nBTMIDI1 CAP 1\n");
          t.Tick(); t.port->Expect("BTMIDI1 SET 1 12\n"); t.now+=6001; t.Tick();
          CHECK(!t.serial.Start()); CHECK(std::strstr(t.serial.Status(),"MIDI setup failed")); }
        { Test t; t.Ready("BTTEST1 STATE IDLE\n"); CHECK(t.serial.Start());
          CHECK(std::strstr(t.serial.MidiStatus(),"unsupported")); }
        for(auto choice : {BroTracker::AudioOutputChoice::Bridge,BroTracker::AudioOutputChoice::Pt8211,BroTracker::AudioOutputChoice::AudioShield}) {
            Test t; t.serial.SetAudioOutputPreference(choice);
            t.Ready("BTTEST1 STATE IDLE\nBTAUDIO1 CAP 1\n");
            CHECK(!t.serial.Start());
            const std::string command=std::string("BTAUDIO1 SET 1 ")+BroTracker::AudioOutputName(choice)+"\n";
            t.Tick(); t.port->Expect(command.c_str());
            const std::string ack=std::string("BTAUDIO1 SELECTED 1 ")+BroTracker::AudioOutputName(choice)+"\n";
            t.Reply(ack.c_str()); CHECK(std::strcmp(t.serial.ActiveAudioOutput(),BroTracker::AudioOutputName(choice))==0); CHECK(t.serial.AudioOutputConfirmed());
            CHECK(!t.serial.Playing()); t.port->Expect("");
            t.port->online=false;t.Tick(); CHECK(!t.serial.AudioOutputConfirmed());
            t.port->online=true;t.now+=1001;t.Tick();t.port->Expect("BTTEST1 HELLO\n");
            t.Reply("BTTEST1 STATE PLAYING\nBTAUDIO1 CAP 1\n");t.Tick();t.port->Expect(command.c_str());
            t.Reply(ack.c_str()); CHECK(std::strcmp(t.serial.ActiveAudioOutput(),BroTracker::AudioOutputName(choice))==0);t.Tick();t.port->Expect(""); CHECK(t.serial.Playing());
        }
        { Test t; t.Ready("BTTEST1 STATE IDLE\nBTAUDIO1 CAP 1\n");
          t.Tick();t.port->Expect("BTAUDIO1 SET 1 default\n");
          t.Reply("BTAUDIO1 SELECTED 1 bridge\n");CHECK(t.serial.AudioOutputConfirmed()); }
        { Test t; t.Ready("BTTEST1 STATE IDLE\nBTAUDIO1 CAP 1\n");
          t.Tick();t.port->Expect("BTAUDIO1 SET 1 default\n");
          t.Reply("BTAUDIO1 SELECTED 1 pt8211\n");CHECK(t.serial.AudioOutputConfirmed());
          CHECK(std::strcmp(t.serial.ActiveAudioOutput(),"pt8211")==0); }
        { Test t; t.Ready("BTTEST1 STATE IDLE\nBTAUDIO1 CAP 1\n");
          t.Tick();t.port->Expect("BTAUDIO1 SET 1 default\n");
          t.Reply("BTAUDIO1 SELECTED 1 audio_shield\n");
          CHECK(t.serial.AudioOutputConfirmed());CHECK(std::strcmp(t.serial.ActiveAudioOutput(),"audio_shield")==0); }
        for(const char* reason : {"shield-enable-failed","shield-readback-failed","shield-config-mismatch"}) {
          Test t;t.serial.SetAudioOutputPreference(BroTracker::AudioOutputChoice::AudioShield);
          t.Ready("BTTEST1 STATE IDLE\nBTAUDIO1 CAP 1\n");t.Tick();t.port->Expect("BTAUDIO1 SET 1 audio_shield\n");
          const std::string error=std::string("BTAUDIO1 ERROR 1 ")+reason+" active=audio_shield\n";
          t.Reply(error.c_str());CHECK(!t.serial.AudioOutputConfirmed());CHECK(!t.serial.Start());
          t.Tick();t.port->Expect("");CHECK(std::strstr(t.serial.Status(),"Audio selection failed"));
        }
        { Test t; t.serial.SetAudioOutputPreference(BroTracker::AudioOutputChoice::Pt8211);
          t.Ready();t.Tick();t.port->Expect("BTAUDIO1 SET 1 pt8211\n");
          t.Reply("BTAUDIO1 SELECTED 1 bridge\n");CHECK(!t.serial.AudioOutputConfirmed());CHECK(!t.serial.Start()); }
        { Test t; t.serial.SetAudioOutputPreference(BroTracker::AudioOutputChoice::Pt8211);
          t.Ready();t.Tick();t.port->Expect("BTAUDIO1 SET 1 pt8211\n");
          t.Reply("BTAUDIO1 ERROR 1 restart-required active=bridge\n");CHECK(!t.serial.Start());
          t.Tick();t.port->Expect(""); }
        { Test t; t.serial.SetAudioOutputPreference(BroTracker::AudioOutputChoice::Pt8211);
          t.Ready();t.Tick();t.port->Expect("BTAUDIO1 SET 1 pt8211\n");
          t.now+=6001;t.Tick();CHECK(!t.serial.AudioOutputConfirmed());CHECK(!t.serial.Start());
          CHECK(std::strstr(t.serial.Status(),"Audio selection failed")); }

        { Test t; t.Ready("BTTEST1 STATE IDLE\n");
          CHECK(!t.serial.PlaybackView(t.now)->callback_performance);
          t.Reply("BTPATTERN1 PERF 1 100 99 2249 55901 0 1741496\n");
          auto view=t.serial.PlaybackView(t.now); CHECK(view->callback_performance);
          CHECK(view->callback_performance->maximum_cycles==55901);
          t.Reply("BTPATTERN1 PERF 1 100 99 2249 999 0 0\n");
          CHECK(t.serial.PlaybackView(t.now)->callback_performance->maximum_cycles==55901);
          CHECK(!t.serial.PlaybackView(t.now+3000)->callback_performance);
          t.port->online=false; t.Tick(); CHECK(!t.serial.PlaybackView(t.now)); }

        { Test t; t.Ready("BTTEST1 STATE IDLE\n");
          t.Reply("BTPATTERN1 TRANSPORTCAP 1\n");
          t.Action(BringUpAction::ResetStop); t.Send("BTTEST1 STOP\n");
          t.Action(BringUpAction::ResetStop); t.Reply("BTTEST1 STOPPED\n");
          CHECK(!t.controls.Quit()); t.Action(BringUpAction::ResetStop); t.Send("BTTEST1 STOP\n");
          t.Reply("BTTEST1 STOPPED\n");
          t.Action(BringUpAction::PatternToggle); t.Send("BTTEST1 START\n");
          t.Action(BringUpAction::PatternToggle); t.Reply("BTTEST1 STARTED\n");
          t.Action(BringUpAction::PatternToggle); t.Send("BTTEST1 PAUSE\n");
          t.Action(BringUpAction::PatternToggle); t.Reply("BTTEST1 PAUSED\n");
          CHECK(t.serial.Paused());
          t.Action(BringUpAction::PatternToggle); t.Send("BTTEST1 CONTINUE\n");
          t.Reply("BTTEST1 CONTINUED\n"); CHECK(t.serial.Playing());
          t.Action(BringUpAction::PatternToggle); t.Send("BTTEST1 PAUSE\n");
          t.Action(BringUpAction::Exit); t.Reply("BTTEST1 PAUSED\n");
          t.port->Expect("");CHECK(t.controls.Quit()); }
        { Test t; t.Ready("BTTEST1 STATE PLAYING\n"); // No capability: preserve legacy restart.
          t.Action(BringUpAction::PatternToggle); t.Send("BTTEST1 START\n"); }
        { Test t; t.Ready("BTTEST1 STATE PLAYING\n");
          t.Reply("BTPATTERN1 TRANSPORTCAP 1\n"); t.now = 1000; t.Send("BTTEST1 STATUS\n");
          t.Action(BringUpAction::PatternToggle); t.Action(BringUpAction::Exit);
          t.Reply("BTTEST1 STATE PLAYING\n"); t.Send("BTTEST1 PAUSE\n");
          t.Reply("BTTEST1 PAUSED\n"); t.port->Expect(""); CHECK(t.controls.Quit()); }
        { Test t; t.Ready("BTTEST1 STATE PLAYING\n");
          t.Reply("BTPATTERN1 TRANSPORTCAP 1\n"); t.now = 1000; t.Send("BTTEST1 STATUS\n");
          t.Action(BringUpAction::PatternToggle); t.Reply("BTTEST1 STATE ERROR\n");
          t.Tick(); t.port->Expect(""); CHECK(!t.serial.TransportPending()); }
        { Test t; t.Action(BringUpAction::Start); CHECK(!t.serial.StartPending());
          t.Action(BringUpAction::Stop); CHECK(!t.controls.Quit());
          t.Action(BringUpAction::Exit); CHECK(t.controls.Quit()); }
        { Test t; t.Ready(); t.Action(BringUpAction::Stop); CHECK(!t.controls.Quit());
          t.Action(BringUpAction::Start); t.Action(BringUpAction::Start);
          t.Send("BTTEST1 START\n"); t.Reply("BTTEST1 STARTED\n");
          t.Action(BringUpAction::Start); t.Send("BTTEST1 START\n"); // restart
          t.Reply("BTTEST1 STARTED\n");
          t.Action(BringUpAction::Stop); t.Action(BringUpAction::Stop); t.Action(BringUpAction::Start);
          t.Send("BTTEST1 STOP\n"); t.Reply("BTTEST1 STOPPED\n");
          CHECK(!t.controls.Quit() && !t.controls.StopPending());
          t.Action(BringUpAction::Stop); CHECK(!t.controls.Quit());
          t.Action(BringUpAction::Start); t.Send("BTTEST1 START\n");
          t.Reply("BTTEST1 STARTED\nBTTEST1 DONE\n");
          t.Action(BringUpAction::Exit); CHECK(t.controls.Quit()); t.port->Expect(""); }
        // Closing the terminal never synthesizes STOP, even with START queued.
        { Test t; t.Ready();t.Action(BringUpAction::Start);t.Action(BringUpAction::Exit);
          CHECK(t.controls.Quit());t.port->Expect(""); }
        { Test t; t.Ready("BTTEST1 STATE PLAYING\n"); t.Action(BringUpAction::Stop);
          t.Action(BringUpAction::Exit); t.Send("BTTEST1 STOP\n");
          t.Reply("BTTEST1 STOPPED\n"); CHECK(t.controls.Quit()); }
        { Test t; t.Ready("BTTEST1 STATE PLAYING\n"); t.Action(BringUpAction::Exit);
          t.port->Expect("");CHECK(t.controls.Quit() && !t.serial.Stopped()); }
        for (bool exiting : {false, true}) {
          Test t; t.Ready(); t.Action(BringUpAction::Start);
          if (exiting) t.Action(BringUpAction::Exit);
          t.port->online = false; t.Tick();
          CHECK(!t.serial.Connected() && t.controls.Quit() == exiting);
          if (!exiting) {
            t.port->online = true; t.now = 1000; t.Tick(); t.port->Expect("BTTEST1 HELLO\n");
            t.Reply("BTTEST1 STATE DONE\n"); t.Tick(); t.port->Expect("");
            CHECK(!t.serial.StartPending());
          }
        }
        { Test t; t.Ready(); t.Action(BringUpAction::Start);
          t.port->hold_write = true; t.Tick(); t.Tick();
          t.port->online = false; t.Tick();
          CHECK(!t.serial.StartCancelled());
          CHECK(std::strstr(t.serial.Status(), "unknown")); }
        { Test t; t.Ready(); t.Action(BringUpAction::StopExit); CHECK(t.controls.Quit()); }
        { Test t; t.Ready("BTTEST1 STATE PLAYING\n"); t.Action(BringUpAction::StopExit);
          t.Send("BTTEST1 STOP\n"); t.Reply("BTTEST1 STOPPED\n");
          CHECK(!t.controls.Quit()); t.Action(BringUpAction::StopExit); CHECK(t.controls.Quit()); }
        std::puts("Shared controls: start/restart/stop/exit/order/timeout/disconnect checks passed");
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}

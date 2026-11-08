#include "ui/bringup_controls.h"
#include "ui/host_logging.h"
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
    BringUpSerial serial;
    BringUpControls controls;
    explicit Test(FILE* log=nullptr):serial(log,std::unique_ptr<SerialTransport>(port)),controls(serial,log) {}
    LocalPatternEditor editor;
    unsigned now = 0;
    unsigned snapshot_id = 1;
    void Tick() { controls.Tick(now++); }
    void Reply(const char* reply) { port->incoming = reply; Tick(); }
    void Action(BringUpAction action) { controls.Request(action, "test", now); }
    void Ready(const char* state = "BTTEST1 STATE DONE\n",bool synchronize=true) {
        // An explicit route starts AUDIO immediately after STATE. Its tests own
        // the exact SET/ACK/error exchange before allowing snapshot readiness.
        if(!synchronize) {
            Tick();port->Expect("BTTEST1 HELLO\n");Reply(state);CHECK(serial.Connected());return;
        }
        Tick(); port->Expect("BTTEST1 HELLO\n"); Reply("BTTEST1 STATE IDLE\n");
        Synchronize(); Reply(state); CHECK(serial.Connected());
    }
    void Synchronize() {
        serial.SyncEditor(editor,now); // Retain the same editor across reconnect recovery.
        Reply("BTPATTERN1 POS 1 0 0 0 0 0 1 1\n");
        Reply("BTPATTERN1 SNAPCAP 1\n"); Tick();
        const auto request="BTPATTERN1 GET 1 "+std::to_string(snapshot_id)+"\n";
        port->Expect(request.c_str());
        BroTracker::RealtimePattern pattern; pattern.active_rows=pattern.active_channels=1;
        BroTracker::PatternSnapshotSender sender(pattern,12500); CHECK(sender.Begin(snapshot_id++,now));
        while(sender.HasData()) {
            std::size_t size;const auto* data=sender.Data(size);
            const std::string line(data,size);sender.Consume(size);Reply(line.c_str());
        }
        serial.SyncEditor(editor,now);CHECK(!serial.UserActionsBlocked());
    }
    void Send(const char* value) { Tick(); Tick(); port->Expect(value); }
};
void AudioReadinessRegressions() {
    BroTracker::ConfigureHostLogging(BroTracker::HostLogMode::Debug);
    FILE* log=std::tmpfile();CHECK(log);
    const auto log_text=[&]() {
        std::fflush(log);std::rewind(log);std::string text;char line[1024];
        while(std::fgets(line,sizeof(line),log))text+=line;
        std::fseek(log,0,SEEK_END);return text;
    };
    // Use real shared controls and serial state with deterministic fake I/O.
    for(const char* state:{"BTTEST1 STATE PAUSED\n","BTTEST1 STATE PLAYING\n"}) {
        Test t(log);t.Ready(state); // Existing playback is established before the conflict.
        t.serial.SetAudioOutputPreference(BroTracker::AudioOutputChoice::Pt8211);
        t.Reply("BTAUDIO1 CAP 1\n");
        t.Tick();t.port->Expect("BTAUDIO1 SET 1 pt8211\n");
        t.Reply("BTAUDIO1 ERROR 1 restart-required active=bridge\n");
        CHECK(!t.serial.AudioOutputConfirmed());CHECK(!t.serial.AudioTransportReady());
        CHECK(std::strcmp(t.serial.ActiveAudioOutput(),"bridge")==0);
        t.Reply("BTPATTERN1 TRANSPORTCAP 1\n");
        t.port->Expect(""); // Rejected setup must not stop or restart existing playback.
        CHECK(std::strstr(t.serial.Status(),"requested=pt8211 active=bridge"));
        CHECK(std::strstr(t.serial.Status(),"restart Teensy"));
        if(t.serial.Playing()) {
            t.Action(BringUpAction::PatternToggle);t.Send("BTTEST1 PAUSE\n");t.Reply("BTTEST1 PAUSED\n");
        }
        t.Action(BringUpAction::PatternToggle);t.Tick();t.port->Expect("");CHECK(t.serial.Paused());
        CHECK(log_text().find("Controls: Audio selection failed: requested=pt8211 active=bridge; restart-required; restart Teensy")!=std::string::npos);
        t.Action(BringUpAction::ResetStop);t.Send("BTTEST1 STOP\n");t.Reply("BTTEST1 STOPPED\n");
        t.Action(BringUpAction::PatternToggle);t.Tick();t.port->Expect("");
        CHECK(!t.serial.Start());CHECK(!t.serial.AudioTransportReady());
        CHECK(std::strstr(t.serial.Status(),"restart Teensy"));
        CHECK(log_text().find("STOP acknowledged; transport stopped")!=std::string::npos);
        CHECK(log_text().find("STOP acknowledged; ready")==std::string::npos);
        // New connection can clear the rejection only with a matching ACK.
        t.port->online=false;t.Tick();CHECK(!t.serial.AudioTransportReady());
        t.port->online=true;t.now+=1001;t.Tick();t.port->Expect("BTTEST1 HELLO\n");
        t.Reply("BTTEST1 STATE IDLE\nBTAUDIO1 CAP 1\n");t.Tick();t.port->Expect("BTAUDIO1 SET 1 pt8211\n");
        CHECK(!t.serial.AudioTransportReady());CHECK(std::strcmp(t.serial.ActiveAudioOutput(),"unknown")==0);
        t.Action(BringUpAction::PatternToggle);t.Tick();t.port->Expect("");
        t.Reply("BTAUDIO1 SELECTED 1 pt8211\n");CHECK(t.serial.AudioTransportReady());
        t.Tick();t.port->Expect("");CHECK(!t.serial.Playing());
    }
    for(const char* response:{"pending","timeout","BTAUDIO1 SELECTED 1 bridge\n","BTTEST1 ERROR command\n"}) {
        Test t(log);t.Ready("BTTEST1 STATE IDLE\n");
        t.serial.SetAudioOutputPreference(BroTracker::AudioOutputChoice::Pt8211);
        t.Reply("BTAUDIO1 CAP 1\n");
        t.Tick();t.port->Expect("BTAUDIO1 SET 1 pt8211\n");
        t.Action(BringUpAction::PatternToggle);t.Tick();t.port->Expect("");
        CHECK(std::strstr(t.serial.Status(),"Audio selection waiting"));
        if(!std::strcmp(response,"pending")) {
            t.Reply("BTTEST1 STATE PAUSED\nBTPATTERN1 TRANSPORTCAP 1\n");
            CHECK(!t.serial.UserActionsBlocked());CHECK(!t.serial.AudioTransportReady());
            CHECK(!t.serial.Continue());CHECK(!t.serial.Start());continue;
        }
        if(!std::strcmp(response,"timeout")) {
            t.now+=6001;t.Tick();t.port->Expect("BTTEST1 STATUS\n");t.Reply("BTTEST1 STATE IDLE\n");
            CHECK(std::strstr(t.serial.Status(),"acknowledgment timeout"));
        } else t.Reply(response);
        t.Reply("BTTEST1 STATE PAUSED\nBTPATTERN1 TRANSPORTCAP 1\n");
        CHECK(!t.serial.AudioTransportReady());CHECK(!t.serial.Continue());CHECK(!t.serial.Start());
        t.Action(BringUpAction::PatternToggle);t.Tick();t.port->Expect("");
        CHECK(std::strstr(t.serial.Status(),"Audio selection failed"));
    }
    for(auto route:{BroTracker::AudioOutputChoice::Default,BroTracker::AudioOutputChoice::Pt8211}) {
        Test t;t.serial.SetAudioOutputPreference(route);
        const char* request=route==BroTracker::AudioOutputChoice::Default?"BTAUDIO1 SET 1 default\n":"BTAUDIO1 SET 1 pt8211\n";
        t.Ready("BTTEST1 STATE IDLE\nBTAUDIO1 CAP 1\n",false);t.Tick();t.port->Expect(request);
        t.Reply("BTAUDIO1 SELECTED 1 pt8211\n");CHECK(t.serial.AudioTransportReady());
        t.Synchronize();t.Reply("BTPATTERN1 TRANSPORTCAP 1\n");
        const auto action=[&](BringUpAction a,const char* command,const char* reply) {
            t.Action(a);t.Action(a);t.Send(command);t.Reply(reply);t.Tick();t.port->Expect("");
        };
        action(BringUpAction::PatternToggle,"BTTEST1 START\n","BTTEST1 STARTED\n");
        action(BringUpAction::PatternToggle,"BTTEST1 PAUSE\n","BTTEST1 PAUSED\n");
        action(BringUpAction::PatternToggle,"BTTEST1 CONTINUE\n","BTTEST1 CONTINUED\n");
        action(BringUpAction::ResetStop,"BTTEST1 STOP\n","BTTEST1 STOPPED\n");
        action(BringUpAction::PatternToggle,"BTTEST1 START\n","BTTEST1 STARTED\n");
        t.port->online=false;t.Tick();CHECK(!t.serial.AudioOutputConfirmed());
        t.port->online=true;t.now+=1001;t.Tick();t.port->Expect("BTTEST1 HELLO\n");
        t.Reply("BTTEST1 STATE PLAYING\nBTAUDIO1 CAP 1\n");t.Tick();t.port->Expect(request);
        CHECK(!t.serial.AudioTransportReady());t.Tick();t.port->Expect("");CHECK(t.serial.Playing());
        t.Reply("BTAUDIO1 SELECTED 1 pt8211\n");CHECK(t.serial.AudioTransportReady());
        t.Tick();t.port->Expect("");CHECK(t.serial.Playing());
    }
    CHECK(log_text().find("Audio selection waiting")!=std::string::npos);
    { // Old firmware without CAP must reject an explicit unsupported preference.
        Test t;t.Ready("BTTEST1 STATE PAUSED\n");
        t.serial.SetAudioOutputPreference(BroTracker::AudioOutputChoice::Pt8211);
        t.Tick();t.port->Expect("BTAUDIO1 SET 1 pt8211\n");
        t.Reply("BTTEST1 ERROR command\n");t.Reply("BTPATTERN1 TRANSPORTCAP 1\n");
        CHECK(!t.serial.Continue());CHECK(!t.serial.Start());
        CHECK(!t.serial.AudioOutputConfirmed());CHECK(std::strstr(t.serial.Status(),"rejected"));
    }
    std::fclose(log);
    BroTracker::ConfigureHostLogging(BroTracker::HostLogMode::Basic);
}
void MidiDelayReadinessRegressions() {
    Test t;t.Ready("BTTEST1 STATE PLAYING\nBTPATTERN1 TRANSPORTCAP 1\n");
    t.editor.SetLocked(false);CHECK(t.editor.Apply(EditorAction::SetValue,69));
    const auto draft=*t.editor.Draft();CHECK(t.editor.Dirty());
    t.serial.SetMidiNoteDelay(82);
    t.Reply("BTAUDIO1 CAP 1\nBTMIDIDELAY1 CAP 1\n");
    t.Tick();t.port->Expect("BTAUDIO1 SET 1 default\n");
    t.Reply("BTAUDIO1 SELECTED 1 bridge\n");t.Tick();t.port->Expect("BTMIDIDELAY1 GET 1 1\n");
    t.Reply("BTMIDIDELAY1 STATE 1 1 25 2\n");
    CHECK(!t.serial.MidiDelayReady());CHECK(std::strstr(t.serial.Status(),"STOP required"));
    t.Tick();t.port->Expect("");CHECK(t.serial.Playing()); // No automatic STOP.
    t.Action(BringUpAction::PatternToggle);t.Send("BTTEST1 PAUSE\n");t.Reply("BTTEST1 PAUSED\n");
    t.Action(BringUpAction::PatternToggle);t.Tick();t.port->Expect("");CHECK(!t.serial.Continue());
    t.Action(BringUpAction::ResetStop);t.Send("BTTEST1 STOP\n");t.Reply("BTTEST1 STOPPED\n");
    t.Tick();t.port->Expect("BTMIDIDELAY1 GET 1 2\n");
    t.Reply("BTMIDIDELAY1 STATE 1 2 25 4\n"); // Cleanup still pending.
    t.Action(BringUpAction::PatternToggle);t.Tick();t.port->Expect("");CHECK(!t.serial.Start());
    t.now+=250;t.Tick();t.Tick();t.port->Expect("BTMIDIDELAY1 GET 1 3\n");
    t.Reply("BTMIDIDELAY1 STATE 1 3 25 0\n");t.Tick();t.port->Expect("BTMIDIDELAY1 SET 1 4 82\n");
    t.Reply("BTMIDIDELAY1 SELECTED 1 3 82 82\n");CHECK(!t.serial.MidiDelayReady()); // Wrong identity.
    t.Reply("BTMIDIDELAY1 SELECTED 1 4 82 82\n");CHECK(t.serial.MidiDelayReady());
    t.Tick();t.port->Expect("");CHECK(!t.serial.Playing());
    CHECK(BroTracker::SamePattern(*t.editor.Draft(),draft));CHECK(t.editor.Dirty());
    t.Action(BringUpAction::PatternToggle);t.Action(BringUpAction::PatternToggle);
    t.Send("BTTEST1 START\n");t.Reply("BTTEST1 STARTED\n");t.Tick();t.port->Expect("");
    // A different host preference on an ordinary reconnect must verify live state,
    // including zero after a previous nonzero setting. Keep the local draft.
    t.port->online=false;t.Tick();CHECK(!t.serial.MidiDelayReady());
    t.serial.SetMidiNoteDelay(0);t.port->online=true;t.now+=1001;t.Tick();t.port->Expect("BTTEST1 HELLO\n");
    t.Reply("BTTEST1 STATE PLAYING\nBTAUDIO1 CAP 1\nBTMIDIDELAY1 CAP 1\n");
    t.Tick();t.port->Expect("BTAUDIO1 SET 1 default\n");
    t.Reply("BTAUDIO1 SELECTED 1 bridge\n");t.Tick();t.port->Expect("BTMIDIDELAY1 GET 1 5\n");
    t.Reply("BTMIDIDELAY1 STATE 1 5 82 2\n");CHECK(!t.serial.MidiDelayReady());
    t.Tick();t.port->Expect("");CHECK(t.serial.Playing());CHECK(BroTracker::SamePattern(*t.editor.Draft(),draft));
    t.Action(BringUpAction::ResetStop);t.Send("BTTEST1 STOP\n");t.Reply("BTTEST1 STOPPED\n");
    t.Tick();t.port->Expect("BTMIDIDELAY1 GET 1 6\n");
    t.Reply("BTMIDIDELAY1 STATE 1 6 82 0\n");t.Tick();t.port->Expect("BTMIDIDELAY1 SET 1 7 0\n");
    t.Reply("BTMIDIDELAY1 SELECTED 1 7 0 0\n");CHECK(t.serial.MidiDelayReady());
    t.Tick();t.port->Expect("");CHECK(!t.serial.Playing());CHECK(BroTracker::SamePattern(*t.editor.Draft(),draft));

    // Matching live state is read-only confirmation even during playback.
    Test matching;matching.Ready("BTTEST1 STATE PLAYING\nBTPATTERN1 TRANSPORTCAP 1\n");
    matching.serial.SetMidiNoteDelay(25);matching.Reply("BTAUDIO1 CAP 1\nBTMIDIDELAY1 CAP 1\n");
    matching.Tick();matching.port->Expect("BTAUDIO1 SET 1 default\n");
    matching.Reply("BTAUDIO1 SELECTED 1 bridge\n");matching.Tick();matching.port->Expect("BTMIDIDELAY1 GET 1 1\n");
    matching.Reply("BTMIDIDELAY1 STATE 1 1 25 2\n");CHECK(matching.serial.MidiDelayReady());
    matching.Tick();matching.port->Expect("");CHECK(matching.serial.Playing());
    for(unsigned failure=0;failure<3;++failure) {
        Test f;f.Ready("BTTEST1 STATE IDLE\nBTPATTERN1 TRANSPORTCAP 1\n");f.serial.SetMidiNoteDelay(82);
        f.Reply("BTAUDIO1 CAP 1\nBTMIDIDELAY1 CAP 1\n");f.Tick();f.port->Expect("BTAUDIO1 SET 1 default\n");
        f.Reply("BTAUDIO1 SELECTED 1 bridge\n");f.Tick();f.port->Expect("BTMIDIDELAY1 GET 1 1\n");
        f.Action(BringUpAction::PatternToggle);f.Tick();f.port->Expect("");CHECK(!f.serial.Continue());
        f.Reply("BTMIDIDELAY1 STATE 1 1 0 0\n");f.Tick();f.port->Expect("BTMIDIDELAY1 SET 1 2 82\n");
        f.Reply("BTTEST1 STATE PAUSED\n");
        if(failure==0)f.Reply("BTMIDIDELAY1 ERROR 1 2 1\n");
        if(failure==1)f.Reply("BTMIDIDELAY1 SELECTED 1 2 82 25\n");
        if(failure==2){f.now+=6001;f.Tick();f.port->Expect("BTTEST1 STATUS\n");f.Reply("BTTEST1 STATE PAUSED\n");}
        f.Action(BringUpAction::PatternToggle);f.Tick();f.port->Expect("");CHECK(!f.serial.Continue());CHECK(!f.serial.Start());
        CHECK(std::strstr(f.serial.Status(),failure==0?"rejected":failure==1?"mismatched":"timeout"));
        f.Action(BringUpAction::ResetStop);f.Send("BTTEST1 STOP\n");f.Reply("BTTEST1 STOPPED\n");
        f.Tick();f.port->Expect("BTMIDIDELAY1 GET 1 3\n"); // Re-query; never replay the uncertain SET.
        f.Reply("BTMIDIDELAY1 STATE 1 3 82 0\n");CHECK(f.serial.MidiDelayReady());f.Tick();f.port->Expect("");
    }
}
int main() {
    try {
        { // Facts are capability-gated observations, never transport authorization.
          const char* facts="BTDEVICE1 FACTS 1 12345670 0123456789abcdef0123456789abcdef uninitialized default\n";
          Test t;t.Ready("BTTEST1 STATE PAUSED\n");
          t.Reply(facts);CHECK(!t.serial.DeviceFacts());
          t.Reply("BTDEVICE1 CAP 2\n");CHECK(!t.serial.DeviceFactsSupported());
          t.Reply("BTDEVICE1 CAP 1\n");t.Reply(facts);CHECK(t.serial.DeviceFacts());
          const auto before=*t.serial.DeviceFacts();
          t.Reply(facts);CHECK(BroTracker::CompareDeviceBoot(before,*t.serial.DeviceFacts())==BroTracker::BootComparison::SameBoot);
          t.Reply("BTDEVICE1 FACTS\n");CHECK(!t.serial.DeviceFacts());
          t.Reply(facts);CHECK(t.serial.DeviceFacts());
          t.Reply("BTDEVICE1 FACTS 2 incomplete\n");CHECK(!t.serial.DeviceFacts());
          t.Reply(facts);CHECK(t.serial.DeviceFacts());t.port->Expect("");
          t.port->online=false;t.Tick();CHECK(!t.serial.DeviceFacts());CHECK(!t.serial.DeviceFactsSupported());
          t.port->online=true;t.now+=1001;t.Tick();t.port->Expect("BTTEST1 HELLO\n");
          t.Reply("BTTEST1 STATE PAUSED\nBTDEVICE1 CAP 1\n");CHECK(!t.serial.DeviceFacts());
          t.Reply(facts);CHECK(BroTracker::CompareDeviceBoot(before,*t.serial.DeviceFacts())==BroTracker::BootComparison::SameBoot);
          t.port->Expect(""); // No automatic START/CONTINUE or identity GET request.
          t.Reply("BTDEVICE1 FACTS 1 12345670 fedcba9876543210fedcba9876543210 initialized pt8211\n");
          CHECK(BroTracker::CompareDeviceBoot(before,*t.serial.DeviceFacts())==BroTracker::BootComparison::NewBoot);
          CHECK(!t.serial.AudioOutputConfirmed()); // Facts cannot replace selection ACK.
          t.port->Expect("");
          t.Reply("BTDEVICE1 FACTS 1 12345671 fedcba9876543210fedcba9876543210 failed audio_shield\n");
          CHECK(BroTracker::CompareDeviceBoot(before,*t.serial.DeviceFacts())==BroTracker::BootComparison::WrongBoard);
          CHECK(t.serial.DeviceFacts()->audio==BroTracker::AudioInitialization::Failed);t.port->Expect("");
        }
        AudioReadinessRegressions();
        MidiDelayReadinessRegressions();
        { Test t;t.Ready("BTTEST1 STATE IDLE\nBTMIDI1 CAP 1\n");
          t.Tick();t.port->Expect("BTMIDI1 SET 1 12\n");t.Reply("BTMIDI1 SELECTED 1 12\n");
          t.Reply("BTMIDI1 OUTPUT 1 1 1 1 8\n");CHECK(t.serial.MidiHealth(t.now).fault);
          CHECK(std::strstr(t.serial.MidiStatus(),"wait STOPPED + cleanup=0"));t.port->Expect("");
          t.Reply("BTMIDI1 OUTPUT 1 1 1 0 8\n");CHECK(std::strstr(t.serial.MidiStatus(),"START requests recovery"));t.port->Expect("");
          const auto at=t.now;t.Reply("BTMIDI1 OUTPUT 1 1 1 0 8\n");
          CHECK(t.serial.MidiHealth(at+3000).known);CHECK(!t.serial.MidiHealth(t.now+3001).known);
          t.Reply("BTTEST1 STATE PLAYING\nBTMIDI1 OUTPUT 1 1 1 0 8\n");
          CHECK(t.serial.Playing());CHECK(std::strstr(t.serial.MidiStatus(),"STOP; wait"));t.port->Expect("");
          t.Reply("BTMIDI1 OUTPUT 1 1 0 0 0\n");CHECK(!t.serial.MidiHealth(t.now).fault);t.port->Expect("");
          t.Reply("BTMIDI1 OUTPUT 1 0 1 1 8\n");CHECK(!t.serial.MidiHealth(t.now).configured);
          CHECK(t.serial.MidiHealth(t.now).known);CHECK(std::strstr(t.serial.MidiStatus(),"restore USB"));t.port->Expect("");
          t.port->online=false;t.Tick();CHECK(!t.serial.MidiHealth(t.now).known);
          t.port->online=true;t.now+=1001;t.Tick();t.port->Expect("BTTEST1 HELLO\n");
          t.Reply("BTTEST1 STATE IDLE\nBTMIDI1 CAP 1\n");CHECK(!t.serial.MidiHealth(t.now).known);
        }
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
            t.Tick(); t.port->Expect(""); CHECK(!t.serial.Playing());
            t.Synchronize(); CHECK(t.serial.Start());
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
            t.Ready("BTTEST1 STATE IDLE\nBTAUDIO1 CAP 1\n",false);
            CHECK(!t.serial.AudioOutputConfirmed());CHECK(std::strstr(t.serial.Status(),"Audio selection waiting"));
            CHECK(!t.serial.Start()); // AUDIO is pending; initial readback cannot run yet.
            const std::string command=std::string("BTAUDIO1 SET 1 ")+BroTracker::AudioOutputName(choice)+"\n";
            t.Tick(); t.port->Expect(command.c_str());
            const std::string ack=std::string("BTAUDIO1 SELECTED 1 ")+BroTracker::AudioOutputName(choice)+"\n";
            t.Reply(ack.c_str()); CHECK(std::strcmp(t.serial.ActiveAudioOutput(),BroTracker::AudioOutputName(choice))==0); CHECK(t.serial.AudioOutputConfirmed());
            CHECK(!t.serial.Playing()); t.port->Expect("");
            t.Synchronize(); // The acknowledged route now permits snapshot readback.
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
          t.Ready("BTTEST1 STATE IDLE\nBTAUDIO1 CAP 1\n",false);t.Tick();t.port->Expect("BTAUDIO1 SET 1 audio_shield\n");
          const std::string error=std::string("BTAUDIO1 ERROR 1 ")+reason+" active=audio_shield\n";
          t.Reply(error.c_str());CHECK(!t.serial.AudioOutputConfirmed());
          t.Synchronize();CHECK(!t.serial.Start()); // Ready snapshot, but failed audio still blocks START.
          t.Tick();t.port->Expect("");CHECK(std::strstr(t.serial.Status(),"Audio selection failed"));
        }
        { Test t; t.serial.SetAudioOutputPreference(BroTracker::AudioOutputChoice::Pt8211);
          t.Ready("BTTEST1 STATE IDLE\n",false);t.Tick();t.port->Expect("BTAUDIO1 SET 1 pt8211\n");
          t.Reply("BTAUDIO1 SELECTED 1 bridge\n");CHECK(!t.serial.AudioOutputConfirmed());
          t.Synchronize();CHECK(!t.serial.Start());CHECK(std::strstr(t.serial.Status(),"Audio selection failed")); }
        { Test t; t.serial.SetAudioOutputPreference(BroTracker::AudioOutputChoice::Pt8211);
          t.Ready("BTTEST1 STATE IDLE\n",false);t.Tick();t.port->Expect("BTAUDIO1 SET 1 pt8211\n");
          t.Reply("BTAUDIO1 ERROR 1 restart-required active=bridge\n");CHECK(!t.serial.AudioOutputConfirmed());
          t.Synchronize();CHECK(!t.serial.Start());CHECK(std::strstr(t.serial.Status(),"Audio selection failed"));
          t.Tick();t.port->Expect(""); }
        { Test t; t.serial.SetAudioOutputPreference(BroTracker::AudioOutputChoice::Pt8211);
          t.Ready("BTTEST1 STATE IDLE\n",false);t.Tick();t.port->Expect("BTAUDIO1 SET 1 pt8211\n");
          t.now+=6001;t.Tick();CHECK(!t.serial.AudioOutputConfirmed());CHECK(!t.serial.Start());
          CHECK(std::strstr(t.serial.Status(),"Audio selection failed"));
          t.port->Expect("BTTEST1 STATUS\n");t.Reply("BTTEST1 STATE IDLE\n");
          t.Synchronize();CHECK(!t.serial.Start());CHECK(!t.serial.AudioOutputConfirmed()); }

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

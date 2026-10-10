#include "ui/bringup_controls.h"
#include <stdexcept>
#include <string>
#include <cstring>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (0)
struct FakeTransport : SerialTransport {
    bool online = true;
    bool hold_write = false;
    std::string incoming, outgoing;
    bool Open(FILE*, const char*) override { return online; }
    void Close() override {}
    bool Healthy() override { return online; }
    int Read(char* data, unsigned size) override {
        unsigned count = std::min(size, static_cast<unsigned>(incoming.size()));
        std::memcpy(data, incoming.data(), count); incoming.erase(0, count); return count;
    }
    int Write(const char* data, unsigned size) override { if (hold_write) return 0; outgoing.append(data, size); return size; }
    const char* Error() const override { return "fake disconnect"; }
    void Expect(const char* value) { CHECK(outgoing == value); outgoing.clear(); }
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
          t.Send("BTTEST1 STOP\n"); t.Reply("BTTEST1 STOPPED\n"); CHECK(t.controls.Quit()); }
        { Test t; t.Ready("BTTEST1 STATE PLAYING\n"); // No capability: preserve legacy restart.
          t.Action(BringUpAction::PatternToggle); t.Send("BTTEST1 START\n"); }
        { Test t; t.Ready("BTTEST1 STATE PLAYING\n");
          t.Reply("BTPATTERN1 TRANSPORTCAP 1\n"); t.now = 1000; t.Send("BTTEST1 STATUS\n");
          t.Action(BringUpAction::PatternToggle); t.Action(BringUpAction::Exit);
          t.Reply("BTTEST1 STATE PLAYING\n"); t.Send("BTTEST1 PAUSE\n");
          t.Reply("BTTEST1 PAUSED\n"); t.Send("BTTEST1 STOP\n");
          t.Reply("BTTEST1 STOPPED\n"); CHECK(t.controls.Quit()); }
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
        // STATUS pending: queued START then exit must preserve START -> STOP.
        { Test t; t.Ready(); t.now = 1000; t.Send("BTTEST1 STATUS\n");
          t.Action(BringUpAction::Start); t.Action(BringUpAction::Exit); t.Action(BringUpAction::Start);
          CHECK(!t.controls.Quit()); t.Reply("BTTEST1 STATE DONE\n");
          t.Send("BTTEST1 START\n"); t.Reply("BTTEST1 STARTED\n");
          t.Send("BTTEST1 STOP\n"); t.Reply("BTTEST1 STOPPED\n"); CHECK(t.controls.Quit()); }
        { Test t; t.Ready("BTTEST1 STATE PLAYING\n"); t.Action(BringUpAction::Stop);
          t.Action(BringUpAction::Exit); t.Send("BTTEST1 STOP\n");
          t.Reply("BTTEST1 STOPPED\n"); CHECK(t.controls.Quit()); }
        { Test t; t.Ready("BTTEST1 STATE PLAYING\n"); t.Action(BringUpAction::Exit);
          t.Send("BTTEST1 STOP\n"); t.now += 6000; t.Tick();
          CHECK(t.controls.Quit() && !t.serial.Stopped()); }
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

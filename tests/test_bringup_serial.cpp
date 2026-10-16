// Linux PTY integration test: no USB device or firmware upload required.
#include "ui/bringup_serial.h"
#include "ui/host_logging.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (0)

struct Peer
{
    int master = -1;
    char path[128]{};
    Peer()
    {
        master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
        CHECK(master >= 0);
        CHECK(grantpt(master) == 0 && unlockpt(master) == 0);
        std::snprintf(path, sizeof(path), "%s", ptsname(master));
    }
    ~Peer() { if (master >= 0) close(master); }
    void Reply(const std::string& text)
    { CHECK(write(master, text.data(), text.size()) == static_cast<ssize_t>(text.size())); }
    void ExpectNoData() { pollfd descriptor{master,POLLIN,0}; CHECK(poll(&descriptor,1,0)==0); }
    void Expect(const char* expected)
    {
        std::string received;
        while (received.size() < std::strlen(expected))
        {
            pollfd descriptor{master, POLLIN, 0};
            CHECK(poll(&descriptor, 1, 1000) == 1);
            char buffer[128];
            const auto count = read(master, buffer, sizeof(buffer));
            CHECK(count > 0);
            received.append(buffer, count);
        }
        CHECK(received == expected);
    }
};

// Real Linux PTY transport with deterministic write ownership/backpressure.
// Zero return owns the complete attempted payload until a positive completion.
class ControlledPtyTransport : public SerialTransport {
public:
    explicit ControlledPtyTransport(const char* device):device_(device),port_(MakeSerialTransport()) {}
    bool gated=false;
    unsigned credit=0, start_attempts=0;
    std::string owned;
    const char* owned_pointer=nullptr;
    void CheckOwned() const { if(owned_pointer) CHECK(std::memcmp(owned_pointer,owned.data(),owned.size())==0); }
    bool Open(FILE* log,const char*) override {return port_->Open(log,device_);}
    bool Healthy() override {CheckOwned(); return port_->Healthy();}
    void Close() override {CheckOwned(); owned.clear(); owned_pointer=nullptr; port_->Close();}
    int Read(char* data,unsigned count) override {return port_->Read(data,count);}
    int Write(const char* data,unsigned count) override {
        CheckOwned();
        if(owned_pointer) {CHECK(data==owned_pointer); CHECK(count==owned.size()); CHECK(owned==std::string(data,count));}
        if(std::string(data,count)=="BTTEST1 START\n") ++start_attempts;
        if(gated && !credit) {
            if(!owned_pointer) {owned.assign(data,count); owned_pointer=data;}
            return 0;
        }
        const auto limit=gated ? std::min(count,credit) : count;
        const auto written=port_->Write(data,limit);
        if(written>0) { if(gated) credit-=static_cast<unsigned>(written); owned.clear(); owned_pointer=nullptr; }
        return written;
    }
    const char* Error() const override {return port_->Error();}
private:
    const char* device_;
    std::unique_ptr<SerialTransport> port_;
};

int main()
{
    try
    {
        BroTracker::ConfigureHostLogging(BroTracker::HostLogMode::Debug);
        // Exercise the actual Linux open path without root bypassing permissions.
        {
            Peer denied;
            CHECK(chmod(denied.path, 0000) == 0);
            const pid_t child = fork();
            CHECK(child >= 0);
            if (child == 0) {
                try {
                    if (geteuid() == 0) CHECK(setuid(65534) == 0);
                    FILE* permission_log = std::tmpfile();
                    CHECK(permission_log);
                    char device[128];
                    std::strcpy(device, denied.path);
                    BringUpSerial serial(permission_log, device);
                    serial.Tick(0);
                    CHECK(std::strstr(serial.Status(), "access denied"));
                    const long first = std::ftell(permission_log);
                    serial.Tick(1000);
                    CHECK(std::ftell(permission_log) == first); // no retry spam
                    std::strcpy(device, "/dev/brotracker-test-absent");
                    serial.Tick(2000);
                    CHECK(std::strcmp(serial.Status(), "Waiting for Teensy USB") == 0);
                    Peer accessible;
                    std::strcpy(device, accessible.path);
                    serial.Tick(3000);
                    accessible.Expect("BTTEST1 HELLO\n");
                    CHECK(!std::strstr(serial.Status(), "access denied"));
                    accessible.Reply("BTTEST1 STATE IDLE\n");
                    serial.Tick(3001);
                    CHECK(serial.Connected());
                    std::rewind(permission_log);
                    char text[4096]{};
                    CHECK(std::fread(text, 1, sizeof(text)-1, permission_log) > 0);
                    CHECK(std::strstr(text, denied.path));
                    CHECK(std::strstr(text, "sudo usermod -aG dialout"));
                    std::fclose(permission_log);
                    _exit(0);
                } catch (...) { _exit(1); }
            }
            int status = 0;
            CHECK(waitpid(child, &status, 0) == child);
            CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
        FILE* log = std::tmpfile();
        CHECK(log);
        {
            Peer peer;
            // Mutable path simulates re-enumeration with another tty number.
            char device[128];
            std::strcpy(device, peer.path);
            BringUpSerial serial(log, device);
            CHECK(!serial.Start());
            serial.Tick(0);
            peer.Expect("BTTEST1 HELLO\n");
            CHECK(!serial.Connected());
            peer.Reply("Test stream: finished\r\nBTTEST1 STATE DO");
            serial.Tick(1);
            CHECK(!serial.Connected());
            peer.Reply("NE\r\n");
            serial.Tick(2);
            CHECK(serial.Connected() && serial.Finished());
            CHECK(serial.Start());
            CHECK(!serial.Finished());
            serial.Tick(3); serial.Tick(4);
            peer.Expect("BTTEST1 START\n");
            // Second press before START acknowledgement must still send STOP.
            CHECK(serial.Stop());
            peer.Reply("BTTEST1 STARTED\n");
            serial.Tick(5); serial.Tick(6);
            peer.Expect("BTTEST1 STOP\n");
            CHECK(!serial.Stopped());
            peer.Reply("BTTEST1 STOPPED\n");
            serial.Tick(7);
            CHECK(serial.Stopped() && serial.Finished());

            CHECK(serial.Start());
            serial.Tick(8); serial.Tick(9);
            peer.Expect("BTTEST1 START\n");
            peer.Reply("BTTEST1 STARTED\nBTTEST1 DONE\n");
            serial.Tick(10);
            CHECK(serial.Finished());
            CHECK(std::strcmp(serial.Status(), "Connected - completed") == 0);
            // An overlong line cannot masquerade as a protocol reply.
            peer.Reply(std::string(300, 'x') + "BTTEST1 STARTED\n");
            serial.Tick(11);
            CHECK(serial.Finished());

            CHECK(serial.Start());
            serial.Tick(12); serial.Tick(13);
            peer.Expect("BTTEST1 START\n");
            peer.Reply("BTTEST1 ERROR playback\n");
            serial.Tick(14);
            CHECK(serial.Finished());
            CHECK(std::strstr(serial.Status(), "ERROR"));

            close(peer.master); peer.master = -1;
            serial.Tick(15);
            CHECK(!serial.Connected() && !serial.Stop());
            Peer replacement;
            std::strcpy(device, replacement.path);
            serial.Tick(1000);
            replacement.Expect("BTTEST1 HELLO\n");
            replacement.Reply("BTTEST1 STATE PLAYING\n");
            serial.Tick(1001);
            CHECK(serial.Connected() && !serial.Finished());
            // Heartbeat catches a silent device; reconnect never replays START.
            serial.Tick(2000); serial.Tick(2001);
            replacement.Expect("BTTEST1 STATUS\n");
            serial.Tick(8000);
            CHECK(!serial.Connected());
            serial.Tick(9000);
            replacement.Expect("BTTEST1 HELLO\n");
            serial.Tick(15000);
            CHECK(!serial.Connected());
        }
        // Regression: both presses arrive while a STATUS reply is pending.
        {
            Peer peer;
            BringUpSerial serial(log, peer.path);
            serial.Tick(0); peer.Expect("BTTEST1 HELLO\n");
            peer.Reply("BTTEST1 STATE DONE\n"); serial.Tick(1);
            serial.Tick(1000); serial.Tick(1001);
            peer.Expect("BTTEST1 STATUS\n");
            CHECK(serial.Start());
            CHECK(serial.Stop());
            CHECK(!serial.Finished());
            peer.Reply("BTTEST1 STATE DONE\n");
            serial.Tick(1002); serial.Tick(1003);
            peer.Expect("BTTEST1 START\n");
            peer.Reply("BTTEST1 STARTED\n");
            serial.Tick(1004); serial.Tick(1005);
            peer.Expect("BTTEST1 STOP\n");
            CHECK(!serial.Stopped());
            peer.Reply("BTTEST1 STOPPED\n"); serial.Tick(1006);
            CHECK(serial.Stopped());
        }
        // Real wire state defines the boundary, not the number of Tick calls.
        // The bounded immediate-write service can send START in the same Tick
        // that consumes STATUS. Gate writes explicitly to cover zero/partial I/O.
        for (int phase = 0; phase < 4; ++phase)
        {
            Peer peer;
            char device[128]; std::strcpy(device,peer.path);
            auto* port=new ControlledPtyTransport(device);
            BringUpSerial serial(log,std::unique_ptr<SerialTransport>(port));
            serial.Tick(0); peer.Expect("BTTEST1 HELLO\n");
            peer.Reply("BTTEST1 STATE DONE\n"); serial.Tick(1);
            serial.Tick(1000); peer.Expect("BTTEST1 STATUS\n");
            CHECK(serial.Start());
            if(phase>0) {
                port->gated=true; port->credit=phase==1 ? 0 : phase==2 ? 5 : 100;
                peer.Reply("BTTEST1 STATE DONE\n"); serial.Tick(1002);
                CHECK(port->start_attempts>0);
                if(phase==1) {CHECK(port->owned=="BTTEST1 START\n"); peer.ExpectNoData();}
                else peer.Expect(phase==2 ? "BTTES" : "BTTEST1 START\n");
            } else {CHECK(port->start_attempts==0); peer.ExpectNoData();}
            close(peer.master); peer.master=-1;
            serial.Tick(1004);
            CHECK(!serial.Connected() && !serial.Finished());
            CHECK(serial.StartCancelled()==(phase==0));
            CHECK(std::strstr(serial.Status(),phase==0 ? "not sent" : "unknown"));
            CHECK(!serial.Stop());
            Peer replacement; std::strcpy(device,replacement.path);
            port->gated=false;
            serial.Tick(2000); replacement.Expect("BTTEST1 HELLO\n");
            replacement.Reply("BTTEST1 STATE DONE\n"); serial.Tick(2001); serial.Tick(2002);
            replacement.ExpectNoData(); // Never replay an attempted or cancelled START.
            CHECK(serial.Start()); CHECK(!serial.StartCancelled());
            serial.Tick(2003); replacement.Expect("BTTEST1 START\n");
        }
        // STOP cannot replace an owned zero-completion or partial START line.
        for(const unsigned prefix : {0u,5u}) {
            Peer peer;
            auto* port=new ControlledPtyTransport(peer.path);
            BringUpSerial serial(log,std::unique_ptr<SerialTransport>(port));
            serial.Tick(0); peer.Expect("BTTEST1 HELLO\n");
            peer.Reply("BTTEST1 STATE IDLE\n"); serial.Tick(1);
            port->gated=true; port->credit=prefix;
            CHECK(serial.Start()); serial.Tick(2);
            if(prefix) peer.Expect("BTTES"); else peer.ExpectNoData();
            CHECK(serial.Stop()); serial.Tick(3); port->CheckOwned();
            peer.ExpectNoData();
            port->credit=100; serial.Tick(4);
            peer.Expect(prefix ? "T1 START\n" : "BTTEST1 START\n");
            peer.ExpectNoData(); CHECK(!serial.Stopped());
            peer.Reply("BTTEST1 STARTED\n"); serial.Tick(5); peer.Expect("BTTEST1 STOP\n");
            CHECK(!serial.Stopped()); peer.Reply("BTTEST1 STOPPED\n"); serial.Tick(6);
            CHECK(serial.Stopped()); peer.ExpectNoData();
        }
        // STOP remains bounded: no acknowledgement is not success.
        {
            Peer peer;
            BringUpSerial serial(log, peer.path);
            serial.Tick(0); peer.Expect("BTTEST1 HELLO\n");
            peer.Reply("BTTEST1 STATE PLAYING\n"); serial.Tick(1);
            CHECK(serial.Stop()); serial.Tick(2); serial.Tick(3);
            peer.Expect("BTTEST1 STOP\n");
            serial.Tick(6002);
            CHECK(!serial.Connected() && !serial.Stopped());
        }
        CHECK(std::fseek(log,0,SEEK_END)==0);
        const auto length=std::ftell(log); CHECK(length>0);
        std::string captured(static_cast<std::size_t>(length),'\0');
        std::rewind(log);
        CHECK(std::fread(&captured[0],1,captured.size(),log)==captured.size());
        const auto* diagnostics=captured.c_str();
        CHECK(std::strstr(diagnostics, "handshake accepted"));
        CHECK(std::strstr(diagnostics, "BTTEST1 DONE"));
        CHECK(std::strstr(diagnostics, "BTTEST1 ERROR playback"));
        CHECK(std::strstr(diagnostics, "USB hangup"));
        CHECK(std::strstr(diagnostics, "timeout"));
        CHECK(std::strstr(diagnostics, "START cancelled before transmission"));
        CHECK(std::strstr(diagnostics, "playback unconfirmed, not replaying"));
        std::fclose(log);
        std::puts("USB bring-up PTY checks passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "USB bring-up test failed: %s\n", error.what());
        return 1;
    }
}

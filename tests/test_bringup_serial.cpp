// Linux PTY integration test: no USB device or firmware upload required.
#include "ui/arkos/bringup_serial.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <unistd.h>

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

int main()
{
    try
    {
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
        // Disconnect at each START boundary: behind STATUS, ready to write,
        // or fully written but not acknowledged. No automatic replay.
        for (int phase = 0; phase < 3; ++phase)
        {
            Peer peer;
            char device[128];
            std::strcpy(device, peer.path);
            BringUpSerial serial(log, device);
            serial.Tick(0); peer.Expect("BTTEST1 HELLO\n");
            peer.Reply("BTTEST1 STATE DONE\n"); serial.Tick(1);
            serial.Tick(1000); serial.Tick(1001);
            peer.Expect("BTTEST1 STATUS\n");
            CHECK(serial.Start());
            if (phase > 0)
            {
                peer.Reply("BTTEST1 STATE DONE\n"); serial.Tick(1002);
                if (phase == 2)
                {
                    serial.Tick(1003); peer.Expect("BTTEST1 START\n");
                }
            }
            close(peer.master); peer.master = -1;
            serial.Tick(1004);
            CHECK(!serial.Connected() && !serial.Finished());
            CHECK(serial.StartCancelled() == (phase < 2));
            CHECK(std::strstr(serial.Status(), phase < 2 ? "not sent" : "unknown"));
            CHECK(!serial.Stop());
            Peer replacement;
            std::strcpy(device, replacement.path);
            serial.Tick(2000); replacement.Expect("BTTEST1 HELLO\n");
            replacement.Reply("BTTEST1 STATE DONE\n"); serial.Tick(2001);
            serial.Tick(2002);
            pollfd descriptor{replacement.master, POLLIN, 0};
            CHECK(poll(&descriptor, 1, 0) == 0); // No replayed START or STOP.
            if (phase < 2)
            {
                // UI uses StartCancelled() to restore the first-press action.
                CHECK(serial.Start());
                CHECK(!serial.StartCancelled());
                serial.Tick(2003); serial.Tick(2004);
                replacement.Expect("BTTEST1 START\n");
            }
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
        std::rewind(log);
        char diagnostics[16384]{};
        CHECK(std::fread(diagnostics, 1, sizeof(diagnostics) - 1, log) > 0);
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

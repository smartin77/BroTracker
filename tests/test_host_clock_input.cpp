// Exercise the production startup demultiplexer without Arduino/USB hardware.
#include <host_clock_input.h>
#include <cstdio>
#include <stdexcept>
#include <string>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (0)
struct Result { std::string protocol; bool synced; std::uint32_t epoch; std::size_t consumed; };
Result Startup(const std::string& usb, std::size_t available_before_timeout = std::string::npos) {
    BroTracker::HostClockInput input;
    std::uint32_t epoch = 0;
    std::size_t consumed = 0;
    bool synced = false;
    while (consumed < usb.size() && consumed < available_before_timeout && !input.Full()) {
        if (input.Feed(usb[consumed++], epoch)) { synced = true; break; }
    }
    // The exact order used by ReadStartupSerialByte: deferred first, then USB.
    std::string protocol;
    for (int c; (c = input.ReadDeferred()) >= 0;) protocol += static_cast<char>(c);
    CHECK(input.ReadDeferred() == -1); // every retained byte delivered once
    protocol += usb.substr(consumed);
    return {protocol, synced, epoch, consumed};
}
int main() {
    try {
        const std::string hello = "BTTEST1 HELLO\n";
        CHECK(Startup(hello).protocol == hello);
        CHECK(!Startup(hello).synced);
        const std::string commands = hello + "BTTEST1 START\nBTTEST1 STOP\nBTTEST1 STATUS\n";
        CHECK(Startup(commands).protocol == commands);
        // Timeout at every byte boundary, including partial prefix and newline.
        for (std::size_t split = 0; split <= commands.size(); ++split)
            CHECK(Startup(commands, split).protocol == commands);
        for (const std::string clock : {"1700000000\n", "T1700000000\n", "EPOCH:1700000000\n"}) {
            const auto before = Startup(clock + commands);
            CHECK(before.synced && before.epoch == 1700000000u && before.protocol == commands);
            const auto after = Startup(hello + clock + "BTTEST1 START\nBTTEST1 STOP\n");
            CHECK(after.synced && after.epoch == 1700000000u);
            CHECK(after.protocol == hello + "BTTEST1 START\nBTTEST1 STOP\n");
        }
        CHECK(Startup("BTTEST1 HELLO\r\nEPOCH:1700000000\n").protocol == "BTTEST1 HELLO\r\n");
        CHECK(Startup("T1700000000\r\n" + hello).protocol == "\n" + hello);
        // Malformed, oversized or too-old time input cannot swallow commands.
        for (const std::string junk : {std::string("EPOCH:bad\n"), std::string("T12\n"), std::string(80, 'x') + "\n"}) {
            auto result = Startup(junk + hello);
            CHECK(!result.synced && result.protocol == junk + hello);
        }
        const std::string pressure = commands + std::string(200, 'x') + "\n" + hello;
        const auto full = Startup(pressure);
        CHECK(!full.synced && full.consumed == BroTracker::HostClockInput::Capacity);
        CHECK(full.protocol == pressure); // remaining USB bytes never consumed
        // A clock line completing exactly at capacity still synchronizes.
        const std::string prefix(116, 'x');
        auto boundary = Startup(prefix + "\n1700000000\n" + hello);
        CHECK(boundary.synced && boundary.protocol == prefix + "\n" + hello);
        std::puts("Startup clock input: early/partial HELLO, command ordering, clock formats/both orders, capacity checks passed");
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}

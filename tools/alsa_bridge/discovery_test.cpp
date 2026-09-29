#include "discovery.h"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (0)
using discovery::Endpoint;

int main()
{
    try
    {
        // Dynamic card numbers are deliberately unrelated to the R36H snapshot.
        const Endpoint teensy{7, "MIDIAudio", "Teensy MIDI/Audio", "hw:CARD=MIDIAudio,DEV=2,SUBDEV=0", true};
        const Endpoint speaker{12, "rockchiprk817co", "rockchip,rk817-codec", "plughw:CARD=rockchiprk817co,DEV=3,SUBDEV=0", false};
        auto usb_output = teensy; usb_output.capture = false;
        std::vector<Endpoint> cards{usb_output, teensy, speaker};
        auto opens = [](const Endpoint&) { return true; };
        CHECK(discovery::Select(cards, true, opens).pcm == teensy.pcm);
        CHECK(discovery::Select(cards, false, opens).pcm == speaker.pcm);
        CHECK(!discovery::Matches(usb_output, false));
        auto conflicting = usb_output; conflicting.name = speaker.name;
        CHECK(!discovery::Matches(conflicting, false));
        auto duplicate = teensy;
        duplicate.card = 19; duplicate.id = "MIDIAudio_1";
        duplicate.pcm = "hw:CARD=MIDIAudio_1,DEV=0,SUBDEV=0";
        cards.push_back(duplicate);
        // First match vanished or was busy at actual open: accept the survivor.
        CHECK(discovery::Select(cards, true, [&](const Endpoint& e) {
            return e.card != teensy.card;
        }).pcm == duplicate.pcm);
        auto expect_error = [&](const std::vector<Endpoint>& list, bool capture,
                                bool viable, const char* message) {
            bool rejected = false;
            try { discovery::Select(list, capture, [&](const Endpoint&) { return viable; }); }
            catch (const std::runtime_error& e) { rejected = std::string(e.what()).find(message) != std::string::npos; }
            CHECK(rejected);
        };
        expect_error(cards, true, true, "Ambiguous viable Teensy");
        auto second_output = speaker; second_output.pcm += "_second";
        expect_error({speaker, second_output}, false, true, "Ambiguous viable Rockchip");
        expect_error({}, true, true, "No usable Teensy");
        expect_error({teensy}, true, false, "No usable Teensy");
        expect_error({speaker}, false, false, "No usable Rockchip");
        expect_error({usb_output}, false, true, "No usable Rockchip");
        // Do not even probe endpoints belonging to another identity/direction.
        int probes = 0;
        discovery::Select(cards, false, [&](const Endpoint&) { ++probes; return true; });
        CHECK(probes == 1);
        std::puts("ALSA discovery policy checks passed (simulated inventory/open results)");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "Discovery test failed: %s\n", error.what());
        return 1;
    }
}

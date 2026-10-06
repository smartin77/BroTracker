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
        const Endpoint teensy{7, "MIDIAudio", "Teensy MIDI/Audio", discovery::PcmName(7, 2, 0, true), true};
        const Endpoint speaker{12, "rockchiprk817co", "rockchip,rk817-codec", discovery::PcmName(12, 3, 0, false), false};
        // Product-name matching is independent of the generated ALSA card ID.
        auto renamed = teensy;
        renamed.card = 23; renamed.id = "USBaudio"; renamed.name = "BroTracker USB audio";
        renamed.pcm = discovery::PcmName(renamed.card, 4, 1, true);
        auto opens = [](const Endpoint&) { return true; };
        CHECK(discovery::Select({renamed, speaker}, true, opens).pcm == renamed.pcm);
        auto legacy_name = teensy; legacy_name.id = "MIDIAudio_renumbered";
        CHECK(discovery::Matches(legacy_name, true));
        auto legacy_id = teensy; legacy_id.name = "Another label";
        CHECK(discovery::Matches(legacy_id, true));
        auto unrelated = renamed; unrelated.name = "Unrelated microphone";
        CHECK(!discovery::Matches(unrelated, true));
        auto renamed_output = renamed; renamed_output.capture = false;
        CHECK(!discovery::Matches(renamed_output, false));
        CHECK(discovery::Select({renamed_output, speaker}, false, opens).pcm == speaker.pcm);
        auto usb_output = teensy; usb_output.capture = false;
        std::vector<Endpoint> cards{usb_output, teensy, speaker};
        CHECK(discovery::Select(cards, true, opens).pcm == teensy.pcm);
        CHECK(discovery::Select(cards, false, opens).pcm == speaker.pcm);
        CHECK(!discovery::Matches(usb_output, false));
        auto conflicting = usb_output; conflicting.name = speaker.name;
        CHECK(!discovery::Matches(conflicting, false));
        auto duplicate = teensy;
        duplicate.card = 19; duplicate.id = "MIDIAudio_1";
        duplicate.pcm = discovery::PcmName(19, 0, 0, true);
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
        expect_error({renamed, teensy}, true, true, "Ambiguous viable Teensy");
        expect_error({renamed_output}, false, true, "No usable Rockchip");
        CHECK(discovery::Select({renamed, teensy}, true, [&](const Endpoint& e) {
            return e.card == renamed.card;
        }).pcm == renamed.pcm);
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
        // Regression: identical ID/name must not collapse two cards into one PCM.
        const Endpoint first{1, "MIDIAudio", "Teensy MIDI/Audio",
                             discovery::PcmName(1, 0, 0, true), true};
        const Endpoint second{7, first.id, first.name,
                              discovery::PcmName(7, 0, 0, true), true};
        CHECK(first.pcm == "hw:CARD=1,DEV=0,SUBDEV=0");
        CHECK(second.pcm == "hw:CARD=7,DEV=0,SUBDEV=0");
        CHECK(first.pcm != second.pcm);
        CHECK(discovery::PcmName(12, 3, 2, false) == "plughw:CARD=12,DEV=3,SUBDEV=2");
        for (const auto& viable : {first, second})
        {
            std::vector<std::string> probed;
            const auto selected = discovery::Select({first, second}, true, [&](const Endpoint& e) {
                probed.push_back(e.pcm);
                return e.pcm == viable.pcm;
            });
            CHECK(selected.card == viable.card);
            CHECK(probed == std::vector<std::string>({first.pcm, second.pcm}));
        }
        bool ambiguous = false;
        try { discovery::Select({first, second}, true, opens); }
        catch (const std::runtime_error& e)
        {
            const std::string message = e.what();
            ambiguous = message.find("Ambiguous viable Teensy") != std::string::npos &&
                        message.find(first.pcm) != std::string::npos &&
                        message.find(second.pcm) != std::string::npos;
        }
        CHECK(ambiguous);
        std::puts("ALSA discovery policy checks passed (simulated inventory/open results)");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "Discovery test failed: %s\n", error.what());
        return 1;
    }
}

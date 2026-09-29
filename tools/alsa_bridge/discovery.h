#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace discovery
{
struct Endpoint
{
    int card;
    std::string id, name, pcm;
    bool capture;
};

// Address the enumerated card, not its potentially duplicated ALSA ID.
inline std::string PcmName(int card, int device, unsigned int subdevice, bool capture)
{
    return std::string(capture ? "hw:CARD=" : "plughw:CARD=") + std::to_string(card) +
        ",DEV=" + std::to_string(device) + ",SUBDEV=" + std::to_string(subdevice);
}

inline bool Matches(const Endpoint& endpoint, bool capture)
{
    const bool teensy = endpoint.id == "MIDIAudio" || endpoint.name == "Teensy MIDI/Audio";
    const bool rockchip = endpoint.id == "rockchiprk817co" || endpoint.name == "rockchip,rk817-codec";
    return endpoint.capture == capture && (capture ? teensy : rockchip && !teensy);
}

// Probe must retain the successfully opened/configured handle. It must not
// close/reopen the winner, which would introduce another enumeration/open race.
template<class Probe>
Endpoint Select(const std::vector<Endpoint>& endpoints, bool capture, Probe probe)
{
    const Endpoint* selected = nullptr;
    for (const auto& endpoint : endpoints)
    {
        if (!Matches(endpoint, capture) || !probe(endpoint)) continue;
        if (selected)
            throw std::runtime_error("Ambiguous viable " + std::string(capture ? "Teensy capture: " : "Rockchip playback: ") +
                                     selected->pcm + " and " + endpoint.pcm + "; use explicit arguments");
        selected = &endpoint;
    }
    if (!selected)
        throw std::runtime_error(std::string("No usable ") + (capture ? "Teensy capture" : "Rockchip playback") +
                                 " endpoint: absent, disappeared, busy, inaccessible or unsupported; see probe diagnostics");
    return *selected;
}
}

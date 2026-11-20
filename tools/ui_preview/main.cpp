/*
 * BroTracker
 *
 * Description: Standalone production pattern renderer preview.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include <algorithm>
#include <filesystem>
#include <iostream>

#include "core/constants.h"
#include "core/logger.h"
#include "core/tune_loader.h"
#include "renderer/framebuffer.h"
#include "ui/pattern_screen.h"
#include "ui/text_renderer.h"
#include "pattern_fixture.h"

int main(int argc, char* argv[])
{
    LogInfo("BroTracker UI starting.");

    if (!LoadUiFont("assets/fonts/brotracker.btf"))
    {
        LogError("Failed to load BroTracker font.");
        return 1;
    }

    Tune tune = LoadTuneFromJson(
        "assets/dummy_my_tune.json"
    );

    if (tune.patterns.empty())
    {
        LogError("No patterns available.");
        return 1;
    }

    Pattern pattern = tune.patterns.front();
    const bool beat_fixture = (argc == 3 || (argc == 4 && std::string(argv[3]) == "--unmuted-digits")) && std::string(argv[1]) == "--beat-readability";
    if (beat_fixture) {
        // Standalone visual fixture only; never changes a tune or the default pattern.
        pattern.length = 32; pattern.channels.resize(8);
        for (unsigned channel=0; channel<8; ++channel) {
            auto& rows=pattern.channels[channel].rows; rows.assign(32, Event{});
            for (unsigned row=0; row<32; ++row) {
                if (channel == 7) continue; // Completely empty channel.
                if (channel == 6) {
                    if (row == 0 || row == 9 || row == 24)
                        rows[row] = Event{static_cast<Note>(60+row%12), 2};
                    continue; // Only three populated cells in the sparse channel.
                }
                if ((row+channel)%3!=0) rows[row].note=static_cast<Note>(60+(channel*3+row)%24);
                if ((row+channel)%4!=2) rows[row].instrument=static_cast<std::uint8_t>(channel%4);
            }
        }
    }

    LogInfo("Creating production main screen preview.");

    Framebuffer framebuffer(SCREEN_WIDTH, SCREEN_HEIGHT);

    const bool instrument=(argc==5 || argc==6) && std::string(argv[1])=="--instrument-preview";
    HostPanelInformation host;
    if(instrument) {
        host.instrument_number=std::stoul(argv[3]);host.instrument_identity=argv[4];
        host.instrument_original_number=host.instrument_number;
        host.instrument_transport="PLAYING";
        const bool sample=host.instrument_identity=="Sample";
        host.instrument_focus=0;
        host.instrument_load_available=sample;
        if(sample) {
            host.instrument_resource=argc==6?argv[5]:"Samples/test.wav";
            host.instrument_name=host.instrument_resource.substr(host.instrument_resource.find_last_of('/')+1);
            host.instrument_availability="Prepared";
            host.transport_hints="Up/Down: Field | Left/Right: Type | Ctrl+Down: Keep | Esc: Roll back";
        } else {
            if(host.instrument_identity=="MIDI"){host.instrument_midi_channel=argc==6?std::stoul(argv[5]):1;host.instrument_focus=3;}
            host.instrument_availability=host.instrument_identity=="MIDI"?"External MIDI device unknown":"Ready";
            host.transport_hints=host.instrument_focus==3?"D-pad U/D: Field | Left/Right: Channel | Select+Down: Keep | B: Roll back":"D-pad U/D: Field | Left/Right: Type | Select+Down: Keep | B: Roll back";
        }
    }
    if (!RenderProductionPatternFixture(framebuffer, tune, pattern, beat_fixture, argc==4,host)) {
        LogError("Preview editor could not initialize.");
        return 1;
    }

    const std::filesystem::path output_path = (beat_fixture || instrument) ? argv[2] : "assets/ui_main_screen.bmp";

    if (!framebuffer.SaveBMP(output_path))
    {
        LogError("Failed to save BMP.");
        return 1;
    }

    LogInfo("Main screen BMP created.");

    std::cout
        << "Output: "
        << output_path.string()
        << '\n';

    return 0;
}

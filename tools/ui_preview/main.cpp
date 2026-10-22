/*
 * BroTracker
 *
 * Description: Basic UI mockup renderer.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include <filesystem>
#include <iostream>

#include "core/constants.h"
#include "core/logger.h"
#include "core/tune_loader.h"
#include "renderer/framebuffer.h"
#include "ui/pattern_screen.h"
#include "ui/text_renderer.h"

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
    const bool beat_fixture = argc == 3 && std::string(argv[1]) == "--beat-readability";
    if (beat_fixture) {
        // Standalone visual fixture only; never changes a tune or the default pattern.
        pattern.length = 32; pattern.channels.resize(8);
        for (unsigned channel=0; channel<8; ++channel) {
            auto& rows=pattern.channels[channel].rows; rows.assign(32, Event{});
            for (unsigned row=0; row<32; ++row) {
                if ((row+channel)%3!=0) rows[row].note=static_cast<Note>(60+(channel*3+row)%24);
                if ((row+channel)%4!=2) rows[row].instrument=static_cast<std::uint8_t>(channel%4);
            }
        }
    }

    LogInfo("Creating main screen mockup.");

    Framebuffer framebuffer(SCREEN_WIDTH, SCREEN_HEIGHT);

    RenderPreviewScreen(
        framebuffer,
        tune,
        pattern);

    const std::filesystem::path output_path = beat_fixture ? argv[2] : "assets/ui_main_screen.bmp";

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

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

int main()
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

    const Pattern& pattern =
        tune.patterns.front();

    LogInfo("Creating main screen mockup.");

    Framebuffer framebuffer(SCREEN_WIDTH, SCREEN_HEIGHT);

    RenderMainScreen(
        framebuffer,
        tune,
        pattern);

    const std::filesystem::path output_path = "assets/ui_main_screen.bmp";

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

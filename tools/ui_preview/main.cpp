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

    // Feed the same snapshot/editor path as BTX without connecting to hardware.
    LivePlaybackView live;
    live.telemetry_available = true;
    BroTracker::DevicePatternSnapshot snapshot;
    snapshot.transfer_id = 1;
    snapshot.tempo_hundredths = 12500;
    snapshot.pattern.active_rows = std::min<unsigned>(pattern.length, 32);
    snapshot.pattern.active_channels = std::min<unsigned>(pattern.channels.size(), 8);
    live.position.rows = snapshot.pattern.active_rows;
    live.position.channels = snapshot.pattern.active_channels;
    for (unsigned row=0; row<snapshot.pattern.active_rows; ++row)
        for (unsigned channel=0; channel<snapshot.pattern.active_channels; ++channel)
            if (row < pattern.channels[channel].rows.size()) {
                const auto& event = pattern.channels[channel].rows[row];
                snapshot.pattern.cells[row][channel] = {event.note, event.instrument};
            }
    live.device_pattern = snapshot;
    live.tempo_hundredths = snapshot.tempo_hundredths;
    LocalPatternEditor editor;
    editor.Sync(live);
    if (!editor.Ready()) {
        LogError("Preview editor could not initialize.");
        return 1;
    }
    for (unsigned row=0; row<12; ++row) editor.Apply(EditorAction::Down);
    RenderMainScreen(framebuffer, tune, pattern, live, &editor);

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

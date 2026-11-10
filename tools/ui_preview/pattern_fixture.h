// Shared offline fixture for preview tools. BroTracker contributors; GPL-3.0-or-later.
#pragma once
#include <algorithm>
#include "ui/pattern_screen.h"

inline bool RenderProductionPatternFixture(Framebuffer& framebuffer, const Tune& tune,
    const Pattern& pattern, bool beat_fixture=false, bool unmuted_digits=false, const HostPanelInformation& host={})
{
    LivePlaybackView live;
    live.telemetry_available = true;
    BroTracker::DevicePatternSnapshot snapshot;
    snapshot.transfer_id = 1;
    snapshot.tempo_hundredths = tune.tempo_hundredths;
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
        return false;
    }
    for (unsigned row=0; row<12; ++row) editor.Apply(EditorAction::Down);
    if(beat_fixture) {
        editor.SelectHeader(unmuted_digits?0:6);
        live.mute_mask=unmuted_digits?2:65;
        live.mute_status="Mute synchronized (fixture)";
    }
    RenderMainScreen(framebuffer, tune, pattern, live, &editor,host);

    return true;
}

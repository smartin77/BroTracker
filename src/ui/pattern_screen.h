/*
 * BroTracker
 *
 * Description: Main pattern screen UI renderer.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#pragma once

#include "core/tune.h"
#include "row_number_display.h"
#include "renderer/framebuffer.h"
#include "live_playback_view.h"
#include "local_pattern_editor.h"
#include "file_list.h"
#include "warning_text.h"

// Caller-owned presentation strings only; no transport/input work in rendering.
struct HostPanelInformation
{
    std::string connection_status;
    std::string transport_hints;
    std::string publication_status{};
    std::string diagnostic_status{};
    std::string midi_status{};
    BroTracker::RowNumbering row_numbering=BroTracker::RowNumbering::D1;
    std::string volume_status{};
    std::string midi_route_status{},midi_usb_status{};
    WarningText text{};
    unsigned instrument_number=0,instrument_original_number=0;
    std::string instrument_identity="Unknown (identity unavailable)";
    std::string instrument_availability="Unknown",instrument_resource="Unknown";
    int instrument_focus=-1;std::string instrument_load_warning,instrument_candidate;
    bool load_save_screen=false,sample_picker=false,instrument_load_available=false;
    std::string pattern_browser_text{};
    FileListView pattern_browser_list{};
};

void RenderMainScreen(
    Framebuffer& framebuffer,
    const Tune& tune,
    const Pattern& pattern,
    const std::optional<LivePlaybackView>& live = std::nullopt,
    const LocalPatternEditor* editor = nullptr,
    const HostPanelInformation& host = {});

// Explicit standalone mockup only; main applications always use RenderMainScreen.
void RenderPreviewScreen(Framebuffer& framebuffer, const Tune& tune, const Pattern& pattern);

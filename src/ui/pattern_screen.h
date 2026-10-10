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
#include "renderer/framebuffer.h"
#include "live_playback_view.h"
#include "local_pattern_editor.h"

// Caller-owned presentation strings only; no transport/input work in rendering.
struct HostPanelInformation
{
    std::string connection_status;
    std::string transport_hints;
};

void RenderMainScreen(
    Framebuffer& framebuffer,
    const Tune& tune,
    const Pattern& pattern,
    const std::optional<LivePlaybackView>& live = std::nullopt,
    const LocalPatternEditor* editor = nullptr,
    const HostPanelInformation& host = {});

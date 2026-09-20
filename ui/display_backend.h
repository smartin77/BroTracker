/*
 * BroTracker
 *
 * Description: Host display presentation boundary.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#pragma once

#include <cstdint>

class Framebuffer;

class DisplayBackend
{
public:
    virtual ~DisplayBackend() = default;

    virtual bool Initialize(
        std::uint32_t width,
        std::uint32_t height) = 0;

    virtual bool Present(
        const Framebuffer& framebuffer) = 0;
};

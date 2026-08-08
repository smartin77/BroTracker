/*
 * BroTracker
 *
 * Description: 640x480 framebuffer.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#pragma once

#include <cstdint>
#include <vector>

#include "color.h"

class Framebuffer
{
public:
    Framebuffer(
        std::uint32_t width,
        std::uint32_t height
    );

    void Clear(Color color);

    void SetPixel(
        std::uint32_t x,
        std::uint32_t y,
        Color color
    );

private:
    std::uint32_t width;
    std::uint32_t height;

    std::vector<std::uint32_t> pixels;
};
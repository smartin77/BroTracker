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
#include <filesystem>
#include <vector>

#include "color.h"

class Framebuffer
{
public:
    Framebuffer(
        std::uint32_t width,
        std::uint32_t height
    );

    std::uint32_t Width() const;
    std::uint32_t Height() const;

    void Clear(Color color);

    void SetPixel(
        int x,
        int y,
        Color color
    );

    void HorizontalLine(
        int x1,
        int x2,
        int y,
        Color color
    );

    void VerticalLine(
        int x,
        int y1,
        int y2,
        Color color
    );

    void Rectangle(
        int x,
        int y,
        int width,
        int height,
        Color color
    );

    void FilledRectangle(
        int x,
        int y,
        int width,
        int height,
        Color color
    );

    bool SaveBMP(const std::filesystem::path& path) const;

private:
    static void Write16(
        std::uint8_t* destination,
        std::uint16_t value
    );

    static void Write32(
        std::uint8_t* destination,
        std::uint32_t value
    );

    std::uint32_t width;
    std::uint32_t height;

    std::vector<Color> pixels;
};
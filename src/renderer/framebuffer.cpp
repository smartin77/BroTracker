/*
 * BroTracker
 *
 * Description: 640x480 framebuffer.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include "framebuffer.h"

#include <fstream>

Framebuffer::Framebuffer(
    std::uint32_t width,
    std::uint32_t height)
    : width(width)
    , height(height)
    , pixels(width * height, Color{0, 0, 0})
{
}

std::uint32_t Framebuffer::Width() const
{
    return width;
}

std::uint32_t Framebuffer::Height() const
{
    return height;
}

void Framebuffer::Clear(Color color)
{
    for (auto& pixel : pixels)
    {
        pixel = color;
    }
}

void Framebuffer::SetPixel(
    int x,
    int y,
    Color color)
{
    if (x < 0 || x >= static_cast<int>(width) ||
        y < 0 || y >= static_cast<int>(height))
    {
        return;
    }

    pixels[static_cast<std::uint32_t>(y) * width +
        static_cast<std::uint32_t>(x)] = color;
}

void Framebuffer::HorizontalLine(
    int x1,
    int x2,
    int y,
    Color color)
{
    for (int x = x1; x <= x2; ++x)
    {
        SetPixel(x, y, color);
    }
}

void Framebuffer::VerticalLine(
    int x,
    int y1,
    int y2,
    Color color)
{
    for (int y = y1; y <= y2; ++y)
    {
        SetPixel(x, y, color);
    }
}

void Framebuffer::Rectangle(
    int x,
    int y,
    int width,
    int height,
    Color color)
{
    HorizontalLine(x, x + width - 1, y, color);
    HorizontalLine(x, x + width - 1, y + height - 1, color);

    VerticalLine(x, y, y + height - 1, color);
    VerticalLine(x + width - 1, y, y + height - 1, color);
}

void Framebuffer::FilledRectangle(
    int x,
    int y,
    int width,
    int height,
    Color color)
{
    for (int current_y = y; current_y < y + height; ++current_y)
    {
        HorizontalLine(x, x + width - 1, current_y, color);
    }
}

bool Framebuffer::SaveBMP(const std::filesystem::path& path) const
{
    constexpr std::uint32_t HEADER_SIZE = 54;

    const std::uint32_t row_size = width * 3;
    const std::uint32_t file_size = HEADER_SIZE + row_size * height;

    std::ofstream file(path, std::ios::binary);

    if (!file)
    {
        return false;
    }

    std::uint8_t header[HEADER_SIZE] = {};

    header[0] = 'B';
    header[1] = 'M';

    Write32(header + 2, file_size);
    Write32(header + 10, HEADER_SIZE);
    Write32(header + 14, 40);
    Write32(header + 18, width);
    Write32(header + 22, height);
    Write16(header + 26, 1);
    Write16(header + 28, 24);
    Write32(header + 34, row_size * height);

    file.write(
        reinterpret_cast<const char*>(header),
        sizeof(header));

    for (int y = static_cast<int>(height) - 1; y >= 0; --y)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const Color& pixel =
                pixels[static_cast<std::uint32_t>(y) * width + x];

            file.put(static_cast<char>(pixel.b));
            file.put(static_cast<char>(pixel.g));
            file.put(static_cast<char>(pixel.r));
        }
    }

    return file.good();
}

void Framebuffer::Write16(
    std::uint8_t* destination,
    std::uint16_t value)
{
    destination[0] =
        static_cast<std::uint8_t>(value & 0xFF);
    destination[1] =
        static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

void Framebuffer::Write32(
    std::uint8_t* destination,
    std::uint32_t value)
{
    destination[0] =
        static_cast<std::uint8_t>(value & 0xFF);
    destination[1] =
        static_cast<std::uint8_t>((value >> 8) & 0xFF);
    destination[2] =
        static_cast<std::uint8_t>((value >> 16) & 0xFF);
    destination[3] =
        static_cast<std::uint8_t>((value >> 24) & 0xFF);
}

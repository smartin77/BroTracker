/*
 * BroTracker
 *
 * Description: Bitmap font text rendering helpers shared by all UI screens.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include "text_renderer.h"

#include <algorithm>

#include "font.h"

namespace
{
    Font ui_font;

    std::uint16_t DecodeUtf8(
        const std::string& text,
        std::size_t& index)
    {
        const std::uint8_t first =
            static_cast<std::uint8_t>(text[index]);

        if (first < 0x80)
        {
            ++index;
            return first;
        }

        if ((first & 0xE0) == 0xC0 &&
            index + 1 < text.size())
        {
            const std::uint16_t codepoint =
                static_cast<std::uint16_t>(
                    ((first & 0x1F) << 6) |
                    (static_cast<std::uint8_t>(
                        text[index + 1]) & 0x3F));

            index += 2;
            return codepoint;
        }

        ++index;
        return 0;
    }
}

bool LoadUiFont(const char* filename)
{
    return ui_font.Load(filename);
}

int DrawWrappedFixedText(Framebuffer& framebuffer, int x, int y,
    int width, int height, const std::string& text, Color color)
{
    constexpr int cell_width = 6;
    constexpr int line_height = 12;
    const int columns = width / cell_width;
    if (columns <= 0 || height <= 0 || text.empty()) return 0;
    int line_y = y;
    std::size_t start = 0;
    while (start < text.size() && line_y < y + height)
    {
        // Panel information is ASCII, so byte and fixed-cell counts coincide.
        auto end = std::min(start + static_cast<std::size_t>(columns), text.size());
        if (end < text.size() && text[end] != ' ')
        {
            const auto space = text.rfind(' ', end);
            if (space != std::string::npos && space > start) end = space;
        }
        int glyph_x = x;
        for (auto index = start; index < end;)
        {
            const auto* glyph = ui_font.Find(DecodeUtf8(text, index));
            if (glyph) for (int row = 0; row < 7; ++row)
                for (int column = 0; column < 5; ++column)
                    if ((glyph->bitmap[row] & (1u << (column + 2))) &&
                        glyph_x + column < x + width && line_y + row + 1 < y + height)
                        framebuffer.SetPixel(glyph_x + column, line_y + row + 1, color);
            glyph_x += cell_width;
        }
        start = end;
        while (start < text.size() && text[start] == ' ') ++start;
        line_y += line_height;
    }
    return std::min(line_y - y, height);
}

void DrawText(
    Framebuffer& framebuffer,
    int x,
    int y,
    const std::string& text,
    Color color)
{
    for (std::size_t index = 0;
        index < text.size();)
    {
        const std::uint16_t codepoint =
            DecodeUtf8(text, index);

        const Glyph* glyph =
            ui_font.Find(codepoint);

        if (glyph == nullptr)
        {
            x += 6;
            continue;
        }

        for (int row = 0; row < 7; ++row)
        {
            const std::uint8_t bitmap =
                glyph->bitmap[row];

            for (int column = 0;
                column < 5;
                ++column)
            {
                if (bitmap &
                    (1u << (column + 2)))
                {
                    framebuffer.SetPixel(
                        x + column,
                        y + row + 1,
                        color);
                }
            }
        }

        // Preserve the proportional glyph width.
        // The bitmap keeps its original left-side spacing; the
        // advance is based on the rightmost used pixel plus the
        // BTF-defined 1 px right spacing.
        int max_x = -1;

        for (int row = 0; row < 7; ++row)
        {
            const std::uint8_t bitmap =
                glyph->bitmap[row];

            for (int column = 0;
                column < 5;
                ++column)
            {
                if (bitmap &
                    (1u << (column + 2)))
                {
                    max_x =
                        std::max(max_x, column);
                }
            }
        }

        x += (max_x >= 0)
            ? max_x + 2
            : 3;
    }
}

void DrawFixedText(
    Framebuffer& framebuffer,
    int x,
    int y,
    const std::string& text,
    Color color)
{
    for (std::size_t index = 0;
        index < text.size();)
    {
        const std::uint16_t codepoint =
            DecodeUtf8(text, index);

        const Glyph* glyph =
            ui_font.Find(codepoint);

        if (glyph != nullptr)
        {
            for (int row = 0; row < 7; ++row)
            {
                const std::uint8_t bitmap =
                    glyph->bitmap[row];

                for (int column = 0;
                    column < 5;
                    ++column)
                {
                    if (bitmap &
                        (1u << (column + 2)))
                    {
                        framebuffer.SetPixel(
                            x + column,
                            y + row + 1,
                            color);
                    }
                }
            }
        }

        x += 6;
    }
}

void DrawCenteredFixedText(
    Framebuffer& framebuffer,
    int cell_x,
    int cell_width,
    int y,
    const std::string& text,
    Color color)
{
    constexpr int CELL_WIDTH = 6;

    const int text_width =
        static_cast<int>(text.length()) * CELL_WIDTH;

    const int x =
        cell_x + (cell_width - text_width) / 2;

    DrawFixedText(
        framebuffer,
        x,
        y,
        text,
        color);
}

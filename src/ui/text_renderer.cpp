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

/*
 * BroTracker
 *
 * Description: Basic UI mockup renderer.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "core/dummy_data.h"
#include "core/logger.h"
#include "ui/font.h"

namespace
{

constexpr int SCREEN_WIDTH = 640;
constexpr int SCREEN_HEIGHT = 480;

struct Color
{
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
};

constexpr Color background        { 16,  16,  16  };
constexpr Color border            { 128, 128, 128 };
constexpr Color header_name       { 130, 130, 196 };
constexpr Color header_value      { 255, 255, 255 };
constexpr Color row_header        { 76, 195, 201 };
constexpr Color row_number        { 130, 130, 196 };
constexpr Color channel_number    { 130, 130, 196 };
constexpr Color note              { 255, 255, 255 };
constexpr Color instrument_number { 130, 130, 196 };

class Framebuffer
{
public:
    Framebuffer()
        : pixels(SCREEN_WIDTH * SCREEN_HEIGHT, background)
    {
    }

    void Clear(Color color)
    {
        for (auto& pixel : pixels)
        {
            pixel = color;
        }
    }

    void SetPixel(int x, int y, Color color)
    {
        if (x < 0 || x >= SCREEN_WIDTH ||
            y < 0 || y >= SCREEN_HEIGHT)
        {
            return;
        }

        pixels[y * SCREEN_WIDTH + x] = color;
    }

    void HorizontalLine(int x1, int x2, int y, Color color)
    {
        for (int x = x1; x <= x2; ++x)
        {
            SetPixel(x, y, color);
        }
    }

    void VerticalLine(int x, int y1, int y2, Color color)
    {
        for (int y = y1; y <= y2; ++y)
        {
            SetPixel(x, y, color);
        }
    }

    void Rectangle(
        int x,
        int y,
        int width,
        int height,
        Color color)
    {
        HorizontalLine(x, x + width - 1, y, color);
        HorizontalLine(
            x,
            x + width - 1,
            y + height - 1,
            color);

        VerticalLine(x, y, y + height - 1, color);
        VerticalLine(
            x + width - 1,
            y,
            y + height - 1,
            color);
    }

    bool SaveBMP(const std::filesystem::path& path) const
    {
        constexpr std::uint32_t HEADER_SIZE = 54;
        constexpr std::uint32_t ROW_SIZE =
            SCREEN_WIDTH * 3;
        constexpr std::uint32_t FILE_SIZE =
            HEADER_SIZE + ROW_SIZE * SCREEN_HEIGHT;

        std::ofstream file(path, std::ios::binary);

        if (!file)
        {
            return false;
        }

        std::uint8_t header[HEADER_SIZE] = {};

        header[0] = 'B';
        header[1] = 'M';

        Write32(header + 2, FILE_SIZE);
        Write32(header + 10, HEADER_SIZE);
        Write32(header + 14, 40);
        Write32(header + 18, SCREEN_WIDTH);
        Write32(header + 22, SCREEN_HEIGHT);
        Write16(header + 26, 1);
        Write16(header + 28, 24);
        Write32(header + 34, ROW_SIZE * SCREEN_HEIGHT);

        file.write(
            reinterpret_cast<const char*>(header),
            sizeof(header));

        for (int y = SCREEN_HEIGHT - 1; y >= 0; --y)
        {
            for (int x = 0; x < SCREEN_WIDTH; ++x)
            {
                const Color& pixel =
                    pixels[y * SCREEN_WIDTH + x];

                file.put(static_cast<char>(pixel.b));
                file.put(static_cast<char>(pixel.g));
                file.put(static_cast<char>(pixel.r));
            }
        }

        return file.good();
    }

private:
    static void Write16(
        std::uint8_t* destination,
        std::uint16_t value)
    {
        destination[0] =
            static_cast<std::uint8_t>(value & 0xFF);
        destination[1] =
            static_cast<std::uint8_t>((value >> 8) & 0xFF);
    }

    static void Write32(
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

    std::vector<Color> pixels;
};

Font brotracker_font;

bool LoadBroTrackerFont()
{
    return brotracker_font.Load(
        "assets/fonts/brotracker.btf");
}

int GlyphAdvance(const Glyph* glyph)
{
    if (glyph == nullptr)
        return 6;

    int min_x = 5;
    int max_x = -1;

    for (int row = 0; row < 7; ++row)
    {
        const std::uint8_t bitmap = glyph->bitmap[row];

        for (int column = 0; column < 5; ++column)
        {
            if (bitmap & (1u << (column + 2)))
            {
                min_x = std::min(min_x, column);
                max_x = std::max(max_x, column);
            }
        }
    }

    if (max_x < min_x)
        return 3;

    return (max_x - min_x + 1) + 1;
}

void DrawText(
    Framebuffer& framebuffer,
    int x,
    int y,
    const std::string& text,
    Color color)
{
    for (char character : text)
    {
        const Glyph* glyph =
            brotracker_font.Find(
                static_cast<std::uint16_t>(
                    static_cast<unsigned char>(character)));

        if (glyph == nullptr)
        {
            x += 6;
            continue;
        }

        for (int row = 0; row < 7; ++row)
        {
            const std::uint8_t bitmap = glyph->bitmap[row];

            for (int column = 0; column < 5; ++column)
            {
                if (bitmap & (1u << (column + 2)))
                {
                    framebuffer.SetPixel(
                        x + column,
                        y + row + 1,
                        color);
                }
            }
        }

        x += GlyphAdvance(glyph);
    }
}

// UI fields use a fixed six-pixel character cell even though the
// glyphs themselves are drawn proportionally.
void DrawFixedText(
    Framebuffer& framebuffer,
    int x,
    int y,
    const std::string& text,
    Color color)
{
    for (char character : text)
    {
        const Glyph* glyph =
            brotracker_font.Find(
                static_cast<std::uint16_t>(
                    static_cast<unsigned char>(character)));

        if (glyph != nullptr)
        {
            for (int row = 0; row < 7; ++row)
            {
                const std::uint8_t bitmap = glyph->bitmap[row];

                for (int column = 0; column < 5; ++column)
                {
                    if (bitmap & (1u << (column + 2)))
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

std::string NoteToString(std::uint8_t note)
{
    if (note == 0xFF)
        return "---";

    static constexpr const char* names[] =
    {
        "C-", "C#", "D-", "D#", "E-", "F-",
        "F#", "G-", "G#", "A-", "A#", "B-"
    };

    return std::string(names[note % 12]) +
        static_cast<char>('0' + note / 12);
}

std::string InstrumentToString(std::uint8_t instrument)
{
    if (instrument == 0xFF)
        return "--";

    return std::string{
        static_cast<char>('0' + instrument / 10),
        static_cast<char>('0' + instrument % 10)
    };
}

void DrawMainHeader(
    Framebuffer& framebuffer,
    const Tune& tune,
    const Pattern& pattern)
{
    framebuffer.Rectangle(
        0, 0, 454, 26, border);

    DrawText(
        framebuffer,
        7, 9,
        "TUNE:",
        header_name);

    DrawText(
        framebuffer,
        43, 9,
        tune.title,
        header_value);

    constexpr int CHANNEL_START_X = 30;
    constexpr int CHANNEL_WIDTH = 53;

    const std::string labels[] =
    {
        "PAT:",
        "POS:",
        "BPM:",
        "LBP:",
        "CPU:"
    };

    const std::string values[] =
    {
        pattern.number < 10
            ? "0" + std::to_string(pattern.number)
            : std::to_string(pattern.number),
        "06",
        std::to_string(tune.tempo),
        "04",
        "03%"
    };

    for (int index = 0; index < 5; ++index)
    {
        const int cell_x =
            CHANNEL_START_X +
            (index + 3) * CHANNEL_WIDTH;

        const int group_cells =
            static_cast<int>(
                labels[index].length() +
                values[index].length());

        const int group_width =
            group_cells * 6;

        const int x =
            cell_x +
            (CHANNEL_WIDTH - group_width) / 2;

        DrawFixedText(
            framebuffer,
            x, 9,
            labels[index],
            header_name);

        DrawFixedText(
            framebuffer,
            x + static_cast<int>(
                    labels[index].length()) * 6,
            9,
            values[index],
            header_value);
    }
}

void DrawPatternFrame(
    Framebuffer& framebuffer)
{
    framebuffer.Rectangle(
        0, 29, 454, 451, border);

    framebuffer.HorizontalLine(
        1, 452, 54, border);

    // Row-number separator: one pixel shorter at both ends
    // than the 13-pixel row cell.
    for (int row = 0; row < 31; ++row)
    {
        const int row_top =
            67 + row * 13;

        if (row_top > 464)
            break;

        framebuffer.VerticalLine(
            29,
            row_top,
            std::min(row_top + 11, 464),
            border);
    }

    constexpr int CHANNEL_SEPARATORS[] =
    {
        82, 135, 188, 241, 294, 347, 400
    };

    for (const int x : CHANNEL_SEPARATORS)
    {
        for (int y = 39; y <= 464; y += 4)
        {
            framebuffer.VerticalLine(
                x,
                y,
                std::min(y + 1, 464),
                border);
        }
    }
}

void DrawRowHeader(
    Framebuffer& framebuffer)
{
    constexpr int CHANNEL_START_X = 30;
    constexpr int CHANNEL_WIDTH = 53;

    DrawFixedText(
        framebuffer,
        5, 38,
        "d0",
        row_header);

    for (int channel = 0; channel < 8; ++channel)
    {
        const int cell_x =
            CHANNEL_START_X +
            channel * CHANNEL_WIDTH;

        DrawCenteredFixedText(
            framebuffer,
            cell_x,
            CHANNEL_WIDTH,
            38,
            std::to_string(channel + 1),
            channel_number);
    }
}

void DrawRowNumbers(
    Framebuffer& framebuffer)
{
    constexpr int FIRST_ROW_Y = 67;
    constexpr int ROW_HEIGHT = 13;

    for (int row = 0; row < 31; ++row)
    {
        const int y =
            FIRST_ROW_Y +
            row * ROW_HEIGHT;

        std::string row_text =
            std::to_string(row);

        if (row < 10)
            row_text.insert(0, "0");

        DrawCenteredFixedText(
            framebuffer,
            0,
            29,
            y,
            row_text,
            row_number);
    }
}

void DrawPatternChannels(
    Framebuffer& framebuffer,
    const Pattern& pattern)
{
    constexpr int FIRST_ROW_Y = 67;
    constexpr int ROW_HEIGHT = 13;

    constexpr int CHANNEL_START_X = 30;
    constexpr int CHANNEL_WIDTH = 53;

    constexpr int FIELD_WIDTH = 6 * 6;

    for (int channel = 0; channel < 8; ++channel)
    {
        const int cell_x =
            CHANNEL_START_X +
            channel * CHANNEL_WIDTH;

        const int field_x =
            cell_x +
            (CHANNEL_WIDTH - FIELD_WIDTH) / 2;

        for (int row = 0; row < 31; ++row)
        {
            const int y =
                FIRST_ROW_Y +
                row * ROW_HEIGHT;

            std::uint8_t note_value = 0xFF;
            std::uint8_t instrument_value = 0xFF;

            if (channel <
                static_cast<int>(pattern.channels.size()))
            {
                const auto& rows =
                    pattern.channels[channel].rows;

                if (row < static_cast<int>(rows.size()))
                {
                    note_value = rows[row].note;
                    instrument_value = rows[row].instrument;
                }
            }

            DrawFixedText(
                framebuffer,
                field_x,
                y,
                NoteToString(note_value),
                note);

            DrawFixedText(
                framebuffer,
                field_x + 4 * 6,
                y,
                InstrumentToString(instrument_value),
                instrument_number);
        }
    }
}

void DrawPattern(
    Framebuffer& framebuffer,
    const Pattern& pattern)
{
    DrawPatternFrame(framebuffer);
    DrawRowHeader(framebuffer);
    DrawRowNumbers(framebuffer);
    DrawPatternChannels(framebuffer, pattern);
}

void RenderMainScreen(
    Framebuffer& framebuffer,
    const Tune& tune,
    const Pattern& pattern)
{
    framebuffer.Clear(background);

    DrawMainHeader(
        framebuffer,
        tune,
        pattern);

    DrawPattern(
        framebuffer,
        pattern);
}

} // namespace

int main()
{
    LogInfo("BroTracker UI starting.");

    if (!LoadBroTrackerFont())
    {
        LogError("Failed to load BroTracker font.");
        return 1;
    }

    Tune tune = CreateDummyTune();

    if (tune.patterns.empty())
    {
        LogError("No patterns available.");
        return 1;
    }

    const Pattern& pattern =
        tune.patterns.front();

    LogInfo("Creating main screen mockup.");

    Framebuffer framebuffer;

    RenderMainScreen(
        framebuffer,
        tune,
        pattern);

    const std::filesystem::path output_path =
        "assets/ui_main_screen.bmp";

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

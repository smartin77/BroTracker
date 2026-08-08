/*
 * BroTracker
 *
 * Description: Basic UI mockup renderer.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

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

constexpr int SCREEN_WIDTH  = 640;
constexpr int SCREEN_HEIGHT = 480;

// -----------------------------------------------------------------------------
// Colors
// -----------------------------------------------------------------------------

struct Color
{
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
};

constexpr Color BLACK       { 0,   0,   0   };
constexpr Color LINE        { 90,  90,  105 };
constexpr Color LINE_DIM    { 55,  55,  70  };
constexpr Color CYAN        { 0,   210, 210 };
constexpr Color TEXT        { 185, 180, 220 };
constexpr Color WHITE       { 215, 215, 215 };
constexpr Color INSTRUMENT  { 125, 85,  55  };
constexpr Color INVERSE_BG  { 150, 150, 165 };
constexpr Color INVERSE_FG  { 35,  35,  45  };

// -----------------------------------------------------------------------------
// Simple framebuffer
// -----------------------------------------------------------------------------

class Framebuffer
{
public:
    Framebuffer()
        : pixels(SCREEN_WIDTH * SCREEN_HEIGHT, BLACK)
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
        if (x2 < x1)
        {
            return;
        }

        for (int x = x1; x <= x2; ++x)
        {
            SetPixel(x, y, color);
        }
    }

    void VerticalLine(int x, int y1, int y2, Color color)
    {
        if (y2 < y1)
        {
            return;
        }

        for (int y = y1; y <= y2; ++y)
        {
            SetPixel(x, y, color);
        }
    }

    void Rectangle(int x, int y, int width, int height, Color color)
    {
        HorizontalLine(x, x + width - 1, y, color);
        HorizontalLine(x, x + width - 1, y + height - 1, color);

        VerticalLine(x, y, y + height - 1, color);
        VerticalLine(x + width - 1, y, y + height - 1, color);
    }

    void FilledRectangle(
        int x,
        int y,
        int width,
        int height,
        Color color)
    {
        for (int py = y; py < y + height; ++py)
        {
            HorizontalLine(
                x,
                x + width - 1,
                py,
                color);
        }
    }

    void DottedVerticalLine(
        int x,
        int y1,
        int y2,
        Color color)
    {
        for (int y = y1; y <= y2; y += 3)
        {
            SetPixel(x, y, color);
        }
    }

    bool SaveBMP(const std::filesystem::path& path) const
    {
        constexpr std::uint32_t HEADER_SIZE = 54;
        constexpr std::uint32_t ROW_SIZE =
            SCREEN_WIDTH * 3;

        constexpr std::uint32_t FILE_SIZE =
            HEADER_SIZE +
            ROW_SIZE * SCREEN_HEIGHT;

        std::ofstream file(path, std::ios::binary);

        if (!file)
        {
            return false;
        }

        std::uint8_t header[54] = {};

        header[0] = 'B';
        header[1] = 'M';

        Write32(header + 2, FILE_SIZE);
        Write32(header + 10, HEADER_SIZE);

        Write32(header + 14, 40);
        Write32(header + 18, SCREEN_WIDTH);
        Write32(header + 22, SCREEN_HEIGHT);

        Write16(header + 26, 1);
        Write16(header + 28, 24);

        Write32(header + 34,
                ROW_SIZE * SCREEN_HEIGHT);

        file.write(
            reinterpret_cast<const char*>(header),
            sizeof(header));

        // BMP is stored bottom-to-top.
        for (int y = SCREEN_HEIGHT - 1; y >= 0; --y)
        {
            for (int x = 0; x < SCREEN_WIDTH; ++x)
            {
                const Color& pixel =
                    pixels[y * SCREEN_WIDTH + x];

                file.put(
                    static_cast<char>(pixel.b));

                file.put(
                    static_cast<char>(pixel.g));

                file.put(
                    static_cast<char>(pixel.r));
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

// -----------------------------------------------------------------------------
// BroTracker font
// -----------------------------------------------------------------------------

Font brotracker_font;

bool LoadBroTrackerFont()
{
    return brotracker_font.Load(
        "assets/fonts/brotracker.btf");
}

void DrawText(
    Framebuffer& framebuffer,
    int x,
    int y,
    const std::string& text,
    Color color)
{
    int cursor_x = x;

    for (char character : text)
    {
        const Glyph* glyph =
            brotracker_font.Find(
                static_cast<std::uint16_t>(
                    static_cast<unsigned char>(character)));

        if (glyph == nullptr)
        {
            cursor_x += 6;
            continue;
        }

        for (int row = 0; row < 7; ++row)
        {
            const std::uint8_t bitmap =
                glyph->bitmap[row];

            for (int column = 0; column < 5; ++column)
            {
                if (bitmap & (1u << column))
                {
                    framebuffer.SetPixel(
                        cursor_x + column,
                        y + row,
                        color);
                }
            }
        }

        cursor_x += 6;
    }
}

// -----------------------------------------------------------------------------
// Note formatting
// -----------------------------------------------------------------------------

std::string NoteToString(std::uint8_t note)
{
    if (note == NOTE_EMPTY)
    {
        return "---";
    }

    static constexpr const char* note_names[] =
    {
        "C-",
        "C#",
        "D-",
        "D#",
        "E-",
        "F-",
        "F#",
        "G-",
        "G#",
        "A-",
        "A#",
        "B-"
    };

    const int octave = note / 12;
    const int note_index = note % 12;

    std::string result =
        note_names[note_index];

    result +=
        static_cast<char>('0' + octave);

    return result;
}

std::string InstrumentToString(std::uint8_t instrument)
{
    if (instrument == 0xFF)
    {
        return "--";
    }

    const int value = instrument;

    std::string result;

    result +=
        static_cast<char>('0' + (value / 10));

    result +=
        static_cast<char>('0' + (value % 10));

    return result;
}

// -----------------------------------------------------------------------------
// UI rendering
// -----------------------------------------------------------------------------

void DrawHeader(
    Framebuffer& framebuffer,
    const Tune& tune,
    const Pattern& pattern)
{
    framebuffer.Rectangle(
        8,
        8,
        624,
        58,
        LINE);

    DrawText(
        framebuffer,
        18,
        28,
        "TUNE",
        CYAN);

    DrawText(
        framebuffer,
        66,
        28,
        tune.title,
        TEXT);

    DrawText(
        framebuffer,
        255,
        28,
        "PATTERN",
        TEXT);

    DrawText(
        framebuffer,
        315,
        28,
        "05",
        WHITE);

    DrawText(
        framebuffer,
        390,
        28,
        "BPM",
        TEXT);

    DrawText(
        framebuffer,
        420,
        28,
        std::to_string(tune.tempo),
        WHITE);

    DrawText(
        framebuffer,
        500,
        28,
        "ROW",
        TEXT);

    DrawText(
        framebuffer,
        536,
        28,
        "08",
        WHITE);
}

void DrawPattern(
    Framebuffer& framebuffer,
    const Pattern& pattern)
{
    constexpr int panel_x = 8;
    constexpr int panel_y = 74;
    constexpr int panel_w = 444;
    constexpr int panel_h = 346;

    constexpr int row_x = 18;
    constexpr int row_separator_x = 56;

    constexpr int channel_start_x = 58;
    constexpr int channel_width = 48;

    constexpr int header_y = 92;
    constexpr int grid_y = 116;
    constexpr int row_height = 19;

    framebuffer.Rectangle(
        panel_x,
        panel_y,
        panel_w,
        panel_h,
        LINE);

    // -------------------------------------------------------------------------
    // Row-number column
    // -------------------------------------------------------------------------

    framebuffer.Rectangle(
        16,
        82,
        38,
        28,
        CYAN);

    DrawText(
        framebuffer,
        27,
        92,
        "d0",
        CYAN);

    // One-pixel separator after row number column.
    framebuffer.VerticalLine(
        row_separator_x,
        grid_y,
        panel_y + panel_h - 1,
        LINE);

    // -------------------------------------------------------------------------
    // Channel headers
    // -------------------------------------------------------------------------

    for (int channel = 0; channel < 8; ++channel)
    {
        const int x =
            channel_start_x +
            channel * channel_width;

        DrawText(
            framebuffer,
            x + 18,
            header_y,
            std::to_string(channel + 1),
            CYAN);

        // Deliberately start below the header.
        if (channel < 7)
        {
            framebuffer.DottedVerticalLine(
                x + channel_width - 1,
                grid_y,
                panel_y + panel_h - 1,
                LINE);
        }
    }

    // Header baseline.
    framebuffer.HorizontalLine(
        row_separator_x,
        panel_x + panel_w - 10,
        112,
        LINE);
    
    // -------------------------------------------------------------------------
    // Pattern rows
    // -------------------------------------------------------------------------

    constexpr int current_row = 8;

    for (int row = 0; row < 16; ++row)
    {
        const int y =
            grid_y +
            row * row_height;

        // Current row frame spans the entire pattern.
        if (row == current_row)
        {
            framebuffer.Rectangle(
                16,
                y - 2,
                420,
                row_height,
                WHITE);
        }

        std::string row_number;

        row_number +=
            "0123456789ABCDEF"[row >> 4];

        row_number +=
            "0123456789ABCDEF"[row & 0x0F];

        DrawText(
            framebuffer,
            row_x,
            y,
            row_number,
            row == current_row
                ? WHITE
                : TEXT);

        if (row == current_row)
        {
            DrawText(
                framebuffer,
                45,
                y,
                ">",
                WHITE);
        }

        for (int channel = 0; channel < 8; ++channel)
        {
            const int x =
                channel_start_x +
                channel * channel_width;

            std::uint8_t note = NOTE_EMPTY;
            std::uint8_t instrument = 0xFF;

            if (channel < static_cast<int>(pattern.channels.size()))
            {
                const auto& rows =
                    pattern.channels[channel].rows;

                if (row < static_cast<int>(rows.size()))
                {
                    note = rows[row].note;
                    instrument = rows[row].instrument;
                }
            }

            const std::string note_text =
                NoteToString(note);

            const std::string instrument_text =
                InstrumentToString(instrument);

            // The first event of the current row is shown
            // in inverse, representing the future event editor
            // preview on the right side.
            if (row == current_row && channel == 0)
            {
                framebuffer.FilledRectangle(
                    x,
                    y - 2,
                    46,
                    row_height,
                    INVERSE_BG);

                DrawText(
                    framebuffer,
                    x + 5,
                    y,
                    note_text,
                    INVERSE_FG);

                DrawText(
                    framebuffer,
                    x + 29,
                    y,
                    instrument_text,
                    INSTRUMENT);
            }
            else
            {
                DrawText(
                    framebuffer,
                    x + 5,
                    y,
                    note_text,
                    row == current_row
                        ? WHITE
                        : TEXT);

                DrawText(
                    framebuffer,
                    x + 29,
                    y,
                    instrument_text,
                    INSTRUMENT);
            }
        }
    }
}

void DrawRightPanel(
    Framebuffer& framebuffer)
{
    // Reserved for the future event/instrument panel.
    // The geometry is already reserved so the left side
    // can be designed against the final 640x480 layout.

    framebuffer.Rectangle(
        460,
        74,
        172,
        346,
        LINE);
}

void DrawFooter(
    Framebuffer& framebuffer)
{
    framebuffer.Rectangle(
        8,
        432,
        624,
        40,
        LINE);

    DrawText(
        framebuffer,
        18,
        448,
        "CH 1-8",
        CYAN);

    // Channel indicators.
    for (int channel = 0; channel < 8; ++channel)
    {
        const int x =
            180 +
            channel * 18;

        if (channel == 0)
        {
            framebuffer.FilledRectangle(
                x,
                447,
                8,
                8,
                CYAN);
        }
        else
        {
            framebuffer.Rectangle(
                x,
                447,
                8,
                8,
                CYAN);
        }
    }

    // Future right-side controls.
    DrawText(
        framebuffer,
        560,
        448,
        "INS",
        TEXT);

    DrawText(
        framebuffer,
        590,
        448,
        "MIX",
        TEXT);

    DrawText(
        framebuffer,
        620,
        448,
        "OPT",
        TEXT);
}

void RenderMainScreen(
    Framebuffer& framebuffer,
    const Tune& tune,
    const Pattern& pattern)
{
    framebuffer.Clear(BLACK);

    DrawHeader(
        framebuffer,
        tune,
        pattern);

    DrawPattern(
        framebuffer,
        pattern);

    DrawRightPanel(
        framebuffer);

    DrawFooter(
        framebuffer);
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

    LogInfo("BroTracker font loaded.");

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
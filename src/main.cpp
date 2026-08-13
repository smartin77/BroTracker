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
constexpr Color border            { 64,  64,  64 };
constexpr Color header_name       { 130, 130, 196 };
constexpr Color header_value      { 255, 255, 255 };
constexpr Color row_header_normal { 38, 120, 124 };
constexpr Color row_header_bright { 76, 195, 201 };
constexpr Color row_number        { 130, 130, 196 };
constexpr Color channel_number    { 130, 130, 196 };
constexpr Color note              { 255, 255, 255 };
constexpr Color instrument_number { 130, 130, 196 };

struct Layout
{
    // Pattern/header area.
    int pattern_x = 0;
    int pattern_y = 29;
    int pattern_width = 454;

    int header_height = 26;
    int pattern_header_height = 26;

    // Pattern rows.
    int first_row_y = 62;
    int row_height = 13;
    int visible_rows = 32;

    // Row number column.
    int row_number_width = 29;

    // Channels.
    int channel_start_x = 30;
    int channel_width = 53;
    int channel_count = 8;

    // Reserved spacing for later UI elements.
    int marker_padding = 1;
    int section_padding = 3;
    int frame_gap = 3;
};

constexpr Layout layout{};

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
            brotracker_font.Find(codepoint);

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

// UI fields use a fixed six-pixel character cell even though
// the glyphs themselves are drawn proportionally.
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
            brotracker_font.Find(codepoint);

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
        static_cast<int>(
            text.length()) *
        CELL_WIDTH;

    const int x =
        cell_x +
        (cell_width - text_width) / 2;

    DrawFixedText(
        framebuffer,
        x,
        y,
        text,
        color);
}

std::string NoteToString(
    std::uint8_t note)
{
    if (note == NOTE_OFF)
    {
        return "OFF";
    }

    if (note == NOTE_EMPTY)
    {
        return "---";
    }

    static constexpr const char* names[] =
    {
        "C-", "C#", "D-", "D#", "E-", "F-",
        "F#", "G-", "G#", "A-", "A#", "B-"
    };

    return std::string(
        names[note % 12]) +
        static_cast<char>(
            '0' + note / 12);
}

std::string InstrumentToString(
    std::uint8_t instrument)
{
    if (instrument == 0xFF)
    {
        return "--";
    }

    return std::string{
        static_cast<char>(
            '0' + instrument / 10),

        static_cast<char>(
            '0' + instrument % 10)
    };
}

void DrawMainHeader(
    Framebuffer& framebuffer,
    const Tune& tune,
    const Pattern& pattern)
{
    framebuffer.Rectangle(
        layout.pattern_x,
        0,
        layout.pattern_width,
        layout.header_height,
        border);

    DrawText(
        framebuffer,
        7,
        9,
        "TUNE:",
        header_name);

    DrawText(
        framebuffer,
        43,
        9,
        tune.title,
        header_value);

    const std::string labels[] =
    {
        "PAT:",
        "POS:",
        "BPM:",
        "LPB:",
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

    for (int index = 0;
         index < 5;
         ++index)
    {
        const int cell_x =
            layout.channel_start_x +
            (index + 3) *
                layout.channel_width;

        const int group_cells =
            static_cast<int>(
                labels[index].length() +
                values[index].length());

        const int group_width =
            group_cells * 6;

        const int x =
            cell_x +
            (layout.channel_width -
             group_width) / 2;

        DrawFixedText(
            framebuffer,
            x,
            9,
            labels[index],
            header_name);

        DrawFixedText(
            framebuffer,
            x +
                static_cast<int>(
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
        layout.pattern_x,
        layout.pattern_y,
        layout.pattern_width,
        SCREEN_HEIGHT -
            layout.pattern_y,
        border);

    /*
     * Pattern header separator.
     *
     * Starts one pixel before channel 1 and ends one pixel
     * before the right edge of channel 8.
     */
    framebuffer.HorizontalLine(
        layout.channel_start_x,
        layout.channel_start_x +
            layout.channel_count *
                layout.channel_width - 1,
        layout.pattern_y +
            layout.pattern_header_height - 1,
        border);

    /*
     * Row-number separator.
     */
    for (int row = 0;
     row < layout.visible_rows;
     ++row)
    {
        const int row_y =
            layout.first_row_y +
            row * layout.row_height;

        framebuffer.VerticalLine(
            layout.row_number_width,
            row_y + 1,
            row_y + layout.row_height - 2,
            border);
    }

    /*
     * Channel separators.
     *
     * They are intentionally dashed rather than solid.
     */
    for (int channel = 1;
         channel < layout.channel_count;
         ++channel)
    {
        const int x =
            layout.channel_start_x +
            channel * layout.channel_width;

        for (int y = 39;
            y < SCREEN_HEIGHT;
            y += 4)
        {
            framebuffer.VerticalLine(
                x,
                y,
                std::min(
                    y + 1,
                    SCREEN_HEIGHT - 1),
                border);
        }
    }

}

void DrawRowHeader(
    Framebuffer& framebuffer)
{
    DrawCenteredFixedText(
        framebuffer,
        0,
        layout.row_number_width,
        38,
        "d1",
        row_header_bright);

    // d1 header: right and bottom frame
    framebuffer.VerticalLine(
        layout.row_number_width,
        39,
        layout.pattern_y +
            layout.pattern_header_height - 2,
        row_header_normal);

    framebuffer.HorizontalLine(
        2,
        layout.row_number_width - 1,
        layout.pattern_y +
            layout.pattern_header_height - 1,
        row_header_normal);

    for (int channel = 0;
         channel < layout.channel_count;
         ++channel)
    {
        const int cell_x =
            layout.channel_start_x +
            channel * layout.channel_width;

        DrawCenteredFixedText(
            framebuffer,
            cell_x,
            layout.channel_width,
            38,
            std::to_string(channel + 1),
            channel_number);
    }
}

void DrawRowNumbers(
    Framebuffer& framebuffer)
{
    for (int row = 0;
         row < layout.visible_rows;
         ++row)
    {
        const int y =
            layout.first_row_y +
            row * layout.row_height +
            2;

        /*
         * Commit 1 uses the new default:
         * decimal, starting at 1.
         */
        std::string row_text =
            std::to_string(row + 1);

        if (row_text.length() < 2)
        {
            row_text.insert(0, "0");
        }

        DrawCenteredFixedText(
            framebuffer,
            0,
            layout.row_number_width,
            y,
            row_text,
            row_number);
    }
}

void DrawPatternChannels(
    Framebuffer& framebuffer,
    const Pattern& pattern)
{
    constexpr int FIELD_WIDTH = 6 * 6;

    for (int channel = 0;
         channel < layout.channel_count;
         ++channel)
    {
        const int cell_x =
            layout.channel_start_x +
            channel * layout.channel_width;

        const int field_x =
            cell_x +
            (layout.channel_width -
             FIELD_WIDTH) / 2;

        for (int row = 0;
             row < layout.visible_rows;
             ++row)
        {
            const int y =
                layout.first_row_y +
                row * layout.row_height +
                2;

            std::uint8_t note_value = NOTE_EMPTY;
            std::uint8_t instrument_value = 0xFF;

            if (channel <
                static_cast<int>(
                    pattern.channels.size()))
            {
                const auto& rows =
                    pattern.channels[channel].rows;

                if (row <
                    static_cast<int>(
                        rows.size()))
                {
                    note_value =
                        rows[row].note;

                    instrument_value =
                        rows[row].instrument;
                }
            }

            if (note_value == NOTE_OFF)
            {
                DrawFixedText(
                    framebuffer,
                    field_x,
                    y,
                    "OFF",
                    note);

                DrawFixedText(
                    framebuffer,
                    field_x + 4 * 6,
                    y,
                    "¯",
                    instrument_number);
            }
            else
            {
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
                    InstrumentToString(
                        instrument_value),
                    instrument_number);
            }
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
    DrawPatternChannels(
        framebuffer,
        pattern);
}

enum class RightMenuId
{
    Options,
    Instrument,
    Mixer
};

struct RightMenuItem
{
    const char* label;
    RightMenuId id;
};

constexpr RightMenuItem RIGHT_MENU[] =
{
    { "OPT", RightMenuId::Options },
    { "INS", RightMenuId::Instrument },
    { "MIX", RightMenuId::Mixer }
};

constexpr int RIGHT_MENU_ACTIVE = 0;

void DrawRightMenu(
    Framebuffer& framebuffer)
{
    constexpr int RIGHT_SECTION_X = 457;
    constexpr int RIGHT_SECTION_WIDTH =
        SCREEN_WIDTH - RIGHT_SECTION_X;
    constexpr int RIGHT_MENU_HEIGHT =
        layout.header_height;

    const int item_count =
        static_cast<int>(
            sizeof(RIGHT_MENU) /
            sizeof(RIGHT_MENU[0]));

    const int item_width =
        RIGHT_SECTION_WIDTH /
        item_count;

    // The menu is a completely independent frame.
    // It mirrors the left main header in height and Y position.
    framebuffer.Rectangle(
        RIGHT_SECTION_X,
        0,
        RIGHT_SECTION_WIDTH,
        RIGHT_MENU_HEIGHT,
        border);

    for (int index = 0;
         index < item_count;
         ++index)
    {
        const int x =
            RIGHT_SECTION_X +
            index * item_width;

        DrawCenteredFixedText(
            framebuffer,
            x,
            item_width,
            9,
            RIGHT_MENU[index].label,
            channel_number);
    }

    // Solid separators between menu items.
    // Leave one full pixel of background between each separator
    // and both the top and bottom frame edges.
    for (int index = 1;
         index < item_count;
         ++index)
    {
        const int x =
            RIGHT_SECTION_X +
            index * item_width;

        framebuffer.VerticalLine(
            x,
            2,
            RIGHT_MENU_HEIGHT - 3,
            border);
    }
}

void DrawRightWorkspace(
    Framebuffer& framebuffer)
{
    constexpr int RIGHT_SECTION_X = 457;
    constexpr int RIGHT_SECTION_WIDTH =
        SCREEN_WIDTH - RIGHT_SECTION_X;

    // This is a separate frame from the menu.
    // The 3 px gap is rows 26, 27 and 28.
    constexpr int WORKSPACE_Y =
        layout.header_height +
        layout.frame_gap;

    framebuffer.Rectangle(
        RIGHT_SECTION_X,
        WORKSPACE_Y,
        RIGHT_SECTION_WIDTH,
        SCREEN_HEIGHT - WORKSPACE_Y,
        border);
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

    DrawRightMenu(framebuffer);
    DrawRightWorkspace(framebuffer);
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

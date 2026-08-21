/*
 * BroTracker
 *
 * Description: Main pattern screen UI renderer.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include "pattern_screen.h"

#include <algorithm>
#include <string>
#include <iomanip>
#include <sstream>

#include "core/constants.h"
#include "core/note.h"
#include "core/note_formatter.h"
#include "renderer/color.h"
#include "text_renderer.h"

namespace
{
    constexpr int SCREEN_WIDTH = static_cast<int>(::SCREEN_WIDTH);
    constexpr int SCREEN_HEIGHT = static_cast<int>(::SCREEN_HEIGHT);

    constexpr Color background                  { 16,  16,  16  };
    constexpr Color border                      { 64,  64,  64  };
    constexpr Color current_row_background      { 30,  30,  30  };
    constexpr Color current_position_background { 153, 153, 153 };
    constexpr Color header_name                 { 130, 130, 196 };
    constexpr Color header_value                { 255, 255, 255 };
    constexpr Color row_header_normal           { 38,  120, 124 };
    constexpr Color row_header_bright           { 76,  195, 201 };
    constexpr Color row_number_normal           { 78,  78,  118 };
    constexpr Color row_number_bright           { 130, 130, 196 };
    constexpr Color row_marker                  { 192, 192, 192 };
    constexpr Color channel_number              { 130, 130, 196 };
    constexpr Color note_color_default          { 255, 255, 255 };
    constexpr Color instrument_number           { 130, 130, 196 };
    constexpr Color empty_note                  { 192, 192, 192 };
    constexpr Color empty_instrument            { 78,  78,  118 };
    constexpr Color current_position_note       { 16,  16,  16  };
    constexpr Color current_position_instrument { 78,  78,  118 };
    constexpr Color current_event_edit_border   { 134, 32,  32  };

    constexpr int pattern_position_highlight_interval = 4;

    constexpr int current_pattern_row = 13;
    constexpr int current_pattern_channel = 0;

    constexpr int current_event_edit_border_thickness = 1;
    constexpr int current_event_edit_padding = 1;

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
        int right_navigation_height = 26;
    };

    constexpr Layout layout{};

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
        std::ostringstream tempo_stream;

        tempo_stream
            << std::fixed
            << std::setprecision(1)
            << tune.tempo;

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

        const std::string header_labels[] =
        {
            "BPM:",
            "PAT:",
            "POS:",
            "CPU:"
        };

        const std::string header_values[] =
        {
            tempo_stream.str(),
            
            pattern.number < 10
                ? "0" + std::to_string(pattern.number)
                : std::to_string(pattern.number),

            "16",

            "03%"
        };

        const int header_item_count =
            static_cast<int>(
                sizeof(header_labels) /
                sizeof(header_labels[0]));

        for (int index = 0;
            index < header_item_count;
            ++index)
        {
            const int cell_x =
                layout.channel_start_x +
                (index + 4) *
                    layout.channel_width;

            const int group_cells =
                static_cast<int>(
                    header_labels[index].length() +
                    header_values[index].length());

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
                header_labels[index],
                header_name);

            DrawFixedText(
                framebuffer,
                x +
                    static_cast<int>(
                        header_labels[index].length()) * 6,
                9,
                header_values[index],
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

            std::string row_text =
                std::to_string(row + 1);

            if (row_text.length() < 2)
            {
                row_text.insert(0, "0");
            }

            const bool is_highlighted_position =
                ((row + 1) %
                    pattern_position_highlight_interval) == 1;

            const Color row_number_color =
                is_highlighted_position
                    ? row_number_bright
                    : row_number_normal;

            DrawCenteredFixedText(
                framebuffer,
                0,
                layout.row_number_width,
                y,
                row_text,
                row_number_color);
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

                const bool is_current_position =
                    channel == current_pattern_channel &&
                    row == current_pattern_row - 1;

                const bool is_empty_event =
                    note_value == NOTE_EMPTY &&
                    instrument_value == 0xFF;

                const Color note_color =
                    is_empty_event
                        ? empty_note
                        : is_current_position
                            ? current_position_note
                            : note_color_default;

                const Color instrument_color =
                    is_empty_event
                        ? empty_instrument
                        : is_current_position
                            ? current_position_instrument
                            : instrument_number;

                DrawFixedText(
                    framebuffer,
                    field_x,
                    y,
                    FormatNote(
                        note_value,
                        AccidentalMode::Sharp),
                    note_color);

                if (note_value == NOTE_OFF)
                {
                    DrawFixedText(
                        framebuffer,
                        field_x + 4 * 6,
                        y,
                        "¯",
                        instrument_color);
                }
                else
                {
                    DrawFixedText(
                        framebuffer,
                        field_x + 4 * 6,
                        y,
                        InstrumentToString(
                            instrument_value),
                        instrument_color);
                }
            }
        }
    }

    void DrawCurrentPatternRow(
    Framebuffer& framebuffer)
    {
        const int row_y =
            layout.first_row_y +
            (current_pattern_row - 1) *
                layout.row_height;

        framebuffer.FilledRectangle(
            layout.pattern_x + 2,
            row_y,
            layout.pattern_width - 4,
            layout.row_height,
            current_row_background);

        DrawFixedText(
            framebuffer,
            layout.row_number_width - 6,
            row_y + 2,
            "¦",
            row_marker);
    }

    void DrawCurrentPosition(
        Framebuffer& framebuffer)
    {
        const int row_y =
            layout.first_row_y +
            (current_pattern_row - 1) *
                layout.row_height;

        const int channel_x =
            layout.channel_start_x +
            current_pattern_channel *
                layout.channel_width;

        framebuffer.FilledRectangle(
            channel_x + 2,
            row_y,
            layout.channel_width - 4,
            layout.row_height,
            current_position_background);
    }

    void DrawCurrentEventEdit(
        Framebuffer& framebuffer)
    {
        constexpr int NOTE_WIDTH = 3 * 6;

        const int row_y =
            layout.first_row_y +
            (current_pattern_row - 1) *
                layout.row_height;

        const int channel_x =
            layout.channel_start_x +
            current_pattern_channel *
                layout.channel_width;

        const int field_x =
            channel_x +
            (layout.channel_width -
                6 * 6) / 2;

        const int text_y =
            row_y + 2;

        const int padding =
            current_event_edit_padding + 1;

        const int thickness =
            current_event_edit_border_thickness;

        // The edit frame matches the full current-position
        // background height, while extending two pixels beyond
        // the note glyph on both left and right sides.
        const int horizontal_padding =
            padding * 2;

        const int x =
            field_x - horizontal_padding;

        const int y =
            row_y - 1;

        const int width =
            NOTE_WIDTH +
            horizontal_padding * 2 - 1;

        const int height =
            layout.row_height + 2;

        for (int i = 0;
            i < thickness;
            ++i)
        {
            framebuffer.Rectangle(
                x - i,
                y - i,
                width + i * 2,
                height + i * 2,
                current_event_edit_border);
        }
    }

    void DrawPattern(
        Framebuffer& framebuffer,
        const Pattern& pattern)
    {
        DrawPatternFrame(framebuffer);

        DrawCurrentPatternRow(
            framebuffer);

        DrawCurrentPosition(
            framebuffer);

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

        constexpr int WORKSPACE_Y =
            layout.header_height +
            layout.frame_gap;

        constexpr int NAVIGATION_Y =
            SCREEN_HEIGHT -
            layout.right_navigation_height;

        constexpr int WORKSPACE_HEIGHT =
            NAVIGATION_Y -
            WORKSPACE_Y -
            layout.frame_gap;

        framebuffer.Rectangle(
            RIGHT_SECTION_X,
            WORKSPACE_Y,
            RIGHT_SECTION_WIDTH,
            WORKSPACE_HEIGHT,
            border);
    }

    void DrawRightChannelNavigation(
        Framebuffer& framebuffer)
    {
        constexpr int RIGHT_SECTION_X = 457;
        constexpr int RIGHT_SECTION_WIDTH =
            SCREEN_WIDTH - RIGHT_SECTION_X;

        constexpr int NAVIGATION_Y =
            SCREEN_HEIGHT -
            layout.right_navigation_height;

        framebuffer.Rectangle(
            RIGHT_SECTION_X,
            NAVIGATION_Y,
            RIGHT_SECTION_WIDTH,
            layout.right_navigation_height,
            border);

        DrawFixedText(
            framebuffer,
            RIGHT_SECTION_X + 8,
            NAVIGATION_Y + 9,
            "CH 1-8",
            row_header_bright);

        DrawFixedText(
            framebuffer,
            RIGHT_SECTION_X + 64,
            NAVIGATION_Y + 9,
            "<",
            row_header_bright);

        DrawFixedText(
            framebuffer,
            RIGHT_SECTION_X + 80,
            NAVIGATION_Y + 9,
            "01",
            header_value);

        DrawFixedText(
            framebuffer,
            RIGHT_SECTION_X + 98,
            NAVIGATION_Y + 9,
            "/",
            header_name);

        DrawFixedText(
            framebuffer,
            RIGHT_SECTION_X + 110,
            NAVIGATION_Y + 9,
            "08",
            header_name);

        DrawFixedText(
            framebuffer,
            RIGHT_SECTION_X + 132,
            NAVIGATION_Y + 9,
            ">",
            row_header_bright);
    }
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

    DrawCurrentEventEdit(
        framebuffer);

    DrawRightMenu(framebuffer);
    DrawRightWorkspace(framebuffer);
    DrawRightChannelNavigation(framebuffer);
}

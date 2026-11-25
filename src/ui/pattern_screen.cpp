/*
 * BroTracker
 *
 * Description: Main pattern screen UI renderer.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include "pattern_screen.h"
#include "bringup_controls.h"
#include "core/tempo.h"

#include <algorithm>
#include <string>
#include <iomanip>
#include <locale>
#include <sstream>

#include "core/constants.h"
#include "core/note.h"
#include "core/note_formatter.h"
#include "renderer/color.h"
#include "text_renderer.h"
#include "instrument_formatter.h"
#include "sample_waveform.h"

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
    constexpr Color editing_frame               { 255, 160, 64 };
    constexpr Color row_header_normal           { 38,  120, 124 };
    constexpr Color row_header_bright           { 76,  195, 201 };
    constexpr Color row_number_normal           { 78,  78,  118 };
    constexpr Color row_number_bright           { 130, 130, 196 };
    constexpr Color row_marker                  { 192, 192, 192 };
    constexpr Color channel_number              { 130, 130, 196 };
    constexpr Color note_normal                 { 208, 208, 208 };
    constexpr Color note_bright                 { 255, 255, 255 };
    constexpr Color instrument_normal           { 130, 130, 196 };
    constexpr Color instrument_bright           { 174, 174, 232 };
    constexpr Color empty_note_normal           { 104, 104, 104 };
    constexpr Color empty_note_bright           { 152, 152, 152 };
    constexpr Color empty_instrument_normal     { 66,  66,  100 };
    constexpr Color empty_instrument_bright     { 104, 104, 156 };
    constexpr Color current_position_note       { 16,  16,  16  };
    constexpr Color current_position_instrument { 78,  78,  118 };
    constexpr Color current_event_edit_border   { 134, 32,  32  };

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
        int visible_rows = PatternGrid::visible_rows;

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
        int right_section_x = 457;
        int content_padding = 8;
    };

    constexpr Layout layout{};

    void DrawMainHeader(
        Framebuffer& framebuffer,
        const Tune& tune,
        const Pattern& pattern,
        const PatternDisplayState& display, const std::optional<LivePlaybackView>& live)
    {
        std::ostringstream tempo_stream;

        const auto hundredths=live && live->tempo_hundredths?live->tempo_hundredths:
            display.device_pattern?display.device_pattern->tempo_hundredths:tune.tempo_hundredths;
        const auto tenth=BroTracker::TempoTenths(hundredths);
        tempo_stream << tenth/10 << '.' << tenth%10;

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
            display.device_mode ? (display.device_pattern ? "TEENSY PATTERN" : "TEENSY (DATA UNKNOWN)") : tune.title,
            header_value);

        const std::string header_labels[] =
        {
            "BPM:",
            "PAT:",
            "POS:",
            display.device_mode ? "CBpk:" : "CPU:"
        };

        const std::string header_values[] =
        {
            display.device_mode && !display.device_pattern ? "--" : tempo_stream.str(),
            
            display.device_mode ? "--" : pattern.number < 10
                ? "0" + std::to_string(pattern.number)
                : std::to_string(pattern.number),

            display.device_mode ? (display.playback_highlight ?
                (display.row < 9 ? "0" : "") + std::to_string(display.row + 1) : "--") : "16",

            display.device_mode ? (live && live->callback_performance && live->callback_performance->steady_callbacks && live->callback_performance->budget_cycles ?
                std::to_string((std::uint64_t(live->callback_performance->maximum_cycles)*100 +
                    live->callback_performance->budget_cycles/2)/live->callback_performance->budget_cycles) + "%" : "--") : "03%"
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
                index == 0 && live && live->tempo_pending ? editing_frame : header_value);
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
        Framebuffer& framebuffer, BroTracker::RowNumbering numbering, int channels = layout.channel_count)
    {
        DrawCenteredFixedText(
            framebuffer,
            0,
            layout.row_number_width,
            38,
            BroTracker::RowNumberingName(numbering),
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

            if(channel < channels) DrawDoubleFixedGlyph(framebuffer,
                cell_x+PatternGrid::header_glyph_x,PatternGrid::header_glyph_y,
                char('1'+channel),channel_number);
            else DrawCenteredFixedText(framebuffer,cell_x,layout.channel_width,38,"--",channel_number);
        }
    }

    void DrawRowNumbers(
        Framebuffer& framebuffer, BroTracker::RowNumbering numbering, int rows = layout.visible_rows)
    {
        for (int row = 0;
            row < rows;
            ++row)
        {
            const int y =
                layout.first_row_y +
                row * layout.row_height +
                2;

            const auto row_text=BroTracker::FormatRowNumber(row,numbering);

            const bool is_highlighted_position =
                PatternGrid::IsBeatRow(static_cast<unsigned>(row));

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
        const Pattern& pattern,
        const PatternDisplayState& display, const LocalPatternEditor* editor)
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

                if (display.device_mode)
                {
                    // One pass: unavailable/out-of-active cells keep empty defaults.
                    if (display.device_pattern && row < static_cast<int>(display.rows) &&
                        channel < static_cast<int>(display.channels))
                    {
                        const auto* draft = editor && editor->Ready() ? editor->Draft() : nullptr;
                        const auto& cell = (draft ? *draft : display.device_pattern->pattern).cells[row][channel];
                        note_value = cell.note;
                        instrument_value = cell.instrument;
                    }
                }

                if (!display.device_mode && channel <
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
                    !display.device_mode && channel == current_pattern_channel &&
                    row == current_pattern_row - 1;

                const bool beat = PatternGrid::IsBeatRow(static_cast<unsigned>(row));
                // Base rhythm colors are per field; a populated neighbor must
                // not promote an empty placeholder. Inverse selection wins.
                const Color note_color = is_current_position ? current_position_note
                    : note_value == NOTE_EMPTY ? (beat ? empty_note_bright : empty_note_normal)
                    : (beat ? note_bright : note_normal);
                const Color instrument_color = is_current_position ? current_position_instrument
                    : instrument_value == 0xFF && note_value != NOTE_OFF ? (beat ? empty_instrument_bright : empty_instrument_normal)
                    : (beat ? instrument_bright : instrument_normal);

                DrawFixedText(
                    framebuffer,
                    field_x,
                    y,
                    FormatNote(
                        note_value,
                        AccidentalMode::Sharp),
                    note_color);

                if (note_value == NOTE_OFF && editor && editor->Ready())
                {
                    DrawFixedText(framebuffer, field_x + 4 * 6, y,
                        editor->OffTiming(row, channel) == LocalNoteOffTiming::EndOfPosition ? "_" : "¯",
                        instrument_color);
                }
                else if (note_value == NOTE_OFF && display.device_pattern)
                {
                    DrawFixedText(framebuffer, field_x + 4 * 6, y,
                        display.device_pattern->pattern.note_off_timing[row][channel] == BroTracker::NoteOffTiming::EndOfPosition ? "_" : "\xC2\xAF",
                        instrument_color);
                }
                else if (note_value == NOTE_OFF && (!display.device_mode || instrument_value == 0xFF))
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
                        FormatInstrument(
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
        const Pattern& pattern,
        const PatternDisplayState& display, const LocalPatternEditor* editor, unsigned mute_mask, BroTracker::RowNumbering numbering)
    {
        DrawPatternFrame(framebuffer);

        if (!display.device_mode)
        {
            DrawCurrentPatternRow(framebuffer);
            DrawCurrentPosition(framebuffer);
        }
        else if (display.playback_highlight)
        {
            framebuffer.FilledRectangle(layout.pattern_x + 2,
                layout.first_row_y + static_cast<int>(display.row) * layout.row_height,
                layout.row_number_width + static_cast<int>(display.channels) * layout.channel_width - 2,
                layout.row_height, current_row_background);
            // U+00A6 is the font's custom arrow (BFM entry 166), not a bar.
            DrawFixedText(framebuffer, layout.row_number_width - 6,
                layout.first_row_y + static_cast<int>(display.row) * layout.row_height + 2,
                "\xC2\xA6", row_marker);
        }

        DrawRowHeader(framebuffer,numbering);
        DrawRowNumbers(framebuffer,numbering);
        DrawPatternChannels(
            framebuffer,
            pattern, display, editor);
        for(unsigned channel=0;channel<display.channels;++channel) if(mute_mask & (1u<<channel)) {
            const int left=layout.channel_start_x+channel*layout.channel_width;
            // Square diagonals: equal X/Y steps, centered on the digit bitmap.
            constexpr int half_length=PatternGrid::header_cross_half, center_y=PatternGrid::header_cross_y;
            const int center_x=left+PatternGrid::header_cross_x;
            for(int step=-half_length;step<=half_length;++step) {
                for(int direction:{-1,1}) {
                    const int x=center_x+step,y=center_y+direction*step;
                    const bool glyph=DoubleFixedGlyphCovers(char('1'+channel),x-(left+PatternGrid::header_glyph_x),y-PatternGrid::header_glyph_y);
                    framebuffer.SetPixel(x,y,glyph?background:row_header_bright);
                }
            }
        }
        if (display.device_pattern && editor && editor->Ready())
        {
            const auto& cursor = editor->Cursor();
            if(cursor.header) {
                framebuffer.Rectangle(layout.channel_start_x+cursor.channel*layout.channel_width+2,31,
                    layout.channel_width-4,22,editing_frame);
                return;
            }
            const int x = layout.channel_start_x + static_cast<int>(cursor.channel) * layout.channel_width +
                (layout.channel_width - 36) / 2 + (cursor.field == EditField::Instrument ? 24 : 0);
            framebuffer.Rectangle(x - 2, layout.first_row_y + static_cast<int>(cursor.row) * layout.row_height,
                cursor.field == EditField::Note ? 22 : 16, layout.row_height, editing_frame);
        }
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
        constexpr int RIGHT_SECTION_X = layout.right_section_x;
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
        constexpr int RIGHT_SECTION_X = layout.right_section_x;
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
        Framebuffer& framebuffer, const PatternDisplayState& display)
    {
        constexpr int RIGHT_SECTION_X = layout.right_section_x;
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
            display.device_mode ? (display.channels ? "CH 1-" + std::to_string(display.channels) : "CH --") : "CH 1-8",
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
            display.device_mode ? "--" : "01",
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
            display.device_mode ? (display.channels ? "0" + std::to_string(display.channels) : "--") : "08",
            header_name);

        DrawFixedText(
            framebuffer,
            RIGHT_SECTION_X + 132,
            NAVIGATION_Y + 9,
            ">",
            row_header_bright);
    }
}

static void DrawSampleWaveform(Framebuffer& fb, int top, int bottom,
    const BroTracker::SampleWaveformCache* cache, bool deferred, const HostBitmap::Face& face,
    const HostBitmap::Face& small, Color dim) {
    using State = BroTracker::SampleWaveformCache::State;
    const int width=fb.Width(), inset=layout.content_padding;
    const int left=inset, right=width-inset-1, middle=(left+right+1)/2;
    const int plot_top=top+30, plot_bottom=bottom-57, center=(plot_top+plot_bottom)/2;
    const int axis=plot_bottom+10;
    constexpr Color plot_background{20,44,45};
    const auto text=[&](int x,int y,const std::string& value,Color ink) {
        HostBitmap::Text(fb,small,x,y+small.ascent,value,ink);
    };
    const auto measure=[&](const std::string& value){return HostBitmap::Measure(small,value).width;};
    const bool show_progress=cache && cache->Metadata().frames &&
        cache->Status()==State::Loading && !cache->StorageLimited();
    HostBitmap::Text(fb,face,inset,top+8+face.ascent,"Edit Sample",header_name);
    fb.HorizontalLine(left,right,top+26,border);
    fb.FilledRectangle(left,plot_top,right-left+1,plot_bottom-plot_top+1,plot_background);
    fb.HorizontalLine(left,right,center,row_header_normal);
    fb.VerticalLine(left,plot_top,plot_bottom,editing_frame);
    fb.VerticalLine(right,plot_top,plot_bottom,editing_frame);
    fb.HorizontalLine(left,right,axis,row_marker);
    for(const int x:{left,middle,right})fb.VerticalLine(x,axis-3,axis+3,row_marker);

    if(cache && cache->BytesLoaded()) {
        const auto& wav=cache->Metadata();
        if(cache->Status()!=State::Ready) {
            const std::string status=cache->StorageLimited()?"Approx. FIT (cache limit)":
                cache->OverviewReady()?"Approx. FIT":"Loading FIT";
            text(inset+112,top+9,status,dim);
        }
        std::ostringstream summary;
        summary.imbue(std::locale::classic());
        summary << wav.rate/1000.0 << " kHz | " << wav.bits << "-bit | "
            << (wav.channels==1?"Mono":"Stereo") << " | "
            << std::fixed << std::setprecision(2) << double(wav.frames)/wav.rate << " s";
        const auto format=summary.str();
        text(width-inset-measure(format),top+9,format,dim);
        const auto to_y=[&](int value) {
            return value>=0 ? center-value*(center-plot_top)/32767
                            : center-value*(plot_bottom-center)/32768;
        };
        const auto plot_columns=std::size_t(right-left-1);
        for(int x=left+1;x<right;++x) {
            const auto column=std::size_t(x-left-1)*cache->Columns()/plot_columns;
            const auto& peak=cache->Column(column);
            if(peak.frames)
                fb.VerticalLine(x,to_y(peak.maximum),to_y(peak.minimum),
                    cache->ColumnExact(column)?row_header_bright:dim);
        }
        const auto midpoint=std::to_string(wav.frames/2);
        const auto end=std::to_string(wav.frames)+" frames";
        text(left,axis+6,"0",header_value);
        text(middle-measure(midpoint)/2,axis+6,midpoint,header_value);
        text(right+1-measure(end),axis+6,end,header_value);
    } else if(!show_progress) {
        const std::string status=cache && cache->Status()==State::Unavailable
            ?"Waveform unavailable":cache && cache->StorageLimited()
            ?"Waveform cache limit":deferred
            ?"Playback has priority. Waveform loading resumes on Stop or Pause."
            :"Loading waveform "+std::to_string(cache?cache->Percent():0)+"%";
        fb.FilledRectangle(middle-measure(status)/2-5,center-8,measure(status)+10,16,plot_background);
        text(middle-measure(status)/2,center-6,status,header_value);
    }

    if(show_progress) {
        const std::string status=(deferred?"Playback priority: ":"Loading waveform: ")+
            std::to_string(cache->Percent())+"%";
        const int text_width=measure(status);
        const int x=right-inset-text_width;
        const int y=plot_bottom-6-small.ascent-small.descent;
        fb.FilledRectangle(x-2,y-1,text_width+4,small.ascent+small.descent+2,plot_background);
        text(x,y,status,editing_frame);
    }

    const char* controls[]={"Preview","Zoom 1x","Fit","Set Start","Set End"};
    constexpr int control_inset=20;
    for(int i=0;i<5;++i) {
        const int x=control_inset+(i+1)*(width-2*control_inset-1)/6;
        text(x-measure(controls[i])/2,bottom-19,controls[i],header_value);
    }
}

static void DrawInstrumentScreen(Framebuffer& fb,const HostPanelInformation& host,
    const std::optional<LivePlaybackView>& live) {
    static const auto face=HostBitmap::Bdf("assets/fonts/terminus/ter-u14n.bdf");
    static const auto bold=HostBitmap::Bdf("assets/fonts/terminus/ter-u14b.bdf");
    static const auto small=HostBitmap::Bdf("assets/fonts/terminus/ter-u12n.bdf");
    constexpr Color dim{143,143,159};
    constexpr int left=layout.pattern_x, inset=layout.content_padding;
    const int width=fb.Width(), height=fb.Height(), panel_width=width-2*left;
    constexpr int header_y=9, row_pitch=18, type_y=header_y+row_pitch+row_pitch/2, name_y=type_y+row_pitch, line_height=14;
    constexpr int common_height=122+line_height;
    constexpr int sample_top=common_height+layout.frame_gap;
    const int footer_top=height-25;
    const int previous_waveform_top=2*height/3+line_height;
    const int waveform_top=sample_top+(footer_top-previous_waveform_top)+layout.frame_gap;
    const int sample_heading=sample_top+12, separator=sample_top+34;
    fb.Rectangle(left,0,panel_width,common_height,border);
    const auto text=[&](int x,int y,const std::string& value,Color color) {
        HostBitmap::Text(fb,face,x,y+face.ascent,value,color);
    };
    const auto measure=[&](const std::string& value){return HostBitmap::Measure(face,value).width;};
    const auto focus=[&](int field,int x,int y,int w,int h=24) {
        if(host.instrument_focus==field)fb.Rectangle(x,y,w,h,editing_frame);
    };
    if(live && live->tempo_hundredths) {
        const auto tenth=BroTracker::TempoTenths(live->tempo_hundredths);
        const auto tempo="BPM "+std::to_string(tenth/10)+"."+std::to_string(tenth%10);
        text((width-measure(tempo))/2,header_y,tempo,header_value);
    }
    text(width-inset-measure(host.instrument_transport),header_y,host.instrument_transport,row_header_bright);

    const auto instrument_label=host.text.Get(WarningText::Instrument);
    HostBitmap::Text(fb,bold,inset,header_y+face.ascent,instrument_label,row_header_bright);
    constexpr int triangle_reach=6;
    const int space=measure(" ");
    const int slot_x=inset+HostBitmap::Measure(bold,instrument_label).width+space+triangle_reach;
    const auto slot=FormatInstrument(static_cast<std::uint8_t>(host.instrument_number-1));
    const int slot_end=slot_x+measure(slot);
    constexpr int slot_focus_padding=3;
    focus(2,slot_x-slot_focus_padding,header_y-3,slot_end-slot_x+2*slot_focus_padding,row_pitch);
    // Preserve a full blank space outside each triangle.
    for(int offset=0;offset<3;++offset) {
        fb.VerticalLine(slot_x-triangle_reach+offset,header_y+6-offset,header_y+6+offset,dim);
        fb.VerticalLine(slot_end+triangle_reach-offset,header_y+6-offset,header_y+6+offset,dim);
    }
    text(slot_x,header_y,slot,header_value);
    if(host.instrument_modified || (host.instrument_original_number && host.instrument_original_number!=host.instrument_number)) {
        std::string marker="*";
        if(host.instrument_original_number && host.instrument_original_number!=host.instrument_number)
            marker+=" ["+FormatInstrument(static_cast<std::uint8_t>(host.instrument_original_number-1))+"]";
        text(slot_end+triangle_reach+1+space,header_y,marker,row_header_bright);
    }
    const bool sample=host.instrument_identity=="Sample";
    const auto identity=sample?host.text.Get(WarningText::Sample):
        host.instrument_identity=="MIDI"?host.text.Get(WarningText::Midi):
        host.instrument_identity=="Sine Synth"?host.text.Get(WarningText::SineSynth):host.instrument_identity;
    const auto type_label=host.text.Get(WarningText::Type);
    text(inset,type_y,type_label,row_header_bright);
    const int type_x=inset+measure(type_label)+16;
    if(host.instrument_focus==0) {
        const auto bounds=HostBitmap::Measure(face,identity);
        constexpr int x_padding=4,y_padding=2;
        fb.Rectangle(type_x-x_padding,type_y+face.ascent+bounds.top-y_padding,
            bounds.width+2*x_padding,bounds.bottom-bounds.top+2*y_padding,editing_frame);
    }
    text(type_x,type_y,identity,header_value);
    const auto edit_arrows=[&](int x,int y,int value_width,bool left_visible,bool right_visible) {
        for(int offset=0;offset<3;++offset) {
            if(left_visible)fb.VerticalLine(x-8+offset,y+6-offset,y+6+offset,dim);
            if(right_visible)fb.VerticalLine(x+value_width+7-offset,y+6-offset,y+6+offset,dim);
        }
    };
    edit_arrows(type_x,type_y,measure(identity),true,true);
    const auto save=host.text.Get(WarningText::Save)+": "+host.text.Get(WarningText::Unavailable);
    text(width-inset-measure(save),type_y,save,dim);

    text(inset,name_y,"Name",row_header_bright);
    if(sample) {
        auto filename=host.instrument_name;
        const auto columns=std::size_t((width-type_x-inset)/face.cell);
        if(filename.size()>columns)filename=filename.substr(0,columns-3)+"...";
        text(type_x,name_y,filename,header_value);
        const auto tuning=BroTracker::ResolveSampleTuning(host.instrument_tuning);
        const int value_x=inset+std::max(measure("Root Note"),measure(host.text.Get(WarningText::FineTune)))+16;
        const auto draw_tuning=[&](int field,int y,const std::string& label,const std::string& value) {
            text(inset,y,label,row_header_bright);
            const int w=measure(value);
            if(host.instrument_tuning_editable) {
                focus(field,value_x-4,y-2,w+8,row_pitch);
                edit_arrows(value_x,y,w,true,true);
            }
            text(value_x,y,value,host.instrument_tuning.Valid()?header_value:dim);
        };
        char cents[3]{char('0'+tuning.cents/10),char('0'+tuning.cents%10),0};
        draw_tuning(BringUpControls::RootNoteField,name_y+row_pitch,"Root Note",FormatNote(Note(tuning.root),AccidentalMode::Sharp));
        draw_tuning(BringUpControls::FineTuneField,name_y+2*row_pitch,host.text.Get(WarningText::FineTune),cents);
    } else if(host.instrument_identity=="MIDI") {
        const int channel_y=name_y+row_pitch;
        text(inset,channel_y,"Channel",row_header_bright);
        const auto value=host.instrument_midi_channel?std::to_string(host.instrument_midi_channel):std::string{};
        constexpr int channel_x=80;
        if(host.instrument_focus==3)focus(3,channel_x-4,channel_y-2,24,row_pitch);
        text(channel_x,channel_y,value,header_value);
        if(host.instrument_midi_channel)edit_arrows(channel_x,channel_y,2*face.cell,
            host.instrument_midi_channel>1,host.instrument_midi_channel<16);
    }

    fb.Rectangle(left,sample_top,panel_width,waveform_top-layout.frame_gap-sample_top,border);
    const std::string section=sample?"Sample WAV":"Availability";
    text(inset,sample_heading,section,header_name);
    HostBitmap::Text(fb,small,inset+measure(section)+24,sample_heading+1+small.ascent,host.instrument_availability,header_value);
    const auto browse=host.text.Get(WarningText::Load)+" WAV";
    const int browse_x=width-inset-measure(browse);
    if(sample) {
        focus(1,browse_x-6,sample_heading-5,measure(browse)+12);
        text(browse_x,sample_heading,browse,host.instrument_load_available?header_value:dim);
    }
    fb.HorizontalLine(inset,width-inset-1,separator,border);
    // Share the shorter frame among the active details, reserving a line for
    // each following item so lengthy paths cannot hide warnings or status.
    std::vector<std::pair<std::string,Color>> details;
    if(sample) {
        details.emplace_back("Path: "+host.instrument_resource,header_value);
        if(!host.instrument_candidate.empty())details.emplace_back("Staged: "+host.instrument_candidate,editing_frame);
    }
    if(!host.instrument_status.empty())details.emplace_back(host.instrument_status,editing_frame);
    if(!host.instrument_load_warning.empty())details.emplace_back(host.instrument_load_warning,editing_frame);
    if(host.instrument_transport.empty() && !host.connection_status.empty())details.emplace_back(host.connection_status,dim);
    text(inset,separator+5,"Description:",header_name);
    constexpr int detail_pitch=13;
    int y=separator+19;
    for(std::size_t i=0;i<details.size();++i) {
        const auto& [value,color]=details[i];
        const int bottom=waveform_top-layout.frame_gap-2-int(details.size()-i-1)*detail_pitch;
        const auto columns=std::size_t((width-2*inset)/small.cell);
        for(std::size_t start=0; start<value.size() && y+detail_pitch<=bottom; y+=detail_pitch) {
            auto end=std::min(start+columns,value.size());
            if(end<value.size() && y+2*detail_pitch>bottom) {
                HostBitmap::Text(fb,small,inset,y+small.ascent,value.substr(start,columns-3)+"...",color);
                y+=detail_pitch;
                break;
            }
            if(end<value.size()) {
                const auto space=value.rfind(' ',end);
                if(space!=std::string::npos && space>=start+columns/2)end=space+1;
            }
            HostBitmap::Text(fb,small,inset,y+small.ascent,value.substr(start,end-start),color);
            start=end;
        }
    }

    fb.Rectangle(left,waveform_top,panel_width,footer_top-waveform_top,border);
    if(sample)DrawSampleWaveform(fb,waveform_top,footer_top,host.sample_waveform,host.waveform_deferred,face,small,dim);
    const int help_baseline=sample
        ? height-2-HostBitmap::Measure(small,host.transport_hints).bottom
        : footer_top+6+small.ascent;
    HostBitmap::Text(fb,small,inset,help_baseline,host.transport_hints,header_value);
}

static void RenderPatternScreen(
    Framebuffer& framebuffer,
    const Tune& tune,
    const Pattern& pattern,
    const std::optional<LivePlaybackView>& live, const LocalPatternEditor* editor,
    const HostPanelInformation& host)
{
    framebuffer.Clear(background);
    if(host.sample_picker) {
        DrawSampleBrowser(framebuffer,host.pattern_browser_list,host.browser_directory,host.sample_browser_confirmation?std::string{}:host.pattern_browser_text);
        if(host.sample_browser_confirmation)DrawWrappedFixedText(framebuffer,8,kSampleBrowserLineHeight+8,framebuffer.Width()-16,framebuffer.Height()-2*kSampleBrowserLineHeight-16,host.pattern_browser_text,header_name);
        return;
    }

    if(host.instrument_number) {DrawInstrumentScreen(framebuffer,host,live);return;}

    const auto display = ResolvePatternDisplay(live, !live);
    // Never show a draft against unrelated or unavailable device dimensions.
    if (editor && (!display.device_pattern || !editor->Ready() ||
        editor->Draft()->active_rows != display.rows ||
        editor->Draft()->active_channels != display.channels)) editor = nullptr;

    DrawMainHeader(
        framebuffer,
        tune,
        pattern, display, live);

    if(host.load_save_screen) {
        static const auto heading=HostBitmap::Bdf("assets/fonts/terminus/ter-u14n.bdf");
        static const auto filename=HostBitmap::Bdf("assets/fonts/terminus/ter-u12n.bdf");
        DrawFileList(framebuffer,host.pattern_browser_list,heading,filename,"Pattern Load/Save");
        DrawWrappedFixedText(framebuffer,layout.pattern_x+8,layout.pattern_y+164,
            layout.pattern_width-16,static_cast<int>(::SCREEN_HEIGHT)-layout.pattern_y-172,host.pattern_browser_text,header_name);
    } else DrawPattern(
        framebuffer,
        pattern, display, editor, live && live->mute_mask?*live->mute_mask:0,host.row_numbering);

    if (!display.device_mode) DrawCurrentEventEdit(framebuffer);

    DrawRightMenu(framebuffer);
    DrawRightWorkspace(framebuffer);
    if (display.device_mode || !host.connection_status.empty() || !host.transport_hints.empty() || !host.publication_status.empty())
    {
        // Derive the padded content interior from the same workspace bounds.
        const int x = layout.right_section_x + 1 + layout.content_padding;
        int y = layout.header_height + layout.frame_gap + 1 + layout.content_padding;
        const int width = static_cast<int>(::SCREEN_WIDTH) - layout.right_section_x - 2 - 2 * layout.content_padding;
        const int bottom = static_cast<int>(::SCREEN_HEIGHT) - layout.right_navigation_height - layout.frame_gap -
            1 - layout.content_padding;
        const auto info = [&](const std::string& text, Color color) {
            y += DrawWrappedFixedText(framebuffer, x, y, width, bottom - y, text, color) + 8;
        };
        // Connection/errors and pending transport stay visible even before HELLO.
        if(!host.volume_status.empty())info(host.volume_status,header_value);
        if (!host.connection_status.empty()) info(host.connection_status, header_value);
        if (!host.midi_route_status.empty()) info(host.midi_route_status, header_value);
        if (!host.midi_usb_status.empty()) info(host.midi_usb_status, header_value);
        if (!host.midi_status.empty()) info(host.midi_status, header_value);
        if (!host.transport_hints.empty()) info(host.transport_hints, header_value);
        if (!host.publication_status.empty()) info(host.publication_status, header_value);
        if (!host.diagnostic_status.empty()) info(host.diagnostic_status, header_value);
        if(live && !live->mute_status.empty()) info(live->mute_status,header_value);
        if (display.device_mode)
        {
            if(live->callback_performance && live->callback_performance->steady_callbacks && live->callback_performance->budget_cycles) {
                const auto& perf=*live->callback_performance;
                const auto hundredths=std::uint64_t(perf.maximum_cycles)*10000/perf.budget_cycles;
                info("Callback peak/budget " + std::to_string(hundredths/100) + "." +
                    (hundredths%100<10 ? "0" : "") + std::to_string(hundredths%100) +
                    "% (steady, cumulative)",header_value);
            } else info("Callback peak/budget unavailable",header_name);
            // Shared read-only presentation: zero is a gain, never a tracker mute.
            if (!live->gains_supported) info("Channel gains unavailable", header_name);
            else {
                info(live->gain_status.empty() ? "Channel gains awaiting state" : live->gain_status, header_name);
                const auto values = [&](const BroTracker::Pcm16ChannelGains& gains) {
                    std::string text;
                    for (unsigned c=0;c<8;++c) {
                        if(c) text += " ";
                        text += std::to_string(c+1) + ":" + std::to_string(gains.channels[c]);
                    }
                    return text;
                };
                if(live->channel_gains) info("Applied Q16 ID " + std::to_string(live->channel_gains->applied_id) +
                    " " + values(live->channel_gains->gains), header_value);
                else info("Applied gains unavailable", header_value);
                if(live->desired_gains) info("Desired Q16 " + values(*live->desired_gains), header_value);
            }
            info(!live->telemetry_available ? "Position telemetry unavailable (legacy / waiting)" :
                !live->fresh ? "Position unavailable / stale; no extrapolation" :
                live->position.paused ? "Device PAUSED (reported logical position)" :
                live->position.running ? "Device playing (reported logical position)" : "Device stopped",
                header_value);
            info(display.device_pattern ? (editor && editor->Ready() ?
                    (live->preview_supported ?
                        (editor->Dirty() ? "WORKING DRAFT * - UNPUBLISHED" : "WORKING DRAFT - PUBLISHED BASELINE") :
                        (editor->Dirty() ? "LOCAL DRAFT * - NOT SENT TO TEENSY" : "LOCAL DRAFT - DEVICE BASELINE")) :
                    "Device pattern (read-only)") :
                live->pattern_loading ? "Loading device pattern..." : "Device pattern unavailable", header_name);
            if (live->telemetry_available)
                info("ROWS " + std::to_string(display.rows) + " CH " + std::to_string(display.channels) +
                    " LOOP " + (live->fresh && live->position.valid ? std::to_string(live->position.loop) : "--"),
                    header_value);
        }
    }
    DrawRightChannelNavigation(framebuffer, display);
}

void RenderMainScreen(Framebuffer& framebuffer, const Tune& tune, const Pattern& pattern,
    const std::optional<LivePlaybackView>& live, const LocalPatternEditor* editor,
    const HostPanelInformation& host) {
    // Missing connection is unavailable application data, never a preview request.
    const std::optional<LivePlaybackView> unavailable=LivePlaybackView{};
    RenderPatternScreen(framebuffer,tune,pattern,live ? live : unavailable,editor,host);
}
void RenderPreviewScreen(Framebuffer& framebuffer, const Tune& tune, const Pattern& pattern) {
    RenderPatternScreen(framebuffer,tune,pattern,std::nullopt,nullptr,{});
}

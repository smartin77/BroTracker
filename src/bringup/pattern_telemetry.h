#pragma once

#include "core/playback/row_events.h"
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <limits>

namespace BroTracker
{
    // Temporary telemetry, independent of BTTEST1 acknowledgement handling.
    // Exact ASCII grammar (single spaces, no trailing fields):
    // BTPATTERN1 POS 1 running valid tick row loop rows channels\n
    // Unsigned decimal fields; booleans 0/1; zero-based row/loop. Invalid
    // positions have tick=row=loop=0. Valid positions require running and must
    // agree with tick/96 and the reported active dimensions. No clock policy.
    struct PatternTelemetry
    {
        bool running = false, valid = false;
        std::uint64_t tick = 0, row = 0, loop = 0;
        std::uint32_t rows = 0, channels = 0;
    };
    constexpr std::size_t kPatternTelemetryLineCapacity = 128;
    // Prefix/version/flags + three maximum uint64 decimals + dimensions + LF/NUL.
    static_assert(kPatternTelemetryLineCapacity >= 16 + 2 + 2 + 2 + 3 * 21 + 3 + 2 + 2,
        "Telemetry must fit worst-case uint64 values");

    inline bool ValidPatternTelemetry(const PatternTelemetry& value) noexcept
    {
        if (value.rows == 0 || value.rows > kRealtimePatternRowCapacity ||
            value.channels == 0 || value.channels > kRealtimePatternChannelCapacity) return false;
        if (!value.valid) return value.tick == 0 && value.row == 0 && value.loop == 0;
        const auto absolute_row = value.tick / kTicksPerRow;
        return value.running && value.row == absolute_row % value.rows &&
            value.loop == absolute_row / value.rows;
    }

    // Main-loop only. Validated fixed storage, including LF and terminating NUL.
    inline bool FormatPatternTelemetry(const PatternTelemetry& value,
        char (&output)[kPatternTelemetryLineCapacity], std::size_t& length) noexcept
    {
        if (!ValidPatternTelemetry(value)) return false;
        const int written = std::snprintf(output, sizeof(output),
            "BTPATTERN1 POS 1 %u %u %" PRIu64 " %" PRIu64 " %" PRIu64 " %u %u\n",
            unsigned(value.running), unsigned(value.valid), value.tick, value.row, value.loop,
            unsigned(value.rows), unsigned(value.channels));
        if (written < 0 || static_cast<std::size_t>(written) >= sizeof(output)) return false;
        length = static_cast<std::size_t>(written);
        return true;
    }

    // Parse a complete line WITHOUT LF/CR/NUL in its extent. On rejection leave
    // output unchanged. Bounded length and checked multiply/add prevent overflow;
    // no sscanf, signs, tabs, missing/extra fields or partial field acceptance.
    inline bool ParsePatternTelemetry(const char* line, std::size_t length,
        PatternTelemetry& output) noexcept
    {
        constexpr char prefix[] = "BTPATTERN1 POS 1 ";
        if (!line || length >= kPatternTelemetryLineCapacity || length < sizeof(prefix) ||
            std::memcmp(line, prefix, sizeof(prefix) - 1) != 0) return false;
        std::size_t cursor = sizeof(prefix) - 1;
        std::uint64_t fields[7]{};
        for (std::size_t field = 0; field < 7; ++field)
        {
            const auto start = cursor;
            while (cursor < length && line[cursor] >= '0' && line[cursor] <= '9')
            {
                const unsigned digit = unsigned(line[cursor++] - '0');
                if (fields[field] > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
                fields[field] = fields[field] * 10 + digit;
            }
            if (cursor == start) return false;
            if (field == 6) { if (cursor != length) return false; }
            else if (cursor == length || line[cursor++] != ' ') return false;
        }
        if (fields[0] > 1 || fields[1] > 1 || fields[5] > kRealtimePatternRowCapacity ||
            fields[6] > kRealtimePatternChannelCapacity) return false;
        const PatternTelemetry value{fields[0] != 0, fields[1] != 0, fields[2], fields[3], fields[4],
            static_cast<std::uint32_t>(fields[5]), static_cast<std::uint32_t>(fields[6])};
        if (!ValidPatternTelemetry(value)) return false;
        output = value;
        return true;
    }

    // Main-loop-owned coalescing slot, separate from command FIFO. An unsent
    // update is replaced; a partially transmitted line must finish intact. At
    // most that line plus ONE latest snapshot are retained under backpressure.
    // Caller gives acknowledgements priority unless a telemetry line is partial.
    // No I/O here, and formatting is never called by the audio owner.
    class PatternTelemetrySender
    {
    public:
        void Offer(const PatternTelemetry& value) noexcept
        { if (ValidPatternTelemetry(value)) { latest_ = value; pending_ = true; } }
        void Clear() noexcept { pending_ = false; length_ = offset_ = 0; }
        bool Partial() const noexcept { return offset_ != 0 && offset_ < length_; }
        const char* Data(std::size_t& count) noexcept
        {
            if (!Partial() && pending_)
            {
                (void)FormatPatternTelemetry(latest_, line_, length_);
                offset_ = 0;
                pending_ = false;
            }
            count = length_ - offset_;
            return line_ + offset_;
        }
        void Consume(std::size_t count) noexcept
        {
            if (count > length_ - offset_) return;
            offset_ += count;
            if (offset_ == length_) length_ = offset_ = 0;
        }
    private:
        PatternTelemetry latest_;
        bool pending_ = false;
        char line_[kPatternTelemetryLineCapacity]{};
        std::size_t length_ = 0, offset_ = 0;
    };
}

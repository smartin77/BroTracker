#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace BroTracker {
// Startup-only demultiplexer. Retain all non-clock bytes in original order,
// including incomplete lines at timeout. No allocation, truncation or replay.
class HostClockInput {
public:
    static constexpr std::size_t Capacity = 128;
    bool Full() const { return size_ == Capacity; }
    // Caller must check Full() BEFORE reading the next byte from USB CDC.
    bool Feed(char c, std::uint32_t& epoch) {
        if (Full()) return false;
        bytes_[size_++] = c;
        if (c != '\r' && c != '\n') return false;
        const std::size_t length = size_ - line_begin_ - 1;
        char line[48]{}; // Keep the existing clock-line limit/formats.
        if (length > 0 && length < sizeof(line)) {
            std::memcpy(line, bytes_ + line_begin_, length);
            if (TryParseUnixEpoch(line, epoch)) {
                size_ = line_begin_; // Remove only the accepted clock line.
                return true;
            }
        }
        line_begin_ = size_;
        return false;
    }
    int ReadDeferred() {
        return read_ < size_ ? static_cast<unsigned char>(bytes_[read_++]) : -1;
    }
private:
    char bytes_[Capacity]{};
    std::size_t size_ = 0, read_ = 0, line_begin_ = 0;
    static bool TryParseUnixEpoch(
        const char* text,
        std::uint32_t& epoch_out)
    {
        if (text == nullptr || text[0] == '\0')
            return false;

        const char* payload = text;

        if (payload[0] == 'E' && payload[1] == 'P' &&
            payload[2] == 'O' && payload[3] == 'C' &&
            payload[4] == 'H' && payload[5] == ':')
        {
            payload += 6;
        }
        else if (payload[0] == 'T')
        {
            ++payload;
        }

        char* end = nullptr;
        const unsigned long parsed = std::strtoul(payload, &end, 10);

        if (end == payload || *end != '\0' || parsed < 946684800ul)
            return false;

        epoch_out = static_cast<std::uint32_t>(parsed);
        return true;
    }
};
} // namespace BroTracker

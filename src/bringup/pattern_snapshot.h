#pragma once

#include "pattern_telemetry.h"

namespace BroTracker
{
    constexpr std::size_t kPatternSnapshotLineCapacity = 96;
    static_assert(kPatternSnapshotLineCapacity >= sizeof("BTPATTERN1 BEGIN 1 ") + 5 * 11,
        "Snapshot lines must fit all uint32 fields plus LF/NUL");
    constexpr std::uint32_t kPatternSnapshotTimeoutMs = 5000;
    constexpr char kPatternSnapshotCapability[] = "BTPATTERN1 SNAPCAP 1";

    struct DevicePatternSnapshot
    {
        std::uint32_t transfer_id = 0, tempo_hundredths = 0;
        RealtimePattern pattern;
    };
    enum class SnapshotMessageKind { Begin, Cell, End, Error };
    struct SnapshotMessage
    {
        SnapshotMessageKind kind = SnapshotMessageKind::Error;
        std::uint32_t fields[5]{};
    };

    // Exact single-space unsigned decimal fields, bounded line, checked uint32
    // conversion. No signs, trailing fields, embedded NUL/CR or partial output.
    inline bool SnapshotFields(const char* line, std::size_t length, const char* prefix,
        std::uint32_t* fields, std::size_t count) noexcept
    {
        const auto prefix_length = std::strlen(prefix);
        if (!line || length >= kPatternSnapshotLineCapacity || length <= prefix_length ||
            std::memcmp(line, prefix, prefix_length)) return false;
        std::uint32_t parsed[5]{};
        if (count > 5) return false;
        auto cursor = prefix_length;
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto start = cursor;
            while (cursor < length && line[cursor] >= '0' && line[cursor] <= '9')
            {
                const unsigned digit = unsigned(line[cursor++] - '0');
                if (parsed[i] > (UINT32_MAX - digit) / 10) return false;
                parsed[i] = parsed[i] * 10 + digit;
            }
            if (start == cursor) return false;
            if (i + 1 == count) { if (cursor != length) return false; }
            else if (cursor == length || line[cursor++] != ' ') return false;
        }
        if (!count || !parsed[0]) return false;
        for (std::size_t i = 0; i < count; ++i) fields[i] = parsed[i];
        return true;
    }
    inline bool ParseSnapshotRequest(const char* line, std::size_t length,
        std::uint32_t& id) noexcept
    {
        std::uint32_t value;
        if (!SnapshotFields(line, length, "BTPATTERN1 GET 1 ", &value, 1)) return false;
        id = value; return true;
    }
    inline bool ParseSnapshotMessage(const char* line, std::size_t length,
        SnapshotMessage& output) noexcept
    {
        struct Definition { const char* prefix; SnapshotMessageKind kind; std::size_t count; };
        constexpr Definition definitions[] = {
            {"BTPATTERN1 BEGIN 1 ", SnapshotMessageKind::Begin, 5},
            {"BTPATTERN1 CELL 1 ", SnapshotMessageKind::Cell, 4},
            {"BTPATTERN1 END 1 ", SnapshotMessageKind::End, 3},
            {"BTPATTERN1 SNAPERR 1 ", SnapshotMessageKind::Error, 1}};
        for (const auto& definition : definitions)
        {
            SnapshotMessage value; value.kind = definition.kind;
            if (SnapshotFields(line, length, definition.prefix, value.fields, definition.count))
            { output = value; return true; }
        }
        return false;
    }
    inline bool ValidSnapshotNote(std::uint32_t note) noexcept
    { return note <= 127 || note == NOTE_EMPTY || note == NOTE_OFF; }
    inline std::uint32_t SnapshotHashByte(std::uint32_t hash, std::uint8_t value) noexcept
    { return (hash ^ value) * 16777619u; }
    // FNV-1a32: rows byte, channels byte, tempo uint32 little-endian, then raw
    // note/instrument byte pairs in row-major order. Not authentication or BTM.
    inline std::uint32_t SnapshotHeaderHash(std::uint32_t rows, std::uint32_t channels,
        std::uint32_t tempo_hundredths) noexcept
    {
        auto hash = SnapshotHashByte(2166136261u, static_cast<std::uint8_t>(rows));
        hash = SnapshotHashByte(hash, static_cast<std::uint8_t>(channels));
        for (unsigned i = 0; i < 4; ++i)
            hash = SnapshotHashByte(hash, static_cast<std::uint8_t>(tempo_hundredths >> (8 * i)));
        return hash;
    }

    enum class SnapshotAssemblyStatus { Ignored, Receiving, Complete, Rejected, Timeout };
    // Host single-owner: fixed staging plus committed snapshot. Failure/cancel
    // never publishes staging. Clear drops connection-scoped committed data.
    class PatternSnapshotAssembler
    {
    public:
        bool Request(std::uint32_t id, std::uint32_t now) noexcept
        {
            if (!id) return false;
            Cancel(); id_ = id; started_ = now; active_ = true; return true;
        }
        void Cancel() noexcept { active_ = begun_ = false; next_cell_ = 0; }
        void Clear() noexcept { Cancel(); available_ = false; committed_ = {}; }
        bool Active() const noexcept { return active_; }
        const DevicePatternSnapshot* Snapshot() const noexcept { return available_ ? &committed_ : nullptr; }
        SnapshotAssemblyStatus Tick(std::uint32_t now) noexcept
        {
            if (active_ && now - started_ >= kPatternSnapshotTimeoutMs)
            { Cancel(); return SnapshotAssemblyStatus::Timeout; }
            return SnapshotAssemblyStatus::Ignored;
        }
        SnapshotAssemblyStatus Accept(const char* line, std::size_t length, std::uint32_t now) noexcept
        {
            if (Tick(now) == SnapshotAssemblyStatus::Timeout) return SnapshotAssemblyStatus::Timeout;
            if (!active_) return SnapshotAssemblyStatus::Ignored;
            SnapshotMessage message;
            if (!ParseSnapshotMessage(line, length, message) || message.fields[0] != id_) return Reject();
            const auto* f = message.fields;
            if (message.kind == SnapshotMessageKind::Begin)
            {
                if (begun_ || f[1] == 0 || f[1] > kRealtimePatternRowCapacity ||
                    f[2] == 0 || f[2] > kRealtimePatternChannelCapacity || !f[3] ||
                    f[4] != f[1] * f[2]) return Reject();
                staging_ = {}; staging_.transfer_id = id_; staging_.tempo_hundredths = f[3];
                staging_.pattern.active_rows = static_cast<std::uint8_t>(f[1]);
                staging_.pattern.active_channels = static_cast<std::uint8_t>(f[2]);
                count_ = f[4]; hash_ = SnapshotHeaderHash(f[1], f[2], f[3]); begun_ = true;
            }
            else if (message.kind == SnapshotMessageKind::Cell)
            {
                if (!begun_ || next_cell_ >= count_ || f[1] != next_cell_ ||
                    !ValidSnapshotNote(f[2]) || f[3] > 255) return Reject();
                const auto channels = staging_.pattern.active_channels;
                staging_.pattern.cells[next_cell_ / channels][next_cell_ % channels] =
                    {static_cast<Note>(f[2]), static_cast<std::uint8_t>(f[3])};
                hash_ = SnapshotHashByte(SnapshotHashByte(hash_, static_cast<std::uint8_t>(f[2])),
                    static_cast<std::uint8_t>(f[3]));
                ++next_cell_;
            }
            else if (message.kind == SnapshotMessageKind::End)
            {
                if (!begun_ || next_cell_ != count_ || f[1] != count_ || f[2] != hash_) return Reject();
                committed_ = staging_; available_ = true; Cancel();
                return SnapshotAssemblyStatus::Complete;
            }
            else return Reject();
            return SnapshotAssemblyStatus::Receiving;
        }
    private:
        SnapshotAssemblyStatus Reject() noexcept { Cancel(); return SnapshotAssemblyStatus::Rejected; }
        DevicePatternSnapshot staging_, committed_;
        bool active_ = false, begun_ = false, available_ = false;
        std::uint32_t id_ = 0, started_ = 0, next_cell_ = 0, count_ = 0, hash_ = 0;
    };

    // Firmware main-loop only. References the SAME immutable pattern/tempo used
    // for audio configuration; lifetime must exceed sender use. No player access,
    // interrupt exclusion, allocation or full-pattern copy. One cell per line.
    class PatternSnapshotSender
    {
    public:
        PatternSnapshotSender(const RealtimePattern& pattern, std::uint32_t tempo_hundredths) noexcept
            : pattern_(pattern), tempo_hundredths_(tempo_hundredths) {}
        bool Begin(std::uint32_t id, std::uint32_t now) noexcept
        {
            if (!id || HasData() || !tempo_hundredths_ || !pattern_.active_rows ||
                pattern_.active_rows > kRealtimePatternRowCapacity || !pattern_.active_channels ||
                pattern_.active_channels > kRealtimePatternChannelCapacity) return false;
            for (std::size_t r = 0; r < pattern_.active_rows; ++r)
                for (std::size_t c = 0; c < pattern_.active_channels; ++c)
                    if (!ValidSnapshotNote(pattern_.cells[r][c].note)) return false;
            id_ = id; started_ = now; phase_ = 0; active_ = true;
            hash_ = SnapshotHeaderHash(pattern_.active_rows, pattern_.active_channels, tempo_hundredths_);
            return true;
        }
        void Clear() noexcept { active_ = false; length_ = offset_ = 0; }
        void Tick(std::uint32_t now) noexcept
        {
            if (active_ && now - started_ >= kPatternSnapshotTimeoutMs)
            { active_ = false; if (!Partial()) length_ = offset_ = 0; }
        }
        bool Partial() const noexcept { return offset_ != 0 && offset_ < length_; }
        bool HasData() const noexcept { return active_ || length_ != 0; }
        const char* Data(std::size_t& count) noexcept
        {
            if (!length_ && active_)
            {
                const auto cells = unsigned(pattern_.active_rows) * pattern_.active_channels;
                int written;
                if (phase_ == 0)
                    written = std::snprintf(line_, sizeof(line_), "BTPATTERN1 BEGIN 1 %" PRIu32 " %u %u %" PRIu32 " %u\n",
                        id_, unsigned(pattern_.active_rows), unsigned(pattern_.active_channels), tempo_hundredths_, cells);
                else if (phase_ <= cells)
                {
                    const auto index = phase_ - 1;
                    const auto& cell = pattern_.cells[index / pattern_.active_channels][index % pattern_.active_channels];
                    written = std::snprintf(line_, sizeof(line_), "BTPATTERN1 CELL 1 %" PRIu32 " %u %u %u\n",
                        id_, unsigned(index), unsigned(cell.note), unsigned(cell.instrument));
                    hash_ = SnapshotHashByte(SnapshotHashByte(hash_, cell.note), cell.instrument);
                }
                else written = std::snprintf(line_, sizeof(line_), "BTPATTERN1 END 1 %" PRIu32 " %u %" PRIu32 "\n",
                    id_, cells, hash_);
                if (written < 0 || static_cast<std::size_t>(written) >= sizeof(line_)) Clear();
                else length_ = static_cast<std::size_t>(written);
            }
            count = length_ - offset_; return line_ + offset_;
        }
        void Consume(std::size_t count) noexcept
        {
            if (count > length_ - offset_) return;
            offset_ += count;
            if (length_ && offset_ == length_)
            {
                length_ = offset_ = 0; ++phase_;
                if (phase_ > unsigned(pattern_.active_rows) * pattern_.active_channels + 1) active_ = false;
            }
        }
    private:
        const RealtimePattern& pattern_;
        std::uint32_t tempo_hundredths_, id_ = 0, started_ = 0, hash_ = 0, phase_ = 0;
        bool active_ = false;
        char line_[kPatternSnapshotLineCapacity]{};
        std::size_t length_ = 0, offset_ = 0;
    };

    enum class PatternOutputLane { None, Command, Position, Snapshot };
    // Complete lines only; ACKs first except when a lower-priority line is already
    // partial. Position/snapshot alternate at line boundaries, with no PCM work.
    class PatternOutputMux
    {
    public:
        PatternOutputMux(PatternTelemetrySender& position, PatternSnapshotSender& snapshot) noexcept
            : position_(position), snapshot_(snapshot) {}
        PatternOutputLane Select(bool command_waiting, bool command_partial) const noexcept
        {
            if (command_partial) return PatternOutputLane::Command;
            if (position_.Partial()) return PatternOutputLane::Position;
            if (snapshot_.Partial()) return PatternOutputLane::Snapshot;
            if (command_waiting) return PatternOutputLane::Command;
            if (snapshot_.HasData() && (!position_.HasData() || snapshot_turn_)) return PatternOutputLane::Snapshot;
            return position_.HasData() ? PatternOutputLane::Position : PatternOutputLane::None;
        }
        void Consume(PatternOutputLane lane, std::size_t count) noexcept
        {
            if (!count) return; // Backpressure is not a completed line/turn.
            if (lane == PatternOutputLane::Position)
            { position_.Consume(count); if (!position_.Partial()) snapshot_turn_ = true; }
            else if (lane == PatternOutputLane::Snapshot)
            { snapshot_.Consume(count); if (!snapshot_.Partial()) snapshot_turn_ = false; }
        }
    private:
        PatternTelemetrySender& position_;
        PatternSnapshotSender& snapshot_;
        bool snapshot_turn_ = false;
    };
}

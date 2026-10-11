#pragma once

#include "pattern_snapshot.h"

namespace BroTracker
{
    constexpr std::uint32_t kPatternPublicationTimeoutMs = 5000;
    static_assert(kPatternSnapshotLineCapacity >= sizeof("BTPUBLISH1 CELL 1 ") + 5 * 11,
        "Publication parser/line storage must fit uint32 fields plus LF/NUL");
    constexpr char kPatternPublicationCapability[] = "BTPUBLISH1 CAP 1";
    constexpr char kPausedPatternPublicationCapability[] = "BTPUBLISH1 PAUSEDCAP 1";
    constexpr unsigned kPatternDiscoveryReplyCount = 10; // Existing discovery + preview CAP/NEXT/STATE.
    enum class PublicationKind { Begin, Cell, Commit };
    struct PublicationMessage { PublicationKind kind; std::uint32_t fields[5]{}; };
    enum class PublicationStatus : unsigned
    {
        Receiving = 0, Ready = 1, Malformed = 2, Busy = 3, Identity = 4,
        Dimensions = 5, Cell = 6, Completion = 7, Timeout = 8,
        SnapshotBusy = 9, QueueFull = 10, NotConnected = 11
    };
    inline bool ParsePublication(const char* line, std::size_t length,
        PublicationMessage& output, bool preview = false) noexcept
    {
        struct Definition { const char* prefix; PublicationKind kind; unsigned count; };
        const Definition definitions[] = {
            {preview ? "BTPREVIEW1 BEGIN 1 " : "BTPUBLISH1 BEGIN 1 ", PublicationKind::Begin, 4},
            {preview ? "BTPREVIEW1 CELL 1 " : "BTPUBLISH1 CELL 1 ", PublicationKind::Cell, 5},
            {preview ? "BTPREVIEW1 COMMIT 1 " : "BTPUBLISH1 COMMIT 1 ", PublicationKind::Commit, 3}};
        for (const auto& d : definitions) {
            PublicationMessage parsed{d.kind, {}};
            if (SnapshotFields(line, length, d.prefix, parsed.fields, d.count)) {
                output = parsed; return true;
            }
        }
        return false;
    }
    // FNV-1a32 dimensions bytes followed by note/instrument/timing triples.
    // Tempo, bindings, identity and PCM are deliberately outside this payload.
    inline std::uint32_t PublicationHeaderHash(unsigned rows, unsigned channels) noexcept {
        return SnapshotHashByte(SnapshotHashByte(2166136261u,
            static_cast<std::uint8_t>(rows)), static_cast<std::uint8_t>(channels));
    }

    // Main-loop-only transaction storage. Audio receives only a frozen const view.
    // Call Freeze BEFORE publishing the pointer under the platform critical section.
    // Neither HELLO, timeout nor disconnect may release submitted storage. Finish
    // only after the audio-owner acknowledgement and snapshot-source synchronization.
    // No locks, allocation, I/O or player access here. <=128 cells, constant work
    // per input line; no indefinite scan. IDs increase across sessions for this boot,
    // preventing replay after lost ACK/HELLO. Exhaustion requires explicit reboot.
    class PatternPublication
    {
    public:
        bool Busy() const noexcept { return receiving_ || ready_ || submitted_; }
        bool Submitted() const noexcept { return submitted_; }
        std::uint32_t Identity() const noexcept { return id_; }
        // Discovery only; zero explicitly means exhausted, never a usable ID.
        // High-water mark includes every BEGIN accepted into staging, even if
        // later cancelled/rejected. Invalid BEGINs that never stage consume no ID.
        std::uint32_t NextIdentity() const noexcept {
            return last_id_ == UINT32_MAX ? 0 : last_id_ + 1;
        }
        bool FormatDiscovery(char* output, std::size_t capacity) const noexcept {
            if (!output || !capacity) return false;
            const auto written = std::snprintf(output,capacity,"BTPUBLISH1 NEXT 1 %" PRIu32 "\n",NextIdentity());
            return written >= 0 && static_cast<std::size_t>(written) < capacity;
        }
        const RealtimePattern* Frozen() const noexcept { return submitted_ ? &staging_ : nullptr; }
        void NewSession() noexcept { if (!submitted_) Cancel(); }
        bool Tick(std::uint32_t now) noexcept {
            if (!submitted_ && (receiving_ || ready_) && now - started_ >= kPatternPublicationTimeoutMs) {
                Cancel(); return true;
            }
            return false;
        }
        // Explicit parsing error invalidates unsubmitted staging, never frozen work.
        void Cancel() noexcept { if (!submitted_) receiving_ = ready_ = false; }
        bool Freeze() noexcept {
            if (!ready_ || submitted_) return false;
            ready_ = false; submitted_ = true; return true;
        }
        void Finish() noexcept { submitted_ = receiving_ = ready_ = false; }
        // Main loop after TakeApplied: copy the accepted revision before releasing
        // frozen storage or advertising APPLIED. No snapshot may be in progress.
        bool Complete(std::uint32_t id, bool success, RealtimePattern& readback) noexcept {
            if (!submitted_ || id != id_) return false;
            if (success) readback = staging_;
            Finish(); return true;
        }
        PublicationStatus Accept(const PublicationMessage& message, std::uint32_t now, bool snapshot_busy = false) noexcept {
            if (submitted_) return PublicationStatus::Busy;
            if (snapshot_busy) return PublicationStatus::SnapshotBusy;
            if (Tick(now)) return PublicationStatus::Timeout;
            const auto* f = message.fields;
            if (message.kind == PublicationKind::Begin) {
                if (Busy()) return PublicationStatus::Busy;
                if (!f[0] || f[0] <= last_id_) return PublicationStatus::Identity;
                if (!f[1] || f[1] > kRealtimePatternRowCapacity || !f[2] ||
                    f[2] > kRealtimePatternChannelCapacity || f[3] != f[1] * f[2])
                    return PublicationStatus::Dimensions;
                staging_ = {}; staging_.active_rows = static_cast<std::uint8_t>(f[1]);
                staging_.active_channels = static_cast<std::uint8_t>(f[2]);
                id_ = last_id_ = f[0]; count_ = f[3]; next_ = 0; started_ = now;
                hash_ = PublicationHeaderHash(f[1],f[2]); receiving_ = true;
                return PublicationStatus::Receiving;
            }
            if (!receiving_ || f[0] != id_) { Cancel(); return PublicationStatus::Identity; }
            if (message.kind == PublicationKind::Cell) {
                if (next_ >= count_ || f[1] != next_ || !ValidSnapshotNote(f[2]) || f[3] > 255 || f[4] > 1) {
                    Cancel(); return PublicationStatus::Cell;
                }
                const auto row = next_ / staging_.active_channels, channel = next_ % staging_.active_channels;
                staging_.cells[row][channel] = {static_cast<Note>(f[2]), static_cast<std::uint8_t>(f[3])};
                staging_.note_off_timing[row][channel] = static_cast<NoteOffTiming>(f[4]);
                hash_ = SnapshotHashByte(SnapshotHashByte(SnapshotHashByte(hash_,
                    static_cast<std::uint8_t>(f[2])), static_cast<std::uint8_t>(f[3])), static_cast<std::uint8_t>(f[4]));
                ++next_; return PublicationStatus::Receiving;
            }
            if (message.kind != PublicationKind::Commit || next_ != count_ || f[1] != count_ || f[2] != hash_) {
                Cancel(); return PublicationStatus::Completion;
            }
            receiving_ = false; ready_ = true; return PublicationStatus::Ready;
        }
    private:
        RealtimePattern staging_;
        bool receiving_ = false, ready_ = false, submitted_ = false;
        std::uint32_t id_ = 0, last_id_ = 0, started_ = 0, count_ = 0, next_ = 0, hash_ = 0;
    };
}

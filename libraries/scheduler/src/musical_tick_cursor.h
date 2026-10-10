#pragma once

#include "musical_timing.h"

namespace BroTracker
{
    struct MusicalTick
    {
        std::uint64_t tick_index = 0;
        std::uint64_t sample_position = 0;
        std::uint64_t sample_offset = 0;
    };

    enum class TickCursorStatus
    {
        Success,
        Tick,
        BlockComplete,
        NotConfigured,
        InvalidConfiguration,
        BlockNotDrained,
        NonconsecutiveBlock,
        RangeOverflow,
        NoBlock,
        ArithmeticExhausted
    };

    // Logical sample-domain positions only: the caller supplies its timeline.
    // No physical clock selection, USB audio authority, external synchronization
    // or tempo/phase correction is implied (D0035 / CORE_ARCHITECTURE.md).
    // Single-owner API; configuration is fixed for a segment. Explicit segment
    // restart is bounded; consecutive processing never scans an elapsed backlog.
    class MusicalTickCursor
    {
    public:
        // Zero configuration values fail without changing state. Valid configuration
        // starts a new segment at tick/sample zero, discarding any previous block.
        // Uses TickToSamplePosition's exact absolute floor rounding and 0.01 BPM
        // precision, independent of the UI's one-decimal display precision.
        [[nodiscard]] TickCursorStatus Configure(std::uint32_t tempo_hundredths,
                                                std::uint32_t sample_rate_hz) noexcept;
        // Retains configuration, discards the current block and clears exhaustion.
        // Without configuration, returns NotConfigured without changing state.
        [[nodiscard]] TickCursorStatus Reset() noexcept;
        // Explicit new logical segment, not elapsed-tick catch-up. Discard the old
        // block and begin with exactly this tick at its absolute floored sample.
        // Bounded conversion; failure preserves cursor and sample output. Caller
        // owns transport decisions and must not replay discarded events.
        [[nodiscard]] TickCursorStatus ResetAtTick(std::uint64_t tick_index,
                                                   std::uint64_t& sample_position) noexcept;
        // Accept [block_start, block_start + sample_count). First start must match
        // the segment start (zero after Reset/Configure, converted after ResetAtTick);
        // later starts must equal the previous end. Pull must first return
        // BlockComplete, even for an empty block. Errors leave state unchanged.
        [[nodiscard]] TickCursorStatus BeginBlock(std::uint64_t block_start,
                                                 std::uint64_t sample_count) noexcept;
        // Returns one Tick in ascending index order (including coincident ticks),
        // or BlockComplete/error. Output changes only on Tick. End-boundary ticks
        // remain pending for the next block; empty blocks consume no ticks.
        // Repeated completed-block pulls return BlockComplete. Timing overflow or
        // tick-index exhaustion latches ArithmeticExhausted until Reset/Configure.
        // The last representable tick is emitted before index exhaustion is reported.
        // At most one absolute conversion (64 iterations) per call, no backlog scan.
        // Caller controls pull count; draining every configuration within a realtime
        // budget is not established. No allocation, I/O, callbacks or independent clock.
        [[nodiscard]] TickCursorStatus Pull(MusicalTick& tick) noexcept;

    private:
        // Test access permits otherwise impractical uint64_t exhaustion boundaries;
        // it can seed otherwise unreachable error states.
        friend struct MusicalTickCursorTestAccess;
        std::uint32_t tempo_hundredths_ = 0;
        std::uint32_t sample_rate_hz_ = 0;
        std::uint64_t next_tick_ = 0;
        std::uint64_t block_start_ = 0;
        std::uint64_t block_end_ = 0;
        bool has_block_ = false;
        bool drained_ = true;
        bool exhausted_ = false;
    };
}

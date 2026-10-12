#pragma once

#include "row_events.h"

namespace BroTracker
{
    struct LogicalChannelState
    {
        Note note = NOTE_EMPTY;
        std::uint8_t instrument = kNoInstrumentUpdate;
    };

    struct RowEventApplication
    {
        RowEvent event;
        LogicalChannelState before;
        LogicalChannelState after;
        // Field presence, not value change: repeated explicit updates stay true.
        // note_updated includes NOTE_OFF; inspect event.note to distinguish it.
        bool note_updated = false;
        bool instrument_updated = false;
    };

    enum class ChannelStateStatus { Success, InvalidChannel, InvalidNote, InvalidNoteOffTiming };

    // Single-owner, ordered logical continuation. Apply each event exactly once;
    // no clock, progression, deduplication or concurrent mutation is provided.
    // Raw IDs are not looked up. A note without a selected instrument is valid
    // logical state and makes no claim about sound. Pattern/loop metadata never
    // resets state. All operations are bounded, allocation-free and noexcept.
    class ChannelPlaybackState
    {
    public:
        // Explicitly clear every channel to no established note/no instrument.
        void Reset() noexcept
        {
            for (auto& channel : channels_) channel = {};
        }

        // Copy a read-only snapshot; out-of-range access leaves output unchanged.
        [[nodiscard]] ChannelStateStatus GetChannel(std::uint32_t channel,
            LogicalChannelState& state) const noexcept
        {
            if (channel >= kRealtimePatternChannelCapacity)
                return ChannelStateStatus::InvalidChannel;
            state = channels_[channel];
            return ChannelStateStatus::Success;
        }

        // Validate channel and note before mutation. On failure all state and
        // result are unchanged. Empty fields preserve their previous state;
        // NOTE_OFF updates note only, and instrument-only updates never imply
        // note-on. Success returns the original command with all timing metadata,
        // before/after snapshots and explicit field-presence flags.
        [[nodiscard]] ChannelStateStatus Apply(const RowEvent& event,
            RowEventApplication& result) noexcept
        {
            if (event.channel >= kRealtimePatternChannelCapacity)
                return ChannelStateStatus::InvalidChannel;
            if (!IsPatternNote(event.note))
                return ChannelStateStatus::InvalidNote;

            if (!ValidNoteOffTiming(event.note_off_timing))
                return ChannelStateStatus::InvalidNoteOffTiming;
            RowEventApplication application;
            application.event = event;
            application.before = channels_[event.channel];
            application.after = application.before;
            application.note_updated = event.note != NOTE_EMPTY;
            application.instrument_updated = event.instrument != kNoInstrumentUpdate;
            if (application.note_updated) application.after.note = event.note;
            if (application.instrument_updated) application.after.instrument = event.instrument;
            channels_[event.channel] = application.after;
            result = application;
            return ChannelStateStatus::Success;
        }

    private:
        LogicalChannelState channels_[kRealtimePatternChannelCapacity]{};
    };
}

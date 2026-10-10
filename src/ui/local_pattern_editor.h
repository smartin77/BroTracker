#pragma once
#include "live_playback_view.h"

enum class EditField { Note, Instrument };
// D0019 runtime/protocol representation is deferred. This is local draft metadata,
// never an instrument ID and never passed to the playback core.
enum class LocalNoteOffTiming { Arrival, EndOfPosition };
enum class EditorAction { None, Up, Down, Left, Right, NextChannel, PreviousChannel, Increment,
    Decrement, NoteOff, Clear, Restore, SetValue };
struct EditCursor { unsigned row = 0, channel = 0; EditField field = EditField::Note; };

// Host-only, fixed-capacity connection-scoped draft. No transport or publication.
// Sync accepts only the shared parser's complete, validated snapshot. Subsequent
// telemetry/snapshots cannot overwrite edits; disconnect explicitly discards them.
class LocalPatternEditor
{
public:
    void Sync(const std::optional<LivePlaybackView>& live) noexcept
    {
        if (!live) { *this = LocalPatternEditor{}; return; }
        if (ready_) return;
        const auto display = ResolvePatternDisplay(live);
        if (!display.device_pattern) return;
        if (!display.device_pattern->transfer_id || !display.device_pattern->tempo_hundredths) return;
        for (unsigned r = 0; r < display.rows; ++r)
            for (unsigned c = 0; c < display.channels; ++c)
            {
                const auto note = display.device_pattern->pattern.cells[r][c].note;
                if (!IsPatternNote(note)) return;
            }
        baseline_ = *display.device_pattern;
        draft_ = baseline_.pattern;
        ready_ = true;
    }
    bool Ready() const noexcept { return ready_; }
    const EditCursor& Cursor() const noexcept { return cursor_; }
    const BroTracker::RealtimePattern* Draft() const noexcept { return ready_ ? &draft_ : nullptr; }
    std::optional<LocalNoteOffTiming> OffTiming(unsigned row, unsigned channel) const noexcept
    {
        if (!ready_ || row >= draft_.active_rows || channel >= draft_.active_channels) return std::nullopt;
        return off_timing_[row][channel];
    }
    bool Dirty() const noexcept
    {
        if (!ready_) return false;
        for (unsigned r = 0; r < draft_.active_rows; ++r)
            for (unsigned c = 0; c < draft_.active_channels; ++c)
                if (draft_.cells[r][c].note != baseline_.pattern.cells[r][c].note ||
                    draft_.cells[r][c].instrument != baseline_.pattern.cells[r][c].instrument ||
                    (draft_.cells[r][c].note == NOTE_OFF &&
                     off_timing_[r][c] != LocalNoteOffTiming::Arrival)) return true;
        return false;
    }
    // Bounds/unsupported actions reject without changing draft or cursor.
    bool Apply(EditorAction action, unsigned value = 0) noexcept
    {
        if (!ready_) return false;
        auto next = cursor_;
        switch (action)
        {
        case EditorAction::Up: if (!next.row) return false; --next.row; break;
        case EditorAction::Down: if (next.row + 1 >= draft_.active_rows) return false; ++next.row; break;
        case EditorAction::Left:
            if (next.field == EditField::Instrument) next.field = EditField::Note;
            else { if (!next.channel) return false; --next.channel; next.field = EditField::Instrument; }
            break;
        case EditorAction::Right:
            if (next.field == EditField::Note) next.field = EditField::Instrument;
            else { if (next.channel + 1 >= draft_.active_channels) return false; ++next.channel; next.field = EditField::Note; }
            break;
        case EditorAction::NextChannel:
            next.channel = (next.channel + 1) % draft_.active_channels; next.field = EditField::Note; break;
        case EditorAction::PreviousChannel:
            next.channel = (next.channel + draft_.active_channels - 1) % draft_.active_channels;
            next.field = EditField::Note; break;
        case EditorAction::Restore:
            draft_ = baseline_.pattern;
            for (auto& row : off_timing_) for (auto& timing : row) timing = LocalNoteOffTiming::Arrival;
            return true;
        case EditorAction::Increment: case EditorAction::Decrement:
        case EditorAction::SetValue: case EditorAction::NoteOff: case EditorAction::Clear:
        {
            auto& cell = draft_.cells[next.row][next.channel];
            const bool note = next.field == EditField::Note;
            auto& timing = off_timing_[next.row][next.channel];
            if (!note && cell.note == NOTE_OFF)
            {
                if (action == EditorAction::Clear) { timing = LocalNoteOffTiming::Arrival; return true; }
                if (action == EditorAction::Increment || action == EditorAction::Decrement)
                {
                    timing = timing == LocalNoteOffTiming::Arrival ?
                        LocalNoteOffTiming::EndOfPosition : LocalNoteOffTiming::Arrival;
                    return true;
                }
                return false; // No numeric instrument editing for OFF.
            }
            const unsigned old = note ? cell.note : cell.instrument;
            const unsigned minimum = note ? kPatternNoteMinimum : 0;
            const unsigned maximum = note ? kPatternNoteMaximum : 254;
            unsigned replacement = value;
            if (action == EditorAction::Clear) replacement = 255;
            else if (action == EditorAction::NoteOff) { if (!note) return false; replacement = NOTE_OFF; }
            else if (action == EditorAction::Increment || action == EditorAction::Decrement)
            {
                if (old > maximum) replacement = minimum;
                else if (action == EditorAction::Increment) {
                    if (old == maximum && !note) return false;
                    replacement = old == maximum ? minimum : old + 1;
                }
                else {
                    if (old == minimum && !note) return false;
                    replacement = old == minimum ? maximum : old - 1;
                }
            }
            else if (replacement < minimum || replacement > maximum) return false;
            if (note) { cell.note = static_cast<Note>(replacement); timing = LocalNoteOffTiming::Arrival; }
            else cell.instrument = static_cast<std::uint8_t>(replacement);
            return true;
        }
        default: return false;
        }
        cursor_ = next;
        return true;
    }
private:
    bool ready_ = false;
    BroTracker::DevicePatternSnapshot baseline_;
    BroTracker::RealtimePattern draft_;
    LocalNoteOffTiming off_timing_[BroTracker::kRealtimePatternRowCapacity]
        [BroTracker::kRealtimePatternChannelCapacity]{};
    EditCursor cursor_;
};

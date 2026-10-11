#pragma once
#include "live_playback_view.h"
#include "bringup/host_pattern_publication.h"

enum class EditField { Note, Instrument };
using LocalNoteOffTiming = BroTracker::NoteOffTiming; // Compatibility name, core metadata only.
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
                if (!IsPatternNote(note) || !BroTracker::ValidNoteOffTiming(display.device_pattern->pattern.note_off_timing[r][c])) return;
            }
        baseline_ = *display.device_pattern;
        draft_ = baseline_.pattern;
        ready_ = true;
    }
    void SetLocked(bool locked) noexcept { locked_=locked; }
    bool Locked() const noexcept { return locked_; }
    const BroTracker::DevicePatternSnapshot* Baseline() const noexcept { return ready_ ? &baseline_ : nullptr; }
    // Called only after exact publication readback. Cursor remains independent.
    bool Rebase(const BroTracker::DevicePatternSnapshot& snapshot) noexcept {
        if(!ready_ || snapshot.version!=2 || snapshot.tempo_hundredths!=baseline_.tempo_hundredths ||
            !BroTracker::SamePattern(snapshot.pattern,draft_)) return false;
        baseline_=snapshot; draft_=snapshot.pattern; return true;
    }
    bool Ready() const noexcept { return ready_; }
    const EditCursor& Cursor() const noexcept { return cursor_; }
    const BroTracker::RealtimePattern* Draft() const noexcept { return ready_ ? &draft_ : nullptr; }
    std::optional<LocalNoteOffTiming> OffTiming(unsigned row, unsigned channel) const noexcept
    {
        if (!ready_ || row >= draft_.active_rows || channel >= draft_.active_channels) return std::nullopt;
        return draft_.note_off_timing[row][channel];
    }
    bool Dirty() const noexcept
    {
        if (!ready_) return false;
        for (unsigned r = 0; r < draft_.active_rows; ++r)
            for (unsigned c = 0; c < draft_.active_channels; ++c)
                if (draft_.cells[r][c].note != baseline_.pattern.cells[r][c].note ||
                    draft_.cells[r][c].instrument != baseline_.pattern.cells[r][c].instrument ||
                    draft_.note_off_timing[r][c] != baseline_.pattern.note_off_timing[r][c]) return true;
        return false;
    }
    // Bounds/unsupported actions reject without changing draft or cursor.
    bool Apply(EditorAction action, unsigned value = 0) noexcept
    {
        if (!ready_) return false;
        if (locked_ && action != EditorAction::Up && action != EditorAction::Down &&
            action != EditorAction::Left && action != EditorAction::Right &&
            action != EditorAction::NextChannel && action != EditorAction::PreviousChannel) return false;
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
            return true;
        case EditorAction::Increment: case EditorAction::Decrement:
        case EditorAction::SetValue: case EditorAction::NoteOff: case EditorAction::Clear:
        {
            auto& cell = draft_.cells[next.row][next.channel];
            const bool note = next.field == EditField::Note;
            auto& timing = draft_.note_off_timing[next.row][next.channel];
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
            if (action == EditorAction::Clear) {
                replacement = 255;
                if (note) cell.instrument = BroTracker::kNoInstrumentUpdate;
            }
            else if (action == EditorAction::NoteOff) { if (!note) return false; replacement = NOTE_OFF; }
            else if (action == EditorAction::Increment || action == EditorAction::Decrement)
            {
                if (note && old == NOTE_EMPTY && remembered_note_) replacement = *remembered_note_;
                else if (old > maximum) replacement = minimum;
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
            if (note) {
                cell.note = static_cast<Note>(replacement); timing = LocalNoteOffTiming::Arrival;
                if (replacement >= kPatternNoteMinimum && replacement <= kPatternNoteMaximum)
                    remembered_note_ = cell.note;
            }
            else cell.instrument = static_cast<std::uint8_t>(replacement);
            return true;
        }
        default: return false;
        }
        cursor_ = next;
        return true;
    }
private:
    bool ready_ = false, locked_ = false;
    BroTracker::DevicePatternSnapshot baseline_;
    BroTracker::RealtimePattern draft_;
    EditCursor cursor_;
    // Session-local entry preference: never copied from snapshots or included in
    // dirty/publication data. Sync(disconnect) clears it with the editor session.
    std::optional<Note> remembered_note_;
};

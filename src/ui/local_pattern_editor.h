#pragma once
#include "live_playback_view.h"
#include "pattern_edit_history.h"
#include "pattern_grid.h"
#include "bringup/host_pattern_publication.h"

enum class EditField { Note, Instrument };
using LocalNoteOffTiming = BroTracker::NoteOffTiming; // Compatibility name, core metadata only.
enum class EditorAction { None, Up, Down, Left, Right, NextChannel, PreviousChannel, Increment,
    Decrement, NoteOff, Clear, Restore, SetValue, Undo, Redo, PreviousBeat, NextBeat };
struct EditCursor { unsigned row = 0, channel = 0; EditField field = EditField::Note; };
constexpr unsigned kEditableInstrumentMaximum = 98; // Displayed 99; wire IDs remain unrestricted.

// Host-only, fixed-capacity connection-scoped draft. No transport or publication.
// Sync accepts only the shared parser's complete, validated snapshot. Subsequent
// telemetry/snapshots cannot overwrite edits. Disconnect disables editing and retains
// bounded history privately until recovery; current protocol has no context identity,
// so reconnect conservatively invalidates history even for identical contents.
class LocalPatternEditor
{
public:
    void Sync(const std::optional<LivePlaybackView>& live) noexcept
    {
        if (!live) { ClearCaptured(); instrument_cycle_empty_=false; ready_=false; locked_=false; baseline_={}; draft_={}; cursor_={}; return; }
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
        ClearCaptured();
        history_.Clear(); // No protocol-backed context continuity across reconnect.
        instrument_cycle_empty_=false;
        baseline_ = *display.device_pattern;
        draft_ = baseline_.pattern;
        ready_ = true;
    }
    void SetLocked(bool locked) noexcept { locked_=locked; }
    bool Locked() const noexcept { return locked_; }
    const BroTracker::DevicePatternSnapshot* Baseline() const noexcept { return ready_ ? &baseline_ : nullptr; }
    // Called only after exact publication readback. Cursor remains independent.
    bool Rebase(const BroTracker::DevicePatternSnapshot& snapshot,std::uint32_t expected_tempo=0) noexcept {
        if(!ready_ || snapshot.version!=2 || snapshot.tempo_hundredths!=(expected_tempo?expected_tempo:baseline_.tempo_hundredths) ||
            !BroTracker::SamePattern(snapshot.pattern,draft_)) return false;
        baseline_=snapshot; draft_=snapshot.pattern; instrument_cycle_empty_=false; return true;
    }
    // Recovery installs verified working metadata (or retained local edits), never
    // rebases the published baseline. Validate before mutation; cursor is independent.
    bool RecoverDraft(const BroTracker::RealtimePattern& pattern) noexcept {
        if(!ready_ || pattern.active_rows!=draft_.active_rows || pattern.active_channels!=draft_.active_channels) return false;
        for(unsigned r=0;r<pattern.active_rows;++r) for(unsigned c=0;c<pattern.active_channels;++c)
            if(!IsPatternNote(pattern.cells[r][c].note) || !BroTracker::ValidNoteOffTiming(pattern.note_off_timing[r][c])) return false;
        if(!BroTracker::SamePattern(draft_,pattern)) { history_.Clear(); instrument_cycle_empty_=false; }
        draft_=pattern; ClearCaptured(); return true;
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
    // Explicit project/new-pattern/session termination boundary; no file identity assumed.
    void ResetContext() noexcept {
        ClearCaptured();
        instrument_cycle_empty_=false; ready_=locked_=false; baseline_={}; draft_={}; cursor_={}; history_.Clear();
    }
    bool CanUndo() const noexcept { return ready_ && !locked_ && history_.CanUndo(); }
    bool CanRedo() const noexcept { return ready_ && !locked_ && history_.CanRedo(); }
    bool Undo() noexcept { if(!CanUndo()) return false; const auto before=draft_; if(!history_.Undo(draft_)) return false; Capture(before); instrument_cycle_empty_=false; return true; }
    bool Redo() noexcept { if(!CanRedo()) return false; const auto before=draft_; if(!history_.Redo(draft_)) return false; Capture(before); instrument_cycle_empty_=false; return true; }
    // Record resulting data only; navigation/no-ops do not enter history.
    bool Apply(EditorAction action, unsigned value = 0) noexcept {
        if(action==EditorAction::Undo) return Undo();
        if(action==EditorAction::Redo) return Redo();
        if(!ready_) return false;
        const auto before=draft_;
        const bool success=ApplyImpl(action,value);
        if(success) {
            Capture(before);
            history_.Record(before,draft_);
            if((action!=EditorAction::Increment && action!=EditorAction::Decrement) ||
                cursor_.field!=EditField::Instrument || draft_.cells[cursor_.row][cursor_.channel].note==NOTE_OFF)
                instrument_cycle_empty_=false;
        }
        return success;
    }
    bool TakeCapturedCell(unsigned index,BroTracker::SparsePreviewCell& output) noexcept {
        if(index>=BroTracker::kSparsePreviewCellCapacity || !captured_valid_[index]) return false;
        output=captured_[index];captured_valid_[index]=false;return true;
    }
private:
    void ClearCaptured() noexcept {for(auto& valid:captured_valid_) valid=false;}
    void Capture(const BroTracker::RealtimePattern& before) noexcept {
        for(unsigned r=0;r<draft_.active_rows;++r) for(unsigned c=0;c<draft_.active_channels;++c)
            if(before.cells[r][c].note!=draft_.cells[r][c].note || before.cells[r][c].instrument!=draft_.cells[r][c].instrument ||
                before.note_off_timing[r][c]!=draft_.note_off_timing[r][c]) {
                const auto i=r*BroTracker::kRealtimePatternChannelCapacity+c;
                captured_[i]={static_cast<std::uint8_t>(r),static_cast<std::uint8_t>(c),draft_.cells[r][c].note,
                    draft_.cells[r][c].instrument,draft_.note_off_timing[r][c]};captured_valid_[i]=true;
            }
    }
    BroTracker::SparsePreviewCell captured_[BroTracker::kSparsePreviewCellCapacity]{};
    bool captured_valid_[BroTracker::kSparsePreviewCellCapacity]{};
private:
    // Bounds/unsupported actions reject without changing draft or cursor.
    bool ApplyImpl(EditorAction action, unsigned value) noexcept
    {
        if (!ready_) return false;
        if (locked_ && action != EditorAction::Up && action != EditorAction::Down &&
            action != EditorAction::PreviousBeat && action != EditorAction::NextBeat &&
            action != EditorAction::Left && action != EditorAction::Right &&
            action != EditorAction::NextChannel && action != EditorAction::PreviousChannel) return false;
        auto next = cursor_;
        switch (action)
        {
        case EditorAction::Up: if (!next.row) return false; --next.row; break;
        case EditorAction::Down: if (next.row + 1 >= draft_.active_rows) return false; ++next.row; break;
        case EditorAction::PreviousBeat: case EditorAction::NextBeat:
            next.row = PatternGrid::BeatDestination(next.row, draft_.active_rows, action == EditorAction::NextBeat);
            if (next.row == cursor_.row) return false;
            break;
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
            const unsigned maximum = note ? kPatternNoteMaximum : kEditableInstrumentMaximum;
            unsigned replacement = value;
            if (action == EditorAction::Clear) {
                replacement = 255;
                if (note) cell.instrument = BroTracker::kNoInstrumentUpdate;
            }
            else if (action == EditorAction::NoteOff) { if (!note) return false; replacement = NOTE_OFF; }
            else if (action == EditorAction::Increment || action == EditorAction::Decrement)
            {
                if (!note) {
                    if(old==BroTracker::kNoInstrumentUpdate) {
                        replacement=instrument_cycle_empty_ && action==EditorAction::Decrement ? maximum : minimum;
                    } else {
                        if(old>maximum) return false; // Preserve higher device IDs; explicit Clear may replace them.
                        replacement=action==EditorAction::Increment ?
                            (old==maximum ? BroTracker::kNoInstrumentUpdate : old+1) :
                            (old==minimum ? BroTracker::kNoInstrumentUpdate : old-1);
                    }
                    instrument_cycle_empty_=replacement==BroTracker::kNoInstrumentUpdate;
                }
                else if (old == NOTE_EMPTY) {
                    replacement = 60; // Yamaha C-3 fallback for empty-note entry.
                    // Bounded preceding-row lookup in this draft channel only.
                    for (unsigned row=next.row; row>0; ) {
                        const auto candidate=draft_.cells[--row][next.channel].note;
                        if (candidate >= kPatternNoteMinimum && candidate <= kPatternNoteMaximum) {
                            replacement=candidate; break;
                        }
                    }
                }
                else if (note && old == NOTE_OFF)
                    replacement = action == EditorAction::Increment ? minimum : maximum;
                else if (old > maximum) replacement = minimum;
                else if (action == EditorAction::Increment) {
                    replacement = old == maximum ? NOTE_OFF : old + 1;
                }
                else {
                    replacement = old == minimum ? NOTE_OFF : old - 1;
                }
            }
            else if (replacement < minimum || replacement > maximum) return false;
            if (note) {
                cell.note = static_cast<Note>(replacement); timing = LocalNoteOffTiming::Arrival;
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
    // Input interaction only: an empty reached by cycling is not fresh field entry.
    bool instrument_cycle_empty_ = false;
    PatternEditHistory history_;
    bool ready_ = false, locked_ = false;
    BroTracker::DevicePatternSnapshot baseline_;
    BroTracker::RealtimePattern draft_;
    EditCursor cursor_;
};

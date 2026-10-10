#pragma once
#include "local_pattern_editor.h"

// Injected monotonic uint32 millisecond timestamps (wrap-safe elapsed subtraction).
// One action per service; stalls discard missed repeats instead of catching up.
namespace EditorRepeatTiming {
constexpr std::uint32_t first_ms = 400, accelerate_ms = 1000, fastest_ms = 2000;
constexpr std::uint32_t slow_ms = 100, medium_ms = 50, fast_ms = 25;
}
class EditorRepeat
{
public:
    void Cancel() noexcept { action_ = EditorAction::None; }
    void Release(EditorAction action) noexcept { if (action == action_) Cancel(); }
    bool Begin(EditorAction action, std::uint32_t now, LocalPatternEditor& editor) noexcept
    {
        Cancel();
        if (!editor.Ready() || (action != EditorAction::Increment && action != EditorAction::Decrement)) return false;
        action_ = action; started_ = last_ = now; repeated_ = false;
        return editor.Apply(action);
    }
    bool Tick(std::uint32_t now, bool enabled, LocalPatternEditor& editor) noexcept
    {
        if (!enabled || !editor.Ready()) { Cancel(); return false; }
        if (action_ == EditorAction::None) return false;
        const auto held = std::uint32_t(now - started_);
        const auto interval = !repeated_ ? EditorRepeatTiming::first_ms :
            held >= EditorRepeatTiming::fastest_ms ? EditorRepeatTiming::fast_ms :
            held >= EditorRepeatTiming::accelerate_ms ? EditorRepeatTiming::medium_ms : EditorRepeatTiming::slow_ms;
        if (std::uint32_t(now - last_) < interval) return false;
        last_ = now; repeated_ = true;
        const bool changed = editor.Apply(action_);
        if (changed && AtEndpoint(editor)) {
            started_ = last_ = now; repeated_ = false;
        }
        return changed;
    }
private:
    bool AtEndpoint(const LocalPatternEditor& editor) const noexcept {
        const auto& cursor = editor.Cursor();
        if (cursor.field != EditField::Note) return false;
        const auto note = editor.Draft()->cells[cursor.row][cursor.channel].note;
        return (action_ == EditorAction::Increment && note == kPatternNoteMaximum) ||
            (action_ == EditorAction::Decrement && note == kPatternNoteMinimum);
    }
    EditorAction action_ = EditorAction::None;
    std::uint32_t started_ = 0, last_ = 0;
    bool repeated_ = false;
};

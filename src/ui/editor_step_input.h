#pragma once
#include "editor_repeat.h"
#include "host_config.h"

// Shared input interpretation only. Logical editor actions and explicit note
// assignment never apply this preference themselves; timing/instruments do not invert.
inline EditorAction ResolveEditorStep(EditorAction request, EditField field,
    BroTracker::NoteStepDirection direction) noexcept {
    if(field!=EditField::Note || direction!=BroTracker::NoteStepDirection::Reversed) return request;
    if(request==EditorAction::Increment) return EditorAction::Decrement;
    if(request==EditorAction::Decrement) return EditorAction::Increment;
    return request;
}
class EditorStepInput {
public:
    explicit EditorStepInput(BroTracker::NoteStepDirection direction=BroTracker::NoteStepDirection::Normal) noexcept : direction_(direction) {}
    void Cancel() noexcept {repeat_.Cancel(); held_request_=EditorAction::None;}
    void Release(EditorAction request) noexcept {if(request==held_request_) Cancel();}
    bool Begin(EditorAction request,std::uint32_t now,LocalPatternEditor& editor) noexcept {
        Cancel();
        if(!editor.Ready() || (request!=EditorAction::Increment && request!=EditorAction::Decrement)) return false;
        held_request_=request;
        return repeat_.Begin(ResolveEditorStep(request,editor.Cursor().field,direction_),now,editor);
    }
    bool Tick(std::uint32_t now,bool enabled,LocalPatternEditor& editor) noexcept {
        return repeat_.Tick(now,enabled,editor);
    }
private:
    EditorRepeat repeat_;
    EditorAction held_request_=EditorAction::None;
    BroTracker::NoteStepDirection direction_;
};

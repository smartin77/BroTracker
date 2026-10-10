#pragma once
#include <SDL.h>
#include "ui/bringup_controls.h"
#include "ui/local_pattern_editor.h"
#include "ui/editor_repeat.h"
// Ignore OS repeat: at most one editor action per physical key press.
inline EditorAction MapWindowsEditorAction(const SDL_Event& event) {
    if (event.type != SDL_KEYDOWN || event.key.repeat ||
        (event.key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI))) return EditorAction::None;
    if (event.key.keysym.sym == SDLK_TAB)
        return event.key.keysym.mod & KMOD_SHIFT ? EditorAction::PreviousChannel : EditorAction::NextChannel;
    if (event.key.keysym.mod & KMOD_SHIFT) return EditorAction::None;
    switch (event.key.keysym.sym) {
    case SDLK_UP: return EditorAction::Up;
    case SDLK_DOWN: return EditorAction::Down;
    case SDLK_LEFT: return EditorAction::Left;
    case SDLK_RIGHT: return EditorAction::Right;
    case SDLK_PAGEUP: return EditorAction::Increment;
    case SDLK_PAGEDOWN: return EditorAction::Decrement;
    case SDLK_o: return EditorAction::NoteOff;
    case SDLK_DELETE: return EditorAction::Clear;
    case SDLK_F4: return EditorAction::Restore;
    default: return EditorAction::None;
    }
}
class WindowsEditorInput {
public:
    bool Handle(const SDL_Event& event, std::uint32_t now, LocalPatternEditor& editor) noexcept {
        if (!editor.Ready()) repeat_.Cancel();
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            focused_ = false; repeat_.Cancel(); return false;
        }
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) focused_ = true;
        if (event.type == SDL_KEYUP) {
            if (event.key.keysym.sym == SDLK_PAGEUP) repeat_.Release(EditorAction::Increment);
            if (event.key.keysym.sym == SDLK_PAGEDOWN) repeat_.Release(EditorAction::Decrement);
        }
        if (event.type != SDL_KEYDOWN) return false;
        if (event.key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI | KMOD_SHIFT)) repeat_.Cancel();
        if (event.key.repeat) return false;
        const auto action = MapWindowsEditorAction(event);
        if (action == EditorAction::None) return false;
        if (!focused_) return true;
        if (action == EditorAction::Increment || action == EditorAction::Decrement)
            repeat_.Begin(action, now, editor);
        else { repeat_.Cancel(); editor.Apply(action); }
        return true;
    }
    void Tick(std::uint32_t now, SDL_Keymod modifiers, LocalPatternEditor& editor) noexcept {
        repeat_.Tick(now, focused_ && !(modifiers & (KMOD_CTRL | KMOD_ALT | KMOD_GUI | KMOD_SHIFT)), editor);
    }
private:
    EditorRepeat repeat_;
    bool focused_ = true;
};
inline BringUpAction MapWindowsAction(const SDL_Event& event) {
    if (event.type == SDL_QUIT) return BringUpAction::Exit;
    if (event.type != SDL_KEYDOWN || event.key.repeat) return BringUpAction::None;
    const auto key = event.key.keysym.sym;
    const auto modifiers = event.key.keysym.mod;
    if (key == SDLK_x && (modifiers & KMOD_CTRL)) return BringUpAction::Exit;
    if (modifiers & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) return BringUpAction::None;
    if (key == SDLK_SPACE) return BringUpAction::PatternToggle;
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) return BringUpAction::ResetStop;
    return BringUpAction::None;
}

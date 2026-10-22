#pragma once
#include <SDL.h>
#include "ui/bringup_controls.h"
#include "ui/local_pattern_editor.h"
#include "ui/editor_step_input.h"
#include "ui/navigation_repeat.h"
#include "ui/tempo_repeat.h"
inline bool MapWindowsPublish(const SDL_Event& event) {
    return event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_F5 && !event.key.repeat &&
        !(event.key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI | KMOD_SHIFT));
}
// Ignore OS repeat: at most one editor action per physical key press.
inline EditorAction MapWindowsEditorAction(const SDL_Event& event) {
    if (event.type != SDL_KEYDOWN || event.key.repeat ||
        (event.key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI))) return EditorAction::None;
    if (event.key.keysym.sym == SDLK_TAB)
        return event.key.keysym.mod & KMOD_SHIFT ? EditorAction::PreviousChannel : EditorAction::NextChannel;
    if (event.key.keysym.mod & KMOD_SHIFT) {
        if (event.key.keysym.sym == SDLK_UP) return EditorAction::PreviousBeat;
        if (event.key.keysym.sym == SDLK_DOWN) return EditorAction::NextBeat;
        return EditorAction::None;
    }
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
    explicit WindowsEditorInput(BroTracker::ValueStepDirection direction=BroTracker::ValueStepDirection::Normal) noexcept : repeat_(direction) {}
    void Cancel() noexcept {
        repeat_.Cancel(); navigation_.Cancel(); active_=-1;
        for(unsigned i=0;i<7;++i) if(pressed_[i]) blocked_[i]=true;
    }
    bool Handle(const SDL_Event& event, std::uint32_t now, LocalPatternEditor& editor) noexcept {
        if(!editor.Ready() || (editor.Locked() && !navigation_.Active()) || MapWindowsPublish(event)) Cancel();
        if(event.type==SDL_QUIT) { Cancel(); return false; }
        if(event.type==SDL_WINDOWEVENT) {
            if(event.window.event==SDL_WINDOWEVENT_FOCUS_LOST) { focused_=false; Cancel(); }
            if(event.window.event==SDL_WINDOWEVENT_FOCUS_GAINED) focused_=true;
            return false;
        }
        if(event.type!=SDL_KEYDOWN && event.type!=SDL_KEYUP) return false;
        const auto mods=std::uint16_t(event.key.keysym.mod & (KMOD_CTRL|KMOD_ALT|KMOD_GUI|KMOD_SHIFT));
        if(mods!=modifiers_) { Cancel(); modifiers_=mods; }
        const int index=Index(event.key.keysym.sym);
        if(event.type==SDL_KEYUP) {
            if(index>=0) { pressed_[index]=false; blocked_[index]=false; if(active_==index) Cancel(); }
            return false;
        }
        if(event.key.repeat) return false;
        if(index>=0) pressed_[index]=true;
        const auto action=MapWindowsEditorAction(event);
        if(action==EditorAction::None) { Cancel(); return false; }
        if(!focused_ || !editor.Ready()) { Cancel(); return true; }
        if(index>=0) {
            const int opposite=index<4 ? (index^1) : -1;
            if(opposite>=0 && pressed_[opposite]) { Cancel(); return true; }
            if(blocked_[index]) return true;
            Cancel(); blocked_[index]=false; active_=index;
            if(action==EditorAction::Increment || action==EditorAction::Decrement) {
                if(editor.Locked()) { Cancel(); return true; }
                repeat_.Begin(action,now,editor);
            } else navigation_.Begin(action,now,editor);
        } else { Cancel(); editor.Apply(action); }
        return true;
    }
    void Tick(std::uint32_t now, SDL_Keymod modifiers, LocalPatternEditor& editor) noexcept {
        const auto mods=std::uint16_t(modifiers & (KMOD_CTRL|KMOD_ALT|KMOD_GUI|KMOD_SHIFT));
        if(mods!=modifiers_) { Cancel(); modifiers_=mods; }
        if(!focused_ || !editor.Ready()) { Cancel(); return; }
        if(navigation_.Active()) navigation_.Tick(now,editor);
        else if(editor.Locked()) Cancel();
        else repeat_.Tick(now,true,editor);
    }
private:
    static int Index(SDL_Keycode key) noexcept {
        switch(key) {
        case SDLK_UP:return 0; case SDLK_DOWN:return 1;
        case SDLK_LEFT:return 2; case SDLK_RIGHT:return 3;
        case SDLK_TAB:return 4; case SDLK_PAGEUP:return 5; case SDLK_PAGEDOWN:return 6;
        default:return -1;
        }
    }
    EditorStepInput repeat_;
    NavigationRepeat navigation_;
    bool pressed_[7]{},blocked_[7]{};
    int active_=-1;
    std::uint16_t modifiers_=0;
    bool focused_=true;
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

class WindowsTempoInput {
public:
    void Cancel() noexcept { step_=0; repeat_.Cancel(); for(unsigned i=0;i<2;++i) if(held_[i]) blocked_[i]=true; }
    bool Handle(const SDL_Event& event,std::uint32_t now,bool available) noexcept {
        if(!available) Cancel();
        if(event.type==SDL_WINDOWEVENT) {
            if(event.window.event==SDL_WINDOWEVENT_FOCUS_LOST) {focused_=false;Cancel();}
            if(event.window.event==SDL_WINDOWEVENT_FOCUS_GAINED) focused_=true;
        }
        if(event.type!=SDL_KEYDOWN && event.type!=SDL_KEYUP) return false;
        int index=event.key.keysym.sym==SDLK_KP_MINUS?0:event.key.keysym.sym==SDLK_KP_PLUS?1:-1;
        if(index<0) return false;
        if(event.type==SDL_KEYUP) {held_[index]=blocked_[index]=false;repeat_.Cancel();return true;}
        if(event.key.repeat || held_[index]) return true;
        held_[index]=true;
        if(held_[1-index]) {Cancel();return true;}
        if(available && focused_ && !blocked_[index]) step_=repeat_.Begin(index?1:-1,now);
        return true;
    }
    int TakeStep() noexcept {const auto step=step_;step_=0;return step;}
    int Tick(std::uint32_t now,bool available) noexcept {if(!available || !focused_) {Cancel();return 0;} return repeat_.Tick(now);}
private:
    TempoRepeat repeat_; bool held_[2]{},blocked_[2]{},focused_=true; int step_=0;
};

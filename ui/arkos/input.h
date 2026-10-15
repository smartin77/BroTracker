#pragma once
#include <SDL.h>
#include "ui/editor_step_input.h"
#include "ui/navigation_repeat.h"
#include "ui/bringup_controls.h"
#include "ui/handheld_control_hints.h"

// Names are SDL semantic controls, not physical/raw indices or S/H layouts.
// Raw fallback requires bindings obtained from the selected SDL GameController.
enum class ArkOSInputStream { Controller, VerifiedRaw };
struct ArkOSInputResult {
    BringUpAction transport = BringUpAction::None;
    bool publish = false;
};

static_assert(SDL_CONTROLLER_BUTTON_MAX <= 32, "Handheld held-state mask must cover all SDL buttons");
class ArkOSEditorInput {
public:
    explicit ArkOSEditorInput(BroTracker::ValueStepDirection direction = BroTracker::ValueStepDirection::Normal) noexcept
        : repeat_(direction) {}
    void Select(SDL_JoystickID id, ArkOSInputStream stream = ArkOSInputStream::Controller,
                const SDL_GameControllerButtonBind* bindings = nullptr) noexcept {
        Cancel(); id_=id; stream_=stream;
        for(unsigned i=0;i<SDL_CONTROLLER_BUTTON_MAX;++i) {
            pressed_[i]=false; bindings_[i]=bindings ? bindings[i] : SDL_GameControllerButtonBind{};
        }
        blocked_=0; focused_=true;
    }
    SDL_JoystickID Device() const noexcept { return id_; }
    void SelectController(SDL_GameController* controller, ArkOSInputStream stream) noexcept {
        SDL_GameControllerButtonBind bindings[SDL_CONTROLLER_BUTTON_MAX]{};
        for(unsigned i=0;i<SDL_CONTROLLER_BUTTON_MAX;++i)
            bindings[i]=SDL_GameControllerGetBindForButton(controller,static_cast<SDL_GameControllerButton>(i));
        Select(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)),stream,bindings);
    }
    void Cancel() noexcept {
        repeat_.Cancel(); navigation_.Cancel(); repeat_direction_=-1;
        for(unsigned b=SDL_CONTROLLER_BUTTON_DPAD_UP;b<=SDL_CONTROLLER_BUTTON_DPAD_RIGHT;++b)
            if(pressed_[b]) blocked_|=1u<<b;
    }
    void Tick(std::uint32_t now, bool connected, LocalPatternEditor& editor) noexcept {
        if(!connected || !focused_ || !editor.Ready()) { Cancel(); return; }
        if(repeat_direction_<0) return;
        if(navigation_.Active()) {
            navigation_.Tick(now,editor);
        } else {
            if(editor.Locked()) { Cancel(); return; }
            repeat_.Tick(now,pressed_[SDL_CONTROLLER_BUTTON_LEFTSHOULDER],editor);
        }
    }
    ArkOSInputResult Handle(const SDL_Event& event, std::uint32_t now,
                           bool connected, LocalPatternEditor& editor) noexcept {
        if(!connected || !editor.Ready() || (editor.Locked() && !navigation_.Active())) Cancel();
        if(event.type==SDL_QUIT) { Cancel(); return {BringUpAction::Exit,false}; }
        if(event.type==SDL_WINDOWEVENT) {
            if(event.window.event==SDL_WINDOWEVENT_FOCUS_LOST) { focused_=false; Cancel(); }
            if(event.window.event==SDL_WINDOWEVENT_FOCUS_GAINED) focused_=true;
            return {};
        }
        if((event.type==SDL_CONTROLLERDEVICEREMOVED && event.cdevice.which==id_) ||
           (event.type==SDL_JOYDEVICEREMOVED && event.jdevice.which==id_)) { Select(-1); return {}; }
        if(id_<0) return {};
        if(stream_==ArkOSInputStream::Controller) {
            if((event.type!=SDL_CONTROLLERBUTTONDOWN && event.type!=SDL_CONTROLLERBUTTONUP) || event.cbutton.which!=id_) return {};
            return Button(event.cbutton.button,event.type==SDL_CONTROLLERBUTTONDOWN,now,connected,editor);
        }
        // Exactly one input stream: controller duplicates and raw axes are ignored.
        if((event.type==SDL_JOYBUTTONDOWN || event.type==SDL_JOYBUTTONUP) && event.jbutton.which==id_) {
            for(unsigned b=0;b<SDL_CONTROLLER_BUTTON_MAX;++b)
                if(bindings_[b].bindType==SDL_CONTROLLER_BINDTYPE_BUTTON && bindings_[b].value.button==event.jbutton.button)
                    return Button(b,event.type==SDL_JOYBUTTONDOWN,now,connected,editor);
        }
        if(event.type==SDL_JOYHATMOTION && event.jhat.which==id_) {
            // Release old hat directions before arming new ones (e.g. Down->Up).
            for(const bool down : {false,true})
                for(unsigned b=SDL_CONTROLLER_BUTTON_DPAD_UP;b<=SDL_CONTROLLER_BUTTON_DPAD_RIGHT;++b) {
                    const auto binding=bindings_[b];
                    if(binding.bindType!=SDL_CONTROLLER_BINDTYPE_HAT) continue;
                    if(binding.value.hat.hat!=event.jhat.hat) continue;
                    const bool desired=(event.jhat.value & binding.value.hat.hat_mask)!=0;
                    if(desired==down) (void)Button(b,down,now,connected,editor);
                }
        }
        return {};
    }
private:
    ArkOSInputResult Button(unsigned b,bool down,std::uint32_t now,bool connected,LocalPatternEditor& editor) noexcept {
        if(b>=SDL_CONTROLLER_BUTTON_MAX || pressed_[b]==down) return {};
        pressed_[b]=down;
        const bool direction=b>=SDL_CONTROLLER_BUTTON_DPAD_UP && b<=SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
        if(!down) {
            blocked_ &= ~(1u<<b);
            if(b==SDL_CONTROLLER_BUTTON_LEFTSHOULDER || repeat_direction_==static_cast<int>(b)) Cancel();
            return {};
        }
        if(b==SDL_CONTROLLER_BUTTON_LEFTSHOULDER) { Cancel(); return {}; }
        if(!focused_) { if(direction) blocked_|=1u<<b; return {}; }
        const bool modifier=pressed_[SDL_CONTROLLER_BUTTON_LEFTSHOULDER];
        if(direction) {
            const unsigned opposite=b==SDL_CONTROLLER_BUTTON_DPAD_UP ? SDL_CONTROLLER_BUTTON_DPAD_DOWN :
                b==SDL_CONTROLLER_BUTTON_DPAD_DOWN ? SDL_CONTROLLER_BUTTON_DPAD_UP :
                b==SDL_CONTROLLER_BUTTON_DPAD_LEFT ? SDL_CONTROLLER_BUTTON_DPAD_RIGHT : SDL_CONTROLLER_BUTTON_DPAD_LEFT;
            if(pressed_[opposite]) { Cancel(); blocked_|=(1u<<b)|(1u<<opposite); return {}; }
            if(blocked_ & (1u<<b)) return {};
            if(!connected || !editor.Ready()) { blocked_|=1u<<b; return {}; }
            if(modifier && (b==SDL_CONTROLLER_BUTTON_DPAD_UP || b==SDL_CONTROLLER_BUTTON_DPAD_DOWN)) {
                if(editor.Locked()) { blocked_|=1u<<b; return {}; }
                Cancel();
                // Cancel blocks already-held directions; this new physical edge is armed.
                blocked_ &= ~(1u<<b); repeat_direction_=static_cast<int>(b);
                repeat_.Begin(b==SDL_CONTROLLER_BUTTON_DPAD_UP ? EditorAction::Increment : EditorAction::Decrement,now,editor);
            } else {
                Cancel();
                const auto action=b==SDL_CONTROLLER_BUTTON_DPAD_UP ? EditorAction::Up :
                    b==SDL_CONTROLLER_BUTTON_DPAD_DOWN ? EditorAction::Down :
                    b==SDL_CONTROLLER_BUTTON_DPAD_LEFT ? (modifier ? EditorAction::PreviousChannel : EditorAction::Left) :
                    (modifier ? EditorAction::NextChannel : EditorAction::Right);
                blocked_ &= ~(1u<<b); repeat_direction_=static_cast<int>(b);
                navigation_.Begin(action,now,editor);
            }
            return {};
        }
        if(b==SDL_CONTROLLER_BUTTON_START) { Cancel(); return {modifier ? BringUpAction::Exit : BringUpAction::PatternToggle,false}; }
        if(b==SDL_CONTROLLER_BUTTON_B) { Cancel(); return {BringUpAction::ResetStop,false}; }
        if(b==SDL_CONTROLLER_BUTTON_A && !modifier) { Cancel(); return {BringUpAction::None,true}; }
        if(b==SDL_CONTROLLER_BUTTON_X && !modifier && connected) { Cancel(); editor.Apply(EditorAction::NoteOff); }
        if(b==SDL_CONTROLLER_BUTTON_Y && connected) { Cancel(); editor.Apply(modifier ? EditorAction::Restore : EditorAction::Clear); }
        return {};
    }
    EditorStepInput repeat_;
    NavigationRepeat navigation_;
    SDL_JoystickID id_=-1;
    ArkOSInputStream stream_=ArkOSInputStream::Controller;
    SDL_GameControllerButtonBind bindings_[SDL_CONTROLLER_BUTTON_MAX]{};
    bool pressed_[SDL_CONTROLLER_BUTTON_MAX]{};
    bool focused_=true;
    unsigned blocked_=0;
    int repeat_direction_=-1;
};

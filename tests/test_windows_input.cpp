#define SDL_MAIN_HANDLED
#include "ui/windows/input.h"
#include "ui/windows/teensy_identity.h"
#include <cstdio>
#include <utility>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s\n", #x); return 1; } } while (0)
static int NavigationRegression() {
    LocalPatternEditor editor;
    std::optional<LivePlaybackView> live=LivePlaybackView{};
    BroTracker::DevicePatternSnapshot snapshot; snapshot.transfer_id=1; snapshot.tempo_hundredths=12753;
    snapshot.pattern.active_rows=32; snapshot.pattern.active_channels=3;
    live->telemetry_available=true; live->position.rows=32; live->position.channels=3;
    live->device_pattern=snapshot; editor.Sync(live);
    WindowsEditorInput input;
    const auto key=[&](SDL_Keycode code,std::uint32_t now,unsigned type=SDL_KEYDOWN,SDL_Keymod mod=KMOD_NONE,unsigned repeat=0) {
        SDL_Event e{}; e.type=type; e.key.keysym.sym=code; e.key.keysym.mod=mod; e.key.repeat=static_cast<Uint8>(repeat);
        return input.Handle(e,now,editor);
    };
    key(SDLK_DOWN,0); CHECK(editor.Cursor().row==1);
    key(SDLK_DOWN,100,SDL_KEYDOWN,KMOD_NONE,1); CHECK(editor.Cursor().row==1);
    for(auto time : {349u,350u,449u,450u,939u,999u,1000u,1059u,1060u,1959u,1999u,2000u,2039u,2040u,6000u,6000u}) {
        input.Tick(time,KMOD_NONE,editor);
        const unsigned expected=time==349 ? 1 : time==350 || time==449 ? 2 : time==450 ? 3 :
            time==939 || time==999 ? 4 : time==1000 || time==1059 ? 5 : time==1060 ? 6 :
            time==1959 || time==1999 ? 7 : time==2000 || time==2039 ? 8 : time==2040 ? 9 : 10;
        CHECK(editor.Cursor().row==expected);
    }
    key(SDLK_DOWN,6001,SDL_KEYUP);
    CHECK(!editor.Dirty() && !editor.CanUndo());
    // The timer is platform-independent, including unsigned timestamp wrap.
    NavigationRepeat timer;
    timer.Begin(EditorAction::Down,UINT32_MAX-100,editor);
    timer.Tick(248,editor); CHECK(editor.Cursor().row==11);
    timer.Tick(249,editor); CHECK(editor.Cursor().row==12);
    timer.Cancel(); timer.Tick(9999,editor); CHECK(editor.Cursor().row==12);
    editor.SetLocked(true);
    key(SDLK_RIGHT,7000); input.Tick(7350,KMOD_NONE,editor);
    CHECK(editor.Cursor().channel==1 && editor.Cursor().field==EditField::Note);
    key(SDLK_TAB,7400); key(SDLK_RIGHT,7401,SDL_KEYUP); // Older release cannot cancel Tab.
    input.Tick(7750,KMOD_NONE,editor); CHECK(editor.Cursor().channel==0);
    key(SDLK_TAB,7751,SDL_KEYUP);
    key(SDLK_TAB,7800,SDL_KEYDOWN,KMOD_SHIFT); CHECK(editor.Cursor().channel==2);
    input.Tick(8150,KMOD_SHIFT,editor); CHECK(editor.Cursor().channel==1);
    input.Tick(8151,KMOD_NONE,editor); input.Tick(9000,KMOD_NONE,editor);
    CHECK(editor.Cursor().channel==1); // Modifier changes require release.
    key(SDLK_TAB,9001); input.Tick(9400,KMOD_NONE,editor); CHECK(editor.Cursor().channel==1);
    key(SDLK_TAB,9401,SDL_KEYUP);
    key(SDLK_PAGEUP,9500); input.Tick(10000,KMOD_NONE,editor); CHECK(!editor.Dirty());
    key(SDLK_PAGEUP,10001,SDL_KEYUP); editor.SetLocked(false);
    key(SDLK_DOWN,11000); key(SDLK_UP,11001); const auto row=editor.Cursor().row;
    input.Tick(12000,KMOD_NONE,editor); CHECK(editor.Cursor().row==row);
    key(SDLK_DOWN,12001,SDL_KEYUP); input.Tick(13000,KMOD_NONE,editor); CHECK(editor.Cursor().row==row);
    key(SDLK_UP,13001,SDL_KEYUP);
    key(SDLK_DOWN,14000); key(SDLK_PAGEUP,14001); input.Tick(14351,KMOD_NONE,editor);
    CHECK(editor.Draft()->cells[row+1][1].note==60); // Value stream replaces navigation, still waits 400 ms.
    input.Tick(14401,KMOD_NONE,editor); CHECK(editor.Draft()->cells[row+1][1].note==61);
    key(SDLK_RIGHT,14500); key(SDLK_PAGEUP,14501,SDL_KEYUP);
    input.Tick(14850,KMOD_NONE,editor); CHECK(editor.Cursor().channel==2);
    SDL_Event focus{}; focus.type=SDL_WINDOWEVENT; focus.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
    input.Handle(focus,15000,editor); focus.window.event=SDL_WINDOWEVENT_FOCUS_GAINED; input.Handle(focus,15001,editor);
    const auto cursor=editor.Cursor(); input.Tick(16000,KMOD_NONE,editor); CHECK(editor.Cursor().channel==cursor.channel);
    key(SDLK_RIGHT,16001,SDL_KEYUP); key(SDLK_DOWN,16002,SDL_KEYUP);
    key(SDLK_TAB,17000); editor.Sync(std::nullopt); input.Tick(17001,KMOD_NONE,editor);
    editor.Sync(live); input.Tick(18000,KMOD_NONE,editor); CHECK(editor.Cursor().channel==0);
    key(SDLK_TAB,18001); input.Tick(19000,KMOD_NONE,editor); CHECK(editor.Cursor().channel==0);
    key(SDLK_TAB,19001,SDL_KEYUP); key(SDLK_DOWN,20000);
    SDL_Event quit{}; quit.type=SDL_QUIT; input.Handle(quit,20001,editor);
    input.Tick(21000,KMOD_NONE,editor); CHECK(editor.Cursor().row==1);
    return 0;
}
static int BeatNavigationRegression() {
    LocalPatternEditor editor;
    std::optional<LivePlaybackView> live=LivePlaybackView{};
    BroTracker::DevicePatternSnapshot snapshot; snapshot.transfer_id=1; snapshot.tempo_hundredths=12500;
    snapshot.pattern.active_rows=32; snapshot.pattern.active_channels=2;
    live->telemetry_available=true; live->position.rows=32; live->position.channels=2;
    live->device_pattern=snapshot; editor.Sync(live);
    editor.Apply(EditorAction::NextChannel); editor.Apply(EditorAction::Right);
    const auto before=*editor.Draft(); WindowsEditorInput input;
    const auto key=[&](SDL_Keycode code,unsigned now,unsigned type=SDL_KEYDOWN,SDL_Keymod mod=KMOD_SHIFT,unsigned repeat=0) {
        SDL_Event e{}; e.type=type; e.key.keysym.sym=code; e.key.keysym.mod=mod; e.key.repeat=repeat;
        return input.Handle(e,now,editor);
    };
    CHECK(key(SDLK_DOWN,0)); CHECK(editor.Cursor().row==4);
    key(SDLK_DOWN,100,SDL_KEYDOWN,KMOD_SHIFT,1); CHECK(editor.Cursor().row==4);
    input.Tick(349,KMOD_SHIFT,editor); CHECK(editor.Cursor().row==4);
    input.Tick(350,KMOD_SHIFT,editor); CHECK(editor.Cursor().row==8);
    input.Tick(450,KMOD_SHIFT,editor); CHECK(editor.Cursor().row==12);
    input.Tick(1000,KMOD_SHIFT,editor); CHECK(editor.Cursor().row==16);
    input.Tick(1059,KMOD_SHIFT,editor); CHECK(editor.Cursor().row==16);
    input.Tick(1060,KMOD_SHIFT,editor); CHECK(editor.Cursor().row==20);
    input.Tick(2000,KMOD_SHIFT,editor); CHECK(editor.Cursor().row==24);
    input.Tick(2040,KMOD_SHIFT,editor); CHECK(editor.Cursor().row==28);
    input.Tick(10000,KMOD_SHIFT,editor); CHECK(editor.Cursor().row==28);
    key(SDLK_LSHIFT,10001,SDL_KEYUP,KMOD_NONE); input.Tick(11000,KMOD_NONE,editor);
    key(SDLK_DOWN,11001,SDL_KEYDOWN,KMOD_NONE,1); CHECK(editor.Cursor().row==28);
    key(SDLK_DOWN,11002,SDL_KEYUP,KMOD_NONE); key(SDLK_DOWN,11003,SDL_KEYDOWN,KMOD_NONE);
    CHECK(editor.Cursor().row==29);
    key(SDLK_LSHIFT,11004); input.Tick(12000,KMOD_SHIFT,editor); CHECK(editor.Cursor().row==29);
    key(SDLK_DOWN,12001,SDL_KEYUP); key(SDLK_UP,12002); CHECK(editor.Cursor().row==28);
    key(SDLK_DOWN,12003); input.Tick(13000,KMOD_SHIFT,editor); CHECK(editor.Cursor().row==28);
    key(SDLK_DOWN,13001,SDL_KEYUP); input.Tick(14000,KMOD_SHIFT,editor); CHECK(editor.Cursor().row==28);
    key(SDLK_UP,14001,SDL_KEYUP); key(SDLK_UP,14002); CHECK(editor.Cursor().row==24);
    SDL_Event focus{}; focus.type=SDL_WINDOWEVENT; focus.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
    input.Handle(focus,14003,editor); focus.window.event=SDL_WINDOWEVENT_FOCUS_GAINED; input.Handle(focus,14004,editor);
    input.Tick(15000,KMOD_SHIFT,editor); CHECK(editor.Cursor().row==24);
    key(SDLK_UP,15001,SDL_KEYUP); key(SDLK_DOWN,15002); CHECK(editor.Cursor().row==28);
    CHECK(editor.Cursor().channel==1 && editor.Cursor().field==EditField::Instrument);
    CHECK(BroTracker::SamePattern(before,*editor.Draft())); CHECK(!editor.Dirty() && !editor.CanUndo());
    return 0;
}
static int TempoRegression() {
    WindowsTempoInput input; SDL_Event e{};
    auto key=[&](SDL_Keycode code,unsigned type,unsigned now,unsigned repeat=0,SDL_Keymod mod=KMOD_NONE) {
        e={};e.type=type;e.key.keysym.sym=code;e.key.repeat=repeat;e.key.keysym.mod=mod;
        return input.Handle(e,now,true);
    };
    CHECK(!key(SDLK_PLUS,SDL_KEYDOWN,0));CHECK(!key(SDLK_MINUS,SDL_KEYDOWN,0));CHECK(!key(SDLK_EQUALS,SDL_KEYDOWN,0));
    CHECK(key(SDLK_KP_PLUS,SDL_KEYDOWN,0));CHECK(input.TakeStep()==5);
    CHECK(key(SDLK_KP_PLUS,SDL_KEYDOWN,100,1));CHECK(input.TakeStep()==0);
    CHECK(input.Tick(399,true)==0);CHECK(input.Tick(400,true)==5);CHECK(input.Tick(1500,true)==25);CHECK(input.Tick(3000,true)==100);
    CHECK(key(SDLK_KP_PLUS,SDL_KEYUP,3001));CHECK(input.Tick(10000,true)==0);
    CHECK(key(SDLK_KP_MINUS,SDL_KEYDOWN,11000,0,KMOD_NUM));CHECK(input.TakeStep()==-5);
    CHECK(key(SDLK_KP_PLUS,SDL_KEYDOWN,11001));CHECK(input.TakeStep()==0);CHECK(input.Tick(12000,true)==0);
    key(SDLK_KP_PLUS,SDL_KEYUP,12001);key(SDLK_KP_MINUS,SDL_KEYUP,12002);
    key(SDLK_KP_PLUS,SDL_KEYDOWN,13000);CHECK(input.TakeStep()==5);
    e={};e.type=SDL_WINDOWEVENT;e.window.event=SDL_WINDOWEVENT_FOCUS_LOST;input.Handle(e,13001,true);
    CHECK(input.Tick(14000,true)==0);
    e.window.event=SDL_WINDOWEVENT_FOCUS_GAINED;input.Handle(e,14001,true);CHECK(input.Tick(15000,true)==0);
    key(SDLK_KP_PLUS,SDL_KEYUP,15001);key(SDLK_KP_PLUS,SDL_KEYDOWN,15002);CHECK(input.TakeStep()==5);
    CHECK(input.Tick(16000,false)==0);CHECK(input.Tick(17000,true)==0);
    return 0;
}
static int HeaderMouseRegression() {
    WindowsHeaderInput input;SDL_Event e{};e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_LEFT;
    CHECK(input.Handle(e,56,42,true)==0);CHECK(input.Handle(e,109,42,true)==-1);
    e.type=SDL_MOUSEMOTION;CHECK(input.Handle(e,109,42,true)==-1);
    e.type=SDL_MOUSEBUTTONUP;input.Handle(e,109,42,true);
    e.type=SDL_MOUSEBUTTONDOWN;CHECK(input.Handle(e,109,42,true)==1);
    e.type=SDL_WINDOWEVENT;e.window.event=SDL_WINDOWEVENT_FOCUS_LOST;input.Handle(e,0,0,false);
    e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_LEFT;CHECK(input.Handle(e,56,42,true)==-1);
    e.type=SDL_MOUSEBUTTONUP;input.Handle(e,0,0,false);
    e.type=SDL_MOUSEBUTTONDOWN;CHECK(input.Handle(e,56,42,false)==-1);
    return 0;
}
int main() {
    CHECK(HeaderMouseRegression()==0);
    CHECK(NavigationRegression()==0);
    CHECK(BeatNavigationRegression()==0);
    CHECK(TempoRegression()==0);
    for(auto mod : {KMOD_NONE,KMOD_CTRL,KMOD_ALT,KMOD_GUI,KMOD_SHIFT}) {
        SDL_Event publish{}; publish.type=SDL_KEYDOWN; publish.key.keysym.sym=SDLK_F5; publish.key.keysym.mod=mod;
        CHECK(MapWindowsPublish(publish)==(mod==KMOD_NONE));
        CHECK(MapWindowsAction(publish)==BringUpAction::None);
        CHECK(MapWindowsEditorAction(publish)==EditorAction::None);
        publish.key.repeat=1; CHECK(!MapWindowsPublish(publish));
        publish.key.repeat=0; publish.type=SDL_KEYUP; CHECK(!MapWindowsPublish(publish));
    }
    SDL_Event mapping{}; mapping.type = SDL_KEYDOWN;
    for (const auto& pair : {std::pair<SDL_Keycode, EditorAction>{SDLK_UP, EditorAction::Up},
        {SDLK_DOWN, EditorAction::Down}, {SDLK_LEFT, EditorAction::Left},
        {SDLK_RIGHT, EditorAction::Right}, {SDLK_TAB, EditorAction::NextChannel}}) {
        mapping.key.keysym.sym = pair.first; CHECK(MapWindowsEditorAction(mapping) == pair.second);
    }
    mapping.key.keysym.sym = SDLK_TAB; mapping.key.keysym.mod = KMOD_SHIFT;
    CHECK(MapWindowsEditorAction(mapping) == EditorAction::PreviousChannel);
    CHECK(MapWindowsAction(mapping) == BringUpAction::None);
    mapping.key.keysym.sym = SDLK_PAGEUP; CHECK(MapWindowsEditorAction(mapping) == EditorAction::None);
    LocalPatternEditor editor;
    std::optional<LivePlaybackView> live = LivePlaybackView{};
    live->telemetry_available = true; live->position.rows = 16; live->position.channels = 2;
    BroTracker::DevicePatternSnapshot snapshot; snapshot.transfer_id = 1; snapshot.tempo_hundredths = 12753;
    snapshot.pattern.active_channels = 2; snapshot.pattern.cells[0][0].note = 60;
    live->device_pattern = snapshot; editor.Sync(live);
    WindowsEditorInput input;
    const auto key = [&](SDL_Keycode code, std::uint32_t now, unsigned type = SDL_KEYDOWN,
        SDL_Keymod mod = KMOD_NONE, unsigned repeat = 0) {
        SDL_Event event{}; event.type = type; event.key.keysym.sym = code;
        event.key.keysym.mod = mod; event.key.repeat = static_cast<Uint8>(repeat);
        return input.Handle(event, now, editor);
    };
    CHECK(key(SDLK_PAGEUP, 0)); CHECK(editor.Draft()->cells[0][0].note == 61);
    CHECK(!key(SDLK_PAGEUP, 100, SDL_KEYDOWN, KMOD_NONE, 1));
    input.Tick(400, KMOD_NONE, editor); CHECK(editor.Draft()->cells[0][0].note == 62);
    key(SDLK_PAGEUP, 401, SDL_KEYUP); input.Tick(1000, KMOD_NONE, editor);
    CHECK(editor.Draft()->cells[0][0].note == 62);
    for (auto cancel : {SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT, SDLK_TAB, SDLK_F4, SDLK_DELETE, SDLK_o}) {
        key(SDLK_PAGEUP, 2000); key(cancel, 2001);
        const auto draft = *editor.Draft(); const auto cursor = editor.Cursor();
        input.Tick(3000, KMOD_NONE, editor);
        CHECK(editor.Draft()->cells[cursor.row][cursor.channel].note == draft.cells[cursor.row][cursor.channel].note);
        CHECK(editor.Draft()->cells[cursor.row][cursor.channel].instrument == draft.cells[cursor.row][cursor.channel].instrument);
        key(cancel, 3001, SDL_KEYUP); key(SDLK_PAGEUP, 3001, SDL_KEYUP);
    }
    key(SDLK_F4, 3100); key(SDLK_PAGEUP, 3200);
    SDL_Event focus{}; focus.type = SDL_WINDOWEVENT; focus.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    input.Handle(focus, 3201, editor); input.Tick(4000, KMOD_NONE, editor);
    focus.window.event = SDL_WINDOWEVENT_FOCUS_GAINED; input.Handle(focus, 4001, editor);
    const auto saved = editor.Draft()->cells[editor.Cursor().row][editor.Cursor().channel];
    input.Tick(5000, KMOD_NONE, editor);
    CHECK(editor.Draft()->cells[editor.Cursor().row][editor.Cursor().channel].note == saved.note);
    key(SDLK_PAGEUP, 5100); input.Tick(5500, KMOD_CTRL, editor);
    const auto modifier_saved = editor.Draft()->cells[editor.Cursor().row][editor.Cursor().channel];
    input.Tick(6000, KMOD_NONE, editor);
    CHECK(editor.Draft()->cells[editor.Cursor().row][editor.Cursor().channel].note == modifier_saved.note);
    key(SDLK_PAGEUP, 6100); editor.Sync(std::nullopt); input.Tick(6500, KMOD_NONE, editor);
    editor.Sync(live); input.Tick(7000, KMOD_NONE, editor);
    CHECK(editor.Draft()->cells[0][0].note == 60);
    key(SDLK_PAGEUP, 7049, SDL_KEYUP);
    CHECK(key(SDLK_RIGHT, 7050)); CHECK(editor.Cursor().field == EditField::Instrument);
    key(SDLK_PAGEUP, 7051);
    CHECK(key(SDLK_TAB, 7052, SDL_KEYDOWN, KMOD_SHIFT));
    CHECK(editor.Cursor().channel == 1 && editor.Cursor().field == EditField::Note);
    input.Tick(7500, KMOD_NONE, editor);
    CHECK(editor.Draft()->cells[0][1].note == NOTE_EMPTY); // Navigation cancelled held edit.
    key(SDLK_TAB, 7500, SDL_KEYUP, KMOD_SHIFT);
    CHECK(key(SDLK_TAB, 7501)); CHECK(editor.Cursor().channel == 0);
    CHECK(editor.Cursor().field == EditField::Note);
    key(SDLK_PAGEUP, 7999, SDL_KEYUP); key(SDLK_RIGHT, 7999, SDL_KEYUP); key(SDLK_TAB, 7999, SDL_KEYUP);
    key(SDLK_PAGEUP, 8000); key(SDLK_PAGEDOWN, 8100);
    key(SDLK_PAGEUP, 8101, SDL_KEYUP); // Release the replaced key.
    input.Tick(8499, KMOD_NONE, editor); CHECK(editor.Draft()->cells[0][0].note == 60);
    input.Tick(8500, KMOD_NONE, editor); CHECK(editor.Draft()->cells[0][0].note == 59);
    key(SDLK_PAGEDOWN, 8501, SDL_KEYUP);
    for (auto key : {SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT, SDLK_TAB,
        SDLK_PAGEUP, SDLK_PAGEDOWN, SDLK_o, SDLK_DELETE, SDLK_F4}) {
        SDL_Event edit{}; edit.type = SDL_KEYDOWN; edit.key.keysym.sym = key;
        CHECK(MapWindowsEditorAction(edit) != EditorAction::None);
        CHECK(MapWindowsAction(edit) == BringUpAction::None);
        edit.key.repeat = 1; CHECK(MapWindowsEditorAction(edit) == EditorAction::None);
        edit.key.repeat = 0; edit.key.keysym.mod = KMOD_CTRL;
        CHECK(MapWindowsEditorAction(edit) == EditorAction::None);
    }
    for (auto key : {SDLK_SPACE, SDLK_RETURN, SDLK_x}) {
        SDL_Event transport{}; transport.type = SDL_KEYDOWN; transport.key.keysym.sym = key;
        CHECK(MapWindowsEditorAction(transport) == EditorAction::None);
    }
    SDL_Event e{}; e.type = SDL_KEYDOWN;
    e.key.keysym.sym = SDLK_SPACE; CHECK(MapWindowsAction(e) == BringUpAction::PatternToggle);
    e.key.repeat = 1; CHECK(MapWindowsAction(e) == BringUpAction::None); e.key.repeat = 0;
    for (auto enter : {SDLK_RETURN, SDLK_KP_ENTER}) {
        for (auto mod : {KMOD_NONE, KMOD_SHIFT, KMOD_NUM, KMOD_CTRL, KMOD_ALT, KMOD_GUI}) {
            SDL_Event stop{}; stop.type = SDL_KEYDOWN;
            stop.key.keysym.sym = enter; stop.key.keysym.mod = mod;
            const auto expected = (mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) ?
                BringUpAction::None : BringUpAction::ResetStop;
            CHECK(MapWindowsAction(stop) == expected);
            CHECK(MapWindowsEditorAction(stop) == EditorAction::None);
            stop.key.repeat = 1; CHECK(MapWindowsAction(stop) == BringUpAction::None);
            stop.key.repeat = 0; stop.type = SDL_KEYUP;
            CHECK(MapWindowsAction(stop) == BringUpAction::None);
        }
    }
    // Both physical Enter keys bypass editing even while accelerated edits are held.
    // SDL mapping coverage is distinct from direct serial STOP hardware testing.
    for(auto enter : {SDLK_RETURN,SDLK_KP_ENTER}) {
        for(bool dirty : {false,true}) {
            CHECK(editor.Apply(EditorAction::Restore));
            if(dirty) CHECK(editor.Apply(EditorAction::Increment));
            const auto before_stop=*editor.Draft();
            SDL_Event initial_stop{}; initial_stop.type=SDL_KEYDOWN; initial_stop.key.keysym.sym=enter;
            CHECK(!input.Handle(initial_stop,2999,editor));
            CHECK(MapWindowsAction(initial_stop)==BringUpAction::ResetStop);
            CHECK(BroTracker::SamePattern(before_stop,*editor.Draft()));
            key(SDLK_PAGEUP,3000); // Arm accelerated editing.
            const auto after_key=*editor.Draft();
            SDL_Event stop{}; stop.type=SDL_KEYDOWN; stop.key.keysym.sym=enter;
            CHECK(!input.Handle(stop,3001,editor));
            CHECK(MapWindowsAction(stop)==BringUpAction::ResetStop);
            CHECK(BroTracker::SamePattern(after_key,*editor.Draft()));
            key(SDLK_PAGEUP,3002,SDL_KEYUP);
            input.Tick(6000,KMOD_NONE,editor);
            CHECK(BroTracker::SamePattern(after_key,*editor.Draft()));
            CHECK(editor.Ready());
        }
    }
    for(auto direction : {BroTracker::ValueStepDirection::Normal,BroTracker::ValueStepDirection::Reversed}) {
        LocalPatternEditor notes; notes.Sync(live); WindowsEditorInput configured(direction);
        auto press=[&](SDL_Keycode code,unsigned now,unsigned type=SDL_KEYDOWN) {
            SDL_Event event{}; event.type=type; event.key.keysym.sym=code;
            return configured.Handle(event,now,notes);
        };
        CHECK(notes.Apply(EditorAction::SetValue,60)); CHECK(press(SDLK_PAGEUP,0));
        const auto increased=direction==BroTracker::ValueStepDirection::Normal?61:59;
        CHECK(notes.Draft()->cells[0][0].note==increased);
        configured.Tick(400,KMOD_NONE,notes);
        CHECK(notes.Draft()->cells[0][0].note==(direction==BroTracker::ValueStepDirection::Normal?62:58));
        press(SDLK_PAGEUP,401,SDL_KEYUP); const auto held=*notes.Draft();
        configured.Tick(1000,KMOD_NONE,notes); CHECK(BroTracker::SamePattern(held,*notes.Draft()));
        CHECK(press(SDLK_PAGEDOWN,1100)); CHECK(notes.Draft()->cells[0][0].note==increased);
        press(SDLK_PAGEDOWN,1101,SDL_KEYUP);
        CHECK(notes.Apply(EditorAction::SetValue,60)); CHECK(notes.Apply(EditorAction::Right));
        CHECK(notes.Apply(EditorAction::SetValue,0)); CHECK(press(SDLK_PAGEUP,1200));
        CHECK(notes.Draft()->cells[0][0].instrument==(direction==BroTracker::ValueStepDirection::Normal?1:255)); press(SDLK_PAGEUP,1201,SDL_KEYUP);
        CHECK(notes.Apply(EditorAction::Left)); CHECK(notes.Apply(EditorAction::NoteOff)); CHECK(notes.Apply(EditorAction::Right));
        CHECK(press(SDLK_PAGEDOWN,1300)); CHECK(notes.OffTiming(0,0)==LocalNoteOffTiming::EndOfPosition);
        press(SDLK_PAGEDOWN,1301,SDL_KEYUP); CHECK(notes.Draft()->cells[0][0].instrument==(direction==BroTracker::ValueStepDirection::Normal?1:255));
        CHECK(notes.Apply(EditorAction::Left)); CHECK(notes.Apply(EditorAction::SetValue,60));
        CHECK(press(SDLK_PAGEUP,2000)); CHECK(press(SDLK_PAGEDOWN,2010));
        press(SDLK_PAGEUP,2011,SDL_KEYUP); // Old release must not cancel opposite hold.
        configured.Tick(2410,KMOD_NONE,notes);
        CHECK(notes.Draft()->cells[0][0].note==(direction==BroTracker::ValueStepDirection::Normal?59:61));
        press(SDLK_PAGEDOWN,2411,SDL_KEYUP); const auto released=*notes.Draft();
        configured.Tick(3000,KMOD_NONE,notes); CHECK(BroTracker::SamePattern(released,*notes.Draft()));
    }
    e.key.keysym.sym = SDLK_x; CHECK(MapWindowsAction(e) == BringUpAction::None);
    for (auto mod : {KMOD_LCTRL, KMOD_RCTRL}) {
        e.key.keysym.mod = mod; CHECK(MapWindowsAction(e) == BringUpAction::Exit);
    }
    e.key.repeat = 1; CHECK(MapWindowsAction(e) == BringUpAction::None);
    e = {}; e.type = SDL_QUIT; CHECK(MapWindowsAction(e) == BringUpAction::Exit);
    for (auto key : {SDLK_ESCAPE, SDLK_VOLUMEUP, SDLK_VOLUMEDOWN, SDLK_a}) {
        e = {}; e.type = SDL_KEYDOWN; e.key.keysym.sym = key;
        CHECK(MapWindowsAction(e) == BringUpAction::None);
    }
    for (auto type : {SDL_KEYUP, SDL_CONTROLLERBUTTONDOWN, SDL_JOYBUTTONDOWN, SDL_CONTROLLERAXISMOTION}) {
        e = {}; e.type = type; CHECK(MapWindowsAction(e) == BringUpAction::None);
    }
    CHECK(IsTeensyIdentity(L"USB\\VID_16C0&PID_048A&MI_00\\instance"));
    CHECK(IsTeensyIdentity(L"usb\\vid_16c0&pid_048a&mi_00\\other"));
    CHECK(!IsTeensyIdentity(L"USB\\VID_16C0&PID_048AB&MI_00"));
    CHECK(!IsTeensyIdentity(L"USB\\VID_1234&PID_048A&MI_00"));
    CHECK(!IsTeensyIdentity(L"BTHENUM\\USB\\VID_16C0&PID_048A"));
    std::puts("Windows keyboard mapping and Teensy identity checks passed");
}

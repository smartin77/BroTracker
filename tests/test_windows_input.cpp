#define SDL_MAIN_HANDLED
#include "ui/windows/input.h"
#include "ui/windows/teensy_identity.h"
#include <cstdio>
#include <utility>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s\n", #x); return 1; } } while (0)
int main() {
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
    CHECK(key(SDLK_RIGHT, 7050)); CHECK(editor.Cursor().field == EditField::Instrument);
    key(SDLK_PAGEUP, 7051);
    CHECK(key(SDLK_TAB, 7052, SDL_KEYDOWN, KMOD_SHIFT));
    CHECK(editor.Cursor().channel == 1 && editor.Cursor().field == EditField::Note);
    input.Tick(7500, KMOD_NONE, editor);
    CHECK(editor.Draft()->cells[0][1].note == NOTE_EMPTY); // Navigation cancelled held edit.
    CHECK(key(SDLK_TAB, 7501)); CHECK(editor.Cursor().channel == 0);
    CHECK(editor.Cursor().field == EditField::Note);
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

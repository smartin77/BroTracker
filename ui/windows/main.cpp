// BroTracker Terminal: Windows presentation/input client; Teensy is the engine.
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include "input.h"
#include "diagnostics.h"
#include "audio_bridge.h"
#include "core/constants.h"
#include "core/tune_loader.h"
#include "renderer/framebuffer.h"
#include "ui/pattern_screen.h"
#include "ui/text_renderer.h"
#include "ui/sdl/sdl_display_backend.h"

namespace {
int ReportFailure(FILE* log, const std::string& detail, const std::string& log_path) {
    WindowsDiagnostic(log, "Terminal error", detail.c_str());
    const std::string message = detail + "\n\n" +
        (log_path.empty() ? "Log path unavailable." : "Log: " + log_path);
    // Native dialog works even when SDL video or logging could not initialize.
    const int count = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0);
    std::wstring wide(count ? count : 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, wide.data(), static_cast<int>(wide.size()));
    MessageBoxW(nullptr, wide.c_str(), L"BroTracker Terminal - Error", MB_OK | MB_ICONERROR);
    return 1;
}
}
int main(int, char*[]) try {
    char* preference_path = SDL_GetPrefPath("BroTracker", "BroTracker Terminal");
    if (!preference_path) return ReportFailure(nullptr, std::string("No writable log location: ") + SDL_GetError(), "");
    const std::filesystem::path log_path = std::filesystem::u8path(preference_path) / "brotracker-terminal.log";
    SDL_free(preference_path);
    const char* append = std::getenv("BROTRACKER_APPEND_LOG");
    std::unique_ptr<FILE, decltype(&std::fclose)> log(
        _wfopen(log_path.c_str(), append && std::strcmp(append, "1") == 0 ? L"a" : L"w"), &std::fclose);
    if (!log) return ReportFailure(nullptr, "Cannot open log file", log_path.u8string());
    std::setvbuf(log.get(), nullptr, _IONBF, 0);
    WindowsDiagnostic(log.get(), "Terminal", "application entry (Windows GUI subsystem)");
    std::fprintf(log.get(), "BroTracker Terminal starting; log=%s\n", log_path.u8string().c_str());
    auto failure = [&](const char* message) {
        return ReportFailure(log.get(), std::string(message) + ": " + SDL_GetError(), log_path.u8string());
    };
    // Prefer packaged assets beside the executable; also allow repository-root
    // development runs without maintaining another copy of the resources.
    char* base = SDL_GetBasePath();
    if (base) {
        auto directory = std::filesystem::u8path(base);
        SDL_free(base);
        if (std::filesystem::exists(directory / "assets/fonts/brotracker.btf"))
            std::filesystem::current_path(directory);
    }
    if (!LoadUiFont("assets/fonts/brotracker.btf")) return failure("Font load failed");
    Tune tune = LoadTuneFromJson("assets/dummy_my_tune.json");
    if (tune.patterns.empty()) return failure("Tune load failed");
    Framebuffer framebuffer(SCREEN_WIDTH, SCREEN_HEIGHT);
    RenderMainScreen(framebuffer, tune, tune.patterns.front());
    SdlDisplayBackend display("BroTracker Terminal", false);
    if (!display.Initialize(640, 480)) return failure("SDL initialization failed");
    std::fprintf(log.get(), "SDL driver=%s; fixed window 640x480; RGB24 framebuffer\n", SDL_GetCurrentVideoDriver());
    std::unique_ptr<WindowsAudioBridge> audio;
    try { audio = std::make_unique<WindowsAudioBridge>(log.get()); }
    catch (const std::exception& error) {
        WindowsDiagnostic(log.get(), "Windows audio disabled", error.what());
    }
    BringUpSerial serial(log.get());
    BringUpControls controls(serial, log.get());
    std::fprintf(log.get(), "Controls: Space START/RESTART; Enter STOP (stay); Ctrl+X/window close EXIT\n");
    bool display_failed = false;
    while (!controls.Quit()) {
        controls.Tick(SDL_GetTicks());
        if (audio) audio->PollDiagnostics();
        SDL_Event event;
        while (!controls.Quit() && SDL_PollEvent(&event)) {
            const auto action = MapWindowsAction(event);
            if (action != BringUpAction::None) {
                const char* source = event.type == SDL_QUIT ? "window close" :
                    action == BringUpAction::Start ? "Space" : action == BringUpAction::Stop ? "Enter" : "Ctrl+X";
                controls.Request(action, source, SDL_GetTicks());
            }
        }
        framebuffer.FilledRectangle(0, 436, SCREEN_WIDTH, 44, Color{16,16,16});
        DrawFixedText(framebuffer, 8, 440, serial.Status(), Color{255,255,255});
        DrawFixedText(framebuffer, 8, 456, controls.StopPending() ?
            (controls.Exiting() ? "STOP pending, then EXIT" : "STOP pending; please wait") :
            "Space: START/RESTART | Enter: STOP | Ctrl+X: EXIT", Color{255,255,255});
        if (!controls.Quit() && !display_failed && !display.Present(framebuffer)) {
            WindowsDiagnostic(log.get(), "Terminal error", SDL_GetError()); display_failed = true;
            controls.Request(BringUpAction::Exit, "display error", SDL_GetTicks());
        }
        SDL_Delay(16);
    }
    if (audio) audio->Stop(); // Join before the shared log closes; CDC STOP has completed.
    std::fprintf(log.get(), "BroTracker Terminal clean shutdown\n");
    // Serial worker is destroyed (and handle closed) before the shared FILE.
    return display_failed ? ReportFailure(log.get(), "Presentation failed; terminal stopped safely", log_path.u8string()) : 0;
}

catch (const std::exception& error) {
    return ReportFailure(nullptr, error.what(), "");
}

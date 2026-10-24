// BroTracker Terminal: Windows presentation/input client; Teensy is the engine.
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include "input.h"
#include "serial_service_limits.h"
#include "ui/host_config.h"
#include "ui/sdl/host_paths.h"
#include "diagnostics.h"
#include "ui/sdl/host_diagnostics.h"
#include "audio_bridge.h"
#include "bringup/diagnostic_channel_gains.h"
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
int main(int argc, char* argv[]) try {
    char* preference_path = SDL_GetPrefPath("BroTracker", "BroTracker Terminal");
    if (!preference_path) return ReportFailure(nullptr, std::string("No writable log location: ") + SDL_GetError(), "");
    const std::filesystem::path log_path = std::filesystem::u8path(preference_path) / "brotracker-terminal.log";
    SDL_free(preference_path);
    const auto config_path=RuntimeConfigPath();
    const auto host_config=BroTracker::LoadHostConfig(config_path,BroTracker::UseSafeConfig(argc,argv));
    BroTracker::ConfigureHostLogging(host_config.config.logging_mode);
    const char* append=std::getenv("BROTRACKER_APPEND_LOG");
    const auto plan=BroTracker::PrepareHostLog(log_path,host_config.config,append && std::strcmp(append,"1")==0);
    std::unique_ptr<FILE,decltype(&std::fclose)> log(plan.use_stderr?nullptr:_wfopen(log_path.c_str(),plan.append?L"a":L"w"),&std::fclose);
    FILE* log_file=log ? log.get() : stderr;
    if(!log) {
        OutputDebugStringA("BTX log unavailable; continuing with stderr. Check user log directory permissions. Previous log was not truncated.\n");
        BroTracker::HostLog(log_file,BroTracker::HostLogLevel::Basic,"Logging","Cannot open resolved log; using stderr. Check directory permissions; startup continues");
    }
    std::setvbuf(log_file,nullptr,_IONBF,0);
    BroTracker::LogHostStartup(log_file,"Windows",host_config,config_path,plan);
    BroTracker::LogHostSystem(log_file);
    auto failure = [&](const char* message) {
        return ReportFailure(log_file, std::string(message) + ": " + SDL_GetError(), log_path.u8string());
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
#if BROTRACKER_WINDOWS_EMPTY_TITLE
    SdlDisplayBackend display("", false);
#else
    SdlDisplayBackend display("BroTracker Terminal", false);
#endif
    if (!display.Initialize(640, 480)) return failure("SDL initialization failed");
    LogSdlHostDiagnostics(log_file);
    std::unique_ptr<WindowsAudioBridge> audio;
    try { audio = std::make_unique<WindowsAudioBridge>(log_file); }
    catch (const std::exception& error) {
        WindowsDiagnostic(log_file, "Windows audio disabled", error.what());
    }
    BringUpSerial serial(log_file);
    serial.SetAudioOutputPreference(host_config.config.audio_output);
    serial.SetMidiOutputChannel(host_config.config.midi_output_channel);
    serial.SetStartupPattern(host_config.config.startup_pattern);
    BroTracker::DiagnosticChannelGainRequest diagnostic_gains(std::getenv("BROTRACKER_DIAGNOSTIC_CHANNEL_GAINS"));
    if(diagnostic_gains.Configuration()==BroTracker::DiagnosticGainConfiguration::Invalid)
        WindowsDiagnostic(log_file, "Invalid BROTRACKER_DIAGNOSTIC_CHANNEL_GAINS", "Expected exactly eight comma-separated Q16 values 0..65536; nothing will be sent");
    WindowsTempoInput tempo_input;
    LocalPatternEditor editor;
    WindowsEditorInput editor_input(host_config.config.value_step_direction);
    BringUpControls controls(serial, log_file);
    BroTracker::HostLogf(log_file,BroTracker::HostLogLevel::Debug,"Controls", "Controls: Space START/PAUSE/CONTINUE when supported (legacy START/RESTART); Enter STOP/reset (stay); Ctrl+X/window close EXIT; F4 Restore; F5 stopped/paused Publish; capability-gated automatic preview\n");
    WindowsHeaderInput header_input;
    bool display_failed = false;
    while (!controls.Quit()) {
        controls.Tick(SDL_GetTicks());
        serial.SyncEditor(editor,SDL_GetTicks());
        BroTracker::Pcm16ChannelGains diagnostic_request;
        if(diagnostic_gains.TakeRequest(serial.Connected(),serial.ChannelGainsSupported(),serial.ChannelGainsSynchronized(),diagnostic_request)) {
            const bool accepted=serial.RequestChannelGains(diagnostic_request);
            WindowsDiagnostic(log_file,"Diagnostic channel gains",accepted ? "One complete request accepted; no automatic retry/reconnect replay" : "Request not accepted; no retry");
        }
        if (audio) audio->PollDiagnostics();
        SDL_Event event;
        while (!controls.Quit() && SDL_PollEvent(&event)) {
            int mouse_x=0,mouse_y=0;
            const bool mouse=event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP;
            const bool mapped=mouse && display.WindowToFramebuffer(event.button.x,event.button.y,mouse_x,mouse_y);
            const int header=header_input.Handle(event,mouse_x,mouse_y,mapped);
            if(header>=0) {if(editor.SelectHeader(unsigned(header))) (void)serial.ToggleChannelMute(unsigned(header));continue;}
            if(tempo_input.Handle(event,SDL_GetTicks(),serial.TempoAvailable())) {
                if(const auto step=tempo_input.TakeStep()) (void)serial.StepTempo(step);
                continue;
            }
            if(event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym==SDLK_F4) (void)serial.RestoreTempo();
            if (MapWindowsPublish(event)) {
                (void)editor_input.Handle(event,SDL_GetTicks(),editor); // Cancel a held edit before freezing.
                if(!controls.Exiting() && !controls.StopPending()) (void)serial.Publish(editor,SDL_GetTicks());
                continue;
            }
            if (editor_input.Handle(event, SDL_GetTicks(), editor)) continue;
            const auto action = MapWindowsAction(event);
            if (action != BringUpAction::None) {
                const char* source = event.type == SDL_QUIT ? "window close" :
                    action == BringUpAction::PatternToggle ? "Space" : action == BringUpAction::ResetStop ? "Enter" : "Ctrl+X";
                WindowsDiagnostic(log_file, "Transport input", source);
                controls.Request(action, source, SDL_GetTicks());
            }
        }
        serial.SyncEditor(editor,SDL_GetTicks()); // Capture edits before incoming recovery replies.
        // Service freshly queued transport before rendering/presenting the UI.
        controls.Tick(SDL_GetTicks());
        serial.SyncEditor(editor,SDL_GetTicks());
        if(const auto step=tempo_input.Tick(SDL_GetTicks(),serial.TempoAvailable())) (void)serial.StepTempo(step);
        editor_input.Tick(SDL_GetTicks(), SDL_GetModState(), editor);
        HostPanelInformation panel{serial.Status(), controls.StopPending() ?
            (controls.Exiting() ? "STOP pending, then EXIT" : "STOP pending; please wait") :
            serial.SupportsPatternTransport() ? "Space: START/PAUSE/CONTINUE | Enter: STOP | Ctrl+X: EXIT | KP +/-: BPM | F4: Restore | F5: Publish" :
            "Space: START/RESTART | Enter: STOP | Ctrl+X: EXIT | KP +/-: BPM | F4: Restore | F5: Publish", serial.PublicationStatus(), diagnostic_gains.Status(), serial.MidiStatus()};
        panel.row_numbering=host_config.config.row_numbering;
        RenderMainScreen(framebuffer, tune, tune.patterns.front(), serial.PlaybackView(SDL_GetTicks()), &editor, panel);
        if (!controls.Quit() && !display_failed && !display.Present(framebuffer)) {
            WindowsDiagnostic(log_file, "Terminal error", SDL_GetError()); display_failed = true;
            controls.Request(BringUpAction::Exit, "display error", SDL_GetTicks());
        }
        for(unsigned pass=0;pass<kWindowsSerialIdleServicePasses && !controls.Quit();++pass) {
            SDL_Delay(kWindowsSerialServiceSliceMs);
            // Capture already accepted edits before processing recovery replies.
            serial.SyncEditor(editor,SDL_GetTicks());
            controls.Tick(SDL_GetTicks());
            serial.SyncEditor(editor,SDL_GetTicks());
        }
    }
    if (audio) audio->Stop(); // Join before the shared log closes; CDC STOP has completed.
    BroTracker::HostLog(log_file,BroTracker::HostLogLevel::Basic,"Terminal","BroTracker Terminal clean shutdown");
    // Serial worker is destroyed (and handle closed) before the shared FILE.
    return display_failed ? ReportFailure(log_file, "Presentation failed; terminal stopped safely", log_path.u8string()) : 0;
}

catch (const std::exception& error) {
    return ReportFailure(nullptr, error.what(), "");
}

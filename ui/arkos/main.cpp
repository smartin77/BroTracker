#include "tools/alsa_bridge/listening_control.h"
/*
 * BroTracker
 *
 * Description: BroTracker Terminal (BTX) for ArkOS/Linux.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ui/bringup_serial.h"
#include "ui/host_config.h"
#include "ui/sdl/host_paths.h"
#include "ui/host_logging.h"
#include "ui/sdl/host_diagnostics.h"
#include "ui/bringup_controls.h"
#include "ui/arkos/input.h"

#include "core/constants.h"
#include "core/logger.h"
#include "core/tune_loader.h"
#include "renderer/framebuffer.h"
#include "ui/pattern_screen.h"
#include "ui/text_renderer.h"
#include "ui/sdl/sdl_display_backend.h"
#include "ui/arkos/diagnostic_status.h"
#include "ui/arkos/offline_click_mode.h"

namespace
{
constexpr const char* kBringUpLogPath =
    "/tmp/brotracker-arkos.log";

void WriteBringUpDiagnostic(
    FILE* log,
    const char* message)
{
    if (log == nullptr)
        return;

    BroTracker::HostLog(log,BroTracker::ClassifyHostDiagnostic("ArkOS",message),"ArkOS",message);
}

void WriteBringUpDiagnostic(
    FILE* log,
    const char* message,
    int value)
{
    if (log == nullptr)
        return;

    BroTracker::HostLogf(log,BroTracker::HostLogLevel::Debug,"ArkOS","%s%d",message,value);
}

void WriteSdlFailure(
    FILE* log,
    const char* message)
{
    if (log == nullptr)
        return;

    BroTracker::HostLogf(log,BroTracker::HostLogLevel::Basic,"ArkOS error","%s: %s",message,SDL_GetError());
}
} // namespace

int main(int argc, char* argv[])
{
    if(argc==2 && !std::strcmp(argv[1],"--offline-diagnostic-info")) {
        std::puts("{\"id\":\"btx-offline-clicks-v2\"}"); return 0;
    }
    if(argc==3 && !std::strcmp(argv[1],"--diagnostic-status")) return RunDiagnosticStatus(argv[2]);
    const char* diagnostic_directory=argc==3 && !std::strcmp(argv[1],"--offline-click-diagnostic") ? argv[2]:nullptr;
    OfflineClickMode diagnostic(diagnostic_directory);
    const char* diagnostic_status=diagnostic.Enabled()?std::getenv("BROTRACKER_DIAGNOSTIC_STATUS_FILE"):nullptr;
    const auto config_path=RuntimeConfigPath();
    const auto host_config=BroTracker::LoadHostConfig(config_path,BroTracker::UseSafeConfig(argc,argv));
    // Evidence retains its established per-user storage, independently of config.
    std::filesystem::path audible_base;
    if(host_config.config.bridge_audible_test) {
        char* evidence_directory=SDL_GetPrefPath("BroTracker","BroTracker Terminal");
        if(evidence_directory) {
            std::error_code error;
            audible_base=std::filesystem::weakly_canonical(
                std::filesystem::u8path(evidence_directory)/"diagnostics"/"bridge-audible",error);
            if(error) audible_base.clear(); // Evidence setup must not prevent normal playback.
        }
        SDL_free(evidence_directory);
    }
    if(argc==2 && !std::strcmp(argv[1],"--bridge-audible-config")) {
        if(!host_config.config.bridge_audible_test) return 2;
        const auto& base=audible_base;
        if(!base.is_absolute()) return 2;
        for(auto parent=base;!parent.empty();parent=parent.parent_path()) {
            if(std::filesystem::exists(parent/".git")){std::fprintf(stderr,"Refusing listening evidence inside a Git checkout\n");return 2;}
            if(parent==parent.parent_path())break;
        }
        std::puts(base.string().c_str());
        return 0;
    }
    if(argc==2 && !std::strcmp(argv[1],"--offline-diagnostic-config")) {
        std::printf("path=%s\nroute=%s\ninvalid=%u\nstatus=%s\nrow_numbering=%s\n",config_path.string().c_str(),
            BroTracker::AudioOutputName(host_config.config.audio_output),unsigned(host_config.config.audio_output_invalid),
            BroTracker::HostConfigDiagnostic(host_config.status),BroTracker::RowNumberingName(host_config.config.row_numbering));
        return (host_config.status==BroTracker::HostConfigStatus::Loaded || host_config.status==BroTracker::HostConfigStatus::Missing)
            && !host_config.config.audio_output_invalid && host_config.config.audio_output!=BroTracker::AudioOutputChoice::Pt8211 ? 0:2;
    }
    if(diagnostic.Enabled() && ((host_config.status!=BroTracker::HostConfigStatus::Loaded && host_config.status!=BroTracker::HostConfigStatus::Missing)
        || host_config.config.audio_output_invalid || host_config.config.audio_output==BroTracker::AudioOutputChoice::Pt8211)) {
        diagnostic.Fail("configuration-not-compatible-with-bridge"); diagnostic.SerialReleased(); return 2;
    }
    BroTracker::ConfigureHostLogging(host_config.config.logging_mode);
    const char* append=std::getenv("BROTRACKER_APPEND_LOG");
    const auto plan=BroTracker::PrepareHostLog(kBringUpLogPath,host_config.config,append && std::strcmp(append,"1")==0);
    FILE* bring_up_log=plan.use_stderr?nullptr:std::fopen(kBringUpLogPath,plan.append?"a":"w");
    const bool owns_log=bring_up_log!=nullptr;
    if(!bring_up_log) {bring_up_log=stderr; LogError("ArkOS log unavailable; using stderr. Check /tmp permissions; startup continues");}
    BroTracker::LogHostStartup(bring_up_log,"ArkOS/Linux",host_config,config_path,plan);
    BroTracker::LogHostSystem(bring_up_log);
    WriteBringUpDiagnostic(
        bring_up_log,
        "UI font load: start");

    if (!LoadUiFont("assets/fonts/brotracker.btf"))
    {
        WriteBringUpDiagnostic(
            bring_up_log,
            "UI font load: failure");
        LogError("Failed to load BroTracker font.");
        return 1;
    }

    WriteBringUpDiagnostic(
        bring_up_log,
        "UI font load: success");

    WriteBringUpDiagnostic(
        bring_up_log,
        "tune load: start");

    Tune tune = LoadTuneFromJson(
        "assets/dummy_my_tune.json");

    if (tune.patterns.empty())
    {
        WriteBringUpDiagnostic(
            bring_up_log,
            "tune load: failure");
        LogError("No patterns available.");
        return 1;
    }

    WriteBringUpDiagnostic(
        bring_up_log,
        "tune load: success");

    WriteBringUpDiagnostic(
        bring_up_log,
        "framebuffer creation: start");

    Framebuffer framebuffer(
        SCREEN_WIDTH,
        SCREEN_HEIGHT);

    WriteBringUpDiagnostic(
        bring_up_log,
        "framebuffer creation: success");

    SdlDisplayBackend display("BroTracker Terminal");

    WriteBringUpDiagnostic(
        bring_up_log,
        "SDL display initialization: start");

    if (!display.Initialize(
            framebuffer.Width(),
            framebuffer.Height()))
    {
        WriteSdlFailure(
            bring_up_log,
            "SDL display initialization: failure");
        LogError("Failed to initialize the SDL display backend.");
        return 1;
    }

    WriteBringUpDiagnostic(
        bring_up_log,
        "SDL display initialization: success");

    SDL_GameController* game_controller = nullptr;
    const int input_init_result = SDL_InitSubSystem(
        SDL_INIT_GAMECONTROLLER |
        SDL_INIT_JOYSTICK);
    const bool input_subsystems_initialized =
        input_init_result == 0;

    if (input_subsystems_initialized)
    {
        WriteBringUpDiagnostic(
            bring_up_log,
            "SDL input subsystem initialization: success");
    }
    else
    {
        WriteSdlFailure(
            bring_up_log,
            "SDL input subsystem initialization: failure");
    }

    if (input_subsystems_initialized)
    {
        const int joystick_count = SDL_NumJoysticks();

        WriteBringUpDiagnostic(
            bring_up_log,
            "detected joystick/controller count: ",
            joystick_count);

        if (joystick_count < 0)
        {
            WriteSdlFailure(
                bring_up_log,
                "joystick/controller detection: failure");
        }

        for (int device_index = 0;
             device_index < joystick_count;
             ++device_index)
        {
            if (SDL_IsGameController(device_index))
            {
                game_controller =
                    SDL_GameControllerOpen(device_index);

                if (game_controller != nullptr)
                {
                    WriteBringUpDiagnostic(
                        bring_up_log,
                        "controller open result: success, device index ",
                        device_index);
                    break;
                }

                WriteSdlFailure(
                    bring_up_log,
                    "controller open result: failure");
            }
            else
            {
                WriteBringUpDiagnostic(bring_up_log,
                    "unmapped joystick ignored; SDL GameController mapping required, device index ",
                    device_index);
            }
        }

        if (joystick_count == 0)
        {
            WriteBringUpDiagnostic(
                bring_up_log,
                "controller/joystick open result: no devices detected");
        }
        else if (game_controller == nullptr)
        {
            WriteBringUpDiagnostic(
                bring_up_log,
                "controller/joystick open result: no device opened");
        }
    }
    else
    {
        WriteBringUpDiagnostic(
            bring_up_log,
            "detected joystick/controller count: unavailable");
        WriteBringUpDiagnostic(
            bring_up_log,
            "controller/joystick open result: unavailable");
    }

    ArkOSEditorInput editor_input(host_config.config.value_step_direction);
    auto select_controller = [&]() {
        if (!game_controller) return;
        const auto stream = SDL_GameControllerEventState(SDL_ENABLE)==SDL_ENABLE ?
            ArkOSInputStream::Controller : ArkOSInputStream::VerifiedRaw;
        SDL_JoystickEventState(SDL_ENABLE);
        editor_input.SelectController(game_controller,stream);
        char guid[64];
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(SDL_GameControllerGetJoystick(game_controller)),guid,sizeof(guid));
        BroTracker::HostLogf(bring_up_log,BroTracker::HostLogLevel::Debug,"ArkOS input",
            "controller=%s guid=%s instance=%d stream=%s; raw fallback uses SDL button/hat bindings only",
            SDL_GameControllerName(game_controller),guid,editor_input.Device(),
            stream==ArkOSInputStream::Controller ? "controller" : "verified raw");
    };
    select_controller();
    WriteBringUpDiagnostic(bring_up_log,kArkOSEditorHints);
    LogSdlHostDiagnostics(bring_up_log);
    { // Serial is closed before diagnostic serial-release status is published.
    std::unique_ptr<listening::Mapping> audible;
    if(host_config.config.bridge_audible_test && !diagnostic.Enabled()) {
        audible=std::make_unique<listening::Mapping>();
        BroTracker::HostLogf(bring_up_log,BroTracker::HostLogLevel::Basic,"Listening",
            "evidence base=%s; control=%s; host replies are NOT audible onset",
            audible_base.empty()?"unavailable":audible_base.string().c_str(),audible->Enabled()?"available":"unavailable; launcher required");
    }
    BringUpSerial serial(bring_up_log);
    if(audible) serial.SetTransportObserver([](void* context,const char* event,std::uint32_t){
        const unsigned kind=!std::strcmp(event,"START")?listening::Start:!std::strcmp(event,"CONTINUE")?listening::Continue:
            !std::strcmp(event,"PAUSE")?listening::Pause:!std::strcmp(event,"DISCONNECT")?listening::Disconnect:
            !std::strcmp(event,"RECONNECT")?listening::Reconnect:!std::strcmp(event,"ERROR")?listening::Error:listening::Stop;
        static_cast<listening::Mapping*>(context)->Publish(kind);
    },audible.get());
    std::uint32_t next_audible_observation=0;
    serial.SetAudioOutputPreference(diagnostic.Enabled()?BroTracker::AudioOutputChoice::Bridge:host_config.config.audio_output);
    serial.SetMidiOutputChannel(host_config.config.midi_output_channel);
    serial.SetStartupPattern(host_config.config.startup_pattern);
    LocalPatternEditor editor;
    BringUpControls controls(serial, bring_up_log);

    WriteBringUpDiagnostic(
        bring_up_log,
        "entering event loop");

    DiagnosticCancelInput diagnostic_cancel;
    while (!controls.Quit() && !diagnostic.Exit())
    {
        controls.Tick(SDL_GetTicks());
        serial.SyncEditor(editor,SDL_GetTicks());
        if(audible && serial.Playing() && SDL_GetTicks()-next_audible_observation>=1000) {
            next_audible_observation=SDL_GetTicks();const auto view=serial.PlaybackView(SDL_GetTicks());
            if(view) audible->Publish(listening::Observation,view->tempo_hundredths,view->position.tick);
        }
        diagnostic.Tick(serial,SDL_GetTicks());
        if(diagnostic.Exit()) break;
        SDL_Event event;

        while (!controls.Quit() && SDL_PollEvent(&event) != 0)
        {
            if(diagnostic.Enabled() && diagnostic_cancel.Accept(event,
                event.type==SDL_CONTROLLERBUTTONDOWN && SDL_GameControllerFromInstanceID(event.cbutton.which))) {
                RequestDiagnosticCancel("btx",event,diagnostic_status); diagnostic.Fail("cancelled"); break;
            }
            if (event.type == SDL_QUIT)
            {
                WriteBringUpDiagnostic(bring_up_log, "EXIT requested by SDL_QUIT");
                controls.Request(BringUpAction::Exit, "SDL_QUIT/display error", SDL_GetTicks());
                break;
            }
            if(diagnostic.Enabled()) continue; // unattended read-only workload; no edits or controller automation
            const auto result=editor_input.Handle(event,SDL_GetTicks(),serial.Connected(),editor);
            if(result.mute_channel>=0) (void)serial.ToggleChannelMute(unsigned(result.mute_channel));
            if(result.restore) (void)serial.RestoreTempo();
            if(const auto step=editor_input.TakeTempoStep()) (void)serial.StepTempo(step);
            if (game_controller && editor_input.Device()<0) {
                SDL_GameControllerClose(game_controller); game_controller=nullptr;
                WriteBringUpDiagnostic(bring_up_log,"selected controller removed; held edits cancelled");
            }
            if (!game_controller && event.type==SDL_CONTROLLERDEVICEADDED && SDL_IsGameController(event.cdevice.which)) {
                game_controller=SDL_GameControllerOpen(event.cdevice.which); select_controller();
            }
            if(result.publish && !controls.Exiting() && !controls.StopPending())
                (void)serial.Publish(editor,SDL_GetTicks());
            if(result.transport!=BringUpAction::None)
                controls.Request(result.transport,"ArkOS mapped input",SDL_GetTicks());
        }
        if(diagnostic.Enabled()) diagnostic_cancel.ObserveReleased(DiagnosticButtonsReleased(),SDL_GetTicks());
        editor_input.Tick(SDL_GetTicks(),serial.Connected() && !controls.Exiting(),editor);
        if(const auto step=editor_input.TakeTempoStep()) (void)serial.StepTempo(step);
        serial.SyncEditor(editor,SDL_GetTicks()); // Observe edits before processing incoming recovery replies.
        controls.Tick(SDL_GetTicks()); // Fresh transport work precedes rendering.
        serial.SyncEditor(editor,SDL_GetTicks());
        HostPanelInformation panel{serial.Status(),
            controls.StopPending() ? (controls.Exiting() ? "STOP pending, then EXIT" : "STOP pending; please wait") :
            editor_input.Device()<0 ? "Mapped SDL controller required | waiting for controller" :
            serial.SupportsPatternTransport() ? kArkOSEditorHints : kArkOSLegacyEditorHints,
            serial.PublicationStatus(), {}, serial.MidiStatus()};
        panel.row_numbering=host_config.config.row_numbering;
        RenderMainScreen(framebuffer, tune, tune.patterns.front(), serial.PlaybackView(SDL_GetTicks()), &editor, panel);

        if(diagnostic_status) DrawDiagnosticStatus(framebuffer,diagnostic_status);
        if (!controls.Quit() && !display.Present(framebuffer))
        {
            WriteSdlFailure(
                bring_up_log,
                "event-loop framebuffer Present: failure");
            controls.Request(BringUpAction::Exit, "SDL_QUIT/display error", SDL_GetTicks());
        }

        SDL_Delay(16);
    }

    if(audible) audible->Publish(listening::Exit);
    } // BringUpControls and BringUpSerial destroyed: Linux Close() is synchronous.
    diagnostic.SerialReleased();
    if (game_controller != nullptr)
        SDL_GameControllerClose(game_controller);

    if (input_subsystems_initialized)
    {
        SDL_QuitSubSystem(
            SDL_INIT_GAMECONTROLLER |
            SDL_INIT_JOYSTICK);
    }

    WriteBringUpDiagnostic(
        bring_up_log,
        "normal shutdown");
    BroTracker::HostLog(bring_up_log,BroTracker::HostLogLevel::Basic,"Terminal","clean shutdown");

    if (owns_log)
        std::fclose(bring_up_log);

    return diagnostic.Failed()?2:0;
}

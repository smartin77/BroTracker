/*
 * BroTracker
 *
 * Description: Minimal ArkOS/Linux UI display client.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bringup_serial.h"

#include "core/constants.h"
#include "core/logger.h"
#include "core/tune_loader.h"
#include "renderer/framebuffer.h"
#include "ui/pattern_screen.h"
#include "ui/text_renderer.h"
#include "ui/sdl/sdl_display_backend.h"

namespace
{
enum class BringUpAction { None, Start, StopExit };

BringUpAction MapBringUpAction(const SDL_Event& event, SDL_JoystickID controller_id)
{
    // Use mapped buttons from the opened controller only. Raw joystick events
    // have no reliable portable mapping and also duplicate controller events.
    if (controller_id < 0 || event.type != SDL_CONTROLLERBUTTONDOWN ||
        event.cbutton.which != controller_id)
        return BringUpAction::None;
    switch (event.cbutton.button)
    {
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
    case SDL_CONTROLLER_BUTTON_B:
        return BringUpAction::Start;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
    case SDL_CONTROLLER_BUTTON_X:
        return BringUpAction::StopExit;
    default:
        return BringUpAction::None;
    }
}

constexpr const char* kBringUpLogPath =
    "/tmp/brotracker-arkos.log";

void WriteBringUpDiagnostic(
    FILE* log,
    const char* message)
{
    if (log == nullptr)
        return;

    std::fputs(message, log);
    std::fputc('\n', log);
    std::fflush(log);
}

void WriteBringUpDiagnostic(
    FILE* log,
    const char* message,
    int value)
{
    if (log == nullptr)
        return;

    std::fprintf(log, "%s%d\n", message, value);
    std::fflush(log);
}

void WriteSdlFailure(
    FILE* log,
    const char* message)
{
    if (log == nullptr)
        return;

    std::fprintf(
        log,
        "%s: %s\n",
        message,
        SDL_GetError());
    std::fflush(log);
}
} // namespace

int main(int, char*[])
{
    const char* append_log = std::getenv("BROTRACKER_APPEND_LOG");
    FILE* bring_up_log = std::fopen(kBringUpLogPath,
        append_log != nullptr && std::strcmp(append_log, "1") == 0 ? "a" : "w");

    if (bring_up_log == nullptr)
        LogError("Failed to open the ArkOS bring-up log.");

    WriteBringUpDiagnostic(
        bring_up_log,
        "application entry");

    LogInfo("BroTracker ArkOS UI starting.");

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

    WriteBringUpDiagnostic(
        bring_up_log,
        "main screen rendering: start");

    RenderMainScreen(
        framebuffer,
        tune,
        tune.patterns.front());

    WriteBringUpDiagnostic(
        bring_up_log,
        "main screen rendering: success");

    SdlDisplayBackend display;

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

    WriteBringUpDiagnostic(
        bring_up_log,
        "first framebuffer Present: start");

    if (!display.Present(framebuffer))
    {
        WriteSdlFailure(
            bring_up_log,
            "first framebuffer Present: failure");
        LogError("Failed to present the BroTracker framebuffer.");
        return 1;
    }

    WriteBringUpDiagnostic(
        bring_up_log,
        "first framebuffer Present: success");

    LogInfo("BroTracker framebuffer displayed.");

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

    const SDL_JoystickID controller_id = game_controller != nullptr
        ? SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(game_controller)) : -1;
    WriteBringUpDiagnostic(bring_up_log,
        "controls: mapped L1/B = START/RESTART; R1/X = STOP, then EXIT; keyboard/raw joystick/axes ignored");
    BringUpSerial serial(bring_up_log);
    bool quit_requested = false;
    bool stop_pending = false;
    bool exit_after_stop = false;
    bool restart_pending = false;
    Uint32 stop_started = 0;
    auto request_stop = [&](bool exit_after)
    {
        // SDL_QUIT may upgrade an outstanding STOP to STOP-then-exit.
        exit_after_stop = exit_after_stop || exit_after;
        if (stop_pending) return;
        if (!serial.Connected() || serial.Finished())
        {
            WriteBringUpDiagnostic(bring_up_log, "EXIT: waiting/ready/finished; no STOP sent");
            quit_requested = true;
        }
        else if (serial.Stop())
        {
            stop_pending = true;
            stop_started = SDL_GetTicks();
            WriteBringUpDiagnostic(bring_up_log, exit_after_stop
                ? "STOP queued; EXIT after acknowledgement"
                : "STOP queued; remain in application after acknowledgement");
        }
    };
    auto update_stop = [&]()
    {
        if (!stop_pending) return;
        if (serial.Stopped())
        {
            WriteBringUpDiagnostic(bring_up_log, exit_after_stop
                ? "STOP acknowledged; EXIT" : "STOP acknowledged; ready (R1/X now exits)");
            stop_pending = false;
            quit_requested = exit_after_stop;
        }
        else if (!serial.Connected() || (exit_after_stop && SDL_GetTicks() - stop_started >= 12500))
        {
            WriteBringUpDiagnostic(bring_up_log, exit_after_stop
                ? "STOP unconfirmed (disconnect/timeout); EXIT"
                : "STOP unconfirmed (disconnect/timeout); remaining in application");
            stop_pending = false;
            quit_requested = exit_after_stop;
        }
    };
    auto request_control = [&](BringUpAction action, const char* button)
    {
        const bool active = serial.Connected() && !serial.Finished();
        const char* requested = action == BringUpAction::Start
            ? (active ? "RESTART" : "START") : (active ? "STOP" : "EXIT");
        char diagnostic[160];
        std::snprintf(diagnostic, sizeof(diagnostic), "%s requested by mapped button %s", requested, button);
        WriteBringUpDiagnostic(bring_up_log, diagnostic);
        if (stop_pending)
        {
            WriteBringUpDiagnostic(bring_up_log, "action ignored: STOP acknowledgement pending");
            return;
        }
        if (action == BringUpAction::StopExit)
            request_stop(false);
        else if (!serial.Connected())
            WriteBringUpDiagnostic(bring_up_log, "START ignored: waiting for Teensy handshake");
        else if (serial.StartPending())
            WriteBringUpDiagnostic(bring_up_log, "START/RESTART ignored: START response pending");
        else if (serial.Start())
        {
            // Firmware START safely stops both players and resets to Test1.
            restart_pending = active;
            WriteBringUpDiagnostic(bring_up_log, active ? "RESTART queued via START" : "START queued");
        }
        else
            WriteBringUpDiagnostic(bring_up_log, "START/RESTART ignored: transport not ready");
    };

    WriteBringUpDiagnostic(
        bring_up_log,
        "entering event loop");

    while (!quit_requested)
    {
        serial.Tick(SDL_GetTicks());
        update_stop();
        SDL_Event event;

        while (!quit_requested && SDL_PollEvent(&event) != 0)
        {
            if (event.type == SDL_QUIT)
            {
                WriteBringUpDiagnostic(bring_up_log, "EXIT requested by SDL_QUIT");
                request_stop(true);
                break;
            }
            const BringUpAction action = MapBringUpAction(event, controller_id);
            if (action != BringUpAction::None)
            {
                const char* button = SDL_GameControllerGetStringForButton(
                    static_cast<SDL_GameControllerButton>(event.cbutton.button));
                request_control(action, button);
            }
        }

        // Temporary bring-up status overlay; leave the tracker layout alone.
        framebuffer.FilledRectangle(0, 436, SCREEN_WIDTH, 44, Color{16, 16, 16});
        DrawFixedText(framebuffer, 8, 440, serial.Status(), Color{255, 255, 255});
        DrawFixedText(framebuffer, 8, 456,
            stop_pending ? (exit_after_stop ? "STOP pending, then EXIT" : "STOP pending; please wait") :
            controller_id < 0 ? "Mapped SDL GameController required" :
            !serial.Connected() ? "Waiting for Teensy | R1 / X: EXIT" :
            serial.StartPending() ? (restart_pending ? "RESTART pending | R1 / X: STOP" : "START pending | R1 / X: STOP") :
            !serial.Finished() ? "L1 / B: RESTART | R1 / X: STOP (stay)" :
            "L1 / B: START | R1 / X: EXIT",
            Color{255, 255, 255});

        if (!quit_requested && !display.Present(framebuffer))
        {
            WriteSdlFailure(
                bring_up_log,
                "event-loop framebuffer Present: failure");
            request_stop(true);
        }

        SDL_Delay(16);
    }

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

    if (bring_up_log != nullptr)
        std::fclose(bring_up_log);

    return 0;
}

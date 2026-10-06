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
#include "ui/bringup_controls.h"

#include "core/constants.h"
#include "core/logger.h"
#include "core/tune_loader.h"
#include "renderer/framebuffer.h"
#include "ui/pattern_screen.h"
#include "ui/text_renderer.h"
#include "ui/sdl/sdl_display_backend.h"

namespace
{
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
        "BroTracker Terminal (BTX): application entry");

    LogInfo("BroTracker Terminal (BTX) starting on ArkOS.");

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
    BringUpControls controls(serial, bring_up_log);

    WriteBringUpDiagnostic(
        bring_up_log,
        "entering event loop");

    while (!controls.Quit())
    {
        controls.Tick(SDL_GetTicks());
        SDL_Event event;

        while (!controls.Quit() && SDL_PollEvent(&event) != 0)
        {
            if (event.type == SDL_QUIT)
            {
                WriteBringUpDiagnostic(bring_up_log, "EXIT requested by SDL_QUIT");
                controls.Request(BringUpAction::Exit, "SDL_QUIT/display error", SDL_GetTicks());
                break;
            }
            const BringUpAction action = MapBringUpAction(event, controller_id);
            if (action != BringUpAction::None)
            {
                const char* button = SDL_GameControllerGetStringForButton(
                    static_cast<SDL_GameControllerButton>(event.cbutton.button));
                char source[96];
                std::snprintf(source, sizeof(source), "mapped button %s", button);
                controls.Request(action, source, SDL_GetTicks());
            }
        }

        // Temporary bring-up status overlay; leave the tracker layout alone.
        framebuffer.FilledRectangle(0, 436, SCREEN_WIDTH, 44, Color{16, 16, 16});
        DrawFixedText(framebuffer, 8, 440, serial.Status(), Color{255, 255, 255});
        DrawFixedText(framebuffer, 8, 456,
            controls.StopPending() ? (controls.Exiting() ? "STOP pending, then EXIT" : "STOP pending; please wait") :
            controller_id < 0 ? "Mapped SDL GameController required" :
            !serial.Connected() ? "Waiting for Teensy | R1 / X: EXIT" :
            serial.StartPending() ? (controls.RestartPending() ? "RESTART pending | R1 / X: STOP" : "START pending | R1 / X: STOP") :
            !serial.Finished() ? "L1 / B: RESTART | R1 / X: STOP (stay)" :
            "L1 / B: START | R1 / X: EXIT",
            Color{255, 255, 255});

        if (!controls.Quit() && !display.Present(framebuffer))
        {
            WriteSdlFailure(
                bring_up_log,
                "event-loop framebuffer Present: failure");
            controls.Request(BringUpAction::Exit, "SDL_QUIT/display error", SDL_GetTicks());
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

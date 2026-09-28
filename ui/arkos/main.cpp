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
    FILE* bring_up_log =
        std::fopen(kBringUpLogPath, "w");

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
    SDL_Joystick* joystick = nullptr;
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
                joystick = SDL_JoystickOpen(device_index);

                if (joystick != nullptr)
                {
                    WriteBringUpDiagnostic(
                        bring_up_log,
                        "joystick open result: success, device index ",
                        device_index);
                    break;
                }

                WriteSdlFailure(
                    bring_up_log,
                    "joystick open result: failure");
            }
        }

        if (joystick_count == 0)
        {
            WriteBringUpDiagnostic(
                bring_up_log,
                "controller/joystick open result: no devices detected");
        }
        else if (game_controller == nullptr &&
                 joystick == nullptr)
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

    BringUpSerial serial(bring_up_log);
    bool quit_requested = false;
    bool start_requested = false;
    bool exit_pending = false;
    Uint32 exit_started = 0;
    auto request_exit = [&]()
    {
        if (exit_pending) return;
        WriteBringUpDiagnostic(bring_up_log, "exit requested");
        if (serial.Finished())
            quit_requested = true;
        else if (serial.Stop())
        {
            exit_pending = true;
            exit_started = SDL_GetTicks();
        }
        else
        {
            WriteBringUpDiagnostic(bring_up_log,
                "exiting disconnected: remote playback cannot be stopped");
            quit_requested = true;
        }
    };

    WriteBringUpDiagnostic(
        bring_up_log,
        "entering event loop");

    while (!quit_requested)
    {
        serial.Tick(SDL_GetTicks());
        if (start_requested && serial.StartCancelled() && !exit_pending)
        {
            // No START bytes reached the device. Allow a fresh first press
            // after reconnection rather than treating it as a stop/exit press.
            WriteBringUpDiagnostic(bring_up_log,
                "START was not sent; waiting for reconnect and a new press");
            start_requested = false;
        }
        if (exit_pending)
        {
            if (serial.Stopped())
            {
                WriteBringUpDiagnostic(bring_up_log, "STOP acknowledged; exiting");
                quit_requested = true;
            }
            else if (!serial.Connected() || SDL_GetTicks() - exit_started >= 12500)
            {
                WriteBringUpDiagnostic(bring_up_log,
                    "STOP unconfirmed (disconnect/timeout); exiting");
                quit_requested = true;
            }
        }
        SDL_Event event;

        while (!quit_requested && SDL_PollEvent(&event) != 0)
        {
            if (event.type == SDL_QUIT)
            {
                request_exit();
                break;
            }
            // SDL also emits joystick events for mapped controllers. Count
            // each physical press once, and ignore keyboard auto-repeat.
            const bool pressed =
                (event.type == SDL_KEYDOWN && event.key.repeat == 0) ||
                event.type == SDL_CONTROLLERBUTTONDOWN ||
                (event.type == SDL_JOYBUTTONDOWN && game_controller == nullptr);
            if (pressed && !exit_pending)
            {
                WriteBringUpDiagnostic(bring_up_log, "input press, SDL event type: ",
                    static_cast<int>(event.type));
                if (start_requested)
                    request_exit();
                else if (serial.Start())
                    start_requested = true;
                else
                    WriteBringUpDiagnostic(bring_up_log,
                        "press ignored: waiting for Teensy handshake");
            }
        }

        // Temporary bring-up status overlay; leave the tracker layout alone.
        framebuffer.FilledRectangle(0, 436, SCREEN_WIDTH, 44, Color{16, 16, 16});
        DrawFixedText(framebuffer, 8, 440, serial.Status(), Color{255, 255, 255});
        DrawFixedText(framebuffer, 8, 456,
            exit_pending ? "Waiting for STOP acknowledgement" :
            start_requested ? "Press again to stop and exit" :
            "USB TEST: first connected press starts WAV sequence",
            Color{255, 255, 255});

        if (!quit_requested && !display.Present(framebuffer))
        {
            WriteSdlFailure(
                bring_up_log,
                "event-loop framebuffer Present: failure");
            request_exit();
        }

        SDL_Delay(16);
    }

    if (game_controller != nullptr)
        SDL_GameControllerClose(game_controller);

    if (joystick != nullptr)
        SDL_JoystickClose(joystick);

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

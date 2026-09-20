/*
 * BroTracker
 *
 * Description: Minimal ArkOS/Linux UI display client.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include <SDL.h>

#include "core/constants.h"
#include "core/logger.h"
#include "core/tune_loader.h"
#include "renderer/framebuffer.h"
#include "ui/pattern_screen.h"
#include "ui/text_renderer.h"
#include "ui/sdl/sdl_display_backend.h"

int main(int, char*[])
{
    LogInfo("BroTracker ArkOS UI starting.");

    if (!LoadUiFont("assets/fonts/brotracker.btf"))
    {
        LogError("Failed to load BroTracker font.");
        return 1;
    }

    Tune tune = LoadTuneFromJson(
        "assets/dummy_my_tune.json");

    if (tune.patterns.empty())
    {
        LogError("No patterns available.");
        return 1;
    }

    Framebuffer framebuffer(
        SCREEN_WIDTH,
        SCREEN_HEIGHT);

    RenderMainScreen(
        framebuffer,
        tune,
        tune.patterns.front());

    SdlDisplayBackend display;

    if (!display.Initialize(
            framebuffer.Width(),
            framebuffer.Height()))
    {
        LogError("Failed to initialize the SDL display backend.");
        return 1;
    }

    if (!display.Present(framebuffer))
    {
        LogError("Failed to present the BroTracker framebuffer.");
        return 1;
    }

    LogInfo("BroTracker framebuffer displayed.");

    SDL_GameController* game_controller = nullptr;
    SDL_Joystick* joystick = nullptr;
    const bool input_subsystems_initialized =
        SDL_InitSubSystem(
            SDL_INIT_GAMECONTROLLER |
            SDL_INIT_JOYSTICK) == 0;

    if (input_subsystems_initialized)
    {
        for (int device_index = 0;
             device_index < SDL_NumJoysticks();
             ++device_index)
        {
            if (SDL_IsGameController(device_index))
            {
                game_controller =
                    SDL_GameControllerOpen(device_index);

                if (game_controller != nullptr)
                    break;
            }
            else
            {
                joystick = SDL_JoystickOpen(device_index);

                if (joystick != nullptr)
                    break;
            }
        }
    }

    bool quit_requested = false;

    while (!quit_requested)
    {
        SDL_Event event;

        while (SDL_PollEvent(&event) != 0)
        {
            if (event.type == SDL_QUIT ||
                event.type == SDL_KEYDOWN ||
                event.type == SDL_CONTROLLERBUTTONDOWN ||
                event.type == SDL_JOYBUTTONDOWN)
            {
                quit_requested = true;
                break;
            }
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

    return 0;
}

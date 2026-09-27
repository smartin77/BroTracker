/*
 * BroTracker
 *
 * Description: SDL2 framebuffer presentation backend.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#pragma once

#include <cstdint>

#include <ui/display_backend.h>

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Window;

class SdlDisplayBackend final : public DisplayBackend
{
public:
    SdlDisplayBackend() = default;
    ~SdlDisplayBackend() override;

    SdlDisplayBackend(const SdlDisplayBackend&) = delete;
    SdlDisplayBackend& operator=(const SdlDisplayBackend&) = delete;

    bool Initialize(
        std::uint32_t width,
        std::uint32_t height) override;

    bool Present(
        const Framebuffer& framebuffer) override;

private:
    void Shutdown();

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool video_initialized_ = false;
};

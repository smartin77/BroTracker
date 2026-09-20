/*
 * BroTracker
 *
 * Description: SDL2 framebuffer presentation backend.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include "sdl_display_backend.h"

#include <SDL.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <type_traits>

#include "renderer/color.h"
#include "renderer/framebuffer.h"

static_assert(
    std::is_standard_layout<Color>::value,
    "SDL RGB24 presentation requires a standard-layout Color");
static_assert(
    sizeof(Color) == 3,
    "SDL RGB24 presentation requires a tightly packed Color");
static_assert(
    offsetof(Color, r) == 0 &&
        offsetof(Color, g) == 1 &&
        offsetof(Color, b) == 2,
    "SDL RGB24 presentation requires Color bytes in RGB order");

namespace
{
    void LogSdlError(const char* operation)
    {
        std::cerr
            << "[ERROR] "
            << operation
            << ": "
            << SDL_GetError()
            << '\n';
    }
}

SdlDisplayBackend::~SdlDisplayBackend()
{
    Shutdown();
}

bool SdlDisplayBackend::Initialize(
    std::uint32_t width,
    std::uint32_t height)
{
    Shutdown();

    if (width == 0 || height == 0)
        return false;

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        LogSdlError("SDL video initialization failed");
        return false;
    }

    video_initialized_ = true;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    window_ = SDL_CreateWindow(
        "BroTracker",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        static_cast<int>(width),
        static_cast<int>(height),
        SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);

    if (window_ == nullptr)
    {
        LogSdlError("SDL window creation failed");
        Shutdown();
        return false;
    }

    renderer_ = SDL_CreateRenderer(
        window_,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if (renderer_ == nullptr)
    {
        renderer_ = SDL_CreateRenderer(
            window_,
            -1,
            SDL_RENDERER_SOFTWARE);
    }

    if (renderer_ == nullptr)
    {
        LogSdlError("SDL renderer creation failed");
        Shutdown();
        return false;
    }

    if (SDL_RenderSetLogicalSize(
            renderer_,
            static_cast<int>(width),
            static_cast<int>(height)) != 0)
    {
        LogSdlError("SDL logical size configuration failed");
        Shutdown();
        return false;
    }

    texture_ = SDL_CreateTexture(
        renderer_,
        SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STREAMING,
        static_cast<int>(width),
        static_cast<int>(height));

    if (texture_ == nullptr)
    {
        LogSdlError("SDL texture creation failed");
        Shutdown();
        return false;
    }

    width_ = width;
    height_ = height;
    return true;
}

bool SdlDisplayBackend::Present(
    const Framebuffer& framebuffer)
{
    if (renderer_ == nullptr ||
        texture_ == nullptr ||
        framebuffer.Width() != width_ ||
        framebuffer.Height() != height_)
    {
        return false;
    }

    void* texture_pixels = nullptr;
    int texture_pitch = 0;

    if (SDL_LockTexture(
            texture_,
            nullptr,
            &texture_pixels,
            &texture_pitch) != 0)
    {
        LogSdlError("SDL texture lock failed");
        return false;
    }

    const std::size_t row_bytes =
        static_cast<std::size_t>(width_) * sizeof(Color);

    if (texture_pitch < 0 ||
        static_cast<std::size_t>(texture_pitch) < row_bytes)
    {
        SDL_UnlockTexture(texture_);
        std::cerr << "[ERROR] SDL texture pitch is too small\n";
        return false;
    }

    const auto* source_pixels =
        reinterpret_cast<const std::uint8_t*>(
            framebuffer.PixelData());
    auto* destination_pixels =
        static_cast<std::uint8_t*>(texture_pixels);

    for (std::uint32_t y = 0; y < height_; ++y)
    {
        std::memcpy(
            destination_pixels +
                static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(texture_pitch),
            source_pixels +
                static_cast<std::size_t>(y) * row_bytes,
            row_bytes);
    }

    SDL_UnlockTexture(texture_);

    if (SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255) != 0 ||
        SDL_RenderClear(renderer_) != 0 ||
        SDL_RenderCopy(renderer_, texture_, nullptr, nullptr) != 0)
    {
        LogSdlError("SDL framebuffer presentation failed");
        return false;
    }

    SDL_RenderPresent(renderer_);
    return true;
}

void SdlDisplayBackend::Shutdown()
{
    if (texture_ != nullptr)
    {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }

    if (renderer_ != nullptr)
    {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }

    if (window_ != nullptr)
    {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }

    if (video_initialized_)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        video_initialized_ = false;
    }

    width_ = 0;
    height_ = 0;
}

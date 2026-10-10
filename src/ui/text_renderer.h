/*
 * BroTracker
 *
 * Description: Bitmap font text rendering helpers shared by all UI screens.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#pragma once

#include <string>

#include "renderer/color.h"
#include "renderer/framebuffer.h"

bool LoadUiFont(const char* filename);

// Word-wrap ASCII informational text within a half-open rectangle; split long words.
// Every glyph pixel is clipped to that rectangle. Returns occupied line height.
int DrawWrappedFixedText(Framebuffer& framebuffer, int x, int y,
    int width, int height, const std::string& text, Color color);

void DrawText(
    Framebuffer& framebuffer,
    int x,
    int y,
    const std::string& text,
    Color color);

// Draws text using a fixed six-pixel character cell per glyph.
void DrawFixedText(
    Framebuffer& framebuffer,
    int x,
    int y,
    const std::string& text,
    Color color);

void DrawCenteredFixedText(
    Framebuffer& framebuffer,
    int cell_x,
    int cell_width,
    int y,
    const std::string& text,
    Color color);

#pragma once

#include <stdint.h>

struct Glyph
{
    uint16_t codepoint;
    uint8_t bitmap[7];
};

class Font
{
public:

    bool Load(const char* filename);

    const Glyph* Find(uint16_t codepoint);

private:

    Glyph glyphs[256];
};
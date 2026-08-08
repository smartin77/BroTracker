/*
 * BroTracker
 *
 * Description: BroTracker BFM/BTF bitmap font loader.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Glyph
{
    std::uint16_t codepoint = 0;
    std::uint8_t bitmap[7] = {};
};

class Font
{
public:

    bool Load(const char* filename);

    const Glyph* Find(std::uint16_t codepoint) const;

private:

    bool LoadBTF(const std::string& filename);

    bool LoadBFM(const std::string& filename);

    std::vector<Glyph> glyphs;
};
/*
 * BroTracker
 *
 * Description: BroTracker BFM/BTF bitmap font loader.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include "font.h"

#include <fstream>
#include <sstream>
#include <string>

bool Font::Load(const char* filename)
{
    glyphs.clear();

    return LoadBTF(filename);
}

const Glyph* Font::Find(
    std::uint16_t codepoint) const
{
    for (const auto& glyph : glyphs)
    {
        if (glyph.codepoint == codepoint)
        {
            return &glyph;
        }
    }

    return nullptr;
}

bool Font::LoadBTF(
    const std::string& filename)
{
    std::ifstream file(filename);

    if (!file)
    {
        return false;
    }

    std::string line;
    std::string source;

    bool valid_header = false;

    while (std::getline(file, line))
    {
        if (line == "BTF1")
        {
            valid_header = true;
        }
        else if (line.rfind("SOURCE=", 0) == 0)
        {
            source = line.substr(7);
        }
    }

    if (!valid_header || source.empty())
    {
        return false;
    }

    // BTF source paths are relative to the BTF file.
    const std::size_t separator =
        filename.find_last_of("/\\");

    std::string directory;

    if (separator != std::string::npos)
    {
        directory =
            filename.substr(0, separator + 1);
    }

    return LoadBFM(
        directory + source);
}

bool Font::LoadBFM(
    const std::string& filename)
{
    std::ifstream file(filename);

    if (!file)
    {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    const std::string data =
        buffer.str();

    std::size_t position = 0;

    while (position < data.size())
    {
        if (data[position] != '"')
        {
            ++position;
            continue;
        }

        ++position;

        const std::size_t key_end =
            data.find('"', position);

        if (key_end == std::string::npos)
        {
            break;
        }

        const std::string key =
            data.substr(
                position,
                key_end - position);

        position = key_end + 1;

        if (position >= data.size() ||
            data[position] != ':')
        {
            continue;
        }

        ++position;

        if (key.empty())
        {
            continue;
        }

        const int codepoint =
            std::stoi(key);

        if (codepoint < 0 ||
            codepoint > 0xFFFF)
        {
            continue;
        }

        const std::size_t array_start =
            data.find('[', position);

        if (array_start == std::string::npos)
        {
            break;
        }

        const std::size_t array_end =
            data.find(']', array_start);

        if (array_end == std::string::npos)
        {
            break;
        }

        const std::string bitmap_data =
            data.substr(
                array_start + 1,
                array_end - array_start - 1);

        std::stringstream values(bitmap_data);

        Glyph glyph;
        glyph.codepoint =
            static_cast<std::uint16_t>(codepoint);

        std::string value;
        int index = 0;

        while (
            std::getline(values, value, ',') &&
            index < 16)
        {
            glyph.bitmap[index] =
                static_cast<std::uint8_t>(
                    std::stoi(value));

            ++index;
        }

        // The BFM data contains a 16-row bitmap.
        // BroTracker BTF defines a 7-row glyph.
        // The useful glyph rows are 5..11.
        Glyph cropped;
        cropped.codepoint =
            glyph.codepoint;

        for (int row = 0; row < 7; ++row)
        {
            cropped.bitmap[row] =
                glyph.bitmap[row + 5];
        }

        glyphs.push_back(cropped);

        position = array_end + 1;
    }

    return !glyphs.empty();
}
#include "note_formatter.h"

extern const char* NOTE_NAMES_SHARP[12];
extern const char* NOTE_NAMES_FLAT[12];

std::string FormatNote(
    Note note,
    AccidentalMode mode)
{
    if (note == NOTE_EMPTY)
    {
        return "---";
    }

    std::uint8_t octave = note / 12;
    std::uint8_t semitone = note % 12;

    const char* name;

    if (mode == AccidentalMode::Sharp)
    {
        name = NOTE_NAMES_SHARP[semitone];
    }
    else
    {
        name = NOTE_NAMES_FLAT[semitone];
    }

    return std::string(name) + std::to_string(octave);
}
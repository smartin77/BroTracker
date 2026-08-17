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

    if (note == NOTE_OFF)
    {
        return "OFF";
    }

    const std::uint8_t semitone = note % 12;

    // Yamaha display convention: MIDI note 60 = C3 (see D0018).
    const int octave = static_cast<int>(note / 12) - 2;

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
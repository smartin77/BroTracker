#pragma once

#include <string>

#include "note.h"

// Yamaha C-0..G-8 (raw 24..127), three-character natural/accidental text.
// Empty -> "---", NOTE_OFF -> "OFF", unsupported pitched values -> "???".
std::string FormatNote(
    Note note,
    AccidentalMode mode
);

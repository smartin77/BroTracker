#pragma once
#include <cstdint>
#include <string>

// User-visible decimal numbers are raw ID + 1. At least two characters,
// expanding to three for 100..255; never truncate or encode 100 as "00".
// The shared pattern field has room for all three characters. 0xFF is empty.
inline std::string FormatInstrument(std::uint8_t instrument) {
    if (instrument == 0xFF) return "--";
    const unsigned number = unsigned(instrument) + 1;
    return (number < 10 ? "0" : "") + std::to_string(number);
}

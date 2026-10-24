#pragma once

#include <cstddef>
#include "row_number_display.h"
#include "bringup/audio_output_selection.h"
#include "bringup/development_pattern_selection.h"
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace BroTracker {
// Host-only startup settings; unrelated to existing pattern/device dimensions.
enum class HostLogMode { Basic, Debug };
enum class ValueStepDirection { Normal, Reversed };
struct HostConfig {
    DevelopmentPattern startup_pattern;
    bool startup_pattern_invalid=false;
    bool bridge_audible_test = false;
    bool bridge_audible_test_invalid = false;
    RowNumbering row_numbering = RowNumbering::D1;
    bool row_numbering_invalid = false;
    unsigned midi_output_channel = 12;
    bool midi_output_invalid = false;
    AudioOutputChoice audio_output = AudioOutputChoice::Default;
    bool audio_output_invalid = false; // Warn, ignore this field, retain other settings.
    std::uint32_t schema_version = 1;
    std::uint32_t new_pattern_length = 32;
    HostLogMode logging_mode = HostLogMode::Basic;
    unsigned preserve_last = 0;
    ValueStepDirection value_step_direction = ValueStepDirection::Normal;
};
constexpr std::size_t kMaxHostConfigBytes = 4096;
enum class HostConfigStatus { Loaded, Missing, SafeDefaults, Invalid, TooLarge, Unreadable };
// Strict INI subset: [btx], schema_version and new_pattern_length are required
// exactly once. Optional [logging]: mode=basic/debug, preserve_last=0..20.
// Optional [editing]: value_step_direction=normal/reversed; deprecated alias
// note_step_direction is accepted, but supplying both names rejects.
// Optional [audio]: output=bridge/pt8211. Empty/absent uses firmware default.
// Invalid nonempty audio output warns and uses firmware default. Case-sensitive.
// Optional [midi]: output_channel=decimal 1..16; absent/empty uses 12.
// Invalid nonempty MIDI channel warns once at startup and uses 12.
// Optional [diagnostics]: bridge_audible_test=yes/on/true/1 or no/off/false/0.
// Missing/empty defaults disabled; invalid warns and disables this field only.
// Linux bridge only; Windows recognizes configuration but does not record.
// Optional [display]: row_numbering=d1/d0/h1/h0; absent/empty uses d1.
// Invalid nonempty display mode warns once and uses d1.
// Optional [development]: startup_pattern is a 1..24-byte ASCII alnum/_/-
// filename stem, optional .btdev, or exact reserved none. No quoted values. Missing/empty/invalid =>
// clean empty32x8/125.00 (invalid warns). Append .btdev under the fixed SD directory.
// Content determines dimensions/tempo; startup once, stopped, no seed/START/replay.
// Whole-line #/; comments and ASCII whitespace are allowed.
// Unknown settings/sections, unsupported versions and nondecimal values reject.
// Every failure preserves output. No allocation; work bounded by 4096 bytes.
HostConfigStatus ParseHostConfig(std::string_view text, HostConfig& output) noexcept;
bool UseSafeConfig(int argc, char* const argv[]) noexcept;
struct HostConfigResolution {
    HostConfig config{};
    HostConfigStatus status = HostConfigStatus::Missing;
};
// Read at most limit+1 bytes; never write/delete config. Safe mode does no I/O.
// This host-only non-realtime loader always returns usable built-in defaults
// when configuration cannot be loaded. Callers supply an absolute path.
// Pass the installed executable directory (SDL_GetBasePath), never argv[0]/CWD.
// Failure to resolve an absolute base yields an unreadable path, with no fallback.
std::filesystem::path ExecutableConfigPath(const std::filesystem::path& directory);
HostConfigResolution LoadHostConfig(const std::filesystem::path& path, bool safe) noexcept;
const char* HostConfigDiagnostic(HostConfigStatus status) noexcept;
} // namespace BroTracker

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace BroTracker {
// Host-only startup settings; unrelated to existing pattern/device dimensions.
enum class HostLogMode { Basic, Debug };
struct HostConfig {
    std::uint32_t schema_version = 1;
    std::uint32_t new_pattern_length = 32;
    HostLogMode logging_mode = HostLogMode::Basic;
    unsigned preserve_last = 0;
};
constexpr std::size_t kMaxHostConfigBytes = 4096;
enum class HostConfigStatus { Loaded, Missing, SafeDefaults, Invalid, TooLarge, Unreadable };
// Strict INI subset: [btx], schema_version and new_pattern_length are required
// exactly once. Optional [logging]: mode=basic/debug, preserve_last=0..20.
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
HostConfigResolution LoadHostConfig(const std::filesystem::path& path, bool safe) noexcept;
const char* HostConfigDiagnostic(HostConfigStatus status) noexcept;
} // namespace BroTracker

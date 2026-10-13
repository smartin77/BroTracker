#include "host_config.h"
#include <array>
#include <fstream>
#include <system_error>

namespace BroTracker {
namespace {
std::string_view Trim(std::string_view s) noexcept {
    const auto first = s.find_first_not_of(" \t\r");
    if (first == s.npos) return {};
    const auto last = s.find_last_not_of(" \t\r");
    return s.substr(first, last - first + 1);
}
bool Decimal(std::string_view s, std::uint32_t& value) noexcept {
    if (s.empty()) return false;
    std::uint32_t n = 0;
    for (const auto ch : s) {
        if (ch < '0' || ch > '9') return false;
        const auto digit = static_cast<unsigned>(ch - '0');
        if (n > (UINT32_MAX - digit) / 10) return false;
        n = n * 10 + digit;
    }
    value = n; return true;
}
}
HostConfigStatus ParseHostConfig(std::string_view text, HostConfig& output) noexcept {
    if (text.size() > kMaxHostConfigBytes) return HostConfigStatus::TooLarge;
    HostConfig candidate;
    bool section = false, version = false, length = false;
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = Trim(text.substr(0, end));
        text = end == text.npos ? std::string_view{} : text.substr(end + 1);
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        if (line == "[btx]") {
            if (section) return HostConfigStatus::Invalid;
            section = true; continue;
        }
        if (!section) return HostConfigStatus::Invalid;
        const auto eq = line.find('=');
        if (eq == line.npos) return HostConfigStatus::Invalid;
        const auto key = Trim(line.substr(0, eq));
        std::uint32_t value;
        if (!Decimal(Trim(line.substr(eq + 1)), value)) return HostConfigStatus::Invalid;
        if (key == "schema_version" && !version) {
            if (value != 1) return HostConfigStatus::Invalid;
            version = true; candidate.schema_version = value;
        } else if (key == "new_pattern_length" && !length) {
            if (value != 16 && value != 32 && value != 64) return HostConfigStatus::Invalid;
            length = true; candidate.new_pattern_length = value;
        } else return HostConfigStatus::Invalid;
    }
    if (!section || !version || !length) return HostConfigStatus::Invalid;
    output = candidate; return HostConfigStatus::Loaded;
}
bool UseSafeConfig(int argc, char* const argv[]) noexcept {
    for (int i = 1; argv && i < argc; ++i)
        if (argv[i] && std::string_view(argv[i]) == "--safe-config") return true;
    return false;
}
HostConfigResolution LoadHostConfig(const std::filesystem::path& path, bool safe) noexcept {
    HostConfigResolution result;
    if (safe) { result.status = HostConfigStatus::SafeDefaults; return result; }
    try {
        std::error_code error;
        if (!path.is_absolute()) { result.status = HostConfigStatus::Unreadable; return result; }
        const bool exists = std::filesystem::exists(path, error);
        if (error) { result.status = HostConfigStatus::Unreadable; return result; }
        if (!exists) return result;
        if (!std::filesystem::is_regular_file(path, error) || error) {
            result.status = HostConfigStatus::Unreadable; return result;
        }
        std::ifstream file(path, std::ios::binary);
        if (!file) { result.status = HostConfigStatus::Unreadable; return result; }
        std::array<char, kMaxHostConfigBytes + 1> buffer{};
        file.read(buffer.data(), buffer.size());
        if (file.bad() || (!file.eof() && file.fail())) {
            result.status = HostConfigStatus::Unreadable; return result;
        }
        result.status = ParseHostConfig({buffer.data(), static_cast<std::size_t>(file.gcount())}, result.config);
    } catch (...) { result = {}; result.status = HostConfigStatus::Unreadable; }
    return result;
}
const char* HostConfigDiagnostic(HostConfigStatus status) noexcept {
    switch (status) {
    case HostConfigStatus::Loaded: return "config.ini loaded";
    case HostConfigStatus::Missing: return "config.ini absent; using built-in defaults";
    case HostConfigStatus::SafeDefaults: return "--safe-config: external config bypassed; using built-in defaults";
    case HostConfigStatus::TooLarge: return "config.ini exceeds 4096 bytes; using defaults. Reduce the file or use --safe-config";
    case HostConfigStatus::Invalid: return "Invalid config.ini; using defaults. Check [btx], schema_version=1 and new_pattern_length=16/32/64, or use --safe-config";
    default: return "Cannot read config.ini; using defaults. Check path/permissions or use --safe-config";
    }
}
} // namespace BroTracker

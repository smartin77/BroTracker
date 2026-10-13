#pragma once
#include "host_config.h"
#include <cstdio>
#include <cstdint>
#include <string_view>
namespace BroTracker {
enum class HostLogLevel { Basic, Debug };
void ConfigureHostLogging(HostLogMode mode) noexcept;
bool HostDebugLogging() noexcept;
HostLogLevel ClassifyHostDiagnostic(std::string_view source, std::string_view message) noexcept;
// Host observation only, outside audio processing. Serialized for host workers.
void HostLog(FILE* file, HostLogLevel level, const char* source, const char* message) noexcept;
void HostLogf(FILE* file, HostLogLevel level, const char* source, const char* format, ...) noexcept;
// UTC epoch milliseconds, explicit signed timezone offset; testable independently.
bool FormatHostTimestamp(std::int64_t epoch_ms, int offset_minutes, std::uint64_t mono_ms,
    char* output, std::size_t capacity) noexcept;
struct HostLogOpenPlan { bool append=false; const char* diagnostic="Current log replaced; rotation disabled"; bool use_stderr=false; };
// Debug-only startup rotation. Failure preserves current log by appending.
// Only exact name(1)..name(20) archives are inspected; symlinks/directories reject.
HostLogOpenPlan PrepareHostLog(const std::filesystem::path& path, const HostConfig& config, bool append) noexcept;
void LogHostStartup(FILE* file, const char* platform, const HostConfigResolution& config,
    const std::filesystem::path& config_path, const HostLogOpenPlan& plan) noexcept;
void LogHostSystem(FILE* file) noexcept;
}

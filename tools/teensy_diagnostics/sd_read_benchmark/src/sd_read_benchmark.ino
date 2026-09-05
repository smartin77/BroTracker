/*
 * BroTracker
 *
 * Description: Teensy 4.1 SD read performance and WAV validation benchmark.
 *
 *              Tests the built-in SD card as documented in:
 *
 *                  tools/teensy_diagnostics/sd_read_benchmark/README.md
 *
 *              The benchmark:
 *
 *              - scans a configurable source directory
 *              - counts files, directories and other entries
 *              - identifies WAV candidates by extension only
 *              - processes candidates in configurable batches
 *              - validates WAV files before benchmarking
 *              - benchmarks only valid WAV files
 *              - measures full-file and chunked reads
 *              - records timing and throughput statistics
 *              - writes a new result file to the SD card in BroTracker/
 *              - preserves previous benchmark result files
 *              - flashes the onboard LED three times on successful completion
 *
 *              This is an experimental diagnostic tool.
 *              It is not the final BroTracker Sample Loader.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include <Arduino.h>
#include <SD.h>
#include <TimeLib.h>
#include <diagnostics.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// Forward declarations for global Arduino entry points
void setup();
void loop();

namespace
{
constexpr char kCompileDate[] = __DATE__;
constexpr char kCompileTime[] = __TIME__;

void SetCompileTimeClock()
{
    int month = 1;

    if (kCompileDate[0] == 'J')
    {
        month = (kCompileDate[1] == 'a')
            ? 1
            : (kCompileDate[2] == 'n' ? 6 : 7);
    }
    else if (kCompileDate[0] == 'F')
        month = 2;
    else if (kCompileDate[0] == 'M')
        month = (kCompileDate[2] == 'r') ? 3 : 5;
    else if (kCompileDate[0] == 'A')
        month = (kCompileDate[1] == 'p') ? 4 : 8;
    else if (kCompileDate[0] == 'S')
        month = 9;
    else if (kCompileDate[0] == 'O')
        month = 10;
    else if (kCompileDate[0] == 'N')
        month = 11;
    else if (kCompileDate[0] == 'D')
        month = 12;

    const int day =
        (kCompileDate[4] == ' ')
            ? kCompileDate[5] - '0'
            : (kCompileDate[4] - '0') * 10 +
              (kCompileDate[5] - '0');

    const int year =
        (kCompileDate[7] - '0') * 1000 +
        (kCompileDate[8] - '0') * 100 +
        (kCompileDate[9] - '0') * 10 +
        (kCompileDate[10] - '0');

    const int hour =
        (kCompileTime[0] - '0') * 10 +
        (kCompileTime[1] - '0');

    const int minute =
        (kCompileTime[3] - '0') * 10 +
        (kCompileTime[4] - '0');

    const int second =
        (kCompileTime[6] - '0') * 10 +
        (kCompileTime[7] - '0');

    setTime(
        hour,
        minute,
        second,
        day,
        month,
        year);

    Teensy3Clock.set(now());
}

void SdDateTimeCallback(
    uint16_t* date,
    uint16_t* time,
    uint8_t* ms10)
{
    const time_t current = now();

    *date = FS_DATE(
        year(current),
        month(current),
        day(current));

    *time = FS_TIME(
        hour(current),
        minute(current),
        second(current));

    *ms10 = (second(current) & 1) ? 100 : 0;
}
}

#include <cstring>

namespace
{
    // ---------------------------------------------------------------------
    // Configuration
    // ---------------------------------------------------------------------

    constexpr const char* BENCHMARK_SOURCE_PATH =
        "/Samples/Wav-HQ/DrumLoop/";

    constexpr std::uint32_t FILES_PER_BATCH = 31;
    constexpr std::uint32_t READ_LOOPS = 10;
    constexpr std::uint32_t READ_CHUNK_SIZE = 4096;

    // Benchmark is expected to run on every startup by default.
    // Enable this guard only if you want to suppress repeated runs of the
    // same firmware build during host-triggered resets.
    constexpr bool SKIP_DUPLICATE_BUILD_RUN = false;

    // Optional host clock synchronization over USB serial.
    // The host can send one of these lines right after connect:
    //   EPOCH:1735689600
    //   T1735689600
    constexpr std::uint32_t HOST_TIME_SYNC_TIMEOUT_MS = 1500;

    constexpr std::uint32_t LED_FLASH_MS = 750;
    constexpr std::uint32_t LED_PAUSE_MS = 750;

    // The batch is deliberately kept fixed-size.
    // No fixed limit exists for the total number of files in the source
    // directory. Batches are processed until the complete directory scan
    // has finished.
    struct Candidate
    {
        char path[256] = {};
    };

    struct WavInfo
    {
        std::uint16_t audio_format = 0;
        std::uint16_t channels = 0;
        std::uint32_t sample_rate = 0;
        std::uint16_t block_align = 0;
        std::uint16_t bits_per_sample = 0;

        std::uint32_t data_offset = 0;
        std::uint32_t data_size = 0;
    };

    enum class ValidationResult
    {
        Valid,
        OpenFailed,
        InvalidRiff,
        MissingWave,
        MissingFmt,
        MissingData,
        InvalidChunkStructure,
        UnsupportedCodec,
        UnsupportedBitDepth,
        UnsupportedSampleRate,
        TruncatedData
    };

    struct ReadStats
    {
        std::uint32_t open_min_us = UINT32_MAX;
        std::uint32_t open_max_us = 0;
        std::uint64_t open_total_us = 0;

        std::uint32_t close_min_us = UINT32_MAX;
        std::uint32_t close_max_us = 0;
        std::uint64_t close_total_us = 0;

        std::uint32_t full_min_us = UINT32_MAX;
        std::uint32_t full_max_us = 0;
        std::uint64_t full_total_us = 0;
        std::uint64_t full_total_bytes = 0;
        std::uint32_t full_failures = 0;

        std::uint32_t chunk_min_us = UINT32_MAX;
        std::uint32_t chunk_max_us = 0;
        std::uint64_t chunk_total_us = 0;
        std::uint64_t chunk_total_bytes = 0;
        std::uint32_t chunk_failures = 0;
    };

    File report_file;

    void* g_report_file_handle = nullptr;

    std::uint8_t read_buffer[READ_CHUNK_SIZE];

    // Scan statistics.
    std::uint32_t files_found = 0;
    std::uint32_t sample_candidates = 0;
    std::uint32_t directories_found = 0;
    std::uint32_t other_entries = 0;

    // Validation / benchmark statistics.
    std::uint32_t valid_wav = 0;
    std::uint32_t validation_failures = 0;
    std::uint32_t benchmarked_samples = 0;
    std::uint32_t full_read_failures = 0;
    std::uint32_t chunked_read_failures = 0;

    std::uint64_t aggregate_full_time_us = 0;
    std::uint64_t aggregate_full_bytes = 0;

    std::uint64_t aggregate_chunk_time_us = 0;
    std::uint64_t aggregate_chunk_bytes = 0;

    bool benchmark_ok = true;
    bool clock_synced_from_host = false;

    // ---------------------------------------------------------------------
    // Reporting
    // ---------------------------------------------------------------------

    void ReportPrint(const char* text)
    {
        Serial.print(text);

        if (g_report_file_handle)
            BroTracker::ToolLogMessage(g_report_file_handle, text);
    }

    void ReportPrint(const __FlashStringHelper* text)
    {
        Serial.print(text);

        if (g_report_file_handle)
        {
            char buffer[256];
            strncpy_P(buffer, (const char*)text, sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
            BroTracker::ToolLogMessage(g_report_file_handle, buffer);
        }
    }

    void ReportPrint(std::uint32_t value)
    {
        Serial.print(value);

        if (g_report_file_handle)
        {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)value);
            BroTracker::ToolLogMessage(g_report_file_handle, buffer);
        }
    }

    void ReportPrint(std::uint64_t value)
    {
        Serial.print(value);

        if (g_report_file_handle)
        {
            char buffer[64];
            uint32_t high = (uint32_t)(value >> 32);
            uint32_t low = (uint32_t)(value & 0xFFFFFFFF);
            if (high > 0)
                snprintf(buffer, sizeof(buffer), "%lu%08lu", (unsigned long)high, (unsigned long)low);
            else
                snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)low);
            BroTracker::ToolLogMessage(g_report_file_handle, buffer);
        }
    }

    void ReportPrintln(std::uint32_t value)
    {
        Serial.println(value);

        if (g_report_file_handle)
        {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)value);
            BroTracker::ToolLogMessage(g_report_file_handle, buffer);
        }
    }

    void ReportPrintln(std::uint64_t value)
    {
        Serial.println(value);

        if (g_report_file_handle)
        {
            char buffer[64];
            uint32_t high = (uint32_t)(value >> 32);
            uint32_t low = (uint32_t)(value & 0xFFFFFFFF);
            if (high > 0)
                snprintf(buffer, sizeof(buffer), "%lu%08lu", (unsigned long)high, (unsigned long)low);
            else
                snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)low);
            BroTracker::ToolLogMessage(g_report_file_handle, buffer);
        }
    }

    void ReportPrintln()
    {
        Serial.println();

        if (g_report_file_handle)
            BroTracker::ToolLogMessage(g_report_file_handle, "");
    }

    void ReportPrintln(const char* text)
    {
        Serial.println(text);

        if (g_report_file_handle)
            BroTracker::ToolLogMessage(g_report_file_handle, text);
    }

    void ReportPrintln(const __FlashStringHelper* text)
    {
        Serial.println(text);

        if (g_report_file_handle)
        {
            char buffer[256];
            strncpy_P(buffer, (const char*)text, sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
            BroTracker::ToolLogMessage(g_report_file_handle, buffer);
        }
    }

    void FlushReport()
    {
        // No-op with the new API; ToolLogMessage() flushes automatically
    }

    // ---------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------

    bool IsWavCandidate(const char* filename)
    {
        const char* dot = std::strrchr(filename, '.');

        if (dot == nullptr)
            return false;

        return std::strcmp(dot, ".wav") == 0 ||
               std::strcmp(dot, ".WAV") == 0;
    }

    std::uint16_t ReadLE16(const std::uint8_t* data)
    {
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(data[0]) |
            (static_cast<std::uint16_t>(data[1]) << 8));
    }

    std::uint32_t ReadLE32(const std::uint8_t* data)
    {
        return
            static_cast<std::uint32_t>(data[0]) |
            (static_cast<std::uint32_t>(data[1]) << 8) |
            (static_cast<std::uint32_t>(data[2]) << 16) |
            (static_cast<std::uint32_t>(data[3]) << 24);
    }

    bool SupportedSampleRate(std::uint32_t rate)
    {
        return
            rate == 8000 ||
            rate == 11025 ||
            rate == 22050 ||
            rate == 44100 ||
            rate == 48000;
    }

    bool SupportedBitDepth(std::uint16_t bits)
    {
        return bits == 8 || bits == 16;
    }

    const char* ValidationReason(ValidationResult result)
    {
        switch (result)
        {
            case ValidationResult::Valid:
                return "OK";

            case ValidationResult::OpenFailed:
                return "file open failed";

            case ValidationResult::InvalidRiff:
                return "invalid RIFF structure";

            case ValidationResult::MissingWave:
                return "missing WAVE identifier";

            case ValidationResult::MissingFmt:
                return "missing fmt chunk";

            case ValidationResult::MissingData:
                return "missing data chunk";

            case ValidationResult::InvalidChunkStructure:
                return "invalid RIFF chunk structure";

            case ValidationResult::UnsupportedCodec:
                return "unsupported codec";

            case ValidationResult::UnsupportedBitDepth:
                return "unsupported bit depth";

            case ValidationResult::UnsupportedSampleRate:
                return "unsupported sample rate";

            case ValidationResult::TruncatedData:
                return "truncated sample data";
        }

        return "unknown validation failure";
    }

    bool ReadExact(
        File& file,
        void* destination,
        std::size_t size)
    {
        auto* output =
            static_cast<std::uint8_t*>(destination);

        std::size_t remaining = size;

        while (remaining > 0)
        {
            const int bytes_read =
                file.read(output, remaining);

            if (bytes_read <= 0)
                return false;

            output += bytes_read;
            remaining -= static_cast<std::size_t>(bytes_read);
        }

        return true;
    }

    bool TryParseUnixEpoch(
        const char* text,
        std::uint32_t& epoch_out)
    {
        if (text == nullptr || text[0] == '\0')
            return false;

        const char* payload = text;

        if (std::strncmp(payload, "EPOCH:", 6) == 0)
            payload += 6;
        else if (payload[0] == 'T')
            payload += 1;

        if (*payload == '\0')
            return false;

        char* end = nullptr;

        const unsigned long parsed =
            std::strtoul(payload, &end, 10);

        if (end == payload || *end != '\0')
            return false;

        // Reject obviously invalid dates (before 2000-01-01 UTC).
        if (parsed < 946684800ul)
            return false;

        epoch_out = static_cast<std::uint32_t>(parsed);
        return true;
    }

    bool TrySyncClockFromHost(
        std::uint32_t timeout_ms)
    {
        if (!Serial)
            return false;

        char line[48] = {};
        std::size_t index = 0;

        const std::uint32_t start = millis();

        while (millis() - start < timeout_ms)
        {
            while (Serial.available() > 0)
            {
                const int raw = Serial.read();

                if (raw < 0)
                    continue;

                const char c = static_cast<char>(raw);

                if (c == '\r' || c == '\n')
                {
                    if (index == 0)
                        continue;

                    line[index] = '\0';

                    std::uint32_t epoch = 0;

                    if (TryParseUnixEpoch(line, epoch))
                    {
                        setTime(static_cast<time_t>(epoch));
                        Teensy3Clock.set(now());
                        return true;
                    }

                    index = 0;
                    continue;
                }

                if (index + 1 < sizeof(line))
                    line[index++] = c;
                else
                    index = 0;
            }
        }

        return false;
    }

    // ---------------------------------------------------------------------
    // WAV validation
    // ---------------------------------------------------------------------
    //
    // The parser walks RIFF chunks instead of assuming a fixed header.
    // This permits metadata chunks such as LIST/JUNK/INFO/bext/fact before
    // the data chunk.
    // ---------------------------------------------------------------------

    ValidationResult ValidateWav(
        const char* path,
        WavInfo& wav)
    {
        File file = SD.open(path, FILE_READ);

        if (!file)
            return ValidationResult::OpenFailed;

        const std::uint32_t file_size =
            static_cast<std::uint32_t>(file.size());

        if (file_size < 12)
        {
            file.close();
            return ValidationResult::InvalidRiff;
        }

        std::uint8_t riff_header[12];

        if (!ReadExact(
                file,
                riff_header,
                sizeof(riff_header)))
        {
            file.close();
            return ValidationResult::InvalidRiff;
        }

        if (std::memcmp(
                riff_header,
                "RIFF",
                4) != 0)
        {
            file.close();
            return ValidationResult::InvalidRiff;
        }

        if (std::memcmp(
                riff_header + 8,
                "WAVE",
                4) != 0)
        {
            file.close();
            return ValidationResult::MissingWave;
        }

        bool have_fmt = false;
        bool have_data = false;

        std::uint32_t cursor = 12;

        while (cursor + 8 <= file_size)
        {
            if (!file.seek(cursor))
            {
                file.close();
                return ValidationResult::InvalidChunkStructure;
            }

            std::uint8_t chunk_header[8];

            if (!ReadExact(
                    file,
                    chunk_header,
                    sizeof(chunk_header)))
            {
                file.close();
                return ValidationResult::InvalidChunkStructure;
            }

            const std::uint32_t chunk_size =
                ReadLE32(chunk_header + 4);

            const std::uint32_t payload_offset =
                cursor + 8;

            if (payload_offset > file_size ||
                chunk_size > file_size - payload_offset)
            {
                file.close();
                return ValidationResult::InvalidChunkStructure;
            }

            if (std::memcmp(
                    chunk_header,
                    "fmt ",
                    4) == 0)
            {
                if (chunk_size < 16)
                {
                    file.close();
                    return ValidationResult::InvalidChunkStructure;
                }

                std::uint8_t fmt[16];

                if (!file.seek(payload_offset) ||
                    !ReadExact(file, fmt, sizeof(fmt)))
                {
                    file.close();
                    return ValidationResult::InvalidChunkStructure;
                }

                wav.audio_format =
                    ReadLE16(fmt + 0);

                wav.channels =
                    ReadLE16(fmt + 2);

                wav.sample_rate =
                    ReadLE32(fmt + 4);

                wav.block_align =
                    ReadLE16(fmt + 12);

                wav.bits_per_sample =
                    ReadLE16(fmt + 14);

                have_fmt = true;
            }
            else if (
                std::memcmp(
                    chunk_header,
                    "data",
                    4) == 0)
            {
                if (!have_data)
                {
                    wav.data_offset = payload_offset;
                    wav.data_size = chunk_size;
                    have_data = true;
                }
            }

            const std::uint32_t padded_size =
                chunk_size + (chunk_size & 1u);

            if (padded_size >
                file_size - payload_offset)
            {
                file.close();
                return ValidationResult::InvalidChunkStructure;
            }

            cursor =
                payload_offset + padded_size;
        }

        file.close();

        if (!have_fmt)
            return ValidationResult::MissingFmt;

        if (!have_data)
            return ValidationResult::MissingData;

        if (wav.audio_format != 1 ||
            wav.channels == 0)
        {
            return ValidationResult::UnsupportedCodec;
        }

        if (!SupportedBitDepth(
                wav.bits_per_sample))
        {
            return ValidationResult::UnsupportedBitDepth;
        }

        if (!SupportedSampleRate(
                wav.sample_rate))
        {
            return ValidationResult::UnsupportedSampleRate;
        }

        if (wav.block_align == 0)
            return ValidationResult::InvalidChunkStructure;

        if (wav.data_offset > file_size ||
            wav.data_size > file_size - wav.data_offset)
        {
            return ValidationResult::TruncatedData;
        }

        return ValidationResult::Valid;
    }

    void PrintWavInfo(const WavInfo& wav)
    {
        ReportPrint(F("  Format: PCM, "));
        ReportPrint(static_cast<std::uint32_t>(wav.channels));
        ReportPrint(F(" channel(s), "));
        ReportPrint(wav.sample_rate);
        ReportPrint(F(" Hz, "));
        ReportPrint(static_cast<std::uint32_t>(
            wav.bits_per_sample));
        ReportPrintln(F(" bit"));

        ReportPrint(F("  PCM data: "));
        ReportPrint(wav.data_size);
        ReportPrint(F(" bytes @ "));
        ReportPrint(wav.data_offset);
        ReportPrintln();
    }

    // ---------------------------------------------------------------------
    // Read benchmark
    // ---------------------------------------------------------------------

    bool MeasureOpenClose(
        const char* path,
        ReadStats& stats)
    {
        const std::uint32_t open_start = micros();

        File file =
            SD.open(path, FILE_READ);

        const std::uint32_t open_end = micros();

        if (!file)
            return false;

        const std::uint32_t close_start = micros();

        file.close();

        const std::uint32_t close_end = micros();

        const std::uint32_t open_us =
            open_end - open_start;

        const std::uint32_t close_us =
            close_end - close_start;

        stats.open_total_us += open_us;

        if (open_us < stats.open_min_us)
            stats.open_min_us = open_us;

        if (open_us > stats.open_max_us)
            stats.open_max_us = open_us;

        stats.close_total_us += close_us;

        if (close_us < stats.close_min_us)
            stats.close_min_us = close_us;

        if (close_us > stats.close_max_us)
            stats.close_max_us = close_us;

        return true;
    }

    bool ReadFullFile(
        const char* path,
        const WavInfo& wav,
        std::uint32_t& elapsed_us,
        std::uint64_t& bytes_read)
    {
        File file =
            SD.open(path, FILE_READ);

        if (!file)
            return false;

        if (!file.seek(wav.data_offset))
        {
            file.close();
            return false;
        }

        bytes_read = 0;

        const std::uint32_t start =
            micros();

        while (bytes_read <
               wav.data_size)
        {
            const std::uint32_t remaining =
                wav.data_size -
                static_cast<std::uint32_t>(
                    bytes_read);

            const std::size_t request =
                remaining < READ_CHUNK_SIZE
                    ? remaining
                    : READ_CHUNK_SIZE;

            const int count =
                file.read(
                    read_buffer,
                    request);

            if (count <= 0)
            {
                file.close();
                return false;
            }

            bytes_read +=
                static_cast<std::uint32_t>(
                    count);
        }

        elapsed_us =
            micros() - start;

        file.close();

        return
            bytes_read == wav.data_size;
    }

    bool ReadChunkedFile(
        const char* path,
        const WavInfo& wav,
        std::uint32_t& elapsed_us,
        std::uint64_t& bytes_read)
    {
        File file =
            SD.open(path, FILE_READ);

        if (!file)
            return false;

        if (!file.seek(wav.data_offset))
        {
            file.close();
            return false;
        }

        bytes_read = 0;

        const std::uint32_t start =
            micros();

        while (bytes_read <
               wav.data_size)
        {
            const std::uint32_t remaining =
                wav.data_size -
                static_cast<std::uint32_t>(
                    bytes_read);

            const std::size_t request =
                remaining < READ_CHUNK_SIZE
                    ? remaining
                    : READ_CHUNK_SIZE;

            const int count =
                file.read(
                    read_buffer,
                    request);

            if (count <= 0)
            {
                file.close();
                return false;
            }

            bytes_read +=
                static_cast<std::uint32_t>(
                    count);
        }

        elapsed_us =
            micros() - start;

        file.close();

        return
            bytes_read == wav.data_size;
    }

    ReadStats BenchmarkFile(
        const char* path,
        const WavInfo& wav)
    {
        ReadStats stats;

        for (std::uint32_t loop = 0;
             loop < READ_LOOPS;
             ++loop)
        {
            if (!MeasureOpenClose(
                    path,
                    stats))
            {
                ++stats.full_failures;
                ++full_read_failures;

                continue;
            }

            std::uint32_t elapsed_us = 0;
            std::uint64_t bytes_read = 0;

            if (ReadFullFile(
                    path,
                    wav,
                    elapsed_us,
                    bytes_read))
            {
                stats.full_total_us +=
                    elapsed_us;

                stats.full_total_bytes +=
                    bytes_read;

                if (elapsed_us < stats.full_min_us)
                    stats.full_min_us = elapsed_us;

                if (elapsed_us > stats.full_max_us)
                    stats.full_max_us = elapsed_us;

                aggregate_full_time_us +=
                    elapsed_us;

                aggregate_full_bytes +=
                    bytes_read;
            }
            else
            {
                ++stats.full_failures;
                ++full_read_failures;
            }

            elapsed_us = 0;
            bytes_read = 0;

            if (ReadChunkedFile(
                    path,
                    wav,
                    elapsed_us,
                    bytes_read))
            {
                stats.chunk_total_us +=
                    elapsed_us;

                stats.chunk_total_bytes +=
                    bytes_read;

                if (elapsed_us < stats.chunk_min_us)
                    stats.chunk_min_us =
                        elapsed_us;

                if (elapsed_us > stats.chunk_max_us)
                    stats.chunk_max_us =
                        elapsed_us;

                aggregate_chunk_time_us +=
                    elapsed_us;

                aggregate_chunk_bytes +=
                    bytes_read;
            }
            else
            {
                ++stats.chunk_failures;
                ++chunked_read_failures;
            }
        }

        return stats;
    }

    std::uint64_t ThroughputBytesPerSecond(
        std::uint64_t bytes,
        std::uint64_t elapsed_us)
    {
        if (elapsed_us == 0)
            return 0;

        return
            (bytes * 1000000ull) /
            elapsed_us;
    }

    void PrintFileStats(
        const ReadStats& stats)
    {
        ReportPrint(F("  Open time average: "));

        if (READ_LOOPS != 0)
        {
            ReportPrint(
                stats.open_total_us /
                READ_LOOPS);
        }
        else
        {
            ReportPrint(static_cast<std::uint32_t>(0));
        }

        ReportPrintln(F(" us"));

        ReportPrint(F("  Close time average: "));

        if (READ_LOOPS != 0)
        {
            ReportPrint(
                stats.close_total_us /
                READ_LOOPS);
        }
        else
        {
            ReportPrint(static_cast<std::uint32_t>(0));
        }

        ReportPrintln(F(" us"));

        ReportPrint(F("  Full-file read min: "));

        if (stats.full_min_us == UINT32_MAX)
            ReportPrintln(F("N/A"));
        else
        {
            ReportPrint(stats.full_min_us);
            ReportPrintln(F(" us"));
        }

        ReportPrint(F("  Full-file read max: "));

        if (stats.full_max_us == 0)
            ReportPrintln(F("N/A"));
        else
        {
            ReportPrint(stats.full_max_us);
            ReportPrintln(F(" us"));
        }

        ReportPrint(F("  Full-file read avg: "));

        ReportPrint(
            READ_LOOPS != 0
                ? stats.full_total_us / READ_LOOPS
                : 0u);

        ReportPrintln(F(" us"));

        ReportPrint(F("  Full-file throughput: "));

        ReportPrint(
            ThroughputBytesPerSecond(
                stats.full_total_bytes,
                stats.full_total_us) /
            1024ull);

        ReportPrintln(F(" KiB/s"));

        ReportPrint(F("  Full-file failures: "));
        ReportPrintln(stats.full_failures);

        ReportPrint(F("  Chunked read min: "));

        if (stats.chunk_min_us == UINT32_MAX)
            ReportPrintln(F("N/A"));
        else
        {
            ReportPrint(stats.chunk_min_us);
            ReportPrintln(F(" us"));
        }

        ReportPrint(F("  Chunked read max: "));

        if (stats.chunk_max_us == 0)
            ReportPrintln(F("N/A"));
        else
        {
            ReportPrint(stats.chunk_max_us);
            ReportPrintln(F(" us"));
        }

        ReportPrint(F("  Chunked read avg: "));

        ReportPrint(
            READ_LOOPS != 0
                ? stats.chunk_total_us / READ_LOOPS
                : 0u);

        ReportPrintln(F(" us"));

        ReportPrint(F("  Chunked throughput: "));

        ReportPrint(
            ThroughputBytesPerSecond(
                stats.chunk_total_bytes,
                stats.chunk_total_us) /
            1024ull);

        ReportPrintln(F(" KiB/s"));

        ReportPrint(F("  Chunked failures: "));
        ReportPrintln(stats.chunk_failures);
    }

    // ---------------------------------------------------------------------
    // Batch processing
    // ---------------------------------------------------------------------

    void ProcessBatch(
        Candidate* batch,
        std::uint32_t batch_count,
        std::uint32_t batch_number)
    {
        ReportPrintln();
        ReportPrint(F("Batch "));
        ReportPrint(batch_number);
        ReportPrint(F(" ("));
        ReportPrint(batch_count);
        ReportPrintln(F(" candidates)"));

        std::uint32_t batch_valid = 0;
        std::uint32_t batch_invalid = 0;

        for (std::uint32_t i = 0;
             i < batch_count;
             ++i)
        {
            WavInfo wav;

            const ValidationResult result =
                ValidateWav(
                    batch[i].path,
                    wav);

            if (result !=
                ValidationResult::Valid)
            {
                ++batch_invalid;
                ++validation_failures;

                ReportPrint(F("  INVALID: "));
                ReportPrint(batch[i].path);
                ReportPrint(F(" - "));
                ReportPrint(
                    ValidationReason(result));

                // fmt chunk was parsed before these checks, so the
                // detected (unsupported) values can still be reported.
                if (result == ValidationResult::UnsupportedCodec ||
                    result == ValidationResult::UnsupportedBitDepth ||
                    result == ValidationResult::UnsupportedSampleRate)
                {
                    ReportPrint(F(" (audio_format="));
                    ReportPrint(static_cast<std::uint32_t>(
                        wav.audio_format));
                    ReportPrint(F(", "));
                    ReportPrint(static_cast<std::uint32_t>(
                        wav.bits_per_sample));
                    ReportPrint(F("-bit, "));
                    ReportPrint(wav.sample_rate);
                    ReportPrint(F(" Hz, "));
                    ReportPrint(static_cast<std::uint32_t>(
                        wav.channels));
                    ReportPrint(F("ch)"));
                }

                ReportPrintln();

                continue;
            }

            ++batch_valid;
            ++valid_wav;

            ReportPrint(F("  VALID: "));
            ReportPrintln(batch[i].path);

            PrintWavInfo(wav);

            const ReadStats stats =
                BenchmarkFile(
                    batch[i].path,
                    wav);

            ++benchmarked_samples;

            PrintFileStats(stats);

            if (stats.full_failures != 0 ||
                stats.chunk_failures != 0)
            {
                benchmark_ok = false;
            }
        }

        ReportPrint(F("Batch summary: valid="));
        ReportPrint(batch_valid);
        ReportPrint(F(", invalid="));
        ReportPrint(batch_invalid);
        ReportPrintln();

        FlushReport();
    }

    // ---------------------------------------------------------------------
    // Scan
    // ---------------------------------------------------------------------

    void ScanSourceDirectory()
    {
        File directory =
            SD.open(
                BENCHMARK_SOURCE_PATH,
                FILE_READ);

        if (!directory ||
            !directory.isDirectory())
        {
            ReportPrint(F(
                "ERROR: Source directory unavailable: "));

            ReportPrintln(
                BENCHMARK_SOURCE_PATH);

            benchmark_ok = false;
            return;
        }

        Candidate batch[FILES_PER_BATCH];

        std::uint32_t batch_count = 0;
        std::uint32_t batch_number = 1;

        while (true)
        {
            File entry =
                directory.openNextFile();

            if (!entry)
                break;

            const char* name =
                entry.name();

            if (entry.isDirectory())
            {
                ++directories_found;
            }
            else
            {
                ++files_found;

                if (IsWavCandidate(name))
                {
                    ++sample_candidates;

                    // entry.name() is only the bare filename, so the
                    // source directory must be prepended to obtain a
                    // path that SD.open() can actually resolve.
                    std::snprintf(
                        batch[batch_count].path,
                        sizeof(batch[batch_count].path),
                        "%s%s",
                        BENCHMARK_SOURCE_PATH,
                        name);

                    ++batch_count;

                    if (batch_count ==
                        FILES_PER_BATCH)
                    {
                        ProcessBatch(
                            batch,
                            batch_count,
                            batch_number++);

                        batch_count = 0;
                    }
                }
                else
                {
                    ++other_entries;
                }
            }

            entry.close();
        }

        directory.close();

        if (batch_count != 0)
        {
            ProcessBatch(
                batch,
                batch_count,
                batch_number);
        }
    }

    // ---------------------------------------------------------------------
    // Report filename
    // ---------------------------------------------------------------------
    //
    // Teensy 4.1 has no battery-backed RTC, so the report filename uses
    // firmware build date/time as required by the benchmark specification.
    //
    // Report setup using the reusable diagnostics API
    // Each benchmark run gets its own sequential log file.

    bool OpenReport()
    {
        void* file_handle = BroTracker::OpenToolLogFile("sd_read_benchmark");

        if (!file_handle)
            return false;

        g_report_file_handle = file_handle;

        ReportPrintln(
            F("BroTracker SD Read Benchmark"));

        ReportPrintln(
            F("============================"));

        ReportPrintln();

        ReportPrint(F("Build date: "));
        ReportPrintln(kCompileDate);

        ReportPrint(F("Build time: "));
        ReportPrintln(kCompileTime);

        ReportPrint(F("Clock source: "));

        if (clock_synced_from_host)
            ReportPrintln(F("host serial epoch"));
        else
            ReportPrintln(F("firmware compile time fallback"));

        ReportPrintln();

        ReportPrint(F("Source: "));
        ReportPrintln(
            BENCHMARK_SOURCE_PATH);

        ReportPrint(F("Output: "));
        ReportPrintln(
            F("BroTracker/sd_read_benchmark-NNNN.log"));

        ReportPrint(F("Files per batch: "));
        ReportPrintln(
            FILES_PER_BATCH);

        ReportPrint(F("Read loops: "));
        ReportPrintln(
            READ_LOOPS);

        ReportPrint(F("Chunk size: "));
        ReportPrintln(
            READ_CHUNK_SIZE);

        ReportPrintln();

        return true;
    }

    // ---------------------------------------------------------------------
    // Aggregate report
    // ---------------------------------------------------------------------

    void PrintAggregateResults()
    {
        ReportPrintln();
        ReportPrintln(F("Scan:"));

        ReportPrint(F("  Files found: "));
        ReportPrintln(files_found);

        ReportPrint(F("  Sample candidates: "));
        ReportPrintln(sample_candidates);

        ReportPrint(F("  Valid WAV: "));
        ReportPrintln(valid_wav);

        ReportPrint(F("  Invalid / unsupported: "));
        ReportPrintln(validation_failures);

        ReportPrint(F("  Directories: "));
        ReportPrintln(directories_found);

        ReportPrint(F("  Other entries: "));
        ReportPrintln(other_entries);

        ReportPrint(F("  Benchmarked samples: "));
        ReportPrintln(benchmarked_samples);

        ReportPrintln();

        ReportPrintln(
            F("Aggregate full-file read:"));

        ReportPrint(F("  Bytes: "));
        ReportPrintln(
            aggregate_full_bytes);

        ReportPrint(F("  Time: "));
        ReportPrint(
            aggregate_full_time_us);

        ReportPrintln(F(" us"));

        ReportPrint(F("  Throughput: "));
        ReportPrint(
            ThroughputBytesPerSecond(
                aggregate_full_bytes,
                aggregate_full_time_us) /
            1024ull);

        ReportPrintln(F(" KiB/s"));

        ReportPrintln();

        ReportPrintln(
            F("Aggregate chunked read:"));

        ReportPrint(F("  Bytes: "));
        ReportPrintln(
            aggregate_chunk_bytes);

        ReportPrint(F("  Time: "));
        ReportPrint(
            aggregate_chunk_time_us);

        ReportPrintln(F(" us"));

        ReportPrint(F("  Throughput: "));
        ReportPrint(
            ThroughputBytesPerSecond(
                aggregate_chunk_bytes,
                aggregate_chunk_time_us) /
            1024ull);

        ReportPrintln(F(" KiB/s"));

        ReportPrintln();

        ReportPrint(F("Full-file read failures: "));
        ReportPrintln(
            full_read_failures);

        ReportPrint(F("Chunked read failures: "));
        ReportPrintln(
            chunked_read_failures);
    }

    // ---------------------------------------------------------------------
    // LED completion
    // ---------------------------------------------------------------------

    void BlinkCompletion()
    {
        pinMode(
            LED_BUILTIN,
            OUTPUT);

        for (int i = 0; i < 3; ++i)
        {
            digitalWrite(
                LED_BUILTIN,
                HIGH);

            delay(
                LED_FLASH_MS);

            digitalWrite(
                LED_BUILTIN,
                LOW);

            delay(
                LED_PAUSE_MS);
        }
    }
}

// ==========================================================================
// Arduino
// ==========================================================================

void setup()
{
    Serial.begin(115200);

    while (!Serial &&
           millis() < 3000)
    {
        // Give the serial monitor time to attach.
    }

    pinMode(
        LED_BUILTIN,
        OUTPUT);

    digitalWrite(
        LED_BUILTIN,
        LOW);

    SetCompileTimeClock();

    clock_synced_from_host =
        TrySyncClockFromHost(HOST_TIME_SYNC_TIMEOUT_MS);

    Serial.println();
    Serial.println(
        "=== BroTracker SD Read Benchmark ===");
    Serial.println();

    if (clock_synced_from_host)
    {
        Serial.println(
            "Clock sync: HOST SERIAL EPOCH");
    }
    else
    {
        Serial.println(
            "Clock sync: COMPILE TIME FALLBACK");
    }

    if (!SD.begin(
            BUILTIN_SDCARD))
    {
        Serial.println(
            "SD initialization: FAIL");

        Serial.println(
            "Benchmark aborted.");

        return;
    }

    Serial.println(
        "SD initialization: PASS");

    FsDateTime::setCallback(SdDateTimeCallback);

    if (!OpenReport())
    {
        Serial.println(
            "Report file open: FAIL");

        Serial.println(
            "Benchmark aborted.");

        return;
    }

    ReportPrintln(
        F("Starting complete source scan..."));

    FlushReport();

    ScanSourceDirectory();

    PrintAggregateResults();

    if (benchmark_ok)
    {
        ReportPrintln();
        ReportPrintln(
            F("Read failures: 0 or no fatal read failures."));
        ReportPrintln(
            F("Write status: OK"));
        ReportPrintln();
        ReportPrintln(
            F("BENCHMARK: PASS"));

        FlushReport();
        report_file.close();

        Serial.println();
        Serial.println(
            "Benchmark complete.");

        Serial.println(
            "Three long LED flashes indicate completion.");

        BlinkCompletion();
    }
    else
    {
        ReportPrintln();
        ReportPrintln(
            F("BENCHMARK: FAIL"));

        FlushReport();
        report_file.close();

        Serial.println();
        Serial.println(
            "Benchmark failed.");

        Serial.println(
            "No completion LED sequence.");
    }
}

void loop()
{
    // Benchmark runs once from setup().
}

/*
 * BroTracker
 *
 * Description: Teensy 4.1 SD card read benchmark.
 *
 *              Tests SD card characteristics, filesystem properties,
 *              WAV discovery and sample read performance.
 *
 *              The benchmark is intended as an early storage test for
 *              the future BroTracker Sample Loader.
 *
 *              Results are written to the SD card and are preserved
 *              between benchmark runs.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include <Arduino.h>
#include <SD.h>

#include <cstdint>
#include <cstring>

namespace
{
    // ---------------------------------------------------------------------
    // Configuration
    // ---------------------------------------------------------------------

    constexpr const char* SOURCE_PATH =
        "/Samples/Wav-HQ/DrumLoop/";

    constexpr const char* OUTPUT_PATH =
        "/BT_benchmarks/";

    constexpr std::size_t FILES_PER_BATCH =
        31;

    constexpr std::size_t READ_LOOPS =
        10;

    constexpr std::size_t READ_CHUNK_SIZE =
        4096;

    constexpr unsigned long LED_LONG_MS =
        500;

    constexpr unsigned long LED_PAUSE_MS =
        300;

    constexpr unsigned long SERIAL_WAIT_MS =
        3000;

    constexpr std::size_t MAX_VALID_FILES =
        1024;

    constexpr std::size_t MAX_PATH_LENGTH =
        256;

    // ---------------------------------------------------------------------
    // Runtime data
    // ---------------------------------------------------------------------

    struct WavInfo
    {
        char path[MAX_PATH_LENGTH];

        uint32_t file_size = 0;
        uint32_t data_offset = 0;
        uint32_t data_size = 0;

        uint32_t sample_rate = 0;

        uint16_t audio_format = 0;
        uint16_t channels = 0;
        uint16_t bits_per_sample = 0;

        bool valid = false;
    };

    struct ScanStatistics
    {
        uint32_t files_found = 0;
        uint32_t valid_wav = 0;
        uint32_t invalid_or_unsupported = 0;
        uint32_t directories = 0;
        uint32_t other_entries = 0;
    };

    struct ReadStatistics
    {
        uint64_t total_bytes = 0;

        uint32_t loops = 0;
        uint32_t failures = 0;

        uint32_t min_us = UINT32_MAX;
        uint32_t max_us = 0;
        uint64_t total_us = 0;

        uint32_t min_throughput_kbps = UINT32_MAX;
        uint32_t max_throughput_kbps = 0;
        uint64_t total_throughput_kbps = 0;
    };

    File report_file;

    WavInfo valid_files[MAX_VALID_FILES];

    uint8_t read_buffer[READ_CHUNK_SIZE];

    ScanStatistics scan_stats;

    std::size_t valid_file_count = 0;

    char report_filename[64];

    // ---------------------------------------------------------------------
    // Reporting
    // ---------------------------------------------------------------------

    void ReportPrint(
        const char* text)
    {
        Serial.print(text);

        if (report_file)
        {
            report_file.print(text);
        }
    }

    void ReportPrint(
        uint32_t value)
    {
        Serial.print(value);

        if (report_file)
        {
            report_file.print(value);
        }
    }

    void ReportPrint(
        uint16_t value)
    {
        Serial.print(value);

        if (report_file)
        {
            report_file.print(value);
        }
    }

    void ReportPrint(
        uint64_t value)
    {
        Serial.print(value);

        if (report_file)
        {
            report_file.print(value);
        }
    }

    void ReportPrintln()
    {
        Serial.println();

        if (report_file)
        {
            report_file.println();
        }
    }

    void ReportPrintln(
        const char* text)
    {
        Serial.println(text);

        if (report_file)
        {
            report_file.println(text);
        }
    }

    void FlushReport()
    {
        if (report_file)
        {
            report_file.flush();
        }
    }

    // ---------------------------------------------------------------------
    // WAV helpers
    // ---------------------------------------------------------------------

    uint32_t ReadLE32(
        File& file)
    {
        uint8_t b[4];

        if (file.read(b, 4) != 4)
        {
            return 0;
        }

        return
            static_cast<uint32_t>(b[0]) |
            (static_cast<uint32_t>(b[1]) << 8) |
            (static_cast<uint32_t>(b[2]) << 16) |
            (static_cast<uint32_t>(b[3]) << 24);
    }

    uint16_t ReadLE16(
        File& file)
    {
        uint8_t b[2];

        if (file.read(b, 2) != 2)
        {
            return 0;
        }

        return
            static_cast<uint16_t>(b[0]) |
            (static_cast<uint16_t>(b[1]) << 8);
    }

    bool ReadFourCC(
        File& file,
        char* value)
    {
        if (file.read(
                reinterpret_cast<uint8_t*>(value),
                4) != 4)
        {
            return false;
        }

        value[4] = '\0';

        return true;
    }

    bool IsSupportedSampleRate(
        uint32_t sample_rate)
    {
        return
            sample_rate == 8000 ||
            sample_rate == 11025 ||
            sample_rate == 22050 ||
            sample_rate == 44100 ||
            sample_rate == 48000;
    }

    bool IsSupportedWav(
        const WavInfo& wav)
    {
        return
            wav.audio_format == 1 &&
            (wav.bits_per_sample == 8 ||
             wav.bits_per_sample == 16) &&
            IsSupportedSampleRate(
                wav.sample_rate);
    }

    bool ParseWav(
        const char* path,
        WavInfo& wav)
    {
        wav = WavInfo{};

        std::strncpy(
            wav.path,
            path,
            MAX_PATH_LENGTH - 1);

        wav.path[MAX_PATH_LENGTH - 1] =
            '\0';

        File file =
            SD.open(
                path,
                FILE_READ);

        if (!file)
        {
            return false;
        }

        wav.file_size =
            static_cast<uint32_t>(
                file.size());

        if (wav.file_size < 12)
        {
            file.close();
            return false;
        }

        char riff[5];
        char wave[5];

        if (!ReadFourCC(file, riff))
        {
            file.close();
            return false;
        }

        ReadLE32(file);

        if (!ReadFourCC(file, wave))
        {
            file.close();
            return false;
        }

        if (std::strcmp(riff, "RIFF") != 0 ||
            std::strcmp(wave, "WAVE") != 0)
        {
            file.close();
            return false;
        }

        bool found_fmt = false;
        bool found_data = false;

        while (file.position() + 8 <= file.size())
        {
            char chunk_id[5];

            if (!ReadFourCC(file, chunk_id))
            {
                break;
            }

            const uint32_t chunk_size =
                ReadLE32(file);

            const uint32_t chunk_data_offset =
                static_cast<uint32_t>(
                    file.position());

            if (std::strcmp(chunk_id, "fmt ") == 0)
            {
                if (chunk_size < 16)
                {
                    file.close();
                    return false;
                }

                wav.audio_format =
                    ReadLE16(file);

                wav.channels =
                    ReadLE16(file);

                wav.sample_rate =
                    ReadLE32(file);

                ReadLE32(file);
                ReadLE16(file);

                wav.bits_per_sample =
                    ReadLE16(file);

                found_fmt = true;
            }
            else if (
                std::strcmp(
                    chunk_id,
                    "data") == 0)
            {
                wav.data_offset =
                    chunk_data_offset;

                wav.data_size =
                    chunk_size;

                found_data = true;
            }

            uint32_t next_position =
                chunk_data_offset +
                chunk_size;

            if (chunk_size & 1u)
            {
                ++next_position;
            }

            if (next_position >
                static_cast<uint32_t>(
                    file.size()))
            {
                file.close();
                return false;
            }

            file.seek(next_position);

            if (found_fmt && found_data)
            {
                break;
            }
        }

        file.close();

        wav.valid =
            found_fmt &&
            found_data &&
            IsSupportedWav(wav);

        return wav.valid;
    }

    // ---------------------------------------------------------------------
    // Directory scan
    // ---------------------------------------------------------------------

    bool HasWavExtension(
        const char* name)
    {
        const std::size_t length =
            std::strlen(name);

        if (length < 4)
        {
            return false;
        }

        const char* extension =
            name + length - 4;

        return
            extension[0] == '.' &&
            (extension[1] == 'w' ||
             extension[1] == 'W') &&
            (extension[2] == 'a' ||
             extension[2] == 'A') &&
            (extension[3] == 'v' ||
             extension[3] == 'V');
    }

    void ScanSourceDirectory()
    {
        scan_stats =
            ScanStatistics{};

        valid_file_count = 0;

        File directory =
            SD.open(
                SOURCE_PATH,
                FILE_READ);

        if (!directory)
        {
            ReportPrintln(
                "ERROR: Source directory cannot be opened.");

            return;
        }

        if (!directory.isDirectory())
        {
            directory.close();

            ReportPrintln(
                "ERROR: Source path is not a directory.");

            return;
        }

        File entry;

        while (true)
        {
            entry =
                directory.openNextFile();

            if (!entry)
            {
                break;
            }

            if (entry.isDirectory())
            {
                ++scan_stats.directories;

                entry.close();
                continue;
            }

            ++scan_stats.files_found;

            const char* name =
                entry.name();

            const bool wav_extension =
                HasWavExtension(name);

            entry.close();

            if (!wav_extension)
            {
                ++scan_stats.other_entries;
                continue;
            }

            WavInfo wav;

            if (!ParseWav(
                    name,
                    wav))
            {
                ++scan_stats.invalid_or_unsupported;
                continue;
            }

            ++scan_stats.valid_wav;

            if (valid_file_count <
                MAX_VALID_FILES)
            {
                valid_files[valid_file_count++] =
                    wav;
            }
        }

        directory.close();
    }

    // ---------------------------------------------------------------------
    // Low-level card / filesystem information
    // ---------------------------------------------------------------------

    void WriteCardInformation()
    {
        ReportPrintln();
        ReportPrintln(
            "=== SD CARD / FILESYSTEM ===");

        ReportPrintln(
            "Interface: Teensy 4.1 built-in SDIO");

        ReportPrintln(
            "Library: Teensy SD.h / SdFat");

        ReportPrintln(
            "Card capacity: not available through SDClass API");

        ReportPrintln(
            "Low-level card identification: pending");

        ReportPrintln(
            "Filesystem geometry: pending");
    }

    // ---------------------------------------------------------------------
    // Read benchmark
    // ---------------------------------------------------------------------

    uint32_t CalculateThroughputKbps(
        uint64_t bytes,
        uint32_t microseconds)
    {
        if (microseconds == 0)
        {
            return 0;
        }

        return static_cast<uint32_t>(
            (bytes * 1000000ULL) /
            (static_cast<uint64_t>(
                microseconds) *
             1024ULL));
    }

    ReadStatistics BenchmarkFile(
        const WavInfo& wav)
    {
        ReadStatistics statistics;

        for (std::size_t loop = 0;
             loop < READ_LOOPS;
             ++loop)
        {
            File file =
                SD.open(
                    wav.path,
                    FILE_READ);

            if (!file)
            {
                ++statistics.failures;
                continue;
            }

            if (!file.seek(
                    wav.data_offset))
            {
                file.close();
                ++statistics.failures;
                continue;
            }

            uint32_t remaining =
                wav.data_size;

            uint64_t bytes_read =
                0;

            const uint32_t start_us =
                micros();

            while (remaining > 0)
            {
                const uint32_t requested =
                    remaining <
                            READ_CHUNK_SIZE
                        ? remaining
                        : READ_CHUNK_SIZE;

                const int result =
                    file.read(
                        read_buffer,
                        requested);

                if (result !=
                    static_cast<int>(
                        requested))
                {
                    ++statistics.failures;
                    break;
                }

                bytes_read +=
                    requested;

                remaining -=
                    requested;
            }

            const uint32_t elapsed_us =
                micros() - start_us;

            file.close();

            if (remaining != 0)
            {
                continue;
            }

            const uint32_t throughput =
                CalculateThroughputKbps(
                    bytes_read,
                    elapsed_us);

            statistics.total_bytes +=
                bytes_read;

            statistics.total_us +=
                elapsed_us;

            ++statistics.loops;

            if (elapsed_us <
                statistics.min_us)
            {
                statistics.min_us =
                    elapsed_us;
            }

            if (elapsed_us >
                statistics.max_us)
            {
                statistics.max_us =
                    elapsed_us;
            }

            if (throughput <
                statistics.min_throughput_kbps)
            {
                statistics.min_throughput_kbps =
                    throughput;
            }

            if (throughput >
                statistics.max_throughput_kbps)
            {
                statistics.max_throughput_kbps =
                    throughput;
            }

            statistics.total_throughput_kbps +=
                throughput;
        }

        return statistics;
    }

    void WriteReadStatistics(
        const ReadStatistics& statistics)
    {
        ReportPrint(
            "  Successful loops: ");

        ReportPrint(
            statistics.loops);

        ReportPrintln();

        ReportPrint(
            "  Failures: ");

        ReportPrint(
            statistics.failures);

        ReportPrintln();

        if (statistics.loops == 0)
        {
            return;
        }

        ReportPrint(
            "  Min read time: ");

        ReportPrint(
            statistics.min_us);

        ReportPrintln(
            " us");

        ReportPrint(
            "  Max read time: ");

        ReportPrint(
            statistics.max_us);

        ReportPrintln(
            " us");

        ReportPrint(
            "  Average read time: ");

        ReportPrint(
            statistics.total_us /
            statistics.loops);

        ReportPrintln(
            " us");

        ReportPrint(
            "  Min throughput: ");

        ReportPrint(
            statistics.min_throughput_kbps);

        ReportPrintln(
            " KiB/s");

        ReportPrint(
            "  Max throughput: ");

        ReportPrint(
            statistics.max_throughput_kbps);

        ReportPrintln(
            " KiB/s");

        ReportPrint(
            "  Average throughput: ");

        ReportPrint(
            statistics.total_throughput_kbps /
            statistics.loops);

        ReportPrintln(
            " KiB/s");
    }

    void BenchmarkBatch(
        std::size_t first_index,
        std::size_t count,
        std::size_t batch_number)
    {
        ReportPrintln();
        ReportPrintln(
            "========================================");

        ReportPrint(
            "Batch ");

        ReportPrint(
            static_cast<uint32_t>(
                batch_number));

        ReportPrintln();

        ReportPrintln(
            "========================================");

        ReportPrint(
            "Files: ");

        ReportPrint(
            static_cast<uint32_t>(
                count));

        ReportPrintln();

        for (std::size_t index = 0;
             index < count;
             ++index)
        {
            const WavInfo& wav =
                valid_files[
                    first_index + index];

            ReportPrint(
                "[");

            ReportPrint(
                static_cast<uint32_t>(
                    index + 1));

            ReportPrint(
                "/");

            ReportPrint(
                static_cast<uint32_t>(
                    count));

            ReportPrint(
                "] ");

            ReportPrintln(
                wav.path);

            ReportPrint(
                "  Size: ");

            ReportPrint(
                wav.file_size);

            ReportPrintln(
                " bytes");

            ReportPrint(
                "  PCM data: ");

            ReportPrint(
                wav.data_size);

            ReportPrintln(
                " bytes");

            ReportPrint(
                "  Format: ");

            ReportPrint(
                wav.sample_rate);

            ReportPrint(
                " Hz / ");

            ReportPrint(
                wav.bits_per_sample);

            ReportPrint(
                " bit / ");

            ReportPrint(
                wav.channels);

            ReportPrintln(
                " ch");

            const ReadStatistics statistics =
                BenchmarkFile(wav);

            WriteReadStatistics(
                statistics);

            FlushReport();
        }
    }

    // ---------------------------------------------------------------------
    // Report filename
    // ---------------------------------------------------------------------

    void CreateReportFilename()
    {
        snprintf(
            report_filename,
            sizeof(report_filename),
            "%sSD_BENCH_%s_%s.txt",
            OUTPUT_PATH,
            __DATE__,
            __TIME__);

        for (char* p = report_filename;
             *p != '\0';
             ++p)
        {
            if (*p == ' ')
            {
                *p = '_';
            }

            if (*p == ':')
            {
                *p = '-';
            }
        }
    }

    // ---------------------------------------------------------------------
    // LED completion signal
    // ---------------------------------------------------------------------

    void BlinkCompletion()
    {
        for (int index = 0;
             index < 3;
             ++index)
        {
            digitalWrite(
                LED_BUILTIN,
                HIGH);

            delay(
                LED_LONG_MS);

            digitalWrite(
                LED_BUILTIN,
                LOW);

            delay(
                LED_PAUSE_MS);
        }
    }
}

void setup()
{
    pinMode(
        LED_BUILTIN,
        OUTPUT);

    digitalWrite(
        LED_BUILTIN,
        LOW);

    Serial.begin(115200);

    const uint32_t serial_start =
        millis();

    while (!Serial &&
           millis() - serial_start <
               SERIAL_WAIT_MS)
    {
    }

    Serial.println();
    Serial.println(
        "========================================");
    Serial.println(
        "BroTracker SD Read Benchmark");
    Serial.println(
        "========================================");
    Serial.println();

    Serial.print(
        "Build date: ");

    Serial.println(
        __DATE__);

    Serial.print(
        "Build time: ");

    Serial.println(
        __TIME__);

    Serial.println();

    Serial.println(
        "Initializing built-in SD card...");

    if (!SD.begin(BUILTIN_SDCARD))
    {
        Serial.println(
            "SD initialization: FAIL");

        Serial.println(
            "Benchmark aborted.");

        return;
    }

    Serial.println(
        "SD initialization: PASS");

    if (!SD.exists(OUTPUT_PATH))
    {
        Serial.println(
            "Creating benchmark output directory...");

        if (!SD.mkdir(OUTPUT_PATH))
        {
            Serial.println(
                "Output directory creation: FAIL");

            Serial.println(
                "Benchmark aborted.");

            return;
        }
    }

    Serial.println(
        "Output directory: PASS");

    CreateReportFilename();

    report_file =
        SD.open(
            report_filename,
            FILE_WRITE);

    if (!report_file)
    {
        Serial.println(
            "Benchmark report creation: FAIL");

        Serial.println(
            "Benchmark aborted.");

        return;
    }

    ReportPrintln(
        "========================================");
    ReportPrintln(
        "BroTracker SD Read Benchmark");
    ReportPrintln(
        "========================================");

    ReportPrint(
        "Build date: ");
    ReportPrintln(
        __DATE__);

    ReportPrint(
        "Build time: ");
    ReportPrintln(
        __TIME__);

    ReportPrintln();

    ReportPrint(
        "Source path: ");
    ReportPrintln(
        SOURCE_PATH);

    ReportPrint(
        "Output path: ");
    ReportPrintln(
        OUTPUT_PATH);

    ReportPrint(
        "Files per batch: ");
    ReportPrint(
        static_cast<uint32_t>(
            FILES_PER_BATCH));
    ReportPrintln();

    ReportPrint(
        "Read loops: ");
    ReportPrint(
        static_cast<uint32_t>(
            READ_LOOPS));
    ReportPrintln();

    ReportPrint(
        "Read chunk size: ");
    ReportPrint(
        static_cast<uint32_t>(
            READ_CHUNK_SIZE));
    ReportPrintln(
        " bytes");

    WriteCardInformation();

    ReportPrintln();
    ReportPrintln(
        "=== SOURCE DIRECTORY SCAN ===");

    ScanSourceDirectory();

    ReportPrint(
        "Files found: ");
    ReportPrint(
        scan_stats.files_found);
    ReportPrintln();

    ReportPrint(
        "Valid WAV: ");
    ReportPrint(
        scan_stats.valid_wav);
    ReportPrintln();

    ReportPrint(
        "Invalid / unsupported: ");
    ReportPrint(
        scan_stats.invalid_or_unsupported);
    ReportPrintln();

    ReportPrint(
        "Directories: ");
    ReportPrint(
        scan_stats.directories);
    ReportPrintln();

    ReportPrint(
        "Other entries: ");
    ReportPrint(
        scan_stats.other_entries);
    ReportPrintln();

    if (valid_file_count == 0)
    {
        ReportPrintln();
        ReportPrintln(
            "No valid WAV files found.");

        ReportPrintln(
            "Benchmark aborted.");

        report_file.flush();
        report_file.close();

        BlinkCompletion();

        return;
    }

    ReportPrintln();
    ReportPrintln(
        "=== READ BENCHMARK ===");

    std::size_t first_index = 0;
    std::size_t batch_number = 1;

    while (first_index < valid_file_count)
    {
        const std::size_t remaining =
            valid_file_count -
            first_index;

        const std::size_t batch_count =
            remaining <
                    FILES_PER_BATCH
                ? remaining
                : FILES_PER_BATCH;

        BenchmarkBatch(
            first_index,
            batch_count,
            batch_number);

        first_index +=
            batch_count;

        ++batch_number;
    }

    ReportPrintln();
    ReportPrintln(
        "========================================");
    ReportPrintln(
        "Benchmark complete");
    ReportPrintln(
        "========================================");

    ReportPrint(
        "Report: ");
    ReportPrintln(
        report_filename);

    FlushReport();
    report_file.close();

    Serial.println();
    Serial.println(
        "Benchmark complete.");

    Serial.print(
        "Report: ");

    Serial.println(
        report_filename);

    Serial.println(
        "Three long LED flashes indicate completion.");

    BlinkCompletion();
}

void loop()
{
    // Benchmark runs once from setup().
}

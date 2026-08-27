/*
 * BroTracker
 *
 * SD Read Benchmark
 *
 * Purpose:
 *
 *   Measure SD-card read performance on Teensy 4.1 while treating the
 *   sample directory as a future BroTracker Instrument/Sample browser.
 *
 * Design:
 *
 *   - The directory is scanned completely.
 *   - The number of supported sample files is not limited by a fixed
 *     dataset-size constant.
 *   - Currently, PCM RIFF/WAVE files are the supported sample format.
 *   - Browser pagination is limited only by the number of rows displayed
 *     on the UI. This is currently 31 rows and is intended to become a
 *     UI configuration value later.
 *   - Sample metadata is loaded only for the requested browser page.
 *   - Benchmark tests rescan the directory instead of keeping every sample
 *     path in RAM.
 *
 * Dataset:
 *
 *   Samples/Wav-HQ/DrumLoop/
 *
 * Tests:
 *
 *   A. Single-file reads
 *      Read every valid sample once.
 *
 *   B. Sequential batch reads
 *      Read 4, 8, 16, 32 and 64 valid samples sequentially.
 *
 *   C. Repeated batch reads
 *      Repeat the 16-sample sequential read three times.
 *
 *   D. Interleaved reads
 *      Read 2, 4 and 8 samples in alternating 1024-byte chunks.
 *
 * Goals:
 *
 *   - Establish baseline SD read performance on Teensy 4.1.
 *   - Measure many-small-file access rather than only large-file throughput.
 *   - Measure storage behaviour relevant to future multi-sample streaming.
 *   - Validate that all supported files in a directory can be discovered.
 *   - Keep browser pagination independent from the number of samples.
 *
 * This benchmark writes a report to BroTracker_SD_Test.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include <Arduino.h>
#include <SD.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{
    constexpr const char* SAMPLE_DIRECTORY =
        "Samples/Wav-HQ/DrumLoop";

    constexpr const char* REPORT_FILE =
        "BroTracker_SD_Test/sd_read_benchmark.txt";

    constexpr const char* BUILD_DATE =
        __DATE__;

    constexpr const char* BUILD_TIME =
        __TIME__;

    /*
     * UI/browser configuration.
     *
     * This is NOT a limit on the number of samples in a directory.
     * It will later be supplied by the actual UI configuration.
     */
    constexpr std::size_t BROWSER_PAGE_ROWS =
        31;

    constexpr std::size_t READ_BUFFER_SIZE =
        4096;

    constexpr std::size_t INTERLEAVED_CHUNK_SIZE =
        1024;

    constexpr std::size_t REPEATED_RUNS =
        3;

    constexpr std::size_t BATCH_SIZES[] =
    {
        4,
        8,
        16,
        32,
        64
    };

    constexpr std::size_t BATCH_SIZE_COUNT =
        sizeof(BATCH_SIZES) /
        sizeof(BATCH_SIZES[0]);

    constexpr std::size_t INTERLEAVED_STREAMS[] =
    {
        2,
        4,
        8
    };

    constexpr std::size_t INTERLEAVED_STREAM_COUNT =
        sizeof(INTERLEAVED_STREAMS) /
        sizeof(INTERLEAVED_STREAMS[0]);

    uint8_t read_buffer[READ_BUFFER_SIZE];

    struct WavInfo
    {
        uint32_t data_offset = 0;
        uint32_t data_size = 0;

        uint16_t channels = 0;
        uint16_t bits_per_sample = 0;

        uint32_t sample_rate = 0;

        bool valid = false;
    };

    struct SampleFile
    {
        char path[128] = {};

        WavInfo wav;

        uint32_t file_size = 0;

        bool valid = false;
    };

    /*
     * Result of scanning a directory.
     *
     * total_entries:
     *     Every non-directory filesystem entry.
     *
     * supported_samples:
     *     Files accepted by the current sample-format validators.
     *
     * invalid_wav:
     *     WAV candidates which failed the current WAV validator.
     *
     * The benchmark does not store all sample paths in RAM.
     */
    struct DiscoverySummary
    {
        uint32_t total_entries = 0;
        uint32_t wav_candidates = 0;
        uint32_t supported_samples = 0;
        uint32_t invalid_wav = 0;
    };

    /*
     * A browser page is deliberately bounded by the UI page size.
     * This is the only fixed sample-list storage used by the browser model.
     */
    struct SampleBrowserPage
    {
        uint32_t page_index = 0;
        uint32_t total_samples = 0;
        uint32_t total_pages = 0;

        std::size_t count = 0;

        SampleFile samples[BROWSER_PAGE_ROWS];
    };

    struct TimingResult
    {
        uint32_t elapsed_us = 0;
        uint64_t bytes = 0;

        double megabytes_per_second = 0.0;

        bool error = false;
        const char* error_type = nullptr;
    };

    struct BenchmarkSummary
    {
        uint32_t successful_reads = 0;
        uint32_t failed_reads = 0;

        uint64_t total_bytes = 0;
        uint64_t total_time_us = 0;
    };

    DiscoverySummary discovery;

    TimingResult batch_results[
        BATCH_SIZE_COUNT];

    TimingResult repeated_results[
        REPEATED_RUNS];

    TimingResult interleaved_results[
        INTERLEAVED_STREAM_COUNT];

    std::size_t repeated_batch_size = 16;

    uint32_t BrowserPageCount(
        uint32_t total_samples)
    {
        if (total_samples == 0)
        {
            return 0;
        }

        return
            (total_samples +
             static_cast<uint32_t>(
                 BROWSER_PAGE_ROWS) -
             1) /
            static_cast<uint32_t>(
                BROWSER_PAGE_ROWS);
    }

    uint16_t ReadLE16(
        const uint8_t* data)
    {
        return static_cast<uint16_t>(
            data[0] |
            (static_cast<uint16_t>(
                data[1]) << 8));
    }

    uint32_t ReadLE32(
        const uint8_t* data)
    {
        return static_cast<uint32_t>(
            data[0] |
            (static_cast<uint32_t>(
                data[1]) << 8) |
            (static_cast<uint32_t>(
                data[2]) << 16) |
            (static_cast<uint32_t>(
                data[3]) << 24));
    }

    bool IsFourCC(
        const uint8_t* data,
        const char* text)
    {
        return
            data[0] == text[0] &&
            data[1] == text[1] &&
            data[2] == text[2] &&
            data[3] == text[3];
    }

    bool IsWavFile(
        const char* name)
    {
        const std::size_t length =
            std::strlen(name);

        if (length < 4)
        {
            return false;
        }

        const char c0 =
            name[length - 4];

        const char c1 =
            name[length - 3];

        const char c2 =
            name[length - 2];

        const char c3 =
            name[length - 1];

        return
            c0 == '.' &&
            (c1 == 'w' || c1 == 'W') &&
            (c2 == 'a' || c2 == 'A') &&
            (c3 == 'v' || c3 == 'V');
    }

    /*
     * Parse a PCM RIFF/WAVE file by scanning its chunks directly from SD.
     *
     * We intentionally do not impose a fixed WAV-header size. Containers
     * containing LIST/JUNK/etc. before the data chunk are valid candidates.
     *
     * RF64 is not treated as a supported format here because proper RF64
     * size handling requires the ds64 chunk and is a separate decision.
     */
    bool ReadWavInfo(
        File& file,
        WavInfo& info,
        const char*& reason)
    {
        reason = "UNKNOWN";

        uint8_t header[12];

        if (!file.seek(0))
        {
            reason = "SEEK_TO_START";
            return false;
        }

        if (file.read(
                header,
                sizeof(header)) !=
            static_cast<int>(
                sizeof(header)))
        {
            reason = "SHORT_RIFF_HEADER";
            return false;
        }

        if (!IsFourCC(
                header,
                "RIFF"))
        {
            reason = "NOT_RIFF";
            return false;
        }

        if (!IsFourCC(
                header + 8,
                "WAVE"))
        {
            reason = "NOT_WAVE";
            return false;
        }

        bool found_fmt = false;

        uint8_t chunk[8];

        while (file.available())
        {
            if (file.read(
                    chunk,
                    sizeof(chunk)) !=
                static_cast<int>(
                    sizeof(chunk)))
            {
                reason = "SHORT_CHUNK_HEADER";
                return false;
            }

            const uint32_t chunk_size =
                ReadLE32(chunk + 4);

            const uint32_t chunk_data_position =
                static_cast<uint32_t>(
                    file.position());

            if (IsFourCC(
                    chunk,
                    "fmt "))
            {
                if (chunk_size < 16)
                {
                    reason = "FMT_TOO_SMALL";
                    return false;
                }

                uint8_t fmt[16];

                if (file.read(
                        fmt,
                        sizeof(fmt)) !=
                    static_cast<int>(
                        sizeof(fmt)))
                {
                    reason = "SHORT_FMT_CHUNK";
                    return false;
                }

                if (ReadLE16(fmt) != 1)
                {
                    reason = "NOT_PCM";
                    return false;
                }

                info.channels =
                    ReadLE16(fmt + 2);

                info.sample_rate =
                    ReadLE32(fmt + 4);

                info.bits_per_sample =
                    ReadLE16(fmt + 14);

                found_fmt = true;

                const uint32_t remaining =
                    chunk_size - 16;

                if (remaining > 0 &&
                    !file.seek(
                        chunk_data_position +
                        chunk_size))
                {
                    reason = "FMT_SKIP_FAILED";
                    return false;
                }
            }
            else if (IsFourCC(
                         chunk,
                         "data"))
            {
                if (!found_fmt)
                {
                    reason = "DATA_BEFORE_FMT";
                    return false;
                }

                info.data_offset =
                    chunk_data_position;

                info.data_size =
                    chunk_size;

                info.valid = true;
                reason = nullptr;

                return true;
            }
            else
            {
                if (!file.seek(
                        chunk_data_position +
                        chunk_size))
                {
                    reason = "CHUNK_SKIP_FAILED";
                    return false;
                }
            }

            if (chunk_size & 1)
            {
                if (!file.seek(
                        file.position() + 1))
                {
                    reason = "CHUNK_PADDING_FAILED";
                    return false;
                }
            }
        }

        reason =
            found_fmt
                ? "DATA_NOT_FOUND"
                : "FMT_NOT_FOUND";

        return false;
    }

    bool BuildSampleFile(
        File& entry,
        const char* name,
        SampleFile& sample,
        const char*& reason)
    {
        const std::size_t directory_length =
            std::strlen(
                SAMPLE_DIRECTORY);

        const std::size_t name_length =
            std::strlen(name);

        if (directory_length + 1 + name_length >=
            sizeof(sample.path))
        {
            reason = "PATH_TOO_LONG";
            return false;
        }

        std::memcpy(
            sample.path,
            SAMPLE_DIRECTORY,
            directory_length);

        sample.path[
            directory_length] =
            '/';

        std::memcpy(
            sample.path +
                directory_length + 1,
            name,
            name_length + 1);

        sample.file_size =
            static_cast<uint32_t>(
                entry.size());

        sample.valid =
            ReadWavInfo(
                entry,
                sample.wav,
                reason);

        return sample.valid;
    }

    /*
     * Scan the complete directory.
     *
     * No dataset-size limit exists here.
     * This routine is intended to become reusable by the Instrument/sample
     * browser layer.
     */
    bool OpenSampleDirectory(
        File& directory)
    {
        directory =
            SD.open(
                SAMPLE_DIRECTORY);

        return static_cast<bool>(
            directory);
    }

    /*
     * Return the next supported sample from an already-open directory.
     *
     * This is the important reusable primitive: callers can enumerate an
     * arbitrary number of samples without allocating an array for the
     * complete directory.
     */
    bool NextSupportedSample(
        File& directory,
        SampleFile& sample)
    {
        while (true)
        {
            File entry =
                directory.openNextFile();

            if (!entry)
            {
                return false;
            }

            if (entry.isDirectory())
            {
                entry.close();
                continue;
            }

            const char* name =
                entry.name();

            if (!IsWavFile(name))
            {
                entry.close();
                continue;
            }

            const char* reason = nullptr;

            const bool valid =
                BuildSampleFile(
                    entry,
                    name,
                    sample,
                    reason);

            entry.close();

            if (!valid)
            {
                continue;
            }

            return true;
        }
    }

    /*
     * Scan the complete directory.
     *
     * No dataset-size limit exists here. Only counters are retained.
     */
    bool ScanSampleDirectory(
        DiscoverySummary& summary)
    {
        summary = {};

        File directory;

        if (!OpenSampleDirectory(
                directory))
        {
            return false;
        }

        while (true)
        {
            File entry =
                directory.openNextFile();

            if (!entry)
            {
                break;
            }

            if (entry.isDirectory())
            {
                entry.close();
                continue;
            }

            ++summary.total_entries;

            const char* name =
                entry.name();

            if (!IsWavFile(name))
            {
                entry.close();
                continue;
            }

            ++summary.wav_candidates;

            SampleFile sample;
            const char* reason = nullptr;

            if (BuildSampleFile(
                    entry,
                    name,
                    sample,
                    reason))
            {
                ++summary.supported_samples;
            }
            else
            {
                ++summary.invalid_wav;
            }

            entry.close();
        }

        directory.close();

        return true;
    }

    /*
     * Load one page of supported samples.
     *
     * The complete directory may contain thousands of samples. Only
     * BROWSER_PAGE_ROWS entries are retained in RAM.
     *
     * Page selection is based on the count of supported sample files,
     * not on filesystem entry number.
     */
    bool LoadSampleBrowserPage(
        uint32_t page_index,
        uint32_t total_samples,
        SampleBrowserPage& page)
    {
        page = {};
        page.page_index = page_index;
        page.total_samples = total_samples;
        page.total_pages =
            BrowserPageCount(
                total_samples);

        if (page_index >= page.total_pages)
        {
            return false;
        }

        const uint32_t first_sample =
            page_index *
            static_cast<uint32_t>(
                BROWSER_PAGE_ROWS);

        File directory;

        if (!OpenSampleDirectory(
                directory))
        {
            return false;
        }

        uint32_t supported_index = 0;

        while (supported_index <
               first_sample)
        {
            SampleFile ignored;

            if (!NextSupportedSample(
                    directory,
                    ignored))
            {
                directory.close();
                return false;
            }

            ++supported_index;
        }

        while (page.count <
               BROWSER_PAGE_ROWS)
        {
            SampleFile sample;

            if (!NextSupportedSample(
                    directory,
                    sample))
            {
                break;
            }

            page.samples[
                page.count++] =
                sample;

            ++supported_index;
        }

        directory.close();

        return page.count > 0;
    }

    TimingResult ReadSample(
        const SampleFile& sample)
    {
        TimingResult result;

        File file =
            SD.open(
                sample.path,
                FILE_READ);

        if (!file)
        {
            result.error = true;
            result.error_type = "OPEN";
            return result;
        }

        if (!file.seek(
                sample.wav.data_offset))
        {
            result.error = true;
            result.error_type = "SEEK";
            file.close();
            return result;
        }

        const uint32_t start =
            micros();

        std::size_t remaining =
            sample.wav.data_size;

        while (remaining > 0)
        {
            const std::size_t request =
                remaining < READ_BUFFER_SIZE
                    ? remaining
                    : READ_BUFFER_SIZE;

            const int bytes_read =
                file.read(
                    read_buffer,
                    request);

            if (bytes_read <= 0)
            {
                result.error = true;
                result.error_type = "READ";
                file.close();
                return result;
            }

            result.bytes +=
                static_cast<uint32_t>(
                    bytes_read);

            remaining -=
                static_cast<std::size_t>(
                    bytes_read);
        }

        result.elapsed_us =
            micros() - start;

        file.close();

        if (result.elapsed_us > 0)
        {
            result.megabytes_per_second =
                static_cast<double>(
                    result.bytes) /
                static_cast<double>(
                    result.elapsed_us);
        }

        return result;
    }

    /*
     * Read every supported sample.
     *
     * The directory is opened once and enumerated once. This is important:
     * a large sample folder must not turn into an O(n²) directory scan.
     */
    BenchmarkSummary RunSingleFileReads()
    {
        BenchmarkSummary summary;

        File directory;

        if (!OpenSampleDirectory(
                directory))
        {
            summary.failed_reads =
                discovery.supported_samples;

            return summary;
        }

        const uint32_t start =
            micros();

        uint32_t sample_index = 0;

        while (sample_index <
               discovery.supported_samples)
        {
            SampleFile sample;

            if (!NextSupportedSample(
                    directory,
                    sample))
            {
                summary.failed_reads +=
                    discovery.supported_samples -
                    sample_index;

                break;
            }

            const TimingResult result =
                ReadSample(sample);

            if (result.error)
            {
                ++summary.failed_reads;
            }
            else
            {
                ++summary.successful_reads;
                summary.total_bytes +=
                    result.bytes;
            }

            ++sample_index;
        }

        summary.total_time_us =
            micros() - start;

        directory.close();

        return summary;
    }

    TimingResult RunSequentialBatch(
        std::size_t count)
    {
        TimingResult result;

        if (count >
            discovery.supported_samples)
        {
            result.error = true;
            result.error_type =
                "NOT_ENOUGH_SAMPLES";

            return result;
        }

        File directory;

        if (!OpenSampleDirectory(
                directory))
        {
            result.error = true;
            result.error_type =
                "DISCOVERY";

            return result;
        }

        const uint32_t start =
            micros();

        for (std::size_t index = 0;
             index < count;
             ++index)
        {
            SampleFile sample;

            if (!NextSupportedSample(
                    directory,
                    sample))
            {
                result.error = true;
                result.error_type =
                    "DISCOVERY";
                break;
            }

            const TimingResult sample_result =
                ReadSample(sample);

            if (sample_result.error)
            {
                result.error = true;
                result.error_type =
                    sample_result.error_type;
                break;
            }

            result.bytes +=
                sample_result.bytes;
        }

        result.elapsed_us =
            micros() - start;

        directory.close();

        if (result.elapsed_us > 0)
        {
            result.megabytes_per_second =
                static_cast<double>(
                    result.bytes) /
                static_cast<double>(
                    result.elapsed_us);
        }

        return result;
    }

    /*
     * Interleaved read using only the requested number of stream states.
     * Stream metadata is bounded by the test configuration, not by the
     * total number of samples in the directory.
     */
    TimingResult RunInterleaved(
        std::size_t stream_count)
    {
        TimingResult result;

        if (stream_count >
            discovery.supported_samples)
        {
            result.error = true;
            result.error_type =
                "NOT_ENOUGH_SAMPLES";

            return result;
        }

        File files[
            INTERLEAVED_STREAM_COUNT];

        std::size_t remaining[
            INTERLEAVED_STREAM_COUNT] = {};

        File directory;

        if (!OpenSampleDirectory(
                directory))
        {
            result.error = true;
            result.error_type =
                "DISCOVERY";

            return result;
        }

        for (std::size_t index = 0;
             index < stream_count;
             ++index)
        {
            SampleFile sample;

            if (!NextSupportedSample(
                    directory,
                    sample))
            {
                result.error = true;
                result.error_type =
                    "DISCOVERY";

                directory.close();

                for (std::size_t close_index = 0;
                     close_index < index;
                     ++close_index)
                {
                    files[close_index].close();
                }

                return result;
            }

            files[index] =
                SD.open(
                    sample.path,
                    FILE_READ);

            if (!files[index])
            {
                result.error = true;
                result.error_type =
                    "OPEN";

                for (std::size_t close_index = 0;
                     close_index < index;
                     ++close_index)
                {
                    files[close_index].close();
                }

                return result;
            }

            if (!files[index].seek(
                    sample.wav.data_offset))
            {
                result.error = true;
                result.error_type =
                    "SEEK";

                for (std::size_t close_index = 0;
                     close_index <= index;
                     ++close_index)
                {
                    files[close_index].close();
                }

                return result;
            }

            remaining[index] =
                sample.wav.data_size;
        }

        directory.close();

        std::size_t active =
            stream_count;

        const uint32_t start =
            micros();

        while (active > 0)
        {
            active = 0;

            for (std::size_t index = 0;
                 index < stream_count;
                 ++index)
            {
                if (remaining[index] == 0)
                {
                    continue;
                }

                ++active;

                const std::size_t request =
                    remaining[index] <
                            INTERLEAVED_CHUNK_SIZE
                        ? remaining[index]
                        : INTERLEAVED_CHUNK_SIZE;

                const int bytes_read =
                    files[index].read(
                        read_buffer,
                        request);

                if (bytes_read <= 0)
                {
                    result.error = true;
                    result.error_type =
                        "READ";

                    for (std::size_t close_index = 0;
                         close_index < stream_count;
                         ++close_index)
                    {
                        files[close_index].close();
                    }

                    return result;
                }

                remaining[index] -=
                    static_cast<std::size_t>(
                        bytes_read);

                result.bytes +=
                    static_cast<uint32_t>(
                        bytes_read);
            }
        }

        result.elapsed_us =
            micros() - start;

        for (std::size_t index = 0;
             index < stream_count;
             ++index)
        {
            files[index].close();
        }

        if (result.elapsed_us > 0)
        {
            result.megabytes_per_second =
                static_cast<double>(
                    result.bytes) /
                static_cast<double>(
                    result.elapsed_us);
        }

        return result;
    }

    void PrintTiming(
        const char* label,
        const TimingResult& result)
    {
        Serial.print(label);
        Serial.print(" | ");

        if (result.error)
        {
            Serial.print("ERROR: ");
            Serial.println(
                result.error_type);

            return;
        }

        Serial.print(
            result.bytes);

        Serial.print(
            " bytes | ");

        Serial.print(
            result.elapsed_us);

        Serial.print(
            " us | ");

        Serial.print(
            result.megabytes_per_second,
            2);

        Serial.println(
            " MB/s");
    }

    void PrintDiscovery()
    {
        Serial.println();
        Serial.println(
            "=== FILE DISCOVERY ===");

        Serial.print(
            "Total filesystem entries: ");

        Serial.println(
            discovery.total_entries);

        Serial.print(
            "WAV candidates: ");

        Serial.println(
            discovery.wav_candidates);

        Serial.print(
            "Supported PCM WAV samples: ");

        Serial.println(
            discovery.supported_samples);

        Serial.print(
            "Invalid/unsupported WAV: ");

        Serial.println(
            discovery.invalid_wav);

        Serial.print(
            "Browser page rows: ");

        Serial.println(
            BROWSER_PAGE_ROWS);

        Serial.print(
            "Browser pages: ");

        Serial.println(
            BrowserPageCount(
                discovery.supported_samples));
    }

    void PrintBrowserPageTest()
    {
        SampleBrowserPage page;

        if (!LoadSampleBrowserPage(
                0,
                discovery.supported_samples,
                page))
        {
            Serial.println(
                "Browser page load failed.");

            return;
        }

        Serial.println();
        Serial.println(
            "=== SAMPLE BROWSER PAGE 1 ===");

        Serial.print(
            "Page rows loaded: ");

        Serial.println(
            page.count);

        Serial.print(
            "Total samples: ");

        Serial.println(
            page.total_samples);

        Serial.print(
            "Total pages: ");

        Serial.println(
            page.total_pages);

        for (std::size_t index = 0;
             index < page.count;
             ++index)
        {
            Serial.print(
                index + 1);

            Serial.print(
                " | ");

            Serial.println(
                page.samples[index].path);
        }
    }

    bool WriteReport(
        const BenchmarkSummary& single_summary)
    {
        if (!SD.exists(
                "BroTracker_SD_Test"))
        {
            if (!SD.mkdir(
                    "BroTracker_SD_Test"))
            {
                return false;
            }
        }

        File report =
            SD.open(
                REPORT_FILE,
                FILE_WRITE);

        if (!report)
        {
            return false;
        }

        report.println(
            "========================================");

        report.println(
            "BroTracker SD Read Benchmark");

        report.println(
            "Teensy 4.1 / NXP i.MX RT1062");

        report.println(
            "========================================");

        report.print(
            "Firmware build: ");

        report.print(
            BUILD_DATE);

        report.print(
            " ");

        report.println(
            BUILD_TIME);

        report.print(
            "Dataset: ");

        report.println(
            SAMPLE_DIRECTORY);

        report.println();

        report.println(
            "=== FILE DISCOVERY ===");

        report.print(
            "Total filesystem entries: ");

        report.println(
            discovery.total_entries);

        report.print(
            "WAV candidates: ");

        report.println(
            discovery.wav_candidates);

        report.print(
            "Supported PCM WAV samples: ");

        report.println(
            discovery.supported_samples);

        report.print(
            "Invalid/unsupported WAV: ");

        report.println(
            discovery.invalid_wav);

        report.print(
            "Browser page rows: ");

        report.println(
            BROWSER_PAGE_ROWS);

        report.print(
            "Browser pages: ");

        report.println(
            BrowserPageCount(
                discovery.supported_samples));

        report.println();

        report.println(
            "=== TEST A: SINGLE FILE READ ===");

        report.print(
            "Successful reads: ");

        report.println(
            single_summary.successful_reads);

        report.print(
            "Failed reads: ");

        report.println(
            single_summary.failed_reads);

        report.print(
            "Total PCM data: ");

        report.println(
            static_cast<uint32_t>(
                single_summary.total_bytes));

        report.print(
            "Total read time: ");

        report.print(
            static_cast<uint32_t>(
                single_summary.total_time_us));

        report.println(
            " us");

        report.println();

        report.println(
            "=== TEST B: SEQUENTIAL BATCH READ ===");

        for (std::size_t index = 0;
             index < BATCH_SIZE_COUNT;
             ++index)
        {
            report.print(
                "Batch: ");

            report.print(
                BATCH_SIZES[index]);

            report.print(
                " files | ");

            if (batch_results[index].error)
            {
                report.print(
                    "SKIPPED | ");

                report.println(
                    batch_results[index]
                        .error_type);

                continue;
            }

            report.print(
                static_cast<uint32_t>(
                    batch_results[index].bytes));

            report.print(
                " bytes | ");

            report.print(
                batch_results[index]
                    .elapsed_us);

            report.print(
                " us | ");

            report.print(
                batch_results[index]
                    .megabytes_per_second,
                2);

            report.println(
                " MB/s");
        }

        report.println();

        report.println(
            "=== TEST C: REPEATED BATCH READ ===");

        report.print(
            "Batch size: ");

        report.println(
            repeated_batch_size);

        for (std::size_t index = 0;
             index < REPEATED_RUNS;
             ++index)
        {
            report.print(
                "Run ");

            report.print(
                index + 1);

            report.print(
                " | ");

            if (repeated_results[index].error)
            {
                report.println(
                    repeated_results[index]
                        .error_type);

                continue;
            }

            report.print(
                static_cast<uint32_t>(
                    repeated_results[index].bytes));

            report.print(
                " bytes | ");

            report.print(
                repeated_results[index]
                    .elapsed_us);

            report.print(
                " us | ");

            report.print(
                repeated_results[index]
                    .megabytes_per_second,
                2);

            report.println(
                " MB/s");
        }

        report.println();

        report.println(
            "=== TEST D: INTERLEAVED READS ===");

        for (std::size_t index = 0;
             index < INTERLEAVED_STREAM_COUNT;
             ++index)
        {
            report.print(
                "Streams: ");

            report.print(
                INTERLEAVED_STREAMS[index]);

            report.print(
                " | Chunk: ");

            report.print(
                INTERLEAVED_CHUNK_SIZE);

            report.print(
                " B | ");

            if (interleaved_results[index].error)
            {
                report.println(
                    interleaved_results[index]
                        .error_type);

                continue;
            }

            report.print(
                static_cast<uint32_t>(
                    interleaved_results[index].bytes));

            report.print(
                " bytes | ");

            report.print(
                interleaved_results[index]
                    .elapsed_us);

            report.print(
                " us | ");

            report.print(
                interleaved_results[index]
                    .megabytes_per_second,
                2);

            report.println(
                " MB/s");
        }

        report.println();

        report.println(
            "========================================");

        report.println(
            "SD READ BENCHMARK COMPLETE");

        report.println(
            "========================================");

        report.flush();
        report.close();

        return true;
    }
}

void setup()
{
    Serial.begin(115200);

    delay(1000);

    Serial.println();
    Serial.println(
        "========================================");

    Serial.println(
        "BroTracker SD Read Benchmark");

    Serial.println(
        "Teensy 4.1 / NXP i.MX RT1062");

    Serial.println(
        "========================================");

    Serial.print(
        "Firmware build: ");

    Serial.print(
        BUILD_DATE);

    Serial.print(
        " ");

    Serial.println(
        BUILD_TIME);

    Serial.print(
        "Dataset: ");

    Serial.println(
        SAMPLE_DIRECTORY);

    if (!SD.begin())
    {
        Serial.println(
            "ERROR: SD initialization failed.");

        return;
    }

    if (!ScanSampleDirectory(
            discovery))
    {
        Serial.println(
            "ERROR: Sample directory could not be scanned.");

        return;
    }

    PrintDiscovery();

    /*
     * Demonstrate the browser model using the first UI page.
     */
    PrintBrowserPageTest();

    Serial.println();
    Serial.println(
        "=== TEST A: SINGLE FILE READ ===");

    const BenchmarkSummary single_summary =
        RunSingleFileReads();

    Serial.print(
        "Successful reads: ");

    Serial.println(
        single_summary.successful_reads);

    Serial.print(
        "Failed reads: ");

    Serial.println(
        single_summary.failed_reads);

    Serial.print(
        "Total PCM data: ");

    Serial.println(
        static_cast<uint32_t>(
            single_summary.total_bytes));

    Serial.print(
        "Total read time: ");

    Serial.print(
        static_cast<uint32_t>(
            single_summary.total_time_us));

    Serial.println(
        " us");

    Serial.println();
    Serial.println(
        "=== TEST B: SEQUENTIAL BATCH READ ===");

    for (std::size_t index = 0;
         index < BATCH_SIZE_COUNT;
         ++index)
    {
        batch_results[index] =
            RunSequentialBatch(
                BATCH_SIZES[index]);

        Serial.print(
            "Batch: ");

        Serial.print(
            BATCH_SIZES[index]);

        Serial.print(
            " files | ");

        PrintTiming(
            "",
            batch_results[index]);
    }

    Serial.println();
    Serial.println(
        "=== TEST C: REPEATED BATCH READ ===");

    Serial.print(
        "Batch size: ");

    Serial.println(
        repeated_batch_size);

    for (std::size_t index = 0;
         index < REPEATED_RUNS;
         ++index)
    {
        repeated_results[index] =
            RunSequentialBatch(
                repeated_batch_size);

        Serial.print(
            "Run ");

        Serial.print(
            index + 1);

        Serial.print(
            " | ");

        PrintTiming(
            "",
            repeated_results[index]);
    }

    Serial.println();
    Serial.println(
        "=== TEST D: INTERLEAVED READS ===");

    for (std::size_t index = 0;
         index < INTERLEAVED_STREAM_COUNT;
         ++index)
    {
        interleaved_results[index] =
            RunInterleaved(
                INTERLEAVED_STREAMS[index]);

        Serial.print(
            "Streams: ");

        Serial.print(
            INTERLEAVED_STREAMS[index]);

        Serial.print(
            " | ");

        PrintTiming(
            "",
            interleaved_results[index]);
    }

    if (WriteReport(
            single_summary))
    {
        Serial.println();
        Serial.println(
            "Benchmark report written to:");

        Serial.println(
            REPORT_FILE);
    }
    else
    {
        Serial.println();
        Serial.println(
            "ERROR: Could not write benchmark report.");
    }

    Serial.println();
    Serial.println(
        "========================================");

    Serial.println(
        "SD READ BENCHMARK COMPLETE");

    Serial.println(
        "========================================");
}

void loop()
{
}

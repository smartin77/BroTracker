/*
 * BroTracker
 *
 * SD Read Benchmark
 *
 * Purpose:
 *
 *   Measure the real-world SD card read performance of a Teensy 4.1
 *   when loading multiple PCM WAV samples.
 *
 *   This benchmark is intentionally focused on the storage layer.
 *   It does not use the Teensy Audio Library and does not generate
 *   audio output.
 *
 * Dataset:
 *
 *   Samples/Wav-HQ/DrumLoop/
 *
 *   The dataset is expected to contain multiple PCM WAV files,
 *   primarily 16-bit / 44.1 kHz, with both mono and stereo files.
 *
 * Tests:
 *
 *   A. Single-file reads
 *      Each WAV file is opened, its WAV header is inspected,
 *      all PCM data is read, and the file is closed.
 *
 *   B. Sequential batch reads
 *      4, 8, 16, 32 and 64 WAV files are loaded sequentially.
 *
 *   C. Repeated batch reads
 *      The same batches are repeated to observe timing variability.
 *
 *   D. Interleaved reads
 *      Multiple WAV files are opened and read in alternating chunks.
 *      This is intended as a storage-level approximation of future
 *      multi-sample streaming.
 *
 * Goals:
 *
 *   - Establish baseline SD read performance on Teensy 4.1.
 *   - Measure the overhead of opening and reading many small files.
 *   - Determine whether multiple sample streams can be serviced
 *     by the SD subsystem.
 *   - Establish useful baseline values before introducing audio
 *     buffering, the Teensy Audio Library, or other streaming layers.
 *
 * The benchmark reads sample data only. It writes a small benchmark report
 * to BroTracker_SD_Test after the benchmark starts and after the tests finish.
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

    constexpr std::size_t MAX_FILES =
        64;

    constexpr std::size_t READ_BUFFER_SIZE =
        4096;

    constexpr std::size_t WAV_HEADER_SIZE =
        256;

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

    constexpr std::size_t INTERLEAVED_CHUNK_SIZE =
        1024;

    uint8_t read_buffer[READ_BUFFER_SIZE];

    struct WavInfo
    {
        std::size_t data_offset = 0;
        std::size_t data_size = 0;

        uint16_t channels = 0;
        uint16_t bits_per_sample = 0;

        uint32_t sample_rate = 0;

        bool valid = false;
    };

    struct SampleFile
    {
        char path[128];

        WavInfo wav;

        std::size_t file_size = 0;

        bool valid = false;
    };

    SampleFile sample_files[MAX_FILES];

    std::size_t sample_file_count = 0;

    struct TimingResult
    {
        uint32_t elapsed_us = 0;
        uint64_t bytes = 0;

        double megabytes_per_second = 0.0;
    };

    struct SingleFileResult
    {
        TimingResult timing;
    };

    struct BatchResult
    {
        std::size_t file_count = 0;
        TimingResult timing;
    };

    struct InterleavedResult
    {
        std::size_t stream_count = 0;
        std::size_t chunk_size = 0;
        TimingResult timing;
    };

    SingleFileResult single_results[MAX_FILES];

    BatchResult batch_results[BATCH_SIZE_COUNT];

    BatchResult repeated_results[REPEATED_RUNS];

    InterleavedResult interleaved_results[
        INTERLEAVED_STREAM_COUNT];

    std::size_t repeated_batch_file_count = 0;

    uint16_t ReadLE16(
        const uint8_t* data)
    {
        return static_cast<uint16_t>(
            data[0] |
            (static_cast<uint16_t>(data[1]) << 8));
    }

    uint32_t ReadLE32(
        const uint8_t* data)
    {
        return static_cast<uint32_t>(
            data[0] |
            (static_cast<uint32_t>(data[1]) << 8) |
            (static_cast<uint32_t>(data[2]) << 16) |
            (static_cast<uint32_t>(data[3]) << 24));
    }

    bool IsFourCC(
        const uint8_t* data,
        const char* text)
    {
        return data[0] == text[0] &&
               data[1] == text[1] &&
               data[2] == text[2] &&
               data[3] == text[3];
    }

    bool ReadWavInfo(
        File& file,
        WavInfo& info)
    {
        uint8_t header[WAV_HEADER_SIZE];

        const int bytes_read =
            file.read(
                header,
                sizeof(header));

        if (bytes_read < 12)
        {
            return false;
        }

        if (!IsFourCC(header, "RIFF") &&
            !IsFourCC(header, "RF64"))
        {
            return false;
        }

        if (!IsFourCC(header + 8, "WAVE"))
        {
            return false;
        }

        /*
         * Parse WAV chunks rather than assuming a fixed 44-byte
         * PCM header. This allows LIST, JUNK and other chunks.
         */
        std::size_t position = 12;

        uint8_t chunk_header[8];

        while (position + 8 <=
               static_cast<std::size_t>(bytes_read))
        {
            std::memcpy(
                chunk_header,
                header + position,
                8);

            const uint32_t chunk_size =
                ReadLE32(
                    chunk_header + 4);

            if (IsFourCC(
                    chunk_header,
                    "fmt "))
            {
                if (chunk_size < 16 ||
                    position + 8 + 16 >
                        static_cast<std::size_t>(
                            bytes_read))
                {
                    return false;
                }

                const uint8_t* fmt =
                    header + position + 8;

                const uint16_t format =
                    ReadLE16(fmt);

                if (format != 1)
                {
                    return false;
                }

                info.channels =
                    ReadLE16(fmt + 2);

                info.sample_rate =
                    ReadLE32(fmt + 4);

                info.bits_per_sample =
                    ReadLE16(fmt + 14);
            }

            if (IsFourCC(
                    chunk_header,
                    "data"))
            {
                info.data_offset =
                    position + 8;

                info.data_size =
                    chunk_size;

                info.valid = true;

                return true;
            }

            position +=
                8 + chunk_size;

            /*
             * WAV chunks are word aligned.
             */
            if (chunk_size & 1)
            {
                ++position;
            }
        }

        return false;
    }

    void PrintWavInfo(
        const SampleFile& sample)
    {
        Serial.print(
            sample.path);

        Serial.print(
            " | ");

        Serial.print(
            sample.file_size);

        Serial.print(
            " bytes | ");

        Serial.print(
            sample.wav.channels);

        Serial.print(
            " ch | ");

        Serial.print(
            sample.wav.sample_rate);

        Serial.print(
            " Hz | ");

        Serial.print(
            sample.wav.bits_per_sample);

        Serial.print(
            " bit | PCM ");

        Serial.print(
            sample.wav.data_size);

        Serial.println(
            " bytes");
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
            (c0 == '.' &&
             (c1 == 'w' || c1 == 'W') &&
             (c2 == 'a' || c2 == 'A') &&
             (c3 == 'v' || c3 == 'V'));
    }

    void DiscoverSamples()
    {
        sample_file_count = 0;

        File directory =
            SD.open(
                SAMPLE_DIRECTORY);

        if (!directory)
        {
            Serial.println(
                "ERROR: Sample directory could not be opened.");

            return;
        }

        while (sample_file_count <
               MAX_FILES)
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

            const char* name =
                entry.name();

            if (!IsWavFile(name))
            {
                entry.close();
                continue;
            }

            SampleFile& sample =
                sample_files[
                    sample_file_count];

            std::strncpy(
                sample.path,
                name,
                sizeof(sample.path) - 1);

            sample.path[
                sizeof(sample.path) - 1] =
                '\0';

            sample.file_size =
                entry.size();

            sample.valid =
                ReadWavInfo(
                    entry,
                    sample.wav);

            entry.close();

            if (!sample.valid)
            {
                Serial.print(
                    "Invalid/unsupported WAV: ");

                Serial.println(
                    sample.path);

                continue;
            }

            ++sample_file_count;
        }

        directory.close();
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
            return result;
        }

        if (!file.seek(
                sample.wav.data_offset))
        {
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

        const uint32_t end =
            micros();

        file.close();

        result.elapsed_us =
            end - start;

        if (result.elapsed_us > 0)
        {
            result.megabytes_per_second =
                (static_cast<double>(
                    result.bytes) /
                 1000000.0) /
                (static_cast<double>(
                    result.elapsed_us) /
                 1000000.0);
        }

        return result;
    }

    TimingResult ReadBatch(
        std::size_t count)
    {
        TimingResult result;

        if (count >
            sample_file_count)
        {
            count =
                sample_file_count;
        }

        const uint32_t start =
            micros();

        for (std::size_t index = 0;
             index < count;
             ++index)
        {
            const TimingResult sample_result =
                ReadSample(
                    sample_files[index]);

            if (sample_result.bytes == 0)
            {
                continue;
            }

            result.bytes +=
                sample_result.bytes;
        }

        const uint32_t end =
            micros();

        result.elapsed_us =
            end - start;

        if (result.elapsed_us > 0)
        {
            result.megabytes_per_second =
                (static_cast<double>(
                    result.bytes) /
                 1000000.0) /
                (static_cast<double>(
                    result.elapsed_us) /
                 1000000.0);
        }

        return result;
    }

    void RunSingleFileTest()
    {
        uint64_t total_bytes = 0;
        uint64_t total_time_us = 0;

        for (std::size_t index = 0;
            index < sample_file_count;
            ++index)
        {
            const TimingResult result =
                ReadSample(
                    sample_files[index]);

            single_results[index].timing =
                result;

            total_bytes += result.bytes;
            total_time_us += result.elapsed_us;
        }
    }

    void RunBatchTests()
    {
        for (std::size_t size_index = 0;
            size_index < BATCH_SIZE_COUNT;
            ++size_index)
        {
            const std::size_t count =
                BATCH_SIZES[size_index];

            batch_results[size_index].file_count =
                count;

            if (count > sample_file_count)
            {
                batch_results[size_index].timing =
                    TimingResult();

                continue;
            }

            batch_results[size_index].timing =
                ReadBatch(count);
        }
    }

    void RunRepeatedBatchTests()
    {
        repeated_batch_file_count =
            sample_file_count < 16
                ? sample_file_count
                : 16;

        for (std::size_t run = 0;
            run < REPEATED_RUNS;
            ++run)
        {
            repeated_results[run].file_count =
                repeated_batch_file_count;

            repeated_results[run].timing =
                ReadBatch(
                    repeated_batch_file_count);
        }
    }

    void RunInterleavedTest(
        std::size_t stream_count)
    {
        if (stream_count >
            sample_file_count)
        {
            return;
        }

        File streams[
            8];

        std::size_t remaining[
            8];

        bool active[
            8];

        uint64_t total_bytes = 0;

        for (std::size_t index = 0;
             index < stream_count;
             ++index)
        {
            streams[index] =
                SD.open(
                    sample_files[index].path,
                    FILE_READ);

            if (!streams[index])
            {
                active[index] = false;
                remaining[index] = 0;
                continue;
            }

            if (!streams[index].seek(
                    sample_files[index]
                        .wav.data_offset))
            {
                streams[index].close();

                active[index] = false;
                remaining[index] = 0;
                continue;
            }

            active[index] = true;

            remaining[index] =
                sample_files[index]
                    .wav.data_size;
        }

        const uint32_t start =
            micros();

        bool work_remaining = true;

        while (work_remaining)
        {
            work_remaining = false;

            for (std::size_t index = 0;
                 index < stream_count;
                 ++index)
            {
                if (!active[index] ||
                    remaining[index] == 0)
                {
                    continue;
                }

                work_remaining = true;

                const std::size_t request =
                    remaining[index] <
                        INTERLEAVED_CHUNK_SIZE
                        ? remaining[index]
                        : INTERLEAVED_CHUNK_SIZE;

                const int bytes_read =
                    streams[index].read(
                        read_buffer,
                        request);

                if (bytes_read <= 0)
                {
                    remaining[index] = 0;
                    active[index] = false;
                    continue;
                }

                total_bytes +=
                    static_cast<uint32_t>(
                        bytes_read);

                remaining[index] -=
                    static_cast<std::size_t>(
                        bytes_read);
            }
        }

        const uint32_t end =
            micros();

        for (std::size_t index = 0;
             index < stream_count;
             ++index)
        {
            if (streams[index])
            {
                streams[index].close();
            }
        }

        const uint32_t elapsed =
            end - start;

        double throughput = 0.0;

        if (elapsed > 0)
        {
            throughput =
                (static_cast<double>(
                    total_bytes) /
                 1000000.0) /
                (static_cast<double>(
                    elapsed) /
                 1000000.0);
        }

        const std::size_t result_index =
            stream_count == 2 ? 0 :
            stream_count == 4 ? 1 :
            2;

        interleaved_results[result_index]
            .stream_count = stream_count;

        interleaved_results[result_index]
            .chunk_size =
                INTERLEAVED_CHUNK_SIZE;

        interleaved_results[result_index]
            .timing.elapsed_us =
                elapsed;

        interleaved_results[result_index]
            .timing.bytes =
                total_bytes;

        interleaved_results[result_index]
            .timing.megabytes_per_second =
                throughput;
    }

    void RunInterleavedTests()
    {
        Serial.println();
        Serial.println(
            "=== TEST D: INTERLEAVED READS ===");

        Serial.print(
            "Chunk size: ");

        Serial.print(
            INTERLEAVED_CHUNK_SIZE);

        Serial.println(
            " bytes");

        for (std::size_t index = 0;
             index < INTERLEAVED_STREAM_COUNT;
             ++index)
        {
            RunInterleavedTest(
                INTERLEAVED_STREAMS[index]);
        }
    }

    void PrintDatasetSummary()
    {
        Serial.println();
        Serial.println(
            "=== DATASET ===");

        Serial.print(
            "Directory: ");

        Serial.println(
            SAMPLE_DIRECTORY);

        Serial.print(
            "WAV files: ");

        Serial.println(
            sample_file_count);

        uint64_t total_pcm = 0;

        for (std::size_t index = 0;
             index < sample_file_count;
             ++index)
        {
            total_pcm +=
                sample_files[index]
                    .wav.data_size;
        }

        Serial.print(
            "Total PCM data: ");

        Serial.print(
            static_cast<uint32_t>(
                total_pcm));

        Serial.println(
            " bytes");
    }
}

void StartReport()
{
    SD.mkdir("BroTracker_SD_Test");

    File report =
        SD.open(
            REPORT_FILE,
            FILE_WRITE);

    if (!report)
    {
        Serial.println();
        Serial.println(
            "ERROR: Could not create benchmark report.");

        return;
    }

    report.println();
    report.println(
        "========================================");

    report.println(
        "BroTracker SD Read Benchmark");

    report.println(
        "Teensy 4.1 / NXP i.MX RT1062");

    report.println(
        "========================================");

    report.println();

    report.print(
        "Firmware build: ");

    report.print(
        BUILD_DATE);

    report.print(
        " ");

    report.println(
        BUILD_TIME);

    report.println();

    report.print(
        "Dataset: ");

    report.println(
        SAMPLE_DIRECTORY);

    report.println();

    report.println(
        "Benchmark status: STARTED");

    report.flush();
    report.close();

    Serial.println();
    Serial.println(
        "Benchmark report initialized:");

    Serial.println(
        REPORT_FILE);
}

void AppendReportStatus(
    const char* status)
{
    File report =
        SD.open(
            REPORT_FILE,
            FILE_WRITE);

    if (!report)
    {
        Serial.println();
        Serial.println(
            "ERROR: Could not update benchmark report.");

        return;
    }

    report.println();

    report.print(
        "Benchmark status: ");

    report.println(
        status);

    report.flush();
    report.close();
}

void WriteReport()
{
    SD.mkdir("BroTracker_SD_Test");

    File report =
        SD.open(
            REPORT_FILE,
            FILE_WRITE);

    if (!report)
    {
        Serial.println();
        Serial.println(
            "ERROR: Could not create benchmark report.");

        return;
    }

    report.println();
    report.println(
        "========================================");

    report.println(
        "BroTracker SD Read Benchmark");

    report.println(
        "Teensy 4.1 / NXP i.MX RT1062");

    report.println(
        "========================================");

    report.println();

    report.print(
        "Firmware build: ");

    report.print(
        BUILD_DATE);

    report.print(
        " ");

    report.println(
        BUILD_TIME);

    report.println();

    report.print(
        "Dataset: ");

    report.println(
        SAMPLE_DIRECTORY);

    report.print(
        "WAV files: ");

    report.println(
        sample_file_count);

    report.println();

    report.println(
        "=== DATASET ===");

    uint64_t total_pcm = 0;

    for (std::size_t index = 0;
         index < sample_file_count;
         ++index)
    {
        total_pcm +=
            sample_files[index]
                .wav.data_size;

        report.print(
            sample_files[index].path);

        report.print(
            " | ");

        report.print(
            sample_files[index]
                .file_size);

        report.print(
            " bytes | ");

        report.print(
            sample_files[index]
                .wav.channels);

        report.print(
            " ch | ");

        report.print(
            sample_files[index]
                .wav.sample_rate);

        report.print(
            " Hz | ");

        report.print(
            sample_files[index]
                .wav.bits_per_sample);

        report.print(
            " bit | PCM ");

        report.print(
            sample_files[index]
                .wav.data_size);

        report.println(
            " bytes");
    }

    report.println();

    report.println(
        "=== TEST A: SINGLE FILE READ ===");

    uint64_t total_single_bytes = 0;
    uint64_t total_single_time = 0;

    for (std::size_t index = 0;
         index < sample_file_count;
         ++index)
    {
        const TimingResult& result =
            single_results[index].timing;

        total_single_bytes +=
            result.bytes;

        total_single_time +=
            result.elapsed_us;

        report.print(
            index + 1);

        report.print(
            "/");

        report.print(
            sample_file_count);

        report.print(
            " ");

        report.print(
            sample_files[index].path);

        report.print(
            " | ");

        report.print(
            result.elapsed_us);

        report.print(
            " us | ");

        report.print(
            result.megabytes_per_second,
            2);

        report.println(
            " MB/s");
    }

    report.println();

    report.print(
        "Total PCM data: ");

    report.print(
        static_cast<uint32_t>(
            total_single_bytes));

    report.println(
        " bytes");

    report.print(
        "Total read time: ");

    report.print(
        static_cast<uint32_t>(
            total_single_time));

    report.println(
        " us");

    report.println();

    report.println(
        "=== TEST B: SEQUENTIAL BATCH READ ===");

    for (std::size_t index = 0;
         index < BATCH_SIZE_COUNT;
         ++index)
    {
        const BatchResult& result =
            batch_results[index];

        report.print(
            "Batch: ");

        report.print(
            result.file_count);

        report.print(
            " files | ");

        report.print(
            result.timing.bytes);

        report.print(
            " bytes | ");

        report.print(
            result.timing.elapsed_us);

        report.print(
            " us | ");

        report.print(
            result.timing.megabytes_per_second,
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
        repeated_batch_file_count);

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

        report.print(
            repeated_results[index]
                .timing.bytes);

        report.print(
            " bytes | ");

        report.print(
            repeated_results[index]
                .timing.elapsed_us);

        report.print(
            " us | ");

        report.print(
            repeated_results[index]
                .timing.megabytes_per_second,
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
        const InterleavedResult& result =
            interleaved_results[index];

        report.print(
            "Streams: ");

        report.print(
            result.stream_count);

        report.print(
            " | Chunk: ");

        report.print(
            result.chunk_size);

        report.print(
            " B | ");

        report.print(
            result.timing.bytes);

        report.print(
            " bytes | ");

        report.print(
            result.timing.elapsed_us);

        report.print(
            " us | ");

        report.print(
            result.timing.megabytes_per_second,
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

    report.println(
        "Benchmark status: COMPLETE");

    report.flush();
    report.close();

    Serial.println();
    Serial.println(
        "Benchmark report saved to:");

    Serial.println(
        REPORT_FILE);
}

void setup()
{
    Serial.begin(115200);

    while (!Serial && millis() < 3000)
    {
        // Give the serial monitor time to attach.
    }

    Serial.println();
    Serial.println(
        "========================================");

    Serial.println(
        "BroTracker SD Read Benchmark");

    Serial.println(
        "Teensy 4.1 / NXP i.MX RT1062");

    Serial.println(
        "========================================");

    Serial.println();

    if (!SD.begin(BUILTIN_SDCARD))
    {
        Serial.println(
            "ERROR: SD initialization failed.");

        return;
    }

    Serial.println(
        "SD initialization: PASS");

    StartReport();

    DiscoverSamples();

    if (sample_file_count == 0)
    {
        Serial.println();
        Serial.println(
            "ERROR: No WAV files found.");

        Serial.print(
            "Expected directory: ");

        Serial.println(
            SAMPLE_DIRECTORY);

        AppendReportStatus(
            "FAILED - no WAV files found");

        return;
    }

    PrintDatasetSummary();

    Serial.println();

    for (std::size_t index = 0;
         index < sample_file_count;
         ++index)
    {
        PrintWavInfo(
            sample_files[index]);
    }

    RunSingleFileTest();

    RunBatchTests();

    RunRepeatedBatchTests();

    RunInterleavedTests();

    WriteReport();

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
    // Benchmark runs once from setup().
}
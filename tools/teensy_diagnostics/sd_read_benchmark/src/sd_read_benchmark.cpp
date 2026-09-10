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
 *              - writes a new result file to the SD card
 *              - preserves previous benchmark result files
 *              - flashes the onboard LED three times on successful completion
 *
 *              Phase 2 additionally measures the sample-streaming access
 *              pattern that the BroTracker sample engine will actually use,
 *              because sequential single-file throughput does not predict it:
 *
 *              - concurrent round-robin streaming from N open files
 *              - per-refill and per-pass latency (min / avg / worst)
 *              - refill deadline misses relative to the per-voice ring buffer
 *              - realtime margin and estimated sustainable voice count
 *              - note-on latency (open + seek + first chunk)
 *              - random-seek read latency
 *              - the same sweep repeated under a simulated audio interrupt
 *                load, measuring audio-cadence jitter caused by SD activity
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

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

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

    constexpr const char* BENCHMARK_OUTPUT_PATH =
        "/BT_benchmarks/";

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

    // ---------------------------------------------------------------------
    // Phase 2: streaming access pattern configuration
    // ---------------------------------------------------------------------
    //
    // These values mirror the proposed BroTracker sample streaming design:
    // one ring buffer per voice, refilled in fixed chunks from the main
    // loop, with the audio path only ever reading from RAM.
    //
    // The sweep runs 1..STREAM_MAX_COUNT concurrently open files so the
    // point at which the SD card stops keeping up can be observed rather
    // than assumed.
    // ---------------------------------------------------------------------

    constexpr std::uint32_t STREAM_MAX_COUNT = 8;
    constexpr std::uint32_t STREAM_CHUNK_SIZE = 4096;
    constexpr std::uint32_t STREAM_RING_BYTES = 8192;
    constexpr std::uint32_t STREAM_PASSES = 64;

    // Worst-case consumption model: 44.1 kHz, 16-bit mono, played back at
    // STREAM_PITCH_PERCENT of the native rate (200 = +12 semitones).
    constexpr std::uint32_t STREAM_SAMPLE_RATE_HZ = 44100;
    constexpr std::uint32_t STREAM_BYTES_PER_FRAME = 2;
    constexpr std::uint32_t STREAM_PITCH_PERCENT = 200;

    constexpr std::uint32_t NOTE_ON_TRIALS = 32;
    constexpr std::uint32_t RANDOM_SEEK_TRIALS = 64;

    // Simulated audio interrupt load. The Teensy Audio Library updates
    // every AUDIO_BLOCK_FRAMES samples; this reproduces that cadence and
    // priority without pulling in the Audio library, so SD-induced audio
    // jitter can be measured by this tool alone.
    constexpr bool STREAM_TEST_WITH_AUDIO_LOAD = true;
    constexpr std::uint32_t AUDIO_BLOCK_FRAMES = 128;
    constexpr std::uint32_t AUDIO_LOAD_VOICES = 8;
    constexpr std::uint8_t AUDIO_LOAD_ISR_PRIORITY = 208;

    constexpr std::uint32_t AudioBlockIntervalUs()
    {
        return (AUDIO_BLOCK_FRAMES * 1000000u) /
               STREAM_SAMPLE_RATE_HZ;
    }

    constexpr std::uint32_t StreamBytesPerSecond()
    {
        return (STREAM_SAMPLE_RATE_HZ *
                STREAM_BYTES_PER_FRAME *
                STREAM_PITCH_PERCENT) / 100u;
    }

    // Audio time represented by one refill chunk. A full round-robin pass
    // must complete within this budget or a voice will run dry.
    constexpr std::uint32_t ChunkAudioUs()
    {
        return static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(STREAM_CHUNK_SIZE) * 1000000ull) /
            StreamBytesPerSecond());
    }

    // Time taken to drain a completely full ring buffer.
    constexpr std::uint32_t RingDrainUs()
    {
        return static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(STREAM_RING_BYTES) * 1000000ull) /
            StreamBytesPerSecond());
    }

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

    std::uint8_t read_buffer[READ_CHUNK_SIZE];

    // ---------------------------------------------------------------------
    // Phase 2 state
    // ---------------------------------------------------------------------

    struct StreamCandidate
    {
        char path[256] = {};
        std::uint32_t data_offset = 0;
        std::uint32_t data_size = 0;
    };

    StreamCandidate stream_candidates[STREAM_MAX_COUNT];
    std::uint32_t stream_candidate_count = 0;

    // Per-voice refill destinations live in RAM2, matching the placement
    // the real streaming engine is expected to use.
    DMAMEM std::uint8_t stream_buffers[STREAM_MAX_COUNT][STREAM_CHUNK_SIZE];

    File stream_handles[STREAM_MAX_COUNT];

    struct StreamResult
    {
        std::uint32_t stream_count = 0;
        std::uint32_t passes = 0;
        std::uint64_t total_bytes = 0;
        std::uint64_t total_us = 0;

        std::uint32_t chunk_min_us = UINT32_MAX;
        std::uint32_t chunk_max_us = 0;
        std::uint64_t chunk_total_us = 0;
        std::uint32_t chunk_count = 0;

        std::uint32_t pass_max_us = 0;
        std::uint64_t pass_total_us = 0;
        std::uint32_t deadline_misses = 0;

        std::uint32_t wraps = 0;
        std::uint32_t read_failures = 0;
        bool started = false;

        std::uint32_t isr_count = 0;
        std::uint32_t isr_max_interval_us = 0;
    };

    // Simulated audio interrupt state.
    volatile std::uint32_t audio_isr_count = 0;
    volatile std::uint32_t audio_isr_last_us = 0;
    volatile std::uint32_t audio_isr_max_interval_us = 0;
    volatile std::int32_t audio_isr_sink = 0;

    IntervalTimer audio_load_timer;

    // Deterministic pseudo-random source for the seek test.
    std::uint32_t random_state = 0x12345678u;

    std::uint32_t NextRandom()
    {
        random_state = (random_state * 1664525u) + 1013904223u;
        return random_state;
    }

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

    // Phase 2 headline results.
    std::uint32_t max_sustainable_streams_idle = 0;
    std::uint32_t max_sustainable_streams_loaded = 0;
    std::uint32_t note_on_worst_us = 0;

    bool benchmark_ok = true;
    bool clock_synced_from_host = false;

    // ---------------------------------------------------------------------
    // Reporting
    // ---------------------------------------------------------------------

    void ReportPrint(const char* text)
    {
        Serial.print(text);

        if (report_file)
            report_file.print(text);
    }

    void ReportPrint(const __FlashStringHelper* text)
    {
        Serial.print(text);

        if (report_file)
            report_file.print(text);
    }

    void ReportPrint(std::uint32_t value)
    {
        Serial.print(value);

        if (report_file)
            report_file.print(value);
    }

    void ReportPrint(std::uint64_t value)
    {
        Serial.print(value);

        if (report_file)
            report_file.print(value);
    }


    void ReportPrintln(std::uint32_t value)
    {
        Serial.println(value);

        if (report_file)
            report_file.println(value);
    }

    void ReportPrintln(std::uint64_t value)
    {
        Serial.println(value);

        if (report_file)
            report_file.println(value);
    }

    void ReportPrintln()
    {
        Serial.println();

        if (report_file)
            report_file.println();
    }

    void ReportPrintln(const char* text)
    {
        Serial.println(text);

        if (report_file)
            report_file.println(text);
    }

    void ReportPrintln(const __FlashStringHelper* text)
    {
        Serial.println(text);

        if (report_file)
            report_file.println(text);
    }

    void ReportPrintSigned(std::int32_t value)
    {
        if (value < 0)
        {
            ReportPrint(F("-"));

            ReportPrint(static_cast<std::uint32_t>(
                -static_cast<std::int64_t>(value)));
        }
        else
        {
            ReportPrint(static_cast<std::uint32_t>(value));
        }
    }

    void FlushReport()
    {
        if (report_file)
            report_file.flush();
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
    // Phase 2: streaming access pattern benchmark
    // ---------------------------------------------------------------------
    //
    // Sequential single-file throughput does not predict the behaviour of a
    // polyphonic sampler. The real pattern is N concurrently open files,
    // each advanced by a small chunk per service pass, with the card's read
    // position jumping between unrelated file locations every time.
    //
    // This phase reproduces that pattern directly.
    // ---------------------------------------------------------------------

    void AudioLoadIsr()
    {
        const std::uint32_t now_us = micros();

        if (audio_isr_count != 0)
        {
            const std::uint32_t interval =
                now_us - audio_isr_last_us;

            if (interval > audio_isr_max_interval_us)
                audio_isr_max_interval_us = interval;
        }

        audio_isr_last_us = now_us;
        ++audio_isr_count;

        // Representative mixing workload: AUDIO_LOAD_VOICES voices summed
        // into one audio block. The arithmetic is irrelevant; the purpose
        // is to occupy the audio interrupt for a realistic duration so that
        // SD-induced interference becomes visible as interval jitter.
        std::int32_t accumulator = 0;

        for (std::uint32_t voice = 0;
             voice < AUDIO_LOAD_VOICES;
             ++voice)
        {
            for (std::uint32_t frame = 0;
                 frame < AUDIO_BLOCK_FRAMES;
                 ++frame)
            {
                const std::uint32_t mixed =
                    (frame + voice) * 2654435761u;

                accumulator +=
                    static_cast<std::int16_t>(mixed >> 16);
            }
        }

        audio_isr_sink = accumulator;
    }

    void StartAudioLoad()
    {
        audio_isr_count = 0;
        audio_isr_last_us = 0;
        audio_isr_max_interval_us = 0;

        audio_load_timer.begin(
            AudioLoadIsr,
            AudioBlockIntervalUs());

        audio_load_timer.priority(
            AUDIO_LOAD_ISR_PRIORITY);
    }

    void StopAudioLoad()
    {
        audio_load_timer.end();
    }

    // Reads exactly `size` bytes and reports how long the complete refill
    // took, including any short reads issued by the filesystem layer.
    bool TimedRead(
        File& file,
        std::uint8_t* destination,
        std::size_t size,
        std::uint32_t& elapsed_us)
    {
        const std::uint32_t start = micros();

        std::uint8_t* output = destination;
        std::size_t remaining = size;

        while (remaining > 0)
        {
            const int count =
                file.read(output, remaining);

            if (count <= 0)
            {
                elapsed_us = micros() - start;
                return false;
            }

            output += count;

            remaining -=
                static_cast<std::size_t>(count);
        }

        elapsed_us = micros() - start;
        return true;
    }

    void RunConcurrentStreamTest(
        std::uint32_t stream_count,
        bool with_audio_load,
        StreamResult& result)
    {
        result = StreamResult();
        result.stream_count = stream_count;

        if (stream_count == 0 ||
            stream_count > stream_candidate_count)
        {
            return;
        }

        std::uint32_t stream_position[STREAM_MAX_COUNT] = {};

        for (std::uint32_t i = 0; i < stream_count; ++i)
        {
            stream_handles[i] =
                SD.open(
                    stream_candidates[i].path,
                    FILE_READ);

            if (!stream_handles[i] ||
                !stream_handles[i].seek(
                    stream_candidates[i].data_offset))
            {
                for (std::uint32_t j = 0; j <= i; ++j)
                {
                    if (stream_handles[j])
                        stream_handles[j].close();
                }

                ++result.read_failures;
                return;
            }

            stream_position[i] =
                stream_candidates[i].data_offset;
        }

        result.started = true;

        if (with_audio_load)
            StartAudioLoad();

        const std::uint32_t deadline_us = ChunkAudioUs();
        const std::uint32_t run_start = micros();

        for (std::uint32_t pass = 0;
             pass < STREAM_PASSES;
             ++pass)
        {
            const std::uint32_t pass_start = micros();

            for (std::uint32_t i = 0; i < stream_count; ++i)
            {
                const StreamCandidate& candidate =
                    stream_candidates[i];

                const std::uint32_t data_end =
                    candidate.data_offset +
                    candidate.data_size;

                // A looping instrument seeks back to its loop point rather
                // than stopping, so the wrap seek is part of the pattern.
                if (stream_position[i] + STREAM_CHUNK_SIZE >
                    data_end)
                {
                    if (!stream_handles[i].seek(
                            candidate.data_offset))
                    {
                        ++result.read_failures;
                        continue;
                    }

                    stream_position[i] =
                        candidate.data_offset;

                    ++result.wraps;
                }

                std::uint32_t elapsed_us = 0;

                const bool ok =
                    TimedRead(
                        stream_handles[i],
                        stream_buffers[i],
                        STREAM_CHUNK_SIZE,
                        elapsed_us);

                if (!ok)
                {
                    ++result.read_failures;
                    continue;
                }

                stream_position[i] += STREAM_CHUNK_SIZE;

                result.total_bytes += STREAM_CHUNK_SIZE;
                result.chunk_total_us += elapsed_us;
                ++result.chunk_count;

                if (elapsed_us < result.chunk_min_us)
                    result.chunk_min_us = elapsed_us;

                if (elapsed_us > result.chunk_max_us)
                    result.chunk_max_us = elapsed_us;
            }

            const std::uint32_t pass_us =
                micros() - pass_start;

            result.pass_total_us += pass_us;

            if (pass_us > result.pass_max_us)
                result.pass_max_us = pass_us;

            if (pass_us > deadline_us)
                ++result.deadline_misses;

            ++result.passes;
        }

        result.total_us = micros() - run_start;

        if (with_audio_load)
        {
            StopAudioLoad();

            result.isr_count = audio_isr_count;

            result.isr_max_interval_us =
                audio_isr_max_interval_us;
        }

        for (std::uint32_t i = 0; i < stream_count; ++i)
        {
            if (stream_handles[i])
                stream_handles[i].close();
        }
    }

    void PrintStreamResult(
        const StreamResult& result,
        bool with_audio_load)
    {
        ReportPrint(F("  Streams: "));
        ReportPrintln(result.stream_count);

        if (!result.started)
        {
            ReportPrintln(
                F("    SKIPPED: could not open all streams"));

            return;
        }

        const std::uint32_t deadline_us = ChunkAudioUs();

        ReportPrint(F("    Passes: "));
        ReportPrintln(result.passes);

        ReportPrint(F("    Refills: "));
        ReportPrintln(result.chunk_count);

        ReportPrint(F("    Loop wraps: "));
        ReportPrintln(result.wraps);

        ReportPrint(F("    Bytes: "));
        ReportPrintln(result.total_bytes);

        ReportPrint(F("    Aggregate throughput: "));

        ReportPrint(
            ThroughputBytesPerSecond(
                result.total_bytes,
                result.total_us) /
            1024ull);

        ReportPrintln(F(" KiB/s"));

        ReportPrint(F("    Refill min: "));

        if (result.chunk_min_us == UINT32_MAX)
            ReportPrintln(F("N/A"));
        else
        {
            ReportPrint(result.chunk_min_us);
            ReportPrintln(F(" us"));
        }

        ReportPrint(F("    Refill avg: "));

        ReportPrint(
            result.chunk_count != 0
                ? static_cast<std::uint32_t>(
                      result.chunk_total_us /
                      result.chunk_count)
                : 0u);

        ReportPrintln(F(" us"));

        ReportPrint(F("    Refill worst: "));
        ReportPrint(result.chunk_max_us);
        ReportPrintln(F(" us"));

        ReportPrint(F("    Pass avg: "));

        ReportPrint(
            result.passes != 0
                ? static_cast<std::uint32_t>(
                      result.pass_total_us /
                      result.passes)
                : 0u);

        ReportPrintln(F(" us"));

        ReportPrint(F("    Pass worst: "));
        ReportPrint(result.pass_max_us);
        ReportPrintln(F(" us"));

        ReportPrint(F("    Pass deadline: "));
        ReportPrint(deadline_us);
        ReportPrintln(F(" us"));

        ReportPrint(F("    Deadline misses: "));
        ReportPrint(result.deadline_misses);
        ReportPrint(F(" of "));
        ReportPrintln(result.passes);

        // Positive margin means the worst observed service pass still fit
        // inside the audio time that one refill chunk represents.
        ReportPrint(F("    Realtime margin: "));

        if (deadline_us != 0)
        {
            const std::int32_t margin =
                static_cast<std::int32_t>(
                    ((static_cast<std::int64_t>(deadline_us) -
                      static_cast<std::int64_t>(result.pass_max_us)) *
                     100) /
                    static_cast<std::int64_t>(deadline_us));

            ReportPrintSigned(margin);
        }
        else
        {
            ReportPrint(static_cast<std::uint32_t>(0));
        }

        ReportPrintln(F(" %"));

        ReportPrint(F("    Ring drain time: "));
        ReportPrint(RingDrainUs());
        ReportPrintln(F(" us"));

        ReportPrint(F("    Worst refill vs ring drain: "));

        if (result.chunk_max_us < RingDrainUs())
            ReportPrintln(F("SAFE"));
        else
            ReportPrintln(F("UNDERRUN RISK"));

        ReportPrint(F("    Estimated sustainable voices: "));

        ReportPrintln(
            result.chunk_max_us != 0
                ? deadline_us / result.chunk_max_us
                : 0u);

        ReportPrint(F("    Read failures: "));
        ReportPrintln(result.read_failures);

        if (with_audio_load)
        {
            ReportPrint(F("    Audio ISR executions: "));
            ReportPrintln(result.isr_count);

            ReportPrint(F("    Audio ISR nominal interval: "));
            ReportPrint(AudioBlockIntervalUs());
            ReportPrintln(F(" us"));

            ReportPrint(F("    Audio ISR worst interval: "));
            ReportPrint(result.isr_max_interval_us);
            ReportPrintln(F(" us"));

            // Any interval beyond two nominal periods means the audio
            // update was delayed long enough to drop a block.
            ReportPrint(F("    Audio cadence: "));

            if (result.isr_max_interval_us <
                (AudioBlockIntervalUs() * 2u))
            {
                ReportPrintln(F("STABLE"));
            }
            else
            {
                ReportPrintln(F("DISTURBED BY SD ACTIVITY"));
            }
        }

        ReportPrintln();
    }

    void RunStreamSweep(bool with_audio_load)
    {
        ReportPrintln();

        if (with_audio_load)
        {
            ReportPrintln(F(
                "Concurrent streaming sweep (simulated audio load):"));
        }
        else
        {
            ReportPrintln(F(
                "Concurrent streaming sweep (idle CPU):"));
        }

        ReportPrintln();

        std::uint32_t sustainable = 0;

        for (std::uint32_t count = 1;
             count <= stream_candidate_count;
             ++count)
        {
            StreamResult result;

            RunConcurrentStreamTest(
                count,
                with_audio_load,
                result);

            PrintStreamResult(result, with_audio_load);

            if (result.started &&
                result.read_failures == 0 &&
                result.deadline_misses == 0)
            {
                sustainable = count;
            }

            if (result.read_failures != 0)
                benchmark_ok = false;

            FlushReport();
        }

        ReportPrint(F("  Highest stream count with zero deadline misses: "));
        ReportPrintln(sustainable);

        if (with_audio_load)
            max_sustainable_streams_loaded = sustainable;
        else
            max_sustainable_streams_idle = sustainable;
    }

    void RunNoteOnLatencyTest()
    {
        ReportPrintln();
        ReportPrintln(F(
            "Note-on latency (open + seek + first refill):"));

        if (stream_candidate_count == 0)
        {
            ReportPrintln(
                F("  SKIPPED: no streaming candidates"));

            return;
        }

        std::uint32_t min_us = UINT32_MAX;
        std::uint32_t max_us = 0;
        std::uint64_t total_us = 0;
        std::uint32_t trials = 0;
        std::uint32_t failures = 0;

        for (std::uint32_t trial = 0;
             trial < NOTE_ON_TRIALS;
             ++trial)
        {
            const StreamCandidate& candidate =
                stream_candidates[
                    trial % stream_candidate_count];

            const std::uint32_t start = micros();

            File file =
                SD.open(candidate.path, FILE_READ);

            if (!file ||
                !file.seek(candidate.data_offset))
            {
                if (file)
                    file.close();

                ++failures;
                continue;
            }

            std::uint32_t read_us = 0;

            const bool ok =
                TimedRead(
                    file,
                    stream_buffers[0],
                    STREAM_CHUNK_SIZE,
                    read_us);

            const std::uint32_t elapsed_us =
                micros() - start;

            file.close();

            if (!ok)
            {
                ++failures;
                continue;
            }

            total_us += elapsed_us;
            ++trials;

            if (elapsed_us < min_us)
                min_us = elapsed_us;

            if (elapsed_us > max_us)
                max_us = elapsed_us;
        }

        ReportPrint(F("  Trials: "));
        ReportPrintln(trials);

        ReportPrint(F("  Failures: "));
        ReportPrintln(failures);

        if (trials == 0)
            return;

        ReportPrint(F("  Min: "));
        ReportPrint(min_us);
        ReportPrintln(F(" us"));

        ReportPrint(F("  Avg: "));

        ReportPrint(
            static_cast<std::uint32_t>(
                total_us / trials));

        ReportPrintln(F(" us"));

        ReportPrint(F("  Worst: "));
        ReportPrint(max_us);
        ReportPrintln(F(" us"));

        note_on_worst_us = max_us;

        // A note must sound in the next audio block, so anything above one
        // audio block period has to be covered by a RAM-resident head cache.
        ReportPrint(F("  Audio block period: "));
        ReportPrint(AudioBlockIntervalUs());
        ReportPrintln(F(" us"));

        ReportPrint(F("  Head cache required: "));

        if (max_us <= AudioBlockIntervalUs())
            ReportPrintln(F("NO"));
        else
            ReportPrintln(F("YES"));

        // Minimum head cache that covers the worst observed note-on cost.
        ReportPrint(F("  Minimum head cache to cover worst case: "));

        ReportPrint(
            static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(max_us) *
                 StreamBytesPerSecond()) /
                1000000ull));

        ReportPrintln(F(" bytes"));
    }

    void RunRandomSeekTest()
    {
        ReportPrintln();
        ReportPrintln(F(
            "Random seek + refill latency (single open file):"));

        if (stream_candidate_count == 0)
        {
            ReportPrintln(
                F("  SKIPPED: no streaming candidates"));

            return;
        }

        const StreamCandidate& candidate =
            stream_candidates[0];

        if (candidate.data_size <= STREAM_CHUNK_SIZE)
        {
            ReportPrintln(
                F("  SKIPPED: candidate too small"));

            return;
        }

        File file =
            SD.open(candidate.path, FILE_READ);

        if (!file)
        {
            ReportPrintln(
                F("  SKIPPED: open failed"));

            return;
        }

        const std::uint32_t range =
            candidate.data_size - STREAM_CHUNK_SIZE;

        std::uint32_t min_us = UINT32_MAX;
        std::uint32_t max_us = 0;
        std::uint64_t total_us = 0;
        std::uint32_t trials = 0;
        std::uint32_t failures = 0;

        for (std::uint32_t trial = 0;
             trial < RANDOM_SEEK_TRIALS;
             ++trial)
        {
            const std::uint32_t offset =
                candidate.data_offset +
                ((NextRandom() % range) & ~511u);

            const std::uint32_t start = micros();

            if (!file.seek(offset))
            {
                ++failures;
                continue;
            }

            std::uint32_t read_us = 0;

            const bool ok =
                TimedRead(
                    file,
                    stream_buffers[0],
                    STREAM_CHUNK_SIZE,
                    read_us);

            const std::uint32_t elapsed_us =
                micros() - start;

            if (!ok)
            {
                ++failures;
                continue;
            }

            total_us += elapsed_us;
            ++trials;

            if (elapsed_us < min_us)
                min_us = elapsed_us;

            if (elapsed_us > max_us)
                max_us = elapsed_us;
        }

        file.close();

        ReportPrint(F("  Trials: "));
        ReportPrintln(trials);

        ReportPrint(F("  Failures: "));
        ReportPrintln(failures);

        if (trials == 0)
            return;

        ReportPrint(F("  Min: "));
        ReportPrint(min_us);
        ReportPrintln(F(" us"));

        ReportPrint(F("  Avg: "));

        ReportPrint(
            static_cast<std::uint32_t>(
                total_us / trials));

        ReportPrintln(F(" us"));

        ReportPrint(F("  Worst: "));
        ReportPrint(max_us);
        ReportPrintln(F(" us"));
    }

    void RunStreamingBenchmark()
    {
        ReportPrintln();
        ReportPrintln(F(
            "=========================================="));

        ReportPrintln(F(
            "Phase 2: sample streaming access pattern"));

        ReportPrintln(F(
            "=========================================="));

        ReportPrintln();

        ReportPrint(F("Streaming candidates: "));
        ReportPrintln(stream_candidate_count);

        if (stream_candidate_count == 0)
        {
            ReportPrintln(F(
                "SKIPPED: no valid WAV file large enough to stream."));

            return;
        }

        ReportPrint(F("Refill chunk size: "));
        ReportPrint(STREAM_CHUNK_SIZE);
        ReportPrintln(F(" bytes"));

        ReportPrint(F("Ring buffer per voice: "));
        ReportPrint(STREAM_RING_BYTES);
        ReportPrintln(F(" bytes"));

        ReportPrint(F("Consumption model: "));
        ReportPrint(STREAM_SAMPLE_RATE_HZ);
        ReportPrint(F(" Hz, 16-bit mono, pitch "));
        ReportPrint(STREAM_PITCH_PERCENT);
        ReportPrintln(F(" %"));

        ReportPrint(F("Consumption rate: "));
        ReportPrint(StreamBytesPerSecond());
        ReportPrintln(F(" bytes/s per voice"));

        ReportPrint(F("Refill deadline: "));
        ReportPrint(ChunkAudioUs());
        ReportPrintln(F(" us"));

        ReportPrint(F("Passes per stream count: "));
        ReportPrintln(STREAM_PASSES);

        FlushReport();

        RunStreamSweep(false);

        FlushReport();

        if (STREAM_TEST_WITH_AUDIO_LOAD)
        {
            RunStreamSweep(true);
            FlushReport();
        }

        RunNoteOnLatencyTest();
        FlushReport();

        RunRandomSeekTest();
        FlushReport();
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

            // Retain the first few sufficiently large files so Phase 2 can
            // stream from several distinct locations on the card.
            if (stream_candidate_count < STREAM_MAX_COUNT &&
                wav.data_size > (STREAM_CHUNK_SIZE * 2u))
            {
                StreamCandidate& candidate =
                    stream_candidates[stream_candidate_count];

                std::snprintf(
                    candidate.path,
                    sizeof(candidate.path),
                    "%s",
                    batch[i].path);

                candidate.data_offset = wav.data_offset;
                candidate.data_size = wav.data_size;

                ++stream_candidate_count;
            }

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
    // A numeric suffix prevents overwriting another result generated by
    // the same build.
    // ---------------------------------------------------------------------

    void FormatResultFilenameForSuffix(
        char* filename,
        std::size_t capacity,
        std::uint32_t suffix)
    {
        const char* month_text =
            kCompileDate;

        int month = 1;

        static const char* months =
            "JanFebMarAprMayJunJulAugSepOctNovDec";

        char month_code[4] =
        {
            month_text[0],
            month_text[1],
            month_text[2],
            '\0'
        };

        const char* month_ptr =
            std::strstr(
                months,
                month_code);

        if (month_ptr != nullptr)
        {
            month =
                static_cast<int>(
                    (month_ptr - months) / 3) + 1;
        }

        const int day =
            (month_text[4] == ' ')
                ? month_text[5] - '0'
                : (month_text[4] - '0') * 10 +
                  (month_text[5] - '0');

        const int year =
            (month_text[7] - '0') * 1000 +
            (month_text[8] - '0') * 100 +
            (month_text[9] - '0') * 10 +
            (month_text[10] - '0');

        const char* time_text =
            kCompileTime;

        const int hour =
            (time_text[0] - '0') * 10 +
            (time_text[1] - '0');

        const int minute =
            (time_text[3] - '0') * 10 +
            (time_text[4] - '0');

        const int second =
            (time_text[6] - '0') * 10 +
            (time_text[7] - '0');

        std::snprintf(
            filename,
            capacity,
            "%sSD_BENCH_%04d%02d%02d_%02d%02d%02d_%04lu.txt",
            BENCHMARK_OUTPUT_PATH,
            year,
            month,
            day,
            hour,
            minute,
            second,
            suffix);
    }

    void BuildResultFilename(
        char* filename,
        std::size_t capacity)
    {
        for (std::uint32_t suffix = 1;
             suffix <= 9999;
             ++suffix)
        {
            FormatResultFilenameForSuffix(
                filename,
                capacity,
                suffix);

            if (!SD.exists(filename))
                return;
        }

        filename[0] = '\0';
    }

    // Returns true if any result file for the exact current firmware build
    // already exists (any suffix 0001..9999).
    bool ResultAlreadyExistsForThisBuild()
    {
        char filename[96];

        for (std::uint32_t suffix = 1;
             suffix <= 9999;
             ++suffix)
        {
            FormatResultFilenameForSuffix(
                filename,
                sizeof(filename),
                suffix);

            if (SD.exists(filename))
                return true;
        }

        return false;
    }

    // ---------------------------------------------------------------------
    // Report setup
    // ---------------------------------------------------------------------

    bool OpenReport()
    {
        if (!SD.exists(
                BENCHMARK_OUTPUT_PATH))
        {
            if (!SD.mkdir(
                    BENCHMARK_OUTPUT_PATH))
            {
                if (!SD.exists(
                        BENCHMARK_OUTPUT_PATH))
                {
                    return false;
                }
            }
        }

        char filename[96];

        BuildResultFilename(
            filename,
            sizeof(filename));

        if (filename[0] == '\0')
            return false;

        report_file =
            SD.open(
                filename,
                FILE_WRITE);

        if (!report_file)
            return false;

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
            BENCHMARK_OUTPUT_PATH);

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

        ReportPrintln();
        ReportPrintln(
            F("Streaming summary:"));

        ReportPrint(F("  Streaming candidates: "));
        ReportPrintln(stream_candidate_count);

        ReportPrint(
            F("  Max concurrent streams (idle): "));

        ReportPrintln(
            max_sustainable_streams_idle);

        ReportPrint(
            F("  Max concurrent streams (audio load): "));

        if (STREAM_TEST_WITH_AUDIO_LOAD)
            ReportPrintln(max_sustainable_streams_loaded);
        else
            ReportPrintln(F("not tested"));

        ReportPrint(F("  Worst note-on latency: "));
        ReportPrint(note_on_worst_us);
        ReportPrintln(F(" us"));

        ReportPrintln();
        ReportPrintln(F(
            "NOTE: the streaming figures above are a measured baseline for"));

        ReportPrintln(F(
            "the reference SD card only. They do not define a BroTracker"));

        ReportPrintln(F(
            "voice-count requirement."));
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

    // Optional protection against duplicate host-triggered startup runs
    // for the same build.
    if (SKIP_DUPLICATE_BUILD_RUN &&
        ResultAlreadyExistsForThisBuild())
    {
        Serial.println(
            "Result file for this build already exists.");

        Serial.println(
            "Skipping duplicate run (likely a second reset after upload).");

        BlinkCompletion();

        return;
    }

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

    RunStreamingBenchmark();

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

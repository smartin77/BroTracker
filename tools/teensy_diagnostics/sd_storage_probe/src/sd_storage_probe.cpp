/*
 * BroTracker
 *
 * Description: Teensy 4.1 SD storage and filesystem diagnostic probe.
 *
 *              Tests the built-in SD card using the Teensy SD library.
 *
 *              The probe verifies:
 *
 *              - SD initialization
 *              - directory creation
 *              - file creation
 *              - binary write
 *              - flush / close
 *              - reopen / read
 *              - binary data verification
 *              - append
 *              - rename
 *              - delete
 *              - close / reopen persistence
 *
 *              Test output is written both to Serial and to a sequential
 *              log file on the SD card:
 *
 *                  BroTracker/sd_storage_probe-NNNN.log
 *
 *              Test binary files are stored in BroTracker/ alongside the
 *              probe report log and are removed after each test.
 *
 *              Temporary binary test files are removed after each test.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include <Arduino.h>
#include <SD.h>
#include <diagnostics.h>

#include <cstddef>
#include <cstdint>

namespace
{
    constexpr const char* TEST_FILE =
        "BroTracker/test.bin";

    constexpr const char* RENAMED_FILE =
        "BroTracker/renamed.bin";

    constexpr std::size_t SMALL_SIZE =
        1024;

    constexpr std::size_t MEDIUM_SIZE =
        64 * 1024;

    constexpr std::size_t LARGE_SIZE =
        1024 * 1024;

    constexpr std::size_t PERSISTENCE_SIZE =
        64 * 1024;

    constexpr std::size_t CHUNK_SIZE =
        512;

    constexpr uint32_t PATTERN_MULTIPLIER =
        37;

    constexpr uint32_t PATTERN_OFFSET =
        17;

    uint8_t write_buffer[CHUNK_SIZE];
    uint8_t read_buffer[CHUNK_SIZE];

    void* report_file_handle = nullptr;

    bool all_tests_passed = true;

    uint8_t ExpectedByte(
        std::size_t index)
    {
        return static_cast<uint8_t>(
            (static_cast<uint32_t>(index) *
                 PATTERN_MULTIPLIER +
             PATTERN_OFFSET) &
            0xFF);
    }

    void ReportPrint(
        const char* text)
    {
        Serial.print(text);

        if (report_file_handle)
        {
            BroTracker::ToolLogMessage(report_file_handle, text);
        }
    }

    void ReportPrint(
        std::size_t value)
    {
        Serial.print(value);

        if (report_file_handle)
        {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%zu", value);
            BroTracker::ToolLogMessage(report_file_handle, buffer);
        }
    }

    void ReportPrintln()
    {
        Serial.println();

        if (report_file_handle)
        {
            BroTracker::ToolLogMessage(report_file_handle, "");
        }
    }

    void ReportPrintln(
        const char* text)
    {
        Serial.println(text);

        if (report_file_handle)
        {
            BroTracker::ToolLogMessage(report_file_handle, text);
        }
    }

    void PrintResult(
        const char* name,
        bool success)
    {
        ReportPrint(name);
        ReportPrint(": ");
        ReportPrintln(
            success ? "PASS" : "FAIL");

        if (!success)
        {
            all_tests_passed = false;
        }
    }

    bool WritePattern(
        File& file,
        std::size_t size,
        std::size_t start_offset = 0)
    {
        std::size_t remaining = size;
        std::size_t offset = start_offset;

        while (remaining > 0)
        {
            const std::size_t count =
                remaining < CHUNK_SIZE
                    ? remaining
                    : CHUNK_SIZE;

            for (std::size_t index = 0;
                 index < count;
                 ++index)
            {
                write_buffer[index] =
                    ExpectedByte(
                        offset + index);
            }

            const std::size_t written =
                file.write(
                    write_buffer,
                    count);

            if (written != count)
            {
                return false;
            }

            offset += count;
            remaining -= count;
        }

        return true;
    }

    bool VerifyPattern(
        File& file,
        std::size_t size,
        std::size_t start_offset = 0)
    {
        std::size_t remaining = size;
        std::size_t offset = start_offset;

        while (remaining > 0)
        {
            const std::size_t count =
                remaining < CHUNK_SIZE
                    ? remaining
                    : CHUNK_SIZE;

            const int bytes_read =
                file.read(
                    read_buffer,
                    count);

            if (bytes_read !=
                static_cast<int>(count))
            {
                return false;
            }

            for (std::size_t index = 0;
                 index < count;
                 ++index)
            {
                if (read_buffer[index] !=
                    ExpectedByte(
                        offset + index))
                {
                    ReportPrint(
                        "Verification mismatch at byte ");

                    ReportPrint(
                        offset + index);

                    ReportPrintln();

                    return false;
                }
            }

            offset += count;
            remaining -= count;
        }

        return true;
    }

    bool CreateAndVerifyFile(
        std::size_t size)
    {
        SD.remove(TEST_FILE);

        File file =
            SD.open(
                TEST_FILE,
                FILE_WRITE);

        if (!file)
        {
            return false;
        }

        const bool write_ok =
            WritePattern(
                file,
                size);

        if (!write_ok)
        {
            file.close();
            return false;
        }

        file.flush();
        file.close();

        file =
            SD.open(
                TEST_FILE,
                FILE_READ);

        if (!file)
        {
            return false;
        }

        const bool verify_ok =
            VerifyPattern(
                file,
                size);

        file.close();

        return verify_ok;
    }

    bool AppendAndVerify(
        std::size_t original_size)
    {
        File file =
            SD.open(
                TEST_FILE,
                FILE_WRITE);

        if (!file)
        {
            return false;
        }

        const bool append_ok =
            WritePattern(
                file,
                original_size,
                original_size);

        if (!append_ok)
        {
            file.close();
            return false;
        }

        file.flush();
        file.close();

        file =
            SD.open(
                TEST_FILE,
                FILE_READ);

        if (!file)
        {
            return false;
        }

        const bool first_half_ok =
            VerifyPattern(
                file,
                original_size,
                0);

        const bool second_half_ok =
            first_half_ok &&
            VerifyPattern(
                file,
                original_size,
                original_size);

        file.close();

        return first_half_ok &&
               second_half_ok;
    }

    bool RenameAndVerify(
        std::size_t expected_size)
    {
        SD.remove(RENAMED_FILE);

        if (!SD.rename(
                TEST_FILE,
                RENAMED_FILE))
        {
            return false;
        }

        if (SD.exists(TEST_FILE))
        {
            return false;
        }

        if (!SD.exists(RENAMED_FILE))
        {
            return false;
        }

        File file =
            SD.open(
                RENAMED_FILE,
                FILE_READ);

        if (!file)
        {
            return false;
        }

        const bool verify_ok =
            VerifyPattern(
                file,
                expected_size);

        file.close();

        return verify_ok;
    }

    bool DeleteAndVerify()
    {
        if (!SD.remove(
                RENAMED_FILE))
        {
            return false;
        }

        return !SD.exists(
            RENAMED_FILE);
    }

    bool RunFileTest(
        const char* label,
        std::size_t size)
    {
        ReportPrintln();
        ReportPrint("-- ");
        ReportPrint(label);
        ReportPrint(" (");
        ReportPrint(size);
        ReportPrintln(" bytes) --");

        const bool initial_ok =
            CreateAndVerifyFile(size);

        PrintResult(
            "Create / write / verify",
            initial_ok);

        if (!initial_ok)
        {
            SD.remove(TEST_FILE);
            return false;
        }

        const bool append_ok =
            AppendAndVerify(size);

        PrintResult(
            "Append / verify",
            append_ok);

        if (!append_ok)
        {
            SD.remove(TEST_FILE);
            return false;
        }

        const std::size_t total_size =
            size * 2;

        const bool rename_ok =
            RenameAndVerify(total_size);

        PrintResult(
            "Rename / verify",
            rename_ok);

        if (!rename_ok)
        {
            SD.remove(TEST_FILE);
            SD.remove(RENAMED_FILE);
            return false;
        }

        const bool delete_ok =
            DeleteAndVerify();

        PrintResult(
            "Delete / verify",
            delete_ok);

        return delete_ok;
    }

    bool RunPersistenceTest()
    {
        ReportPrintln();
        ReportPrintln(
            "-- Close / reopen persistence test --");

        SD.remove(TEST_FILE);

        File file =
            SD.open(
                TEST_FILE,
                FILE_WRITE);

        if (!file)
        {
            PrintResult(
                "Create persistence file",
                false);

            return false;
        }

        const bool write_ok =
            WritePattern(
                file,
                PERSISTENCE_SIZE);

        file.flush();
        file.close();

        PrintResult(
            "Write persistence file",
            write_ok);

        if (!write_ok)
        {
            SD.remove(TEST_FILE);
            return false;
        }

        file =
            SD.open(
                TEST_FILE,
                FILE_READ);

        if (!file)
        {
            PrintResult(
                "Reopen persistence file",
                false);

            SD.remove(TEST_FILE);
            return false;
        }

        const bool verify_ok =
            VerifyPattern(
                file,
                PERSISTENCE_SIZE);

        file.close();

        PrintResult(
            "Verify after close/reopen",
            verify_ok);

        const bool delete_ok =
            SD.remove(TEST_FILE) &&
            !SD.exists(TEST_FILE);

        PrintResult(
            "Delete after close/reopen",
            delete_ok);

        return write_ok &&
               verify_ok &&
               delete_ok;
    }

    void CleanupTemporaryFiles()
    {
        SD.remove(TEST_FILE);
        SD.remove(RENAMED_FILE);
    }
}

void setup()
{
    Serial.begin(115200);

    while (!Serial && millis() < 3000)
    {
        // Give the serial monitor a short time to attach.
    }

    Serial.println();
    Serial.println(
        "=== BroTracker SD Storage Probe ===");
    Serial.println();

    Serial.println(
        "Initializing diagnostics...");

    if (!BroTracker::DiagnosticsInitialize())
    {
        Serial.println(
            "Diagnostics initialization: FAIL");

        Serial.println();
        Serial.println(
            "SD storage probe aborted.");

        return;
    }

    Serial.println(
        "Diagnostics initialization: PASS");

    Serial.println();
    Serial.println(
        "-- Filesystem setup --");

    // Verify BroTracker directory exists (created by DiagnosticsInitialize)
    if (!SD.exists("BroTracker"))
    {
        Serial.println(
            "BroTracker directory available: FAIL");

        Serial.println();
        Serial.println(
            "SD storage probe aborted.");

        return;
    }

    Serial.println(
        "BroTracker directory available: PASS");

    /*
     * Open tool-specific log file using the reusable diagnostics API.
     * Each probe run gets its own sequential log file.
     */
    report_file_handle = BroTracker::OpenToolLogFile("sd_storage_probe");

    if (!report_file_handle)
    {
        Serial.println(
            "Report file open: FAIL");

        Serial.println();
        Serial.println(
            "SD storage probe aborted.");

        return;
    }

    ReportPrintln();
    ReportPrintln(
        "========================================");
    ReportPrintln(
        "BroTracker SD Storage Probe");
    ReportPrintln(
        "========================================");

    ReportPrint(
        "Test directory: ");
    ReportPrintln(
        "BroTracker");

    ReportPrint(
        "Report file: ");
    ReportPrintln(
        "BroTracker/sd_storage_probe-NNNN.log");

    ReportPrintln();

    const bool small_ok =
        RunFileTest(
            "Small file",
            SMALL_SIZE);

    const bool medium_ok =
        RunFileTest(
            "Medium file",
            MEDIUM_SIZE);

    const bool large_ok =
        RunFileTest(
            "Large file",
            LARGE_SIZE);

    const bool persistence_ok =
        RunPersistenceTest();

    PrintResult(
        "Close / reopen persistence",
        persistence_ok);

    CleanupTemporaryFiles();

    const bool cleanup_ok =
        !SD.exists(TEST_FILE) &&
        !SD.exists(RENAMED_FILE);

    PrintResult(
        "Temporary files removed",
        cleanup_ok);

    ReportPrintln();
    ReportPrintln(
        "========================================");

    const bool overall_ok =
        small_ok &&
        medium_ok &&
        large_ok &&
        persistence_ok &&
        cleanup_ok;

    if (overall_ok)
    {
        ReportPrintln(
            "SD STORAGE PROBE: PASS");

        ReportPrintln(
            "========================================");

        ReportPrintln();
        ReportPrintln(
            "Probe complete.");

        BroTracker::CloseToolLogFile(report_file_handle);
        report_file_handle = nullptr;

        Serial.println();
        Serial.println(
            "Report saved to:");
        Serial.println(
            "BroTracker/sd_storage_probe-NNNN.log");

        Serial.println();
        Serial.println(
            "Test files removed from BroTracker/.");
        Serial.println(
            "Test directory and report preserved.");

        Serial.println();
        Serial.println(
            "Three long LED flashes indicate completion.");

        BroTracker::DiagnosticBlink(3);
    }
    else
    {
        ReportPrintln(
            "SD STORAGE PROBE: FAIL");

        ReportPrintln(
            "========================================");

        ReportPrintln();
        ReportPrintln(
            "Probe complete.");

        BroTracker::CloseToolLogFile(report_file_handle);
        report_file_handle = nullptr;

        Serial.println();
        Serial.println(
            "Report saved to:");
        Serial.println(
            "BroTracker/sd_storage_probe-NNNN.log");

        Serial.println();
        Serial.println(
            "Test files removed from BroTracker/.");
        Serial.println(
            "Test directory and report preserved.");
    }
}

void loop()
{
    // All tests run once from setup().
}
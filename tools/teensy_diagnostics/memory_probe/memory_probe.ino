/*
 * BroTracker
 *
 * Description: Teensy 4.1 memory placement and access-latency probe.
 *              Answers the open questions in
 *              docs/TEENSY_MEMORY_ARCHITECTURE.md by printing where
 *              FASTRUN/DMAMEM/EXTMEM/default globals and functions
 *              actually land, and by measuring their relative access cost.
 *              Results are logged to Serial and appended as a new,
 *              uniquely-numbered file under BroTracker/ on the built-in
 *              SD card, so existing card content is never touched.
 *
 *              This is the Arduino IDE / Teensyduino copy of
 *              tools/teensy_diagnostics/src/main.cpp (kept identical by
 *              hand, since PlatformIO's src/ layout and Arduino IDE's
 *              folder-name-must-match-sketch-name rule can't share one
 *              file). Prefer building via PlatformIO when possible; only
 *              update this copy if the PlatformIO source changes.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include <Arduino.h>
#include <SD.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

// Populated by the Teensyduino startup code on Teensy 4.1 boards; zero if
// no PSRAM chip is soldered on.
extern "C" uint8_t external_psram_size;

extern "C"
{
    extern unsigned long _stext;
    extern unsigned long _etext;
}

namespace
{
    constexpr std::size_t BUFFER_SIZE = 4096;
    constexpr int ITERATIONS = 1000;

    constexpr const char* LOG_DIRECTORY = "BroTracker";
    constexpr const char* RUN_COUNTER_PATH = "BroTracker/run_count.txt";

    // Non-attributed globals default to RAM1 (DTCM) on Teensy 4.x.
    volatile uint8_t default_buffer[BUFFER_SIZE];

    // RAM2 / DMA-capable region.
    DMAMEM volatile uint8_t dma_buffer[BUFFER_SIZE];

    // External PSRAM; only backed by real memory if a chip is installed.
    EXTMEM volatile uint8_t psram_buffer[BUFFER_SIZE];

    FLASHMEM void FunctionInFlash()
    {
        __asm__ volatile ("nop");
    }

    // Forwards every write to Serial and (if open) the SD log file, so the
    // rest of the probe only has to log each value in one place.
    class TeeOutput : public Print
    {
    public:
        void Begin(File& file)
        {
            log_file = &file;
        }

        size_t write(uint8_t byte) override
        {
            Serial.write(byte);

            if (log_file != nullptr)
            {
                log_file->write(byte);
            }

            return 1;
        }
    private:
        File* log_file = nullptr;
    };

    TeeOutput output;

    void PrintAddress(
        const char* label,
        const volatile void* address)
    {
        output.print(label);
        output.print(": 0x");
        output.println(
            reinterpret_cast<std::uint32_t>(address),
            HEX);
    }

    // Writes then reads back `buffer` ITERATIONS times and returns the
    // average CPU cycles per byte touched, using the ARM DWT cycle counter.
    std::uint32_t MeasureAccessCycles(
        volatile uint8_t* buffer,
        std::size_t size)
    {
        const std::uint32_t start = ARM_DWT_CYCCNT;

        for (int iteration = 0; iteration < ITERATIONS; ++iteration)
        {
            for (std::size_t index = 0; index < size; ++index)
            {
                buffer[index] = static_cast<uint8_t>(index);
            }

            volatile uint8_t sink = 0;

            for (std::size_t index = 0; index < size; ++index)
            {
                sink += buffer[index];
            }
        }

        const std::uint32_t end = ARM_DWT_CYCCNT;

        const std::uint32_t total_bytes =
            static_cast<std::uint32_t>(size) *
            static_cast<std::uint32_t>(ITERATIONS) * 2;

        return (end - start) / total_bytes;
    }

    // Reads/increments a small counter file on the SD card so repeated runs
    // each get their own log file instead of overwriting previous results.
    unsigned int NextRunNumber()
    {
        unsigned int run_number = 1;

        File counter_file = SD.open(RUN_COUNTER_PATH, FILE_READ);

        if (counter_file)
        {
            run_number = counter_file.parseInt() + 1;
            counter_file.close();
        }

        // FILE_WRITE appends on Teensy's SD library, so the old value must
        // be removed first to avoid accumulating stale counter lines.
        SD.remove(RUN_COUNTER_PATH);

        File updated_file = SD.open(RUN_COUNTER_PATH, FILE_WRITE);

        if (updated_file)
        {
            updated_file.println(run_number);
            updated_file.close();
        }

        return run_number;
    }
}

void setup()
{
    // Enable the ARM Cortex-M7 DWT cycle counter (CoreDebug/DWT registers
    // are at fixed addresses on any Cortex-M7, independent of core headers).
    *reinterpret_cast<volatile uint32_t*>(0xE000EDFCu) |= (1u << 24); // DEMCR.TRCENA
    *reinterpret_cast<volatile uint32_t*>(0xE0001000u) |= 1u;         // DWT_CTRL.CYCCNTENA

    Serial.begin(115200);

    while (!Serial && millis() < 3000)
    {
        // Wait briefly for the host serial monitor to attach.
    }

    const bool sd_ready = SD.begin(BUILTIN_SDCARD);

    File log_file;

    if (sd_ready)
    {
        SD.mkdir(LOG_DIRECTORY); // No-op if it already exists; never erases the card.

        const unsigned int run_number = NextRunNumber();

        char filename[48];
        std::snprintf(
            filename,
            sizeof(filename),
            "%s/memory_probe_%03u.txt",
            LOG_DIRECTORY,
            run_number);

        log_file = SD.open(filename, FILE_WRITE);
        output.Begin(log_file);

        Serial.print("Logging to SD: ");
        Serial.println(filename);
    }
    else
    {
        Serial.println(
            "SD card not detected - logging to Serial only.");
    }

    output.println();
    output.println("=== BroTracker Teensy 4.1 memory probe ===");
    output.println();

    output.println("-- Symbol addresses (compare against the datasheet / linker map) --");

    PrintAddress(
        "ITCM start        (linker _stext)",
        reinterpret_cast<const volatile void*>(&_stext));

    PrintAddress(
        "ITCM end          (linker _etext)",
        reinterpret_cast<const volatile void*>(&_etext));

    PrintAddress(
        "FLASHMEM function (expect Flash/FlexSPI)",
        reinterpret_cast<const volatile void*>(&FunctionInFlash));

    PrintAddress(
        "Default global    (expect RAM1/DTCM)",
        default_buffer);

    PrintAddress(
        "DMAMEM buffer     (expect RAM2)",
        dma_buffer);

    output.print("PSRAM detected: ");
    output.print(external_psram_size);
    output.println(" MB");

    if (external_psram_size > 0)
    {
        PrintAddress(
            "EXTMEM buffer     (expect PSRAM)",
            psram_buffer);
    }
    else
    {
        output.println(
            "No PSRAM chip detected - skipping EXTMEM address/latency test.");
    }

    output.println();
    output.println("-- Relative access cost (average CPU cycles per byte) --");

    const std::uint32_t default_cycles =
        MeasureAccessCycles(default_buffer, BUFFER_SIZE);

    output.print("Default (RAM1/DTCM): ");
    output.print(default_cycles);
    output.println(" cycles/byte");

    const std::uint32_t dma_cycles =
        MeasureAccessCycles(dma_buffer, BUFFER_SIZE);

    output.print("DMAMEM  (RAM2):      ");
    output.print(dma_cycles);
    output.println(" cycles/byte");

    if (external_psram_size > 0)
    {
        const std::uint32_t psram_cycles =
            MeasureAccessCycles(psram_buffer, BUFFER_SIZE);

        output.print("EXTMEM  (PSRAM):     ");
        output.print(psram_cycles);
        output.println(" cycles/byte");
    }

    output.println();
    output.println(
        "Probe complete - answers open questions 1-3 and 8 in "
        "docs/TEENSY_MEMORY_ARCHITECTURE.md.");

    if (log_file)
    {
        log_file.close();
    }

    Serial.println();
    Serial.println("Done. Safe to reset or reflash the board now.");
}

void loop()
{
    // Nothing to do; all measurements run once in setup().
}

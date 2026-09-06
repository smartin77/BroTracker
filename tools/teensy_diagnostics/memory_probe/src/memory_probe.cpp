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
 *              This is the Arduino IDE / Teensyduino
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include <Arduino.h>
#include <SD.h>
#include <DMAChannel.h>
#include <diagnostics.h>

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
    extern unsigned long _sdata;
    extern unsigned long _edata;
    extern unsigned long _sbss;
    extern unsigned long _ebss;
    extern unsigned long _estack;
}

namespace
{
    constexpr std::size_t BUFFER_SIZE = 4096;
    constexpr int ITERATIONS = 1000;

    // Non-attributed globals default to RAM1 (DTCM) on Teensy 4.x.
    volatile uint8_t default_buffer[BUFFER_SIZE];

    // RAM2 / DMA-capable region.
    DMAMEM volatile uint8_t dma_buffer[BUFFER_SIZE];

    // DMA test destination in RAM2.
    DMAMEM volatile uint8_t dma_test_destination[BUFFER_SIZE];

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
        void SetLogFile(void* file)
        {
            log_file = file;
        }

        size_t write(uint8_t byte) override
        {
            Serial.write(byte);

            // Buffer the character until newline for logging
            if (byte == '\n' || buffer_pos >= sizeof(buffer) - 1)
            {
                if (byte != '\n' && buffer_pos < sizeof(buffer) - 1)
                    buffer[buffer_pos++] = byte;

                buffer[buffer_pos] = '\0';

                if (log_file && buffer_pos > 0)
                {
                    BroTracker::ToolLogMessage(log_file, buffer);
                }

                buffer_pos = 0;
                return 1;
            }

            if (buffer_pos < sizeof(buffer) - 1)
                buffer[buffer_pos++] = byte;

            return 1;
        }

    private:
        void* log_file = nullptr;
        char buffer[256] = {};
        size_t buffer_pos = 0;
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

    bool RunDmaMemoryTest(
        volatile uint8_t* source,
        volatile uint8_t* destination,
        std::size_t size)
    {
        for (std::size_t index = 0; index < size; ++index)
        {
            source[index] = static_cast<uint8_t>(index);
            destination[index] = 0;
        }

        DMAChannel dma;
        dma.sourceBuffer(source, static_cast<unsigned int>(size));
        dma.destinationBuffer(destination, static_cast<unsigned int>(size));
        dma.transferSize(1);

        dma.enable();

        while (!dma.complete())
        {
        }

        dma.clearComplete();

        for (std::size_t index = 0; index < size; ++index)
        {
            if (destination[index] != static_cast<uint8_t>(index))
            {
                return false;
            }
        }

        return true;
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

    // Initialize diagnostics and open tool-specific log file
    if (BroTracker::DiagnosticsInitialize())
    {
        void* tool_log_file = BroTracker::OpenToolLogFile("memory_probe");

        if (tool_log_file)
        {
            output.SetLogFile(tool_log_file);
            Serial.println("Tool log file created - logging to SD");
        }
        else
        {
            Serial.println("WARNING: Could not open tool log file - logging to Serial only");
        }
    }
    else
    {
        Serial.println("SD card not detected - logging to Serial only.");
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

    PrintAddress(
    "DTCM data start    (linker _sdata)",
    reinterpret_cast<const volatile void*>(&_sdata));

    PrintAddress(
        "DTCM data end      (linker _edata)",
        reinterpret_cast<const volatile void*>(&_edata));

    PrintAddress(
        "DTCM BSS start     (linker _sbss)",
        reinterpret_cast<const volatile void*>(&_sbss));

    PrintAddress(
        "DTCM BSS end       (linker _ebss)",
        reinterpret_cast<const volatile void*>(&_ebss));

    PrintAddress(
        "DTCM stack top     (linker _estack)",
        reinterpret_cast<const volatile void*>(&_estack));

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

    output.println();
    output.println("-- DMA memory test --");

    const bool dma_ram2_ok = RunDmaMemoryTest(
        dma_buffer,
        dma_test_destination,
        BUFFER_SIZE);

    output.print("RAM2 -> RAM2 DMA: ");
    output.println(dma_ram2_ok ? "PASS" : "FAIL");

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

    Serial.println();
    Serial.println(
        "Three long LED flashes indicate completion.");

    BroTracker::DiagnosticBlink(3);

    Serial.println();
    Serial.println("Done. Safe to reset or reflash the board now.");
}

void loop()
{
    // Nothing to do; all measurements run once in setup().
}

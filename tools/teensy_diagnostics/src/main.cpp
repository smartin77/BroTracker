/*
 * BroTracker
 *
 * Description: Teensy 4.1 memory placement and access-latency probe.
 *              Answers the open questions in
 *              docs/TEENSY_MEMORY_ARCHITECTURE.md by printing where
 *              FASTRUN/DMAMEM/EXTMEM/default globals and functions
 *              actually land, and by measuring their relative access cost.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include <Arduino.h>

#include <cstddef>
#include <cstdint>

// Populated by the Teensyduino startup code on Teensy 4.1 boards; zero if
// no PSRAM chip is soldered on.
extern "C" uint8_t external_psram_size;

namespace
{
    constexpr std::size_t BUFFER_SIZE = 4096;
    constexpr int ITERATIONS = 1000;

    // Non-attributed globals default to RAM1 (DTCM) on Teensy 4.x.
    volatile uint8_t default_buffer[BUFFER_SIZE];

    // RAM2 / DMA-capable region.
    DMAMEM volatile uint8_t dma_buffer[BUFFER_SIZE];

    // External PSRAM; only backed by real memory if a chip is installed.
    EXTMEM volatile uint8_t psram_buffer[BUFFER_SIZE];

    FASTRUN void FunctionInItcm()
    {
        __asm__ volatile ("nop");
    }

    void FunctionInFlashDefault()
    {
        __asm__ volatile ("nop");
    }

    void PrintAddress(
        const char* label,
        const volatile void* address)
    {
        Serial.print(label);
        Serial.print(": 0x");
        Serial.println(
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

    Serial.println();
    Serial.println("=== BroTracker Teensy 4.1 memory probe ===");
    Serial.println();

    Serial.println("-- Symbol addresses (compare against the datasheet / linker map) --");

    PrintAddress(
        "FASTRUN function  (expect ITCM)",
        reinterpret_cast<const volatile void*>(&FunctionInItcm));

    PrintAddress(
        "Default function  (expect Flash/FlexSPI)",
        reinterpret_cast<const volatile void*>(&FunctionInFlashDefault));

    PrintAddress(
        "Default global    (expect RAM1/DTCM)",
        default_buffer);

    PrintAddress(
        "DMAMEM buffer     (expect RAM2)",
        dma_buffer);

    Serial.print("PSRAM detected: ");
    Serial.print(external_psram_size);
    Serial.println(" MB");

    if (external_psram_size > 0)
    {
        PrintAddress(
            "EXTMEM buffer     (expect PSRAM)",
            psram_buffer);
    }
    else
    {
        Serial.println(
            "No PSRAM chip detected - skipping EXTMEM address/latency test.");
    }

    Serial.println();
    Serial.println("-- Relative access cost (average CPU cycles per byte) --");

    const std::uint32_t default_cycles =
        MeasureAccessCycles(default_buffer, BUFFER_SIZE);

    Serial.print("Default (RAM1/DTCM): ");
    Serial.print(default_cycles);
    Serial.println(" cycles/byte");

    const std::uint32_t dma_cycles =
        MeasureAccessCycles(dma_buffer, BUFFER_SIZE);

    Serial.print("DMAMEM  (RAM2):      ");
    Serial.print(dma_cycles);
    Serial.println(" cycles/byte");

    if (external_psram_size > 0)
    {
        const std::uint32_t psram_cycles =
            MeasureAccessCycles(psram_buffer, BUFFER_SIZE);

        Serial.print("EXTMEM  (PSRAM):     ");
        Serial.print(psram_cycles);
        Serial.println(" cycles/byte");
    }

    Serial.println();
    Serial.println(
        "Probe complete - answers open questions 1-3 and 8 in "
        "docs/TEENSY_MEMORY_ARCHITECTURE.md.");
}

void loop()
{
    // Nothing to do; all measurements run once in setup().
}

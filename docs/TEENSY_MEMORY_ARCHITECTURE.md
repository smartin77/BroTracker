# Teensy 4.1 Memory Architecture

These are preliminary working notes for the BroTracker low-level memory architecture.

This document is not a final architecture decision. The detailed Teensy 4.1 analysis will be completed before the realtime kernel memory model is finalized.

## Reference Platform

Teensy 4.1 is the reference realtime platform for BroTracker.

The initial target is the NXP i.MX RT1062 running at 600 MHz.

## Internal Memory

Teensy 4.1 provides two primary internal RAM regions:

- RAM1: 512 KB
- RAM2: 512 KB

RAM1 is used as tightly coupled memory and is divided between instruction and data use:

- ITCM — Instruction Tightly Coupled Memory
- DTCM — Data Tightly Coupled Memory

RAM2 is the preferred internal memory region for DMA-capable buffers and larger working data.

## Working Memory Model

### ITCM

ITCM is intended for time-critical code where deterministic and very fast instruction execution matters.

Potential BroTracker uses:

- interrupt handlers;
- realtime audio processing;
- mixer and DSP hot paths;
- timing-critical scheduler code;
- other frequently executed realtime routines.

Non-realtime code such as UI handling, importers, project loading and debugging should not consume ITCM unnecessarily.

### DTCM

DTCM is intended for time-critical runtime state and other frequently accessed CPU data.

Potential BroTracker uses:

- sequencer state;
- playback state;
- voice state;
- envelopes and LFO state;
- MIDI parser state;
- small realtime lookup tables;
- stack and other runtime data.

The available DTCM capacity must account for stack usage and other runtime allocations.

### RAM2 / DMAMEM

RAM2 is intended for data buffers and DMA-related storage.

Potential BroTracker uses:

- audio buffers;
- SD buffers and caches;
- SPI buffers;
- display buffers;
- DMA-related data structures;
- other large working buffers.

The `DMAMEM` mechanism should be evaluated as part of the final platform implementation.

### Flash

On-board Flash is intended for normal program code and persistent read-only data that does not require realtime execution from TCM.

Potential uses include:

- normal application code;
- UI code;
- importers;
- fonts and static resources;
- constant data and lookup tables where appropriate.

Teensy-specific placement mechanisms such as `FLASHMEM`, `PROGMEM` and `FASTRUN` must be evaluated during the detailed platform analysis.

### Experimentally Verified Placement

The current Teensy 4.1 Arduino IDE / Teensyduino toolchain was tested with the BroTracker memory probe.

The probe confirmed the following address spaces and placement behaviour:

- ITCM starts at `0x00000000`.
- DTCM starts at `0x20000000`.
- RAM2 / DMAMEM starts at `0x20200000`.
- Flash / FlexSPI starts at `0x60000000`.
- `FLASHMEM` functions are placed in the Flash / FlexSPI address space.
- Default global data is placed in DTCM.
- `DMAMEM` data is placed in RAM2.

In the diagnostic build, the linker reported the following current ranges:

- ITCM: `0x00000000` to `0x00012438`
- DTCM data: `0x20000000` to `0x20003C40`
- DTCM stack boundary: `0x20068000`

These values describe the memory placement of the diagnostic firmware build and must not be treated as a BroTracker runtime memory budget.

### PSRAM / EXTMEM

Optional PSRAM provides substantially more storage than the internal RAM and is intended for large data sets rather than realtime CPU state.

Potential BroTracker uses:

- sample data;
- wavetables;
- large pattern/project data;
- caches;
- other large non-critical data.

Realtime inner-loop state and interrupt-critical data should remain in internal memory.

### Experimental Result

The physical Teensy 4.1 used for the initial memory probe reports:

- PSRAM detected: `0 MB`

Consequently, `EXTMEM` placement and PSRAM access latency could not be measured on this hardware.

The BroTracker memory model must therefore continue to treat PSRAM as optional platform capability rather than assuming its availability.

## Experimental Verification

The initial physical Teensy 4.1 memory probe verified the basic placement behaviour of the Arduino IDE / Teensyduino toolchain.

Verified:

- ITCM linker start address: `0x00000000`
- Current ITCM end address in the diagnostic build: `0x00012438`
- DTCM data start: `0x20000000`
- DTCM BSS end: `0x20003C40`
- RAM2 / DMAMEM start: `0x20200000`
- `FLASHMEM` function address observed in Flash/FlexSPI space: `0x60001619`
- Default global data observed in DTCM: `0x200029C0`
- DMAMEM data observed in RAM2: `0x20200000`
- PSRAM availability on the tested hardware: `0 MB`

The initial access benchmark reported approximately `5 cycles/byte` for both the tested DTCM and RAM2 buffers. This is a preliminary diagnostic measurement, not a final latency model.

These results confirm the basic linker placement model, but do not yet define the final BroTracker memory allocation policy.

## Dynamic Allocation Policy

The realtime kernel should not use runtime dynamic allocation.

The following should therefore be avoided in realtime code:

- `new` / `delete`;
- `malloc` / `free`;
- `std::string` and Arduino `String` for runtime state;
- dynamically growing containers.

Memory required by the realtime engine should be allocated statically or from explicitly controlled fixed-size pools established before realtime operation begins.

This does not mean that C++ itself is unsuitable for Teensy. Normal C++ language features may be used where they do not introduce unwanted runtime overhead or dynamic allocation.

## Exceptions

C++ exceptions are not part of the BroTracker runtime error-handling model.

Realtime code should use explicit status/result values rather than exception handling.

## Preliminary Memory Layout

The current working model is:

    Flash
    ├── normal application code
    ├── UI code
    ├── importers
    └── static resources

    RAM1
    ├── ITCM
    │   └── realtime / hot-path code
    └── DTCM
        ├── kernel state
        ├── realtime state
        └── stack

    RAM2
    └── DMAMEM
        ├── audio buffers
        ├── SD buffers
        ├── DMA buffers
        └── display buffers

    PSRAM
    ├── samples
    ├── wavetables
    ├── patterns / project data
    └── large caches

This layout is provisional and must not be treated as a final allocation policy.

## Open Questions

The following must be verified before finalizing the kernel memory architecture:

1. ~~Exact Teensy 4.1 RAM1 ITCM/DTCM allocation and linker behaviour.~~
   Basic linker placement verified experimentally; detailed final allocation policy remains open.
2. ~~How the Teensyduino toolchain places code and data by default.~~
   Basic default placement verified experimentally on Teensy 4.1.
3. ~~`ITCM`, `DTCM`, `FASTRUN`, `FLASHMEM`, `DMAMEM` and `EXTMEM` behaviour.~~
   ITCM, DTCM, `FLASHMEM` and `DMAMEM` placement verified experimentally.
   `FASTRUN` placement is represented by the ITCM linker range rather than a reliable runtime function-pointer address.
   `EXTMEM` could not be tested because the physical board reported no PSRAM.
4. Cache behaviour and coherency requirements for DMA.
5. Audio/I2S DMA buffer requirements.
6. SDIO and SD-card buffering requirements.
7. SPI and display DMA requirements.
8. PSRAM access characteristics and practical latency.
   PSRAM availability was checked experimentally; the tested Teensy 4.1 reports no PSRAM, so latency remains unverified.
9. Stack and heap placement and limits.
10. A concrete memory budget for the BroTracker realtime kernel.
11. Which Teensy/PJRC libraries are suitable for use inside the realtime kernel.
12. Which operations must remain outside the realtime execution path.

These questions will be analyzed before committing to the final low-level kernel memory layout.

The Teensy diagnostics tool under `tools/teensy_diagnostics/` is used to experimentally verify memory placement and relative access characteristics before the final memory allocation policy is defined.

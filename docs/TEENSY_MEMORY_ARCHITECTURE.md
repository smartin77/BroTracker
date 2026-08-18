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

### PSRAM / EXTMEM

Optional PSRAM provides substantially more storage than the internal RAM and is intended for large data sets rather than realtime CPU state.

Potential BroTracker uses:

- sample data;
- wavetables;
- large pattern/project data;
- caches;
- other large non-critical data.

Realtime inner-loop state and interrupt-critical data should remain in internal memory.

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

1. Exact Teensy 4.1 RAM1 ITCM/DTCM allocation and linker behaviour.
2. How the Teensyduino toolchain places code and data by default.
3. `ITCM`, `DTCM`, `FASTRUN`, `FLASHMEM`, `DMAMEM` and `EXTMEM` behaviour.
4. Cache behaviour and coherency requirements for DMA.
5. Audio/I2S DMA buffer requirements.
6. SDIO and SD-card buffering requirements.
7. SPI and display DMA requirements.
8. PSRAM access characteristics and practical latency.
9. Stack and heap placement and limits.
10. A concrete memory budget for the BroTracker realtime kernel.
11. Which Teensy/PJRC libraries are suitable for use inside the realtime kernel.
12. Which operations must remain outside the realtime execution path.

These questions will be analyzed before committing to the final low-level kernel memory layout.

The Teensy diagnostics tool under `tools/teensy_diagnostics/` is used to experimentally verify memory placement and relative access characteristics before the final memory allocation policy is defined.

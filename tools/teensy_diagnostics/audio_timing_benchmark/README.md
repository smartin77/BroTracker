# Teensy 4.1 Audio Timing Benchmark

## Purpose

This benchmark establishes the baseline realtime processing characteristics of the Teensy 4.1 audio platform before implementing the complete BroTracker audio engine.

The benchmark measures:

- deterministic audio-block processing behaviour
- execution time, jitter and processing margin
- Scheduler advancement overhead and correctness
- processing overruns
- realtime timing stability

The benchmark is intentionally independent from the future BroTracker Audio Engine.

## Scope

This benchmark does **NOT** implement:

- audio synthesis
- sample playback
- mixing architecture
- SD streaming during audio processing
- MIDI output or timing
- tracker playback
- effects processing

## Implementation

The benchmark uses the actual reusable BroTracker implementations without duplication.

The reusable libraries are:

- `libraries/scheduler/` — platform-independent Scheduler
- `libraries/teensy/diagnostics/` — Teensy-specific diagnostics infrastructure

Both are maintained as single sources of truth in the repository and are consumed by the benchmark through PlatformIO's library discovery mechanism.

The benchmark sketch is located at:

```text
tools/teensy_diagnostics/audio_timing_benchmark/
```

The PlatformIO project is defined by the repository-level:

```text
platformio.ini
```

The benchmark uses the `audio_timing_benchmark` PlatformIO environment.

## Hardware Requirements

- Teensy 4.1
- USB connection to the host for programming and Serial output
- SD card for result logging

## PlatformIO Setup

The project uses PlatformIO with the Arduino framework and Teensy 4.1 board support.

The relevant environment is:

```ini
[env:audio_timing_benchmark]
platform = teensy
framework = arduino
board = teensy41
```

PlatformIO automatically discovers the reusable libraries from the repository `libraries/` directory.

No file copying, hard links, symlinks, or manually maintained library duplicates are required.

## Building

From the BroTracker repository root:

```bash
C:\Users\<username>\.platformio\penv\Scripts\platformio.exe run -e audio_timing_benchmark
```

If the PlatformIO CLI is available in the terminal `PATH`, the shorter form can be used:

```bash
pio run -e audio_timing_benchmark
```

A successful build produces:

```text
.pio/build/audio_timing_benchmark/firmware.hex
```

The dependency graph should include:

```text
BroTrackerDiagnostics
BroTrackerScheduler
```

## Uploading to Teensy 4.1

The configured upload protocol is `teensy-gui`.

The easiest workflow in VS Code is to use the PlatformIO **Upload** action for the `audio_timing_benchmark` environment.

Alternatively:

```bash
C:\Users\<username>\.platformio\penv\Scripts\platformio.exe run -e audio_timing_benchmark -t upload
```

Connect the Teensy 4.1 by USB. If required, press the physical Program button on the Teensy during the upload process.

After a successful upload, the benchmark starts automatically.

## Serial Monitor

The project is configured for:

```text
115200 baud
```

In VS Code / PlatformIO, start the monitor for the `audio_timing_benchmark` environment.

Alternatively:

```bash
C:\Users\<username>\.platformio\penv\Scripts\platformio.exe device monitor -e audio_timing_benchmark
```

The complete workflow can also be executed with:

```bash
C:\Users\<username>\.platformio\penv\Scripts\platformio.exe run -e audio_timing_benchmark -t upload -t monitor
```

## Expected Benchmark

The benchmark tests three configurations:

| Sample Rate | Block Size | Block Duration | Intended Use |
| ------------- | ------------ | ---------------- | -------------- |
| 44.1 kHz | 256 | ~5.8 ms | Low latency |
| 44.1 kHz | 512 | ~11.6 ms | Balanced |
| 44.1 kHz | 1024 | ~23.2 ms | Larger processing block |

Each configuration processes 1000 deterministic audio blocks.

For each configuration the benchmark reports:

- minimum processing time
- average processing time
- maximum processing time
- jitter
- processing margin
- processing overruns
- expected Scheduler position
- actual Scheduler position
- Scheduler advancement status

## Verified Teensy 4.1 Result

The benchmark has been successfully built, uploaded and executed on a physical Teensy 4.1.

Verified results:

| Sample Rate | Block Size | Min | Avg | Max | Jitter | Overruns | Scheduler |
| ------------- | ------------ | ----- | ----- | ----- | -------- | ---------- | ----------- |
| 44.1 kHz | 256 | 2 µs | 2 µs | 3 µs | 1 µs | 0 | PASS |
| 44.1 kHz | 512 | 4 µs | 4 µs | 5 µs | 1 µs | 0 | PASS |
| 44.1 kHz | 1024 | 8 µs | 8 µs | 9 µs | 1 µs | 0 | PASS |

Scheduler positions were verified exactly:

```text
Expected scheduler position: 281600
Actual scheduler position: 281600
Scheduler advancement: PASS
```

```text
Expected scheduler position: 563200
Actual scheduler position: 563200
Scheduler advancement: PASS
```

```text
Expected scheduler position: 1126400
Actual scheduler position: 1126400
Scheduler advancement: PASS
```

All three configurations completed with:

```text
Processing overruns: 0
```

The benchmark completed normally and reported:

```text
Benchmark finished. Device is safe to unplug.
```

## Output

A typical run begins with:

```text
BroTracker Audio Timing Benchmark
Starting initialization...
Diagnostics initialized - results logged to SD card

Testing BroTracker Scheduler independence from wall-clock timing
Measuring audio processing block performance
```

Each configuration then reports its timing measurements and Scheduler verification.

The benchmark finishes with:

```text
=== Benchmark Complete ===
All measurements finished
Indicating completion with LED flash...
Benchmark finished. Device is safe to unplug.
```

## Interpretation

### Processing Time

- **Min/Avg/Max** — execution time for the measured workload per block
- **Jitter** — difference between maximum and minimum processing time

Lower processing time and lower jitter leave more headroom for future realtime processing.

### Processing Margin

Processing margin is the available time between the measured processing duration and the audio block deadline.

- Positive margin — processing completed within the available block time
- Zero or negative margin — processing reached or exceeded the deadline

### Processing Overruns

An overrun occurs when processing exceeds the available duration of the audio block.

For a realtime system, the target is:

```text
Processing overruns: 0
```

### Scheduler Advancement

The benchmark verifies that the Scheduler's logical playback timeline advances according to the number of processed samples.

Expected position:

```text
block_size × number_of_blocks
```

Actual position is obtained from:

```cpp
Scheduler::GetPosition()
```

The result must match exactly.

This verifies that Scheduler advancement is deterministic and independent of the wall-clock measurement used by the benchmark.

## Measurement Clock

The benchmark deliberately separates the logical Scheduler timeline from the physical measurement clock.

```text
Scheduler Timeline
    |
    +-- logical playback position
    +-- advances by processed sample count
    +-- independent of wall-clock time

Measurement Clock
    |
    +-- measures actual execution duration
    +-- detects jitter and overruns
    +-- used for diagnostics only
```

The benchmark uses Teensy's `micros()` for execution-time measurement.

The Scheduler does not use this measurement clock to advance its logical timeline.

## Deterministic Workload

The benchmark applies deterministic arithmetic work to a sample buffer.

The workload is intentionally simple and predictable. It does not depend on:

- filesystem access
- MIDI output
- external state
- non-deterministic application behaviour

This allows the benchmark to establish a baseline for the processing boundary before more complex audio-engine components are introduced.

## SD Card Output

The diagnostics infrastructure logs benchmark results to:

```text
BroTracker/audio_timing_benchmark.log
```

Serial output remains available even if SD logging is unavailable.

## LED Indication

The benchmark uses the Teensy LED as a simple execution indicator:

- one flash — initialization complete
- three flashes — benchmark completed

## Architecture

This benchmark follows the reusable-library architecture defined for BroTracker.

### Platform-independent reusable code

```text
libraries/scheduler/
```

Contains the Scheduler implementation that is not tied to Teensy-specific hardware.

### Teensy-specific reusable infrastructure

```text
libraries/teensy/diagnostics/
```

Contains diagnostics and logging functionality used by Teensy diagnostic tools.

### Diagnostic benchmark

```text
tools/teensy_diagnostics/audio_timing_benchmark/
```

Contains the benchmark-specific sketch and documentation.

The benchmark does not maintain private copies of the reusable Scheduler or Diagnostics implementations.

## Troubleshooting

### `scheduler.h` or `diagnostics.h` not found

Verify that PlatformIO is building from the BroTracker repository root and that the repository `libraries/` directory contains:

```text
libraries/
├── scheduler/
│   ├── library.properties
│   └── src/
│       ├── scheduler.h
│       └── scheduler.cpp
│
└── teensy/
    └── diagnostics/
        ├── library.properties
        └── src/
            ├── diagnostics.h
            └── diagnostics.cpp
```

Clean the PlatformIO build directory and rebuild if necessary:

```bash
C:\Users\<username>\.platformio\penv\Scripts\platformio.exe run -e audio_timing_benchmark -t clean
C:\Users\<username>\.platformio\penv\Scripts\platformio.exe run -e audio_timing_benchmark
```

### PlatformIO command `pio` is not recognized

PlatformIO may be installed in the PlatformIO virtual environment without its executable being in the system `PATH`.

Use the full executable path:

```text
C:\Users\<username>\.platformio\penv\Scripts\platformio.exe
```

Alternatively, run the command from a VS Code PlatformIO terminal where the environment is available.

### Upload problems

- Verify the Teensy 4.1 is connected by USB.
- Use the Teensy Program button if the uploader does not automatically detect the board.
- Verify that the PlatformIO upload protocol is configured for the project.
- Close any application that has locked the Teensy serial port.

### No Serial output

Verify:

```text
115200 baud
```

and that the correct Teensy serial port is selected.

The benchmark starts automatically after upload.

### SD logging problems

Verify that an SD card is inserted and accessible to the Teensy.

The benchmark's Serial output can still be used to inspect the timing measurements.

## Future Measurements

After establishing this baseline, future benchmarks may measure:

- software synthesis overhead
- sample playback overhead
- mixing overhead
- SD streaming overhead
- MIDI transmission overhead
- USB Audio overhead
- end-to-end audio-to-MIDI latency

Each subsequent benchmark can be compared against this baseline to determine how much processing budget is consumed by individual BroTracker subsystems.

## Architecture Notes

The benchmark is intentionally independent from the future BroTracker Audio Engine.

It can therefore be:

- executed independently for platform verification
- reused when the audio engine is implemented
- extended to test individual engine components in isolation

The Scheduler integration verifies that the audio processing boundary can correctly advance the shared logical timeline without making the Scheduler dependent on realtime measurement mechanisms.

## References

- `docs/TEENSY_AUDIO_ARCHITECTURE.md` — Teensy audio architecture and realtime audio benchmark documentation
- `docs/SCHEDULER.md` — Scheduler specification and timing requirements
- `libraries/scheduler/` — platform-independent BroTracker Scheduler library
- `libraries/teensy/diagnostics/` — Teensy diagnostics library
- `platformio.ini` — PlatformIO project configuration

## Future Enhancements

Possible improvements include:

- configurable workload complexity
- multiple workload profiles
- statistical output such as variance and percentiles
- automatic repeat runs
- jitter distribution analysis
- comparison against stored baseline measurements
- future MIDI timing integration

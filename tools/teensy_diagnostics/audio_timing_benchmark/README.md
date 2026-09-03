# Teensy 4.1 Audio Timing Benchmark

## Purpose

This benchmark establishes the baseline realtime processing characteristics of the Teensy 4.1 audio platform before implementing the complete BroTracker audio engine.

The benchmark measures:

- deterministic audio-block processing behaviour
- execution time, jitter and processing margin
- Scheduler advancement overhead and correctness
- processing overruns
- realtime timing stability

The benchmark is documented in `docs/TEENSY_AUDIO_ARCHITECTURE.md` (Realtime Audio Benchmark section).

## Scope

This benchmark **does NOT** implement:

- audio synthesis
- sample playback
- mixing architecture
- SD streaming during audio processing
- MIDI output or timing
- tracker playback
- effects processing

The purpose is to establish the baseline processing characteristics of the realtime audio boundary itself, independent of application logic.

## Hardware Requirements

- Teensy 4.1
- USB connection to host for Serial output
- SD card (for result logging via existing diagnostics infrastructure)

## Building and Running

### Using Arduino IDE with BroTracker Diagnostics

This benchmark is designed to use the existing BroTracker diagnostics infrastructure for SD card logging.

#### Option 1: Arduino IDE with Diagnostics (Recommended)

1. Open `audio_timing_benchmark.ino` in Arduino IDE
2. Add the diagnostics implementation:
   - Create a new tab in the Arduino IDE sketch
   - Add `#include` statement to include `firmware/teensy/BroTracker/diagnostics.h`
   - Alternatively, add `diagnostics.cpp` as a tab in the sketch
3. Select Board: Teensy 4.1
4. Select USB Type: Serial
5. Compile and upload
6. Monitor Serial output at 115200 baud
7. Results are written to `BroTracker/audio_timing_benchmark.log` on the SD card
8. Three LED flashes indicate successful completion

#### Option 2: Arduino IDE Serial-Only (Minimal Setup)

If integrating diagnostics is not practical:

1. Open `audio_timing_benchmark.ino` in Arduino IDE
2. Select Board: Teensy 4.1
3. Select USB Type: Serial
4. Compile and upload
5. Monitor Serial output at 115200 baud
6. Results appear only in Serial console, not on SD card
7. Modify DiagnosticsInitialize() to return true and DiagnosticLog() to no-op if needed

#### Building in Arduino IDE - Project Structure

When building this benchmark in Arduino IDE, the project needs:

- `audio_timing_benchmark.ino` (the main sketch)
- `diagnostics.h` and `diagnostics.cpp` (from firmware/teensy/BroTracker/)
- or appropriate `#include` statements pointing to the diagnostics headers

The simplest approach is to add diagnostics as sketch tabs:

1. Right-click the sketch tab → Add File
2. Select `firmware/teensy/BroTracker/diagnostics.cpp`
3. Create a new tab for diagnostics.h includes

#### Troubleshooting Build Issues

**Linker Error: "undefined reference to DiagnosticsInitialize"**

- Ensure `diagnostics.cpp` is included in the Arduino IDE sketch (as a tab or in the same directory)
- Verify that `diagnostics.cpp` compiles without errors
- Check that header paths are correct (adjust `#include` paths if needed)

**SD Card Not Found**

- Verify SD card is properly inserted in the Teensy SD adapter
- Check that `SD.begin()` succeeds (the DiagnosticsInitialize() function will fail silently if SD is not available)
- Results will be printed to Serial console even if SD is unavailable

**Serial Output Not Appearing**

- Verify USB cable is properly connected
- Check that Arduino IDE Serial Monitor is set to 115200 baud
- Ensure Teensy USB Type is set to "Serial"
- Try pressing the Teensy reset button if output stops

### Expected Console Output

```
BroTracker Audio Timing Benchmark
Starting initialization...
Diagnostics initialized successfully

Testing BroTracker Scheduler independence from wall-clock timing
Measuring audio processing block performance

=== Audio Timing Benchmark ===
Sample Rate (Hz): 44100
Block Size (samples): 256
Block Duration (us): 5804
Starting measurement...

Blocks measured: 1000
Min processing time (us): XXX
Avg processing time (us): XXX
Max processing time (us): XXX
Jitter (us): XXX
Processing margin (us): XXX
Processing overruns: X
Expected scheduler position: 4096000
Actual scheduler position: 4096000
Scheduler advancement: PASS

[Additional configurations...]

=== Benchmark Complete ===
All measurements finished
Indicating completion with LED flash...
Benchmark finished. Device is safe to unplug.
```

## Configurations Tested

The benchmark tests multiple audio configurations:

| Sample Rate | Block Size | Block Duration | Use Case |
|-------------|------------|----------------|----------|
| 44.1 kHz    | 256        | ~5.8 ms        | Low-latency |
| 44.1 kHz    | 512        | ~11.6 ms       | Balanced |
| 44.1 kHz    | 1024       | ~23.2 ms       | Battery-efficient |

Additional configurations can be added by modifying the `configs[]` array in `RunBenchmark()`.

## Interpreting Results

### Processing Time

- **Min/Average/Max**: Execution time for the measured workload per block
- **Jitter**: Difference between max and min (execution-time variation)

### Processing Margin

- **Positive**: Available headroom for realtime work
  - Example: +500 us = 500 microseconds of spare time per block
- **Negative**: Indicates processing overload
  - Should be zero for realtime suitability

### Processing Overruns

- **Count**: Number of blocks where execution time exceeded available block duration
- **Significance**: Even one overrun indicates potential realtime issues
- **Expected**: Zero for stable realtime behaviour

### Scheduler Advancement

The benchmark verifies that the Scheduler's logical playback timeline advances according to processed sample count, not elapsed wall-clock time.

- **Expected Position**: (block_size × total_blocks) samples
- **Actual Position**: Scheduler.GetPosition() after all blocks
- **Status**: PASS if positions match exactly, FAIL if divergent

This confirms that:
- Scheduler does NOT depend on millis() or micros()
- Scheduler advances deterministically
- Scheduler is suitable as the common timing reference for audio and MIDI

## Measurement Clock

The benchmark uses Teensy's `micros()` for measurement timing, separate from the Scheduler.

This separation is intentional and required by the architecture:

```
Scheduler Timeline (logical)
    |
    +-- represents playback samples
    +-- increments by AdvanceSamples(N)
    +-- independent of wall-clock time

Measurement Clock (actual execution)
    |
    +-- measures elapsed microseconds
    +-- detects jitter and overruns
    +-- used only for diagnostics
```

## SD Card Output

Results are written to the SD card at:

```
BroTracker/audio_timing_benchmark.log
```

This uses the existing BroTracker diagnostics infrastructure. Previous benchmark files are preserved with timestamps.

## Deterministic Workload

The benchmark applies a deterministic arithmetic operation (bit rotation) to each sample in the buffer.

This creates predictable CPU work without:
- dependency on external state
- filesystem access
- MIDI output
- non-deterministic branching

The workload is intentionally simple so that timing results primarily represent the audio processing path rather than application complexity.

## Next Steps

After establishing this baseline, future benchmarks may measure:

- software synthesis overhead
- sample playback overhead
- mixing overhead
- SD streaming overhead
- MIDI transmission overhead
- USB Audio overhead
- end-to-end audio-to-MIDI latency

Each subsequent measurement can be compared against this baseline to identify which subsystems consume processing budget.

## Architecture Notes

The benchmark is intentionally independent from the future BroTracker Audio Engine. It can be:

- reused when the engine is implemented;
- executed independently for platform verification;
- extended to test specific engine components in isolation.

The Scheduler integration verifies that the audio processing boundary can correctly advance the shared logical timeline without the Scheduler depending on realtime measurement mechanisms.

## References

- `docs/TEENSY_AUDIO_ARCHITECTURE.md` - Teensy audio architecture and Realtime Audio Benchmark section
- `docs/SCHEDULER.md` - Scheduler specification and timing requirements
- `src/runtime/scheduler.h/cpp` - BroTracker Scheduler implementation
- `firmware/teensy/BroTracker/diagnostics.h/cpp` - Diagnostics infrastructure

## Future Enhancements

Possible improvements to this benchmark:

- Configurable workload complexity
- Multiple workload profiles (arithmetic, memory access, etc.)
- Statistical output (variance, percentiles)
- Configuration stored on SD for flexibility
- Automatic repeat runs with averaged results
- Jitter histogram or distribution visualization
- Comparison with baseline measurements
- MIDI timing integration (Phase 4)


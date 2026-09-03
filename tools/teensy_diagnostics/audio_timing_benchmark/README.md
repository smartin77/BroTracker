# Teensy 4.1 Audio Timing Benchmark

## Purpose

This benchmark establishes the baseline realtime processing characteristics of the Teensy 4.1 audio platform before implementing the complete BroTracker audio engine.

The benchmark measures:

- deterministic audio-block processing behaviour
- execution time, jitter and processing margin
- Scheduler advancement overhead and correctness
- processing overruns
- realtime timing stability

The benchmark is documented in [docs/TEENSY_AUDIO_ARCHITECTURE.md](../../docs/TEENSY_AUDIO_ARCHITECTURE.md) (Realtime Audio Benchmark section).

## Scope

This benchmark **does NOT** implement:

- audio synthesis
- sample playback
- mixing architecture
- SD streaming during audio processing
- MIDI output or timing
- tracker playback
- effects processing

## Implementation Details

This benchmark uses the actual, reusable BroTracker implementations **without any duplication**.

**Per Architectural Decisions D0037 & D0038:**

- **Scheduler**: `src/runtime/scheduler.h/cpp` (single source of truth, platform-independent)
- **Diagnostics**: `firmware/teensy/BroTracker/diagnostics.h/cpp` (single source of truth, Teensy-specific infrastructure)

The components are consumed through Arduino IDE's native library discovery mechanism:

- `libraries/BroTrackerScheduler/` contains hard links to the platform-independent Scheduler
- `libraries/BroTrackerDiagnostics/` contains hard links to the Teensy diagnostics infrastructure
- Arduino IDE automatically discovers these libraries when you open the sketch
- Hard links ensure single source of truth: the files in libraries/ are the same physical files as the repository originals
- Changes to the original files immediately appear in the hard-linked copies
- No copying, no duplication, no maintenance burden

This approach satisfies the Arduino-specific integration requirement from D0038:
> "Arduino IDE integration must expose the existing source-of-truth implementation to the sketch without creating duplicated or forked source code."

## Hardware Requirements

- Teensy 4.1
- USB connection to host for Serial output and programming
- SD card (for result logging)

## Building and Running with Arduino IDE

### Setup (One Time)

1. Install Arduino IDE (https://www.arduino.cc/en/software) - version 1.8.15 or later
2. Install Teensyduino add-on (https://www.pjrc.com/teensy/teensyduino.html)
3. Verify that Teensy 4.1 board support is available

### Build and Upload

1. Open `audio_timing_benchmark.ino` in Arduino IDE
   - File → Open
   - Navigate to: `tools/teensy_diagnostics/audio_timing_benchmark/audio_timing_benchmark.ino`

2. Arduino IDE automatically discovers the reusable libraries:
   - `libraries/BroTrackerScheduler/` (hard links to `src/runtime/scheduler.h/cpp`)
   - `libraries/BroTrackerDiagnostics/` (hard links to `firmware/teensy/BroTracker/diagnostics.h/cpp`)
   
   No additional setup required. Arduino IDE searches `{sketch_directory}/libraries/` automatically.

3. Select Teensy 4.1 board and USB settings:
   - Tools → Board → Teensy 4.1
   - Tools → USB Type → Serial
   - Tools → Speed → 600 MHz (or other available frequency)

4. Compile:
   - Sketch → Verify/Compile (Ctrl+R)
   - Arduino IDE will compile the sketch and both reusable libraries (via hard links)

5. Connect Teensy 4.1 to USB and press the Teensy Program Button (physical button on the board)

6. Upload:
   - Sketch → Upload (Ctrl+U)
   - Arduino IDE will upload the firmware to the Teensy

7. After upload completes, the benchmark runs automatically:
   - LED flashes once during initialization
   - All three audio configurations are measured
   - Three LED flashes indicate successful completion and safe to unplug

### Monitor Serial Output

The benchmark writes output to both Serial console and SD card:

1. Open Serial Monitor:
   - Tools → Serial Monitor (Ctrl+Shift+M)
   - Set baud rate to **115200**

2. Output appears as benchmark runs (approximately 10-15 seconds for all configurations)

3. Results are also logged to SD card:
   - Location: `BroTracker/audio_timing_benchmark.log`
   - Timestamped entries with all measurements

## Output

The benchmark produces comprehensive timing measurements for each configuration tested:

```
BroTracker Audio Timing Benchmark
Starting initialization...
Diagnostics initialized - results logged to SD card

Testing BroTracker Scheduler independence from wall-clock timing
Measuring audio processing block performance

=== Audio Timing Benchmark ===
Sample Rate (Hz): 44100
Block Size (samples): 256
Block Duration (us): 5805

Starting measurement...

Blocks measured: 1000
Min processing time (us): 125
Avg processing time (us): 245
Max processing time (us): 389
Jitter (us): 264
Processing margin (us): 5416
Processing overruns: 0

Expected scheduler position: 1126400
Actual scheduler position: 1126400
Scheduler advancement: PASS

=== Benchmark Complete ===
All measurements finished
Indicating completion with LED flash...
Benchmark finished. Device is safe to unplug.
```

## Interpretation

- **Sample Rate (Hz)**: Samples per second for this configuration
- **Block Size (samples)**: Number of samples processed in each audio block
- **Block Duration (us)**: Available processing time for each block
- **Min/Avg/Max processing time (us)**: Best, average, and worst-case execution times observed
- **Jitter (us)**: Timing variability = max - min (lower is better for realtime)
- **Processing margin (us)**: Headroom before deadline
  - Positive: Can safely process this block within deadline
  - Negative: Processing exceeded deadline (overrun occurred)
- **Processing overruns**: Count of blocks that exceeded their deadline
- **Scheduler advancement**: Verification that Scheduler position matches expected audio timeline

## Result Files

**Serial Monitor** (console):
- Real-time output at 115200 baud
- Printed during benchmark execution

**SD Card Log** (persistent):
- File: `BroTracker/audio_timing_benchmark.log`
- Timestamped entries
- Can be transferred to host for analysis

**LED Indicator**:
- One flash: initialization complete
- Three flashes: all benchmarks completed successfully

## Arduino IDE Integration Architecture

This benchmark demonstrates Arduino IDE integration per architectural decisions **D0037 and D0038**:

**D0037 — Teensy Reusable Infrastructure:**
- Single source of truth for each reusable component
- No duplication, forking, or local reimplementation
- Diagnostic tools consume existing repository implementations

**D0038 — Arduino IDE Integration for Reusable Components:**
- Arduino IDE integration exposes existing source-of-truth implementations without creating duplicates
- Reusable components remain in their documented architectural locations
- Arduino IDE-specific packaging is a build/integration concern, not an architectural change

**How It Works:**

Arduino IDE automatically discovers libraries in `{sketch_directory}/libraries/`. This benchmark uses:

1. Hard links in `libraries/BroTrackerScheduler/src/` → `src/runtime/scheduler.h/cpp`
2. Hard links in `libraries/BroTrackerDiagnostics/src/` → `firmware/teensy/BroTracker/diagnostics.h/cpp`
3. Proper `library.properties` metadata files for Arduino IDE library discovery

**Key Benefits:**

- **Single physical files**: Hard links point to the same actual files as the repository originals
- **Zero duplication**: Changes to the original files immediately appear in the hard-linked copies
- **No maintenance burden**: Implementations are managed in one location only
- **No build system required**: Pure Arduino IDE workflow, no PlatformIO, CMake, or other tools
- **Preserves repository architecture**: Components remain in their documented ownership locations

## Troubleshooting

**Compilation Error: "scheduler.h not found" or "diagnostics.h not found"**
- Verify that `audio_timing_benchmark.ino` is in: `tools/teensy_diagnostics/audio_timing_benchmark/`
- Verify that the Arduino libraries exist in the sketch directory:
  - `libraries/BroTrackerScheduler/library.properties` and `src/scheduler.h/cpp`
  - `libraries/BroTrackerDiagnostics/library.properties` and `src/diagnostics.h/cpp`
- These are hard links to the actual implementations; do not delete or manually edit them
- If links are missing, you can recreate them by running the hard link creation commands in this directory

**Compilation Error related to BroTracker namespace, Scheduler class, or diagnostics functions**
- Verify Teensyduino add-on is installed (provides Arduino.h, SD.h, TimeLib.h)
- Verify Teensy 4.1 board support is enabled
- Try: Tools → Board Manager → Search "teensy" → Install/Update

**Serial output not appearing**
- Verify USB cable is properly connected
- Check that Serial Monitor baud rate is set to **115200**
- Try: Tools → Get Board Info (should show Teensy serial number)

**SD card not found or results not logged**
- Verify SD card is properly inserted
- Check SD card file system is FAT32
- DiagnosticsInitialize() may return false if SD fails
- Results still appear in Serial Monitor even if SD is unavailable

**Teensy not responding during upload**
- Try pressing Teensy Program Button (physical button on board) during upload
- Try: Tools → Ports → verify correct Teensy port is selected
- Restart Arduino IDE if needed

=== Audio Timing Benchmark ===
Sample Rate (Hz): 44100
Block Size (samples): 256
Block Duration (us): 5805

Starting measurement...

Blocks measured: 1000
Min processing time (us): 125
Avg processing time (us): 245
Max processing time (us): 389
Jitter (us): 264
Processing margin (us): 5416
Processing overruns: 0

Expected scheduler position: 1126400
Actual scheduler position: 1126400
Scheduler advancement: PASS
```

## Interpretation

- **Processing margin (us)**: Headroom available before deadline. Negative values indicate overruns.
- **Jitter (us)**: Variability in processing time (max - min). Lower is better.
- **Overruns**: Count of blocks that exceeded their processing deadline
- **Scheduler advancement**: Verification that Scheduler position matches expected sample count

## Troubleshooting



- Ensure Teensy USB Type is set to "Serial"
- Try pressing the Teensy reset button if output stops
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


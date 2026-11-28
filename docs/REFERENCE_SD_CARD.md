# Reference SD Card

## Purpose

BroTracker uses a specific SD card as the **reference storage medium for SD-card performance testing and development**.

The reference card provides a known baseline for:

- SD-card read performance
- filesystem behavior
- WAV sample loading
- sequential sample streaming
- future SD-related performance regression testing

The reference card is **not intended to define the minimum SD-card specification for BroTracker**. It provides a reproducible hardware baseline against which SD-related changes can be evaluated.

## Reference Card

The currently selected reference card is an unbranded/unauthenticated 128 GB microSD card.

The card is believed to be a low-cost Chinese-produced card. **The actual manufacturer and NAND/controller have not been independently identified.**

Because the physical branding cannot be considered reliable, BroTracker documentation identifies this card by its measured properties and test results rather than by the apparent brand.

### Card / Volume Information

| Property | Value |
| --- | --- |
| Nominal capacity | 128 GB |
| Reported capacity | 124.97 GB |
| Reported capacity (bytes) | 134,185,222,144 |
| Filesystem | exFAT |
| Cluster size | 128 KiB |
| Free space at test time | 124.97 GB |
| Interface | Teensy 4.1 built-in SD interface |
| Reference status | **Reference card** |

## Benchmark Environment

The reference measurements were obtained using:

- **Board:** Teensy 4.1
- **Storage:** built-in Teensy 4.1 SD interface
- **Filesystem:** exFAT
- **Cluster size:** 128 KiB
- **Benchmark:** `tools/teensy_diagnostics/sd_read_benchmark`
- **Read chunk size:** 4 KiB
- **Read loops:** 10
- **Valid WAV files:** 33
- **Total benchmarked data:** 89,136,100 bytes
- **Benchmark runs:** 5

## Benchmark Results

Five consecutive benchmark runs produced highly consistent results.

| Measurement | Result |
| --- | ---: |
| WAV candidates | 64 |
| Supported WAV files | 33 |
| Unsupported WAV files | 31 |
| Benchmarked files | 33 |
| Total data | 89,136,100 bytes |
| Full-file throughput | ~19.3 MiB/s |
| 4 KiB chunked throughput | ~19.3 MiB/s |
| Full-read failures | 0 |
| Chunked-read failures | 0 |
| Benchmark result | **PASS** |

Across the five runs, full-file throughput ranged from approximately **19,298 to 19,328 KiB/s**, corresponding to a variation of approximately **0.16%** between the slowest and fastest measurement.

The 4 KiB chunked-read benchmark produced effectively the same throughput as the full-file read benchmark.

## Streaming Benchmark Results

The SD card was tested using the BroTracker Phase 2 sample-streaming benchmark on Teensy 4.1.

### Test configuration

- Concurrent streams: 1–8
- WAV format: 16-bit PCM, 44.1 kHz
- Streaming chunk size: 4 KiB
- Ring buffer per stream: 8 KiB
- Playback model: 200% of native sample rate
- Streaming passes: 64 per stream count
- Audio-load test: enabled
- Reference hardware: Teensy 4.1

### Results

All tested stream counts from 1 through 8 completed with:

- zero deadline misses
- zero read failures
- stable simulated audio cadence

At 8 concurrent streams:

| Metric | Result |
| --- | ---: |
| Worst refill latency | 3.7 ms |
| Worst pass time | 20.3 ms |
| Refill deadline | 23.2 ms |
| Realtime margin | 12% |
| Ring drain time | 46.4 ms |
| Audio cadence | Stable |

### Note-on latency

The benchmark measured open + seek + first refill latency separately.

| Metric | Result |
| --- | ---: |
| Worst note-on latency | 19.2 ms |
| Audio block period | 2.9 ms |
| Minimum indicated head cache | ~3.4 KiB |

The measured note-on latency indicates that a resident head cache is required if BroTracker is expected to provide reliable low-latency sample triggering.

### Interpretation

The reference SD card can sustain the tested 1–8 concurrent streaming access pattern without deadline misses, including under simulated audio interrupt load.

The result validates SD-backed sample streaming as a feasible approach for BroTracker.

The benchmark does not establish a final BroTracker voice-count limit. The measured 8-stream result is a hardware and access-pattern baseline only.

Future changes to buffer size, refill policy, sample format, playback rate, SD card, or voice architecture require revalidation.

The measurements indicate that, on the reference card and current Teensy 4.1/SdFat configuration:

sequential SD reads are highly repeatable;
approximately 19.3 MiB/s sequential read throughput is achievable;
4 KiB sequential read operations introduce no significant throughput penalty in this benchmark;
no read errors occurred during the five test runs.

The reference SD card can sustain the tested 1–8 concurrent streaming access pattern without deadline misses, including under simulated audio interrupt load.

The result validates SD-backed sample streaming as a feasible approach for BroTracker.

The benchmark does not establish a final BroTracker voice-count limit. The measured 8-stream result is a hardware and access-pattern baseline only.

The measurements should be considered a reference baseline, not a guaranteed performance requirement for arbitrary SD cards.

Future changes to buffer size, refill policy, sample format, playback rate, SD card, or voice architecture require revalidation.

## Supported WAV Files

The benchmark currently accepts the supported WAV format defined by the benchmark implementation.

Of the 64 discovered WAV files:

- **33 files were valid and benchmarked**
- **31 files were rejected as unsupported**

The rejected files were 24-bit, 44.1 kHz stereo WAV files and were rejected by the current validation rules.

This does not indicate an SD-card problem.

## Why a Reference Card Is Necessary

SD cards are not equivalent storage devices.

Two cards with the same advertised capacity, filesystem and nominal speed class may exhibit substantially different behavior in:

- sustained sequential reads
- small-block reads
- random access
- file-open latency
- seek latency
- write performance
- filesystem behavior
- performance consistency

For an embedded tracker, **maximum advertised transfer speed is therefore not sufficient to predict real-world sample-streaming behavior**.

The reference card provides BroTracker development with a reproducible hardware baseline.

Performance changes in the SD layer, filesystem layer, sample engine, buffering strategy or scheduler can be tested against this baseline.

## Important Limitation

The current benchmark measures **sequential file reading**.

It does **not yet establish**:

- random-read performance;
- worst-case file-open latency;
- seek latency;
- simultaneous streaming of multiple samples;
- SD performance while audio playback is active;
- performance under filesystem fragmentation;
- performance across different SD cards.

Therefore, the current ~19.3 MiB/s result **must not be interpreted as the maximum or minimum SD performance required by BroTracker**.

Further benchmarks are required before defining a formal minimum SD-card requirement.

## Future SD Card Qualification

Before defining officially supported SD cards, BroTracker should evaluate cards using a standardized test procedure.

Potential qualification tests include:

1. Sequential large-file read
2. 4 KiB sequential read
3. Small-file open/read latency
4. Random/semi-random reads
5. Multiple simultaneous sample streams
6. Read performance during audio playback
7. Repeated cold-start tests
8. Filesystem fragmentation
9. Long-duration streaming
10. Repeated power-cycle testing

A card should be considered **qualified** only after passing the relevant BroTracker workload tests.

## Reference Card Selection Principle

The reference card is not selected because of its advertised brand or speed rating.

It is selected because its behavior has been measured on the actual BroTracker hardware.

The reference benchmark therefore represents a **measured hardware baseline**, rather than a theoretical specification derived from the card's advertised capabilities.

## Dedicated 32 GB card comparison (2026-11-28)

The owner-supplied SanDisk 32 GB test card completed the existing isolated SD
diagnostics on Teensy serial 17681760. The original reference card and all prior
measurements above were preserved. This is a card/filesystem/workload comparison,
not a new minimum-card specification or an authenticity test.

| Property | Dedicated test card |
| --- | --- |
| Raw CID reported by SdFat | `6f0303534441424310aa001504018b00` |
| Reported physical capacity | 61,076,480 sectors / 31,271,157,760 bytes |
| Filesystem / cluster | FAT32 / 64 sectors = 32 KiB |
| Data-cluster capacity | 954,086 clusters / 31,263,490,048 bytes |
| Available before diagnostic writes | 26,074,742,784 bytes |
| Available after native fixture creation | 25,818,857,472 bytes |

No format, repartition or cluster change was performed. Existing Samples and BTT
evidence were not targeted. PREPARE created ten native deterministic PCM16 mono
fixtures in the existing diagnostic's dedicated namespace; all reopened PCM hash
and frame-count references matched the independent native calculator.

The command-driven baseline passed RAM/SD short-file and large-file EOF checks
plus 2/4/8-voice simultaneous/staggered starts. The selected `low_ram` configuration
then passed 8 KiB rings, 4 KiB reads/staging, 50% threshold, 300 seconds of requested
149 ms ISR retriggers, and eight six-minute files to EOF. All file hashes matched;
underruns, I/O/content errors and pool failures were zero.

| Isolated low-RAM diagnostic metric | Historical exFAT reference | New FAT32 card |
| --- | ---: | ---: |
| Largest per-run/voice p99 upper bound | 3.220 ms | 1.080 ms |
| Maximum seek + read | 3.551 ms | **22.563 ms** |
| Minimum ready frames, excluding final drain | 897 / 20.340 ms | 1153 / 26.145 ms |
| Maximum source callback | 183.172 us | 188.142 us |
| Peak Audio Library update | 6.317% | 6.490% |
| Eight-file EOF seek/read duty | 47.69% | 9.00% |
| Verified complete retrigger generations | 18,056 | 16,208 |
| Rejected stale completions | 506 | 0 |

The historical headline includes a 173 ms/300 s retrigger run and extra 149 ms
collision runs; this focused run uses the current runner's 149 ms/300 s workload.
The new card's 22.563 ms maximum occurred during full-file EOF playback on one
voice; lower p99/duty does not remove this larger observed tail. Zero stale
discards means interrupted-read rejection was not observed on the new card.
The matching six-minute EOF case alone has p99 upper bounds 3.090 / 0.970 ms,
maximum seek/read 3.551 / 22.563 ms and callback maxima 160.405 / 164.783 us
(old/new). These are isolated diagnostic results, separate from later production
streaming measurements. Filesystem, cluster size, layout/occupancy, cache state
and different exposure histories prevent attributing every change to hardware.

The original DrumLoop read benchmark also passed with 4 KiB reads and ten loops:
64 candidates, 33 valid, 31 unsupported, and 89,136,100 bytes. Supported and
unsupported metadata manifests match the historical September 8 log; full WAV
content hashes were not measured by that benchmark.

| Original read benchmark | Historical September 8 log | New card |
| --- | ---: | ---: |
| Aggregate full-file throughput | 19,314 KiB/s | 21,507 KiB/s |
| Aggregate chunked throughput | 19,311 KiB/s | 21,502 KiB/s |
| Full-file / chunked read failures | 0 / 0 | 0 / 0 |

The existing storage probe passed 1 KiB/64 KiB/1 MiB create/write/read, append,
rename/delete and 64 KiB close/reopen correctness. Its fixed temporary paths were
verified absent before upload and cleanup passed. **Write throughput remains
unmeasured.** Current normal `teensy41_dev` with both reviewed BTT fixes was
restored; board/build identity, IDLE and serial closure were verified.

Evidence, raw logs, structured comparisons, image/dataset identities and limits:
`build/sd-card-comparison/sandisk-20261128-e42d943a6c/REPORT.md`. This run does not
establish audible quality, full-capacity authenticity, fragmentation tolerance,
production mixed/pitched workloads or power-loss recovery. The dedicated card
remains installed; the owner handles later card exchange.

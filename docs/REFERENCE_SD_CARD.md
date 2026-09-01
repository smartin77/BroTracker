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

### Interpretation

The measurements indicate that, on the reference card and current Teensy 4.1/SdFat configuration:

- sequential SD reads are highly repeatable;
- approximately **19.3 MiB/s** sequential read throughput is achievable;
- 4 KiB sequential read operations introduce no significant throughput penalty in this benchmark;
- no read errors occurred during the five test runs.

These results should be considered a **reference baseline**, not a guaranteed performance requirement for arbitrary SD cards.

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

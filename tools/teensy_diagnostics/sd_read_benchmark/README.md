# BroTracker SD Read Benchmark

## Purpose

The BroTracker SD Read Benchmark measures the performance and reliability of reading sample files from an SD card on Teensy 4.1.

The benchmark is intended as a diagnostic tool and as a foundation for the future BroTracker Sample Loader.

The main goal is to determine whether sample data can be read from the SD card reliably and quickly enough to support future sample playback directly from storage.

The benchmark also verifies that BroTracker can create and write valid result files to the SD card.

This is an experimental benchmark. It does not implement the final Sample Loader.

## What the Benchmark Tests

The benchmark:

1. Scans a configurable directory on the SD card.
2. Counts all directory entries found.
3. Identifies sample candidates by supported file extension only.
4. Does not validate sample contents during directory scanning.
5. Processes sample candidates in configurable batches.
6. Validates the selected samples before benchmarking.
7. Logs invalid or unsupported samples and removes them from the benchmark set.
8. Benchmarks only successfully validated samples.
9. Reads the selected samples repeatedly.
10. Measures SD read performance.
11. Records timing and throughput statistics.
12. Continues with the next batch until all sample candidates have been processed.
13. Writes the benchmark results to the SD card.
14. Signals completion using three long LED flashes.

## Source Directory

The benchmark uses a configurable source directory.

The path is defined in the benchmark configuration and can be changed without modifying the benchmark logic.

The directory may contain additional files or subdirectories. These are counted separately and are not treated as valid WAV files.

## Sample Discovery, Validation and Benchmarking

The benchmark separates sample discovery from sample validation and performance measurement.

### Sample Discovery

The source directory is scanned completely.

The benchmark does not assume or limit the number of files in the directory. The directory may contain hundreds or thousands of samples.

A file is considered a sample candidate based only on its supported file extension.

The benchmark does not inspect the file contents during the initial directory scan.

For example, a file with a `.wav` extension is counted as a WAV sample candidate regardless of whether its contents are actually a valid WAV file.

Directories and files with unsupported extensions are not treated as sample candidates.

### Batch Selection

Sample candidates are processed in configurable batches.

The batch size only determines how many samples are handled at one time. It is not a limit on the total number of samples in the directory.

For example, with a batch size of 31:

- a directory containing 31 samples produces one batch;
- a directory containing 100 samples produces four batches;
- a directory containing thousands of samples is processed until the complete directory has been covered.

No fixed maximum number of samples is assumed.

### Sample Validation

Before a batch is benchmarked, every sample candidate in that batch is validated.

Validation is a preparation step for the benchmark and is not part of the measured SD read performance.

The purpose of this phase is to ensure that the performance benchmark measures only samples that can actually be loaded.

Invalid samples are:

- recorded in the benchmark report;
- assigned a validation failure reason where possible;
- removed from the benchmark set for that batch.

The benchmark never measures the read performance of an invalid sample.

### Benchmark Set

After validation, only successfully validated samples are passed to the benchmark.

For example:

WAV candidates: 31  
Valid samples: 28  
Invalid samples: 3  
Benchmarked samples: 28

The three invalid samples remain recorded in the report.

This separation prevents invalid or corrupted files from affecting the performance measurements while preserving complete information about the contents of the test directory.

### Relationship to the Future Sample Browser and Sample Loader

The future BroTracker Sample Browser is expected to follow the same discovery principle.

The browser will:

- scan the directory;
- identify samples by supported extension;
- display all matching samples;
- present them in configurable pages, for example 31 rows per page.

The browser does not need to validate the contents of every sample merely to display the directory.

Validation occurs when a sample is actually requested for loading.

The future Sample Loader will therefore:

1. receive a selected sample;
2. attempt to load it;
3. validate the file while loading;
4. reject and report the sample if its contents are invalid.

The benchmark differs only in that it performs this validation phase in advance so that performance measurements are performed exclusively on valid samples.

The validation and parsing functionality developed for the benchmark should therefore be suitable for reuse by the future Sample Loader.

## WAV Validation

WAV validation is performed only after a sample candidate has been selected for the benchmark.

The initial directory scan does not validate WAV contents.

A file becomes a WAV candidate solely because it has the supported `.wav` extension.

During the validation phase, the benchmark parses the RIFF/WAVE structure and determines whether the file can be loaded as a supported PCM WAV sample.

The parser must not assume a fixed WAV header size.

RIFF/WAVE files may contain additional chunks or metadata before the `data` chunk, including chunks such as:

- `LIST`
- `JUNK`
- `INFO`
- `fact`
- `bext`
- other RIFF/WAVE chunks

The parser must therefore walk the RIFF chunk structure until it finds the `data` chunk.

The beginning of the `data` chunk is treated as the beginning of the PCM sample data.

The initial supported formats are:

- PCM WAV / RIFF
- 8-bit PCM
- 16-bit PCM
- 8 kHz
- 11.025 kHz
- 22.05 kHz
- 44.1 kHz
- 48 kHz

Where practical, the benchmark should record the reason why a sample failed validation, such as:

- invalid RIFF structure
- missing WAVE identifier
- missing `fmt` chunk
- missing `data` chunk
- invalid RIFF chunk structure
- unsupported codec
- unsupported bit depth
- unsupported sample rate
- truncated sample data

A validation failure must not prevent the benchmark from continuing with the remaining samples.

## Batch Processing

Sample candidates are processed in batches.

The default batch size is 31 files.

The batch size is configurable.

The batch size is not a limit on the total number of files or samples.

The complete source directory is always processed, regardless of how many sample candidates it contains.

For each batch:

1. Select up to the configured number of sample candidates.
2. Validate each candidate.
3. Log validation failures.
4. Exclude invalid candidates from the benchmark.
5. Benchmark only successfully validated samples.
6. Continue with the next batch.

This provides an analogy to a tracker sample browser while keeping the benchmark independent from the future UI implementation.

## Read Benchmark

Each valid WAV file is read repeatedly using a configurable number of read loops.

The benchmark should measure at least:

- file open time
- file read time
- file close time
- total read time
- number of bytes read
- effective read throughput

Statistics should include:

- minimum
- maximum
- average

Read failures should be recorded separately.

## Full-File and Chunked Reads

The benchmark should support measuring both full-file reads and chunked reads.

### Full-File Read

The complete PCM data of a WAV file is read.

This measures the performance of loading an entire sample into memory.

### Chunked Read

The PCM data is read in configurable chunks.

The chunk size is configurable and should initially use a value suitable for evaluating future streaming playback.

Chunked reading is particularly important because the future Sample Loader may need to obtain sample data from the SD card while the tracker is playing.

The benchmark should record chunked-read timing and throughput separately from full-file reads.

## Benchmark Output

Benchmark results are written to:

/BT_benchmarks/

The output directory is configurable.

If the directory does not exist, the benchmark creates it.

Existing files in the directory must not be deleted.

Each benchmark run creates a new result file.

The benchmark must not clear or otherwise modify previous benchmark results.

## Result File

Because Teensy 4.1 does not provide a battery-backed real-time clock, the benchmark must not rely on a persistent system date/time.

The result filename and report should therefore use the firmware compilation date and time.

Example filename:

SD_BENCH_20260830_112530.txt

The report should contain at least:

- benchmark build date
- benchmark build time
- source directory
- output directory
- files per batch
- read loop count
- chunk size
- number of files found
- number of valid WAV files
- number of invalid or unsupported files
- number of directories
- batch results
- per-file read statistics
- aggregate statistics
- read failures
- write status

If a firmware build identifier or Git commit identifier is available, it may also be included in the report.

## Scan Statistics

The initial directory scan should distinguish between:

- Files found
- Sample candidates
- Directories
- Other entries

Sample candidates are identified only by supported file extension.

The initial scan must not classify a sample candidate as valid or invalid based on its file contents.

Validation results are collected later, during batch preparation.

The final report should therefore distinguish between:

- total sample candidates;
- successfully validated samples;
- validation failures;
- samples actually benchmarked;
- benchmark read failures.

Directories must not be counted as sample candidates.

## Example Result Structure

A result file may use a structure similar to:

BroTracker SD Read Benchmark

Build date: 2026-08-30
Build time: 11:25:30

Source:
/Samples/Wav-HQ/DrumLoop/

Output:
/BT_benchmarks/

Files per batch: 31
Read loops: 10
Chunk size: 4096 bytes

Scan:
  Files found: 184
  Valid WAV: 137
  Invalid / unsupported: 42
  Directories: 5
  Other entries: 0

Batch 1
  Files: 31
  ...

Batch 2
  Files: 31
  ...

Aggregate:
  ...

Read failures: 0
Write status: OK

The exact report format may evolve as benchmark requirements become clearer.

## LED Completion Signal

When the complete benchmark has finished and the result has been successfully written to the SD card, the Teensy onboard LED should perform three long flashes.

The LED sequence provides a simple headless indication that the benchmark has completed.

## Configuration

The following values should be configurable without changing the benchmark logic:

- source directory
- output directory
- number of files per batch
- number of read loops
- chunk size

Example configuration:

``` c++
const char* BENCHMARK_SOURCE_PATH = "/Samples/Wav-HQ/DrumLoop/";
const char* BENCHMARK_OUTPUT_PATH = "/BT_benchmarks/";

constexpr uint32_t FILES_PER_BATCH = 31;
constexpr uint32_t READ_LOOPS = 10;
constexpr uint32_t READ_CHUNK_SIZE = 4096;
```

These values are initial benchmark parameters and may be changed during testing.

## Why These Metrics Matter

The benchmark is not intended only to measure maximum SD card throughput.

The future BroTracker Sample Loader may need to obtain sample data from the SD card while the tracker is actively playing.

For that reason, consistency and latency are as important as raw throughput.

The benchmark results will help determine:

- how long opening a sample takes;
- how long reading sample data takes;
- how much read time varies between operations;
- how large a read buffer should be;
- whether chunked reading is practical;
- whether samples can be accessed directly from storage during playback;
- whether additional buffering or preloading is required;
- whether SD card performance introduces unacceptable timing risks.

The benchmark therefore provides real hardware measurements that will be used when designing the future Sample Loader and storage strategy.

## Scope

This benchmark intentionally does not implement:

- the final Sample Loader;
- tracker playback;
- audio streaming;
- sample scheduling;
- sample caching;
- UI integration;
- sample editing;
- sample conversion.

Those features will be designed separately based on the results of this benchmark and subsequent implementation work.

## Future Use

The benchmark is expected to evolve together with the BroTracker storage architecture.

Future tests may include:

- different SD cards;
- different WAV file sizes;
- different sample rates;
- different channel counts;
- different read chunk sizes;
- sequential versus non-sequential reads;
- repeated access to the same file;
- simultaneous playback and SD access;
- buffering strategies;
- worst-case read latency;
- filesystem fragmentation effects.

The benchmark should remain a standalone Teensy diagnostic tool so that storage performance can be measured independently of the main BroTracker application.

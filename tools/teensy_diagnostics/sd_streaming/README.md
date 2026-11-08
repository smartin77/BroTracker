# SD streaming diagnostic (Teensy 4.1, no PSRAM)

Isolated PCM16 mono / 44100 Hz / one native-rate voice per logical tracker channel.
Production firmware, playback and USB bridge sources are untouched. The diagnostic
uses onboard SDIO through the production `SD.begin(BUILTIN_SDCARD)` API
(SdFat FIFO_SDIO), fixed FsFile handles and the user-confirmed attached PT8211
module (BCK=21, WS=20, DIN=7). Mono is duplicated left/right at a fixed 1/8 mix
gain. This is the previously validated PT8211 output; it has no presence/ACK
interface. No USB audio graph or codec I2C configuration is instantiated.
Storage initialization precedes activation of the hardware audio clock, matching
the production preparation order. Failed initialization can be retried with INFO.

Each voice has a compile-time 8/16/32 KiB RAM2 ring and an immutable 4 KiB start
cache. One shared, compile-time RAM2 staging buffer bounds reads (16 KiB for
the matrix; 4/8 KiB for the two finalists). The default is 4 KiB
reads and 16 KiB per voice. Start caches are populated before activation. Refills
run only in the foreground, choosing the eligible voice with least ready audio;
at native rate, ready frames are proportional to time to depletion. Read size
must fit the ring. 50% and 75% thresholds are compared by the runner; free-space
requirements can delay a refill past its threshold.

Audio consumes published PCM only. Teensy's `allocate()` uses the statically
initialized AudioMemory block pool, not malloc/new. No filesystem calls, waits,
heap allocation or logging occur in the callback. On starvation, it emits zero
for the missing frame and advances the source timeline; refill skips missing
frames rather than replaying delayed audio. Underruns count missing frames,
normal EOF has a separate counter, and short reads stop the affected voice with
an explicit error. No retries or SD writes are performed during playback.

Every interrupt-owned retrigger increments a generation, resets the published
ring extent and immediately plays the immutable head cache. The producer reads
into staging, checks the generation, copies into unpublished ring space, then
checks again and publishes inside a short audio-interrupt exclusion. An old read
cannot publish its extent after a retrigger. Only one foreground producer exists;
a new generation cannot refill concurrently with that copy. Cached heads cover
the copy interval. Retrigger stress uses the real audio ISR, so it can interrupt
SD reads. Stale-discard counts establish whether that interleaving occurred. Each complete
retrigger interval is also checked against a stopped-read reference prefix hash;
content errors and the number of verified generations are reported separately.

This is a forward one-shot diagnostic. It does not implement loops, slicing,
pitch conversion, production scheduler integration or production voice policy.
The initial stop/retrigger points are diagnostic block-aligned controls.

## Build and run

From the repository root (Python needs pyserial):

```powershell
pio run -d tools/teensy_diagnostics/sd_streaming -e teensy41 -e ring8 -e ring32
pio run -d tools/teensy_diagnostics/sd_streaming -e teensy41 -t upload
python tools/teensy_diagnostics/sd_streaming/run.py --port COM4 --stage baseline
python tools/teensy_diagnostics/sd_streaming/run.py --port COM4 --stage sweep --ring 16384
```

Run baseline first. Flash `ring8`, then sweep with `--ring 8192`; flash `ring32`,
then sweep with `--ring 32768`. The runner checks the actual flashed ring size.
Read sizes are 2/4/8/16 KiB, excluding 16 KiB with the 8 KiB ring because one read
cannot fit. Each size uses 50/75% thresholds and simultaneous/37 ms staggered
starts for eight seconds. Compare results before selecting candidates. Flash
each candidate's matching ring build and stress. The measured finalists are
`low_ram` (8 KiB rings / 4 KiB staging) and `headroom` (16 KiB rings / 8 KiB staging):

```powershell
pio run -d tools/teensy_diagnostics/sd_streaming -e low_ram -t upload
python tools/teensy_diagnostics/sd_streaming/run.py --port COM4 --stage stress --ring 8192 --read 4096 --threshold 50
pio run -d tools/teensy_diagnostics/sd_streaming -e headroom -t upload
python tools/teensy_diagnostics/sd_streaming/run.py --port COM4 --stage stress --ring 16384 --read 8192 --threshold 50
```

Stress first qualifies simultaneous/staggered starts, then runs five minutes of
ISR retriggers (149 ms requested, quantized to 51 blocks / 148.027 ms) followed
by six minutes of uninterrupted full-file EOF playback with the generated
fixtures. The first low-RAM run used 173 ms (59 blocks / 171.247 ms); a separate
149 ms collision test exercises a read interrupted by retrigger. For shorter
externally supplied files the runner repeats finite files, including
preparation gaps, for at least five minutes in aggregate. Run both finalists.

`PREPARE` creates `/BroTracker/SDStreamPOC-20261106/` with eight distinct six-minute
WAVs (`voice01.wav`..`voice08.wav`), a 0.5-second `short.wav`, and a 20-second
`large.wav`. All are PCM16 mono 44100 Hz. Each file is a deterministic sawtooth
with a different frequency (110+37*index Hz), amplitude approximately 0.10; the
fixed 1/8 output mix leaves headroom. The long files contain 31,752,000 PCM bytes
each, and the EOF fixture contains 1,764,000 bytes, exceeding internal sample RAM.
Creation releases all read handles and read ownership, acquires exclusive write
ownership, creates files with O_EXCL, flushes/closes them, then reacquires read
ownership. Existing correctly formatted/sized files are reused; conflicts are
reported without replacement. Only a partial file exclusively created by the
current call is removed on write failure. No writes occur during playback.

Discovery first uses the dedicated fixture folder, then documented development
paths if needed. It reports all selected paths, sizes, reference hashes, card CID,
sector count, filesystem and cluster size. Missing fixtures or SD initialization
failure stops the runner. `INFO` retries failed initialization and rescans stopped
media. A successful initialization is retained across INFO calls. Boot prepares
storage before starting audio, then waits for commands; use PREPARE followed by
INFO before testing a new card.

Raw serial command:
`RUN voices read_bytes threshold_percent duration_ms stagger_ms retrigger_ms mode`
where mode 0=long streams, 1=short RAM, 2=the same short WAV streamed,
3=the separate large-file EOF fixture. `PING` reports
build/fixture readiness. Runs are bounded to one hour; foreground commands are
processed between runs. `INFO` closes prior handles and rescans stopped media.

## Measurements and interpretation

Per voice: underrun frames, minimum ready frames excluding the final intentional
EOF drain, read count, p99 latency upper bound, maximum seek-plus-read time,
I/O errors, stale completions rejected, retrigger content errors/verified
generations, normal EOFs, source position and PCM hash. UINT32_MAX for minimum
ready in RAM mode means not applicable; read latency is not applicable when the
read count is zero.
The p99 histogram has 10 us buckets through 20.47 ms; its overflow bucket uses the
observed maximum as the conservative upper bound. Callback time includes mixing,
per-sample diagnostic hashing and fixed-pool transport; CPU peak includes the
Audio Library update graph. Pool failures are separate failures. Startup prints
linker-derived static memory headroom and PSRAM detection. Linker maps are emitted
for all environments. Stack/local headroom is not measured stack high-water use.

EOF hashes compare every source frame delivered by the renderer with a stopped,
sequential full-file reference hash, including the short RAM/SD pair. This checks
the digital sample path; it is not captured analog/I2S output verification. Hash
collisions remain possible. Truncated and retriggered runs cannot be compared
with the full-file hash. Listening and physical capture require separate checks.

The 140000-byte DTCM RAM comparison buffer and roughly 64 KiB of read histograms
are diagnostic overhead, not required streaming storage. Ring/cache/staging RAM2
is 112/176/304 KiB for 8/16/32 KiB rings respectively; the Audio Library, SD and USB
serial add their own static storage. The fixed 16 KiB staging allocation is used
in every matrix sweep, even with smaller reads. Finalists are rebuilt and
stress-tested with staging reduced to the actual read size; their streaming
storage is 100 KiB (`low_ram`) and 168 KiB (`headroom`), including heads/staging. Compare actual linker maps, not just the
ring arithmetic, before making a production memory recommendation.

## Design review

Reviewed D0039, D0041, D0055, D0060, Teensy memory/audio architecture, Sample Format,
reference-card benchmarks, current streaming outline, WAV validation/loading,
legacy SamplePlayer, portable RAM voice, production callback and a fresh
`teensy41_dev` linker map. The existing benchmark simulates round-robin demand;
this diagnostic consumes PCM from real I2S-clocked AudioStream callbacks.

[Polyend VoiceStreamManager](https://github.com/polyend/tracker-ce/blob/main/core/AudioEngine/VoiceStreamManager.cpp)
and its [hybrid streaming guide](https://github.com/polyend/tracker-ce/blob/main/docs/developer_guide.md)
were design references only. Relevant ideas are cached starts, depletion-based
refill urgency and generation-checked completion. This implementation is original
and uses Teensy's internal memory budget, not their SDRAM allocations or sector
mapping/async APIs. BroTracker's slicing and pitched playback requirements remain
future production work.

Current build/hardware evidence and the actual patch are saved under
`_backyard/sd_streaming/`. See `RESULTS.md` there for the current run status.

## Measured result (2026-11-07)

On the inserted exFAT card (CID `56534441505053440000000a6400b900`, 128 KiB
clusters), both finalists passed repeated ISR retriggers and eight distinct
six-minute WAVs through EOF, with zero underruns, I/O errors, content errors or
AudioMemory pool failures. Every complete-file hash matched the reference.

| Configuration | Streaming storage | Complete RAM2 static | Read p99 upper / maximum | Maximum source callback | Minimum ready audio | Steady seek/read duty |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Recommended: 8 KiB rings, 4 KiB reads/staging, 50% | 100 KiB | 120000 B | 3.220 / 3.551 ms | 183.172 us | 20.340 ms | 47.69% |
| Runner-up: 16 KiB rings, 8 KiB reads/staging, 50% | 168 KiB | 189632 B | 3.460 / 3.828 ms | 181.502 us | 17.438 ms | 26.33% |

Minimum ready includes startup/retrigger head-cache phases and excludes final EOF
drain; larger rings improve steady refill capacity, while the shared 4 KiB head
cache still limits startup. p99 is the largest per-voice/per-run upper bound,
not a pooled p99. Callback budget is 2902.494 us. The maximum Audio Library update
metric was 6.317% / 6.262%, respectively; this is not a full production CPU profile.

The low-RAM configuration rejected 506 stale completions over collision tests;
the runner-up rejected 2020. Across both, 34264 complete retrigger generations
passed reference-prefix checks. The matrix passed 36/44 workloads; full-ring
reads failed, and 2 KiB reads consumed about 88-91% seek/read duty despite passing.

This qualifies the tested native-rate diagnostic workload. Pitched playback,
production integration, fragmentation, other cards and loops remain unqualified.
Physical output capture and a fresh listening check remain pending. Full logs,
per-voice measurements, linker maps and the actual patch are under the ignored
`_backyard/sd_streaming/` directory.

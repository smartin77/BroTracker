# Production SD sample streaming — 2026-11-07

## Status

Implemented, built, flashed and hardware-tested on feature/sd-streaming without
PSRAM. All ten successful Windows BTX / USB audio bridge workloads below had
zero device underruns and I/O errors. Original published/working session contents
were restored and verified by readback; playback is stopped. The existing four
pattern files and WAVs were not overwritten. Physical listening and output capture
remain pending; host bridge counters are not physical-output validation.

The card initialized after the requested physical power cycle. Earlier
post-firmware-swap SD initialization failures remain recorded; no SD initialization
or USB bridge implementation change is included. Initial harness issues (sandbox
host access, empty-title window selection and premature START before working
readback) were corrected in ignored test tooling, without firmware changes.

## Bindings and format

| Display / raw ID | Resource | Frames | PCM bytes | Duration |
| --- | --- | ---: | ---: | ---: |
| 01 / 0 | Samples/test2.wav | 68284 | 136568 | 1.548390 s |
| 04 / 3 | Samples/test3.wav | 68284 | 136568 | 1.548390 s |
| 05 / 4 | Samples/test.wav | 306970 | 613940 | 6.960770 s |

Fresh original-production HELLO before changes confirmed all three files as
PCM format1, mono,16-bit,44100 Hz; no mismatch or source conversion. MIDI02/raw1
and Synth03/raw2 are retained. Reference note 60, native rate 44100 Hz, positive
Q32.32 pitch, linear interpolation, fixture 80% gain, Note Off arrival/deferred
boundary, mute release, Stop/Start and scheduled sample offsets retain their
existing code paths. Native speed is independent of pattern BPM.

## Memory and ownership

Eight 8192-byte rings=65536 B, three shared 4096-byte immutable starts=12288 B,
shared read staging4096 B: total81920 B (80 KiB), all RAM2. Removed full PCM arrays
were 136568 B each (273136 B total). No PSRAM or full-sample fallback is used.
Persistent fixed FsFile objects are opened/prepared before audio activation.
One positional cursor per source is serialized in the foreground. Separate voices
have independent rings, playheads, phases and 64-bit generations. Open handles are
idle between seek/read phases; each entire operation holds SdReadScope. Only the
existing pattern/session writers use the write guard, outside active playback.
Their deferred intent and explicit failures remain unchanged. No WAV writer or
runtime sample replacement API is added.

Audio renders from cache/published spans only. Committed renderer state is
exported after AudioBlock, preserving tentative-render rollback. Validation
copies never publish/cancel storage state. Stops/retriggers/configuration advance
generations; completion checks both generation and source identity. A read fills
shared staging, copies only free future ring slots, then publishes with audio
excluded. A new generation cannot consume the old ring while a copy is in flight.

Refills use 4096-byte reads with a 50% source-frame threshold and earlier refill
when less than 2048 output frames remain at the current pitch. Selection uses time
until depletion, not just source-frame counts. Short sources fully in the cache
finish through this same player. Starvation emits silence and advances the source
timeline; no stale backlog is replayed. EOF is counted independently. I/O failure
latches refill failure for that generation, with the next note allowing a retry.

Existing notes 24..127 remain accepted. The finite cache/ring cannot guarantee
eight arbitrarily high pitched streams: higher pitch reduces cached output time
and increases bandwidth demand. No silent pitch limit is added. Actual supported
simultaneous pitched workloads still need hardware qualification.

| Region / budget | Original production | Streaming production | Delta |
| --- | ---: | ---: | ---: |
| ITCM code | 145032 B | 146632 B | +1600 B |
| ITCM bank allocation | 163840 B | 163840 B | 0 |
| DTCM static variables | 59616 B | 62464 B | +2848 B |
| DTCM remaining local/stack budget | 300832 B | 297984 B | -2848 B |
| RAM2 static variables | 296480 B | 105248 B | -191232 B |
| RAM2 remaining heap budget | 227808 B | 419040 B | +191232 B |
| EXTMEM used | 0 | 0 | 0 |

Totals are from fresh baseline ELF/size output and the final production linker
map/ELF. RAM2 arithmetic includes a 16-byte alignment reduction beyond replacing
the arrays. DTCM includes descriptors, counters, the 512-byte read histogram and
larger voice/binding metadata. Streaming owner object is 1704 B DTCM; voice state
also lives in the existing transactional renderer copies. These are static region
budgets, not runtime stack/heap high-water measurements.

## Validation

- Production teensy41_dev baseline build: success.
- Production teensy41_dev streaming build and upload: success.
- Retained teensy41_pattern_sd_playback regression build: success.
- Host CMake build and CTest:7/7 suites pass (final run 46.91 s).
- Five focused streaming tests cover RAM equality at 0.5x/1x/1.5x/4x through
  cache boundaries, ring wrap, endpoint clamp and EOF; stale generation rejection;
  starvation silence and timeline advance; 8 cached voices with scheduled offsets;
  pitch-aware priority/capacity/EOF; Note Off offset and reconfiguration identity.
- Existing compiler warnings remain in snapshot/volume/stem formatting and the
  existing storage operation-name copy. No new warning is reported in the new
  streaming files. Warnings are recorded in the build log.
- Hardware: ten Windows BTX cases pass, plus deferred-save/stopped-write exclusion.
  Per-case raw logs, before/after device counters, host logs and assertions are in
  hardware-results.json, hardware-supplemental.json and hardware-summary.json.
  Original-session readbacks match (session-restore-final.json); final-state.json
  confirms zero lifetime underruns/errors and no active voices.

BTSTREAM1 GET is a foreground diagnostic command. IO reports reads, I/O errors,
p99 upper bound (100 us buckets; final bucket uses maximum), maximum seek/read
microseconds and discarded stale completions. VOICE reports channel, source
position, lifetime underrun output frames, natural EOF count, minimum ready source
frames excluding final EOF drain, last-generation pre-gain PCM FNV-1a32 and active
stream flag. The existing PERF command/periodic record includes the full source
callback, committed state publication and MIDI collection. A callback/hash pass
is separate from Windows WASAPI host errors and from physical audible artifacts.

## Production hardware measurements

Windows BTX used the unchanged bridge route: Teensy MIDI/Audio capture at 44100 Hz
(stereo float WASAPI client) to Headphones (BOMGE USB Audio Device), whose Windows
mix rate is 48000 Hz. The mono engine is duplicated into stereo by the existing
output graph. Onboard SDIO served the three actual WAV paths above; no synthetic
substitutes or file conversion. Previous diagnostic card identification was
CID 56534441505053440000000a6400b900 / exFAT 128 KiB clusters; this production run
did not reread CID and does not independently requalify another card.

| Workload | Natural EOF counts by active channel | Device underruns / I/O errors |
| --- | --- | --- |
| test2 alone, native | 1 | 0 / 0 |
| test3 alone, native | 1 | 0 / 0 |
| test.wav alone, native | 1 | 0 / 0 |
| Three instruments concurrently | 1,1,1 | 0 / 0 |
| Eight sample channels, native | 1,1,1,1,1,1,1,1 | 0 / 0 |
| Eight channels retriggered over eight rows; both Note Off timings | 0,0,0,0,0,0,0,0 | 0 / 0 |
| Four channels at 0.5x,2x,~1.498x,4x | 1,1,1,1 | 0 / 0 |
| Eight channels, Stop at 0.6 s then Start | 1,1,1,1,1,1,1,1 | 0 / 0 |
| Eight channels at 2x through EOF | 1,1,1,1,1,1,1,1 | 0 / 0 |
| Eight channels, native, 60 s with pattern repetition | 7,7,6,7,7,6,7,7 | 0 / 0 |

The 64-row 100 BPM test pattern lasts 9.6 s. The 306970-frame Amen file ends after
6.960770 s, before the next pattern trigger, without tempo matching. Its native
pre-gain rendered PCM FNV-1a32 is 7a914186, consistent across solo/concurrent
completed generations. test2=e555b927 and test3=ecf52049 exactly match the earlier
full-RAM loader checksums (3847600423 / 3975487561). This is device PCM integrity
evidence, not captured physical output. The 60 s run intentionally stops during
the last Amen generation; its partial final hash is not compared to the full-file
hash. Complete EOF counts are reported separately from that Stop.

All 8 native concurrent voices reached EOF with matching individual hashes and
zero underruns. At that first eight-voice snapshot, per-channel minimum ready
source frames were 1921,1793,1665,1537,1409,1281,1281,1153 (minimum 26.145 ms at
unity). Lifetime minima after the mixed-pitch and 2x tests were
1729,1538,1282,1026,770,514,514,258 source frames. These minima are cumulative,
exclude final EOF drain and are not uniform output-time margins at mixed pitch.
The short 2x eight-voice EOF test passed; it is not a long-duration 2x throughput
guarantee. Existing high-pitch acceptance is retained without silently limiting
notes; extreme aggregate pitch and storage stalls can still exhaust finite buffers.

Final foreground counters: 6063 seek/read operations, zero I/O errors, pooled p99
upper 2700 us (100 us buckets), maximum 5277 us. Seven stale completions were
rejected during the retrigger/Note Off workload. Maximum source callback was
231854 cycles / 386.423 us at 600 MHz, 13.31% of the 1741496-cycle / 2902.493 us block
budget, with zero over-budget callbacks. This source callback includes committed
stream publication and MIDI collection; it is not a total CPU-utilization or
whole-Audio-Library measurement. The firmware image and memory map are the same
ones used for all tests; no intervening diagnostic flash was needed.

A supplemental foreground control check confirmed SAVE while Playing returns
`deferred-playing`, then STOP and a stopped SAVE succeed while source handles
remain prepared. No save intent was silently discarded or read/write exclusion
bypassed. The original session was then restored again and verified.

Every successful BTX run logged nonzero capture and render submissions, zero
host dropped frames and zero render-empty events. Each reported one capture
discontinuity in the first flow interval after startup; that counter did not
increase in later flow/shutdown records. Startup
silence-fill counters are retained in hardware-summary.json. A separate failed
startup had CDC bounded-receive-buffer overflow/reconnects before START; its log
is host-cdc-overflow-attempt.log, and it is not counted as an SD underrun or a
successful playback case. Successful retry cases contain no CDC errors.
Multi-voice runs reached full scale (host peak 1.0); existing gains/mix saturation
were preserved. Audible artifacts, clipping assessment and physical output
continuity have not been listened to or captured in this task.

The harness saved uniquely named sdstream-*.btdev fixtures, including setup
attempts, and used temporary working/published session contents. It never sent
PATTERNSAVE/SEED/MIGRATE/REPLACE for patern1..4; those existing files are preserved.
Created fixture names and successful save replies are in test2-eof-publish.log.
The final restored session is recorded in original-session.json and
session-restore-final.json. No BTX test process remains running.

## Remaining checks

Physical listening and physical-output capture/comparison remain pending.
Long mixed Sample/Synth/MIDI workloads, fragmented files, other cards, extreme
aggregate pitch, and injected permanent read failures need further qualification.
No slicing, reverse, sample loops, new formats or time stretching were added.
The tested native and short 2x three-file workloads do not promise arbitrary
pitched eight-file throughput. The prior isolated eight-distinct-long-file
measurements remain separate diagnostic evidence.

## Design provenance

The isolated tools/teensy_diagnostics/sd_streaming implementation and measurements
informed the memory/read sizes. Reviewed Polyend's
[VoiceStreamManager header](https://github.com/polyend/tracker-ce/blob/main/core/AudioEngine/VoiceStreamManager.h),
[implementation](https://github.com/polyend/tracker-ce/blob/main/core/AudioEngine/VoiceStreamManager.cpp)
and [hybrid streaming guide](https://github.com/polyend/tracker-ce/blob/main/docs/developer_guide.md)
for prepared starts, pitch/depletion priority and stale completion concepts only.
The BroTracker implementation is original GPLv3 code; no PolyForm code or SDRAM
budget was imported. Output configuration, USB bridge, existing sample source
files and reusable RAM players remain unchanged except that the RAM-only player
explicitly rejects a streamed view rather than reading beyond its cache.

Generated logs, source snapshots, actual diff, binaries and map are stored in
ignored _backyard/production-sd-20261107/. No branch change, staging, commit,
push or merge was performed.

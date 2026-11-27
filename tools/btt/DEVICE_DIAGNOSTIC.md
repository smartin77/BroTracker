# Opt-in physical BTT/SD diagnostic

**Corrected physical run passed both cases, all 16 async cycles and suite cleanup.**
The 2026-11-27 run passed SD/WAV/source validation/pattern access, then failed
`noedit_rewrite_reopen_validate` (4); CLEANUP reported 309. See the findings below.
The corrected run used `iofix-261127-b-8d351f`; normal firmware was restored.
The separate
`teensy41_btt_diagnostic` environment builds a serial-only Teensy 4.1 image. It
does not construct an audio graph, start transport, publish a tune, execute FX,
read working sessions/configuration, or enable production Tune Load/Save.
Normal `teensy41_dev` remains the default environment.

## Owner build, upload and run

Run from the repository root. Upload/serial commands below are instructions for
the owner; later runs must use a fresh identifier. The verification run below was explicitly
authorized to upload, run once and restore normal firmware.

```powershell
# Build both images without uploading.
$env:BTT_DIAG_RUN_ID = 'iofix-261127-a'  # Choose a new private identifier for each later run.
pio run -e teensy41_btt_diagnostic
pio run -e teensy41_dev

# OWNER ONLY: close BTX/other serial monitors, then upload the opt-in image.
pio run -e teensy41_btt_diagnostic -t upload

# OWNER ONLY: substitute the Teensy's current COM port. Record the entire log.
pio device monitor -e teensy41_btt_diagnostic --port COM7 --baud 115200 --filter log2file
```

The USB profile changes to ordinary USB serial; its port may differ from the
normal MIDI/audio/serial firmware. At the serial prompt send these exact ASCII
lines, each followed by Enter:

```text
BTTDIAG STATUS
BTTDIAG RUN
```

Startup and STATUS only report identity/state. They do not initialize the SD,
create directories/files, or begin tests. Only the exact RUN command starts SD
initialization, preflight and the test sequence. Overlong/unknown commands are
rejected, as are embedded NUL/control/non-ASCII bytes. One RUN is accepted per
boot; repeated RUN cannot recreate files.
There is no production transport: stopped-only is enforced by the absence of a
player/audio graph or any path that can start one, not a simulated STOP ACK.

If a test fails, it stops further testing and reports cleanup as unconfirmed.
Capture STATUS and the failure records. To request checked foreground cleanup:

```text
BTTDIAG CLEANUP
BTTDIAG STATUS
```

CLEANUP is accepted only after a failure. It may drain the adapter's own job one
turn at a time. A quarantined close or backing handle is not called successful;
the objects/files remain retained. A repeated cleanup request may retry an owned
job close after the fault clears. Backing quarantine cannot be discarded away
because this diagnostic never deletes files. `PASS step=cleanup_only` is **not**
a passing suite. A closed file's recorded I/O error remains historical evidence
but does not alone block cleanup: refs/writer/current wrapper/quarantine and the
pool's CanDestroy guard decide safety. First failure and per-slot snapshots
survive cleanup and remain visible in STATUS. No automatic retry, delete,
overwrite, rename or promotion occurs.

## Files, collisions and deterministic fixtures

All new diagnostic writes use `O_CREAT|O_EXCL` in
`/BroTracker/.btt-prototype/<BTT_DIAG_RUN_ID>/`. The default identifier is
`iofix-01`; the example build above uses `iofix-261127-a`. The build script
accepts 1–24 ASCII letters/digits/underscore/hyphen, starting with a letter/digit,
and records the selected directory in the source hash and build manifest.
Confirm the exact `BTTDIAG1 DIRECTORY` line with STATUS before a later RUN.
The directory (and missing parents) is created only after RUN. Every expected file is
checked for an existing-file collision before any file is created. O_EXCL is
still enforced on each actual create; preflight is not an overwrite permission.
An existing directory is allowed; an existing target file stops the run with
`COLLISION`/code 101. Existing tunes, samples, sessions and config are not touched.

Expected files from one successful boot/run:

| File within the private directory | Contents |
| --- | --- |
| `device-diagnostic.wav` | 6,188 bytes; 3,072 deterministic PCM16 mono frames at 44,100 Hz |
| `00000001-0-00000001.btt` | Case 0 input, explicit C-3/00 plus alternative tuning and unsupported values |
| `00000001-1-00000002.btt` | Case 0 byte-identical no-edit rewrite |
| `00000001-2-00000003.btt` | Case 0 BPM 150.00 edit |
| `00000002-0-00000001.btt` | Case 1 input with Root absent on instrument 11 and Fine absent on 22 |
| `00000002-1-00000002.btt` | Case 1 independent insertion of Root 60 and Fine 0 at unchanged BPM |
| `00000002-2-00000003.btt` | Case 1 defaults plus BPM 150.00 edit |

The actual `Backings<FsBackend>` registry allocates the filenames;
its process-local domain advances from 1 to 2 between the two arena lifecycles.
It is not a general-purpose importer of pre-existing paths. A new firmware boot
starts that namespace sequence again and will refuse retained names in that
same run directory. To prepare another run, choose a fresh identifier and rebuild
before the owner's upload. Do not remove, overwrite or rename any original
evidence. The first run's files directly in `/BroTracker/.btt-prototype/` remain
untouched; new runs use only the selected child. No card access is needed to
prepare the build, and runtime preflight still refuses a reused/colliding ID.
There is intentionally no firmware cleanup/delete command. Interrupted/failed
outputs may be partial and must not be treated as valid tunes.

`device_fixture.h` builds bounded native-C++ envelopes on the Teensy, not a Python
generated blob. Each has two distinct fixed16 patterns (IDs 101 and 303), order
`[101,303,101]`, all three FX slots, unknown FX including a present zero value,
opaque optional INST/TUNE TLVs and an unknown optional XTRA section. Sample
instrument 33 explicitly saves unsupported Root 0/Fine 199 and remains unused.
The two required instruments share the generated WAV while retaining their own
tuning. Original BPM is 125.00; edits use 150.00.

The explicit-default rewrite must be byte-identical. The absent-default cases
must match a separately assembled native expected envelope, including original
TLV order, appended absent defaults, unsupported values, unknown data, both
patterns/order and recomputed CRCs. Every output is synced, checked closed,
reopened through real FsFile backing, validated, then byte-compared before PASS.
Pattern access uses stable IDs; no FX interpretation or tune publication occurs.
This exercises Rewrite/seal/reopen/validation, not Store promotion or FAT atomicity.

Each case then performs eight cooperative candidate cycles: four Ready/share/
clone/release/reclaim checks, and four cancellations at progressively later
service boundaries, the last at Ready **before** acquisition of the completion
pin. The real producer, SourceService and singleton path pool are used. Service
performs one async preparation action per loop after producer refill service.
There is no blocking preparation loop and no prepared source is played.

## Expected serial results and evidence

Capture the BUILD record (Git HEAD and source SHA), all PASS/FAIL, DEF, OWN, MEM,
timing and final records. The build manifest under
`.pio/build/teensy41_btt_diagnostic/diagnostic-identity.json` lists hashed project
inputs; SDK versions are in the build log. Preserve the uploaded HEX SHA-256 too.
Preparation evidence, build/test logs, firmware identity, maps and comparison are
saved under `build/btt-device-diagnostic/`. Physical logs belong beside these,
clearly labelled with card/filesystem, firmware SHA and run date.

A successful run ends with:

```text
BTTDIAG1 PASS step=byte_identical_explicit_defaults ... case=0 ...
BTTDIAG1 PASS step=independent_default_insertion ... case=1 ...
BTTDIAG1 PASS step=arena_handles_checked_cleanup ...
BTTDIAG1 PASS step=suite run=COMPLETE stopped_only=1 files=RETAINED arena_peak=...
```

Expect 16 `async_cycle` PASS records, two checked arena cleanups and no FAIL.
At each cycle start/end the producer has three **reserved, unprepared development
slots**, no candidate/clone leases and only those three resource-path records.
Ready/shared cycles own two candidate leases and a clone to one source. Reclaim
must refuse that clone, then return to the baseline after its final release.
Backing refs return to the source Document's one ref after Discard. Final cleanup
has zero live arena objects/leases/path records and closed files; arena used returns
to zero. The historical arena peak remains reported. Reserved slots are not
evidence that production samples were opened: the inspection is intentionally empty.

FAIL names the step and a code. Source failures include numeric SourceStatus per
definition (`Missing=4`, `Capacity=5`, `PathCapacity=6`, `Unsupported=7`, `Io=8`,
`Quarantined=10`, `Stale=11`, `GenerationExhausted=12`, `StorageUnavailable=13`).
Foundation/Code values are scoped by step; custom diagnostic codes 100–108 cover
guards/collision/directory/WAV I/O, 200–208 comparison/schema checks, 300–312
candidate/ownership/cleanup checks, and 400 SD initialization. Open-unavailable
does not distinguish every physical cause of a missing/open failure. Preserve
all artifacts and per-definition status rather than interpreting every I/O
failure as corruption or a missing file.

Copy all seven files after a successful checked cleanup and capture their hashes.
For example, on the owner's mounted SD drive (replace `E:` appropriately):

```powershell
Get-FileHash E:/BroTracker/.btt-prototype/iofix-261127-a/* -Algorithm SHA256
```

The two case-0 input/no-edit BTT hashes must match. Do not infer FAT/power-loss
recovery from a completed close/reopen or from a matching hash.

## Measurement scope and differences from production

PASS `us` is elapsed `micros()` around the named operation; serial formatting,
traffic, allocator observations and MSP snapshots happen afterward. Source
creation/rewrites/validation include their stopped SD work. Async records sum
producer/adapter service, Candidate Step and Cancel suboperations; maximum is
the largest such suboperation, not whole-loop or concurrent-playback latency.
The cycle begin/reclaim/cleanup durations are reported separately. Core USB/timer
interrupt delays can contribute to elapsed time.

Heavy objects use the real 64 KiB bounded arena with placement construction and
CanDestroy guards. The arena and the same 143,360-byte producer PCM Storage use
RAM2; arena storage/PCM are explicitly initialized after RUN. `arena_peak` is
the peak actual arena `used()` observation, not a C++ heap or stack watermark.
Counts describe the diagnostic's known candidate/clone, producer slot, path-pool
and backing owners, not a new global ownership monitor.

`heap_used_snapshot`/`heap_free_blocks_snapshot` are newlib mallinfo current
allocator counters. `heap_uncommitted_snapshot` is the observed interval from
`sbrk(0)` to the linker heap limit when `heap_snapshot_valid=1`; it excludes
allocator free blocks and is not a measured heap peak or an allocation guarantee.
MSP and `boundary_msp_min` sample only report boundaries. They do not observe
deep callee/IRQ stack peaks. No live stack memory is painted. Linker gaps in the
build report are static capacity figures, explicitly separate from these snapshots.

This standalone image uses USB_SERIAL and no production USB audio patch, player,
MIDI, transactional renderer copies, working/published session, UI or control
handlers. Its larger DTCM gap cannot establish full production admission. The
normal build must contain neither BTTDIAG commands, diagnostic arena/storage nor
diagnostic file-operation paths; the saved ELF/map comparison verifies this.

Current compile/link figures, not runtime observations:

| Metric | Normal `teensy41_dev` | Standalone diagnostic |
| --- | ---: | ---: |
| ITCM banks | 8 | 4 |
| RAM1 static bytes | 157,888 | 24,960 |
| RAM1 linker local/stack gap | 104,256 | 368,256 |
| RAM2 static bytes | 166,688 | 221,312 |
| RAM2 linker heap interval | 357,600 | 302,976 |

The diagnostic holds a real 65,536-byte arena plus 143,360-byte producer PCM
Storage. The minimal image omits much of normal application state and retains
only the code exercised by this executable; it is not the full-BTT reservation
link with every diagnostic root. Production memory changes are zero. The normal firmware code/data
sections are byte-identical to the pre-change build in this comparison. Stopped-only observations from the corrected run are recorded below; runtime
heap/stack/IRQ peaks remain unmeasured.

Remaining physical checks: run on an identified card/filesystem, capture timings,
ownership/arena/allocator/MSP observations, verify files after close/reopen and
off-card copy, and record collision/failure behavior deliberately on expendable
diagnostic artifacts. Physical cache/SD timing, heap/stack/IRQ peaks, FAT power-loss
recovery, source-change enforcement, concurrent-playback deadlines and complete
production memory admission remain unproven. Native fixture and host entry-point
simulations are regressions, not physical results.

## First physical failure and software correction

The two original logs are `logs/device-monitor-261127-212130.log` and
`logs/device-monitor-261127-212741.log`, uploaded source SHA
`8707d1b2e1ce7fb2b9b82ad1a2d7b7269bcfd3a2dfe96a067ac6d940adc238e6`.
They do **not** identify the failed backend call or prove an open-handle leak.
Zero path refs/backing refs also do not establish a checked close.

The installed SdFat FAT/exFAT implementations reject seek past logical EOF even
for a writable file. Rewrite's first output is a 24-byte directory entry at
offset 32 in a zero-length file. The old fake allowed this seek; applying the
real rule reproduces code 4 in the old implementation. FsBackend now zero-fills
gaps and Resize growth with bounded sequential writes from EOF, checking every
result/error flag. Truncate remains a shrink operation; preallocation is not
used as a substitute for valid file length. This demonstrates a defect consistent
with the failure, not a retrospective measurement of the physical failed call.

Code 309 in the old cleanup checked sticky Inspect() results before CanDestroy.
It could therefore refuse already-closed resources after a recorded write error.
Current `BACKING` records separate `recorded`, `refs`, `writer`, `wrapper_open`,
`quarantine` and `poisoned` per domain/slot/generation. `first_failure` snapshots
are historical, `current` snapshots describe live metadata, and `cleanup_checked`
snapshots record the state before arena destruction. Invalid snapshots do not
prove closure. `HISTORY`/STATUS retain the original step/code/case/cycle even if
cleanup completes or later fails.

SdFat close calls sync and can close the inner file even on failure; FsBaseFile
retains its pointer then, so its isOpen() is a wrapper-retention observation, not
an OS-handle leak counter. Genuine quarantine remains blocking regardless of
wrapper/inner-open observations. No error/quarantine is cleared to obtain PASS.
Evidence and regressions are in `build/btt-device-io-fix/`; physical confirmation
of the correction passed in the single stopped-only run recorded below.

## Corrected physical verification (2026-11-27)

One RUN was executed on Teensy serial 17681760, initially COM4, with fresh
`BTT_DIAG_RUN_ID=iofix-261127-b-8d351f`. STATUS verified HEAD
`e10d57a6b25960ce4a8bdb13190355241857d4e0`, source SHA
`5424b5f1d730c36efa7415a0fafa03147f1aa2219c2180bf62d02c06f9c1ed66`
and `/BroTracker/.btt-prototype/iofix-261127-b-8d351f` before RUN.
Both fixture cases passed rewrite/reopen/validation, BPM/unknown/unsupported/default
checks and eight asynchronous cycles each. Final `PASS step=suite run=COMPLETE`
was followed by STATUS with code=0 and first_step=none. No failure, cleanup-only
command or retry was needed. Both normal suite cleanups reported zero source
slots, candidate leases, paths/bytes and backing refs; all backing wrappers were
closed with no quarantine. Files were retained. Original evidence was not
removed, renamed or overwritten. The port was available; no client was terminated.

Observed arena peak was 37,372 bytes; cleanup arena occupancy was zero. Allocator
snapshots reported zero allocated/free-block bytes and 302,976 uncommitted bytes;
MSP boundary minimum was 0x2005ff58. These snapshots exclude transient callee/IRQ
peaks and are not production headroom. No-edit rewrite/reopen took 120,620 and
121,016 microseconds; BPM edit/reopen took 125,112 and 125,515 microseconds.
These are single stopped-run observations, not playback deadlines.

`teensy41_dev` was uploaded afterward and rediscovered on COM3. Read-only
`BTTEST1 HELLO` returned `BTINS1 TUNINGBUILD 1 Nov 27 2026 16:31:13`, matching
the uploaded normal ELF. Full raw/text serial logs, command events, upload logs,
image identities and prior evidence snapshots are under
`build/btt-device-io-fix/iofix-261127-b-8d351f/`.
Card model/filesystem and off-card output hashes were not independently captured.
Deliberate physical collision/close-failure tests, production memory admission,
concurrent-playback timing, stack/heap/IRQ peaks and power-loss recovery remain
outside this successful stopped-only verification.

## Restore normal firmware

Close the serial monitor, then the owner can run:

```powershell
pio run -e teensy41_dev -t upload
```

Reconnect BTX to the normal USB MIDI/audio/serial firmware. Diagnostic files remain
on SD as evidence; normal firmware does not interpret or remove them.

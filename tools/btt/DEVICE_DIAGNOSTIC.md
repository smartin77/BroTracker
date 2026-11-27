# Opt-in physical BTT/SD diagnostic

**Prepared, not run on hardware. Physical results are pending.** The separate
`teensy41_btt_diagnostic` environment builds a serial-only Teensy 4.1 image. It
does not construct an audio graph, start transport, publish a tune, execute FX,
read working sessions/configuration, or enable production Tune Load/Save.
Normal `teensy41_dev` remains the default environment.

## Owner build, upload and run

Run from the repository root. Upload/serial commands below are instructions for
the owner; the preparation task does not execute them or access hardware.

```powershell
# Build both images without uploading.
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
a passing suite. No automatic retry, delete, overwrite, rename or promotion occurs.

## Files, collisions and deterministic fixtures

All writes use `O_CREAT|O_EXCL` in `/BroTracker/.btt-prototype/`. The directory
(and `/BroTracker` if absent) is created only after RUN. Every expected file is
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

The actual production `Backings<FsBackend>` registry allocates the filenames;
its process-local domain advances from 1 to 2 between the two arena lifecycles.
It is not a general-purpose importer of pre-existing paths. A new firmware boot
starts that namespace sequence again and will refuse the retained files. After
capturing evidence, the owner must archive/move the diagnostic files off-card,
or deliberately remove **only these diagnostic artifacts** before another run.
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
Get-FileHash E:/BroTracker/.btt-prototype/* -Algorithm SHA256
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
| RAM1 static bytes | 157,888 | 23,936 |
| RAM1 linker local/stack gap | 104,256 | 369,280 |
| RAM2 static bytes | 166,688 | 221,312 |
| RAM2 linker heap interval | 357,600 | 302,976 |

The diagnostic holds a real 65,536-byte arena plus 143,360-byte producer PCM
Storage. The minimal image omits much of normal application state and retains
only the code exercised by this executable; it is not the full-BTT reservation
link with every diagnostic root. Production memory changes are zero. Its ITCM
bytes are identical to the pre-change build; the existing TUNINGBUILD timestamp
is the only data-byte difference. Physical occupancy/heap/MSP results are pending.

Remaining physical checks: run on an identified card/filesystem, capture timings,
ownership/arena/allocator/MSP observations, verify files after close/reopen and
off-card copy, and record collision/failure behavior deliberately on expendable
diagnostic artifacts. Physical cache/SD timing, heap/stack/IRQ peaks, FAT power-loss
recovery, source-change enforcement, concurrent-playback deadlines and complete
production memory admission remain unproven. Native fixture and host entry-point
simulations are regressions, not physical results.

## Restore normal firmware

Close the serial monitor, then the owner can run:

```powershell
pio run -e teensy41_dev -t upload
```

Reconnect BTX to the normal USB MIDI/audio/serial firmware. Diagnostic files remain
on SD as evidence; normal firmware does not interpret or remove them.

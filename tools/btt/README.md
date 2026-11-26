# Isolated BTT backing and arena prototype

Developer-only code. No production Load/Save/session/UI/playback handler links
these files. No encoding, FX or playback behavior changes are introduced.

`ReaderOwner` holds either a compatibility host `shared_ptr` fixture or a pin
on an already-created fixed control. The bounded path never constructs a shared
control block. `Backings<Backend>` embeds **three** controls/handles: active,
candidate input/output, and a spare for verification/failed-close cleanup.
Document copies share controls, not additional slots. Exhaustion returns
`Capacity`. The compatibility shared-pointer path is not the target owner plan.

## Write-once policy

1. `Create` grants one movable writer to an exclusively created file in
   `/BroTracker/.btt-prototype/`. Caller must initialize SD and provision that
   directory before constructing `FsBackend(true)`; default readiness is false.
2. Names contain registry domain, slot and generation; counters never wrap into
   a live identity. O_EXCL collisions, including leftovers from another boot,
   fail without overwriting/deleting the preexisting file. There is no arbitrary
   pathname import, rename, snapshot collection or recovery subsystem.
3. `Finish` checks sync/close, reopens read-only and checks exact tracked size
   (at most 1 MiB) before granting a pin. Failed acquisition leaves the caller's
   prior owner intact. Failed/cancelled writers cannot be acquired as sealed data.
4. Sealed files cannot reopen for writing. Copies and failed document candidates
   keep them pinned until explicitly discarded by the caller. Alternate wrappers
   see the same identity; Rewrite rejects source aliases. `Discard` refuses
   deletion while a pin or writer exists. Retired files remain until explicit
   deletion; final release does not silently discard previous valid bytes.
5. Last release checks close. An uncertain close quarantines the slot; `Inspect`
   exposes the error and `Discard` retries checked cleanup. Pinned/open/quarantined
   controls cannot be reused or destroyed. Remove failures retain registered
   bytes for retry. Discarded tokens are stale; reused slots have fresh generations.
   Reconstructing a pool at the same address gets a fresh process-domain identity.

Identity is not a pathname. All prototype mutations go through the registry;
the real backend's raw methods are private to it. FsFile alone does not freeze
FAT contents. Future production integration MUST reserve the namespace against
every other writer/deleter/rename and removable-media replacement. Today's
production handlers do not enforce that policy. This is not physical SD proof.

FsFile handles are in-place rather than heap SDFile wrappers. Each bounded
open/seek/read/write/resize/sync/close/remove holds only an operation-length SD
lease, never a permanent lock. Transfers are at most 512 bytes. Candidate growth
uses a checked seek/one-byte extension; the rewriter fills every envelope byte
before validation. SD timing and durable recovery are outside this prototype.

## Arena lifetime

`Arena` uses caller-provided 32-byte-aligned storage and sixteen destructor
records. `Init` requires no live objects; `New` checks bytes, records and natural
alignment before noexcept placement construction. No packed structs, general
allocator or hidden fallback is used. Destroyed records invalidate handles;
the bump region is reclaimed on explicit `Reset`, without compacting holes.

Destroy documents/pins and temporary writers first, resolve quarantined closes,
then destroy the pool and reset the arena. Reset preflights every destruction
guard before touching any object; a pinned pool returns `Busy`. Metadata must
outlive Object handles; these are checked handles, not owners of the arena.
The caller-owned backend context must also outlive the pool and every pin;
the SD service is not owned or destroyed by a slot or document.
The arena destructor does not implicitly invalidate live objects. Calls and
refcounts are foreground-only. The reference-counter range exceeds the possible
number of owners in the bounded target address space; explicit acquisition also
checks its counter limit.

`backing_probe.cpp` initializes arena metadata in the first aligned part of
reserved RAM2, and pool/documents/workspace/pattern in the remainder, with explicit
destruction. Only small Object handles are on its CPU stack. That entry point is
rooted in an isolated diagnostic ELF and never called by production.

## Verification

Run from the repository root:

```powershell
cmake -S . -B build/btt-backing-prototype/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DBROTRACKER_BUILD_WINDOWS_TERMINAL=OFF -DBROTRACKER_BUILD_ARKOS_UI=OFF
cmake --build build/btt-backing-prototype/cmake --target BroTrackerBttBackingTests BroTrackerBttFoundationTests BroTrackerBttPatternLayoutTests
ctest --test-dir build/btt-backing-prototype/cmake -R '^BroTrackerBtt(Backing|Foundation|PatternLayout)Tests$' --output-on-failure
python tools/teensy_diagnostics/btt_admission/analyze.py --out build/btt-backing-prototype --prepare-config
pio run -c build/btt-backing-prototype/map-platformio.ini -e teensy41_dev
python tools/teensy_diagnostics/btt_admission/analyze.py --out build/btt-backing-prototype --prototype --measure
python tools/teensy_diagnostics/btt_admission/budgets.py --out build/btt-backing-prototype --prototype
```

The admission runner requires the existing production baseline ELF/idedata. It
compiles the actual foundation and adapter without exceptions/RTTI, comparing the
baseline layout before reservation linking. No generated source shim is used.
Allocation scopes include fixed control/arena initialization and owner lifetime,
not just operations after setup. Fixture vectors, fake disk bytes and reporting
are excluded. The fake backend tests the same registry/arena logic, not physical
FAT/cache/DMA behavior or power loss. Path/source coexistence, physical stack/heap
peaks, prefetch, FX execution and production recovery remain separate.

# Candidate resource admission (developer prototype)

`resource_admission.{h,cpp}` is isolated from firmware handlers and playback.
It uses the **existing singleton** `ResourcePaths()` and real two-byte
`ResourcePathRef`. No second path pool, PCM ring, or Sample prefix cache exists
in this prototype. Old, candidate, session, rollback and source owners compete
for the same 8,192 string bytes / 256 records. Capacity failure is an admissible
result; it never authorizes eviction of a live owner or path truncation.

`InspectInstrument` rechecks the accepted document's instrument section CRC and
schema using the existing bounded Workspace. `SavedInstrument` retains stable
ID, section/resource spans, original uint16 parameters and separate presence/
understood bits. Unknown encodings and all other optional fields stay in the
owned immutable backing. `Candidate` pins that backing without copying a file,
directory or complete tune. Raw spans remain recoverable after cancelling runtime
preparation, even when the original Document owner is released; revalidate the
backing to recreate a Document for lossless rewriting.

`Begin` scans at most 99 definitions sequentially and materializes exactly the
explicit initial pattern into caller arena scratch. It retains that pattern's
stable ID alongside readiness. Requirements conservatively
include every nonzero instrument update in that pattern and explicit per-channel
incoming instruments. A pitched note with no known incoming/update instrument,
or an absent referenced ID, blocks readiness. No preceding or later pattern is
decoded. Unused definitions are retained and classified, but acquire no runtime
path or source. This intentionally does not solve later prefetch or eviction.

Supported Sample parameters use independent absent defaults Root 60/C-3 and
Fine 0/00. Present unsupported numbers remain original/effective numeric values
with an unsupported-setting status, never replaced by defaults. Unknown-encoding
parameter values must be displayed as opaque saved bytes, not the numeric zero
in their uninterpreted view. MIDI defaults to channel 1 only when absent. Known
settings on the wrong type are retained as inappropriate. Unknown types remain
saved definitions without a playable binding.

Path status distinguishes absent, invalid runtime syntax, too long, unavailable
base, opaque encoding, owned and temporary pool capacity. Only SD-root base 0
is admitted. Base 1 needs an explicit project context which this prototype does
not invent; no host filesystem resolution is performed. A valid UTF-8 saved
reference need not be a supported runtime ASCII path. Nothing is normalized or
truncated. Every path retained after a temporary scan owns a real pool ref.

`Step` performs at most one path or source acquisition/poll. Pending retains the
same definition, its path and every earlier lease; it is Progress, not failure.
A source failure rolls
back only this candidate's acquisitions and retains its saved snapshot/backing.
`Cancel` releases runtime paths/sources; `Discard` additionally releases saved
state. `Begin` retries a disposable unlinked candidate. Linked active/rollback
owners refuse cancellation/reinitialization and arena destruction. Initial
resource readiness is **not** permission to play: envelope support, unresolved
order, unknown FX, source preparation, publication and transport have independent
requirements. The prototype never changes their behavior.

`SourceService` has both the original synchronous host fake and a developer-only
real producer implementation in `sd_source_service.{h,cpp}`.
Acquisition must pin a compatible immutable asset/format generation and own its
cache-key path. `SourceLease` is movable and cloneable only through checked
Retain; source slots cannot be reused while any candidate, active, rollback or
sounding-voice pin remains. Root/Fine Tune live per instrument, outside the PCM
key, so two tunings share one compatible source. The real adapter prepares PCM
through the existing asynchronous producer; Acquire itself does not advance SD
work. It pins prepared state, not immutable physical SD contents. Physical media
and source-change policy still need enforcement before production integration.

`SdSampleStreaming` now exposes independent generation-checked candidate pins;
see [the exact API](../../docs/SD_SAMPLE_STREAMING.md#candidate-source-pins-2026-11-27).
Acquire from `PreparedGeneration(path)`, clone through checked retain, access
through a valid token and balance every ownership increment with release. The
producer retains its path and prepared PCM/format generation until all existing
candidate, binding, rollback and published voice protections are gone. Reset
refuses live pins; generations never wrap or reset. Full source capacity and
generation exhaustion are explicit through `PreparationCapacity()`; legacy
preparation failure results remain unchanged. No physical SD immutability or
playback admission follows from pins. Root/Fine Tune remain outside the PCM key.

The adapter pins an exact prepared generation immediately or starts one checked
owned job. `AcquireFor(requester,path,lease)` scopes the request independently of
lease references; Candidate uses its own stable address. `Acquire` uses the
adapter address for direct developer clients. A second requester waits without
owning, consuming or cancelling the first request. Retain/Release map SourceToken
back to a checked producer pin; Access checks that pin before exposing view/path.
The service/producers must outlive all SourceLeases and cleanup. Tokens may not
be transplanted between backend contexts, and a Candidate address must not be
reused until `CanDestroy()` succeeds.

The scheduler services active refills first and calls adapter `Service()` once
per foreground turn. Service checks refill need/headroom and advances at most
one job action. Acquire, Candidate Step and cancellation do not advance it.
Legacy producer step/cancel/getters cannot advance, cancel or consume an owned
job. Job tickets never wrap/reset. Completion pins the exact source before
returning to foreground reclaim callers; Take transfers that pin. Reset refuses
outstanding owned tickets. A completed foreign legacy job remains reserved until
its caller explicitly `AcknowledgePreparation()`s the consumed/discarded result.
Existing legacy callers retain their previous behavior among themselves and no
production UI/publication caller is changed here.

`CancelFor` affects only the matching requester. Candidate Cancel drops its paths
and prior leases immediately, returning Busy while its own job closes, Io for
quarantined close, and Ok only after checked cleanup. `cleanup_status()` preserves
Pending/Quarantined explicitly. Discard/Begin and arena destruction refuse pending
cleanup. Continue Service and retry Cancel/Discard; failed close keeps its file,
reserved slot and path until checked retry succeeds. Cancellation after Ready
drops the producer's unconsumed completion pin; its now-unowned ordinary cache
entry is reclaimable, not forcibly evicted past other owners. No destructor spins
or performs file work. `SdSourceService::CanDestroy()` also excludes live leases.

Missing/open-unavailable, unsupported/malformed format, source/path capacity, I/O,
storage unavailable, stale identity, generation/ticket exhaustion and quarantine
have distinct statuses. A borrowed PreparePath view or replaceable rollback mask
is still not a candidate ownership credential. Production scheduler handoff,
publication, source-change policy and Tune Load/Save remain unimplemented.

Current ARM sizes: adapter 28 bytes, Candidate 5,976 (two 11,952), SourceLease 12,
producer 4,760. The producer increases production RAM1 static by 32 bytes after
linker alignment; RAM2 is unchanged. The actual adapter replaces the old 1,024-byte
unknown allowance in lifecycle.csv. Peak proposed arena phase is 54,427 bytes
including the 2,048-byte guard, leaving 11,109 of 65,536. The isolated full BTT
reservation link crosses to nine ITCM banks and leaves a 71,360-byte RAM1 link gap
before runtime stack allowances; production stays at eight banks and 104,256.
These compile/link figures do not establish physical SD or runtime admission.
Complete evidence is under `build/btt-source-adapter/`.

`StoppedExchange` simulates pointer/owner handoff. Commit requires a caller's
simulated stopped assertion, a Ready candidate and no retained previous owner.
Rollback swaps current/previous; ReleasePrevious discards only that old owner.
A sounding SourceLease clone survives that release. Detach clears the active
link so its caller can dispose it. Candidate objects must outlive the exchange;
call ReleasePrevious/Detach before destroying its context. There is no production
binding installation, audio ACK, MIDI change or transport acknowledgement here.

## Verification and reproduction

```powershell
cmake -S . -B build/btt-resource-admission/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DBROTRACKER_BUILD_WINDOWS_TERMINAL=OFF -DBROTRACKER_BUILD_ARKOS_UI=OFF
cmake --build build/btt-resource-admission/cmake --target BroTrackerBttResourceTests BroTrackerBttBackingTests BroTrackerBttFoundationTests BroTrackerBttPatternLayoutTests BroTrackerSdWavTests BroTrackerBringUpControlsTests
ctest --test-dir build/btt-resource-admission/cmake -R '^BroTracker(Btt(Resource|Backing|Foundation|PatternLayout)Tests|SdWavTests|BringUpControlsTests)$' --output-on-failure
python tools/teensy_diagnostics/btt_admission/analyze.py --out build/btt-resource-admission --resources --mapped-baseline build/btt-backing-prototype/mapped/teensy41_dev --measure
python tools/teensy_diagnostics/btt_admission/budgets.py --out build/btt-resource-admission --resources
```

The optional mapped-baseline argument reuses only a production-equivalent build;
its ELF sections must still match the production baseline. If unavailable,
generate/build an isolated mapped configuration using the admission tool's
existing prepare-config workflow. Relevant production sources must match that
baseline; reusing old code is not authorization to skip this check.

Original synchronous-fake allocation scopes include setup/destructor teardown, fixed backing ownership,
all candidate acquisition/release, and simulated handoff. Fixture vectors, fake
disk bytes, report I/O and framework registration are excluded. The fake source
catalog contains metadata/pins only, not a second set of PCM buffers. Host tests
exercise the real path allocator's record/byte limits and first-fit fragmentation.
Target layout/frame/link measurements remain distinct from host memory figures.
No physical SD/stack/heap, realtime deadline, or FAT/power-loss claim follows.

The real-producer target is `BroTrackerBttSdSourceTests`; it is included alongside
`BroTrackerSdWavTests`, the existing BTT suites, controls and core voice tests.
Its counted scopes include candidate admission, Pending polls, adapter Service,
lease clone/release, cancellation and repeated source/path reuse. Only allocations
inside fake FsFile open/seek/read container/string bookkeeping are excluded;
fixture/WAV/BTT generation and report I/O happen outside counted scopes. The
adapter fixture uses the same real singleton path pool and existing producer PCM
Storage, with no fake SourceService or additional runtime PCM cache.

```powershell
cmake -S . -B build/btt-source-adapter/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DBROTRACKER_BUILD_WINDOWS_TERMINAL=OFF -DBROTRACKER_BUILD_ARKOS_UI=OFF
cmake --build build/btt-source-adapter/cmake --target BroTrackerBttSdSourceTests BroTrackerBttResourceTests BroTrackerBttBackingTests BroTrackerBttFoundationTests BroTrackerBttPatternLayoutTests BroTrackerSdWavTests BroTrackerTests BroTrackerBringUpControlsTests BroTrackerVariableRateBringUpTests
ctest --test-dir build/btt-source-adapter/cmake -R '^BroTracker(Tests|Btt(Resource|Backing|Foundation|PatternLayout|SdSource)Tests|SdWavTests|BringUpControlsTests|VariableRateBringUpTests)$' --output-on-failure
python tools/teensy_diagnostics/btt_admission/analyze.py --out build/btt-source-adapter --prepare-config
pio run -c build/btt-source-adapter/map-platformio.ini -e teensy41_dev
python tools/teensy_diagnostics/btt_admission/analyze.py --out build/btt-source-adapter --resources --measure
python tools/teensy_diagnostics/btt_admission/budgets.py --out build/btt-source-adapter --resources
```

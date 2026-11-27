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

`Step` performs at most one path or source acquisition. A source failure rolls
back only this candidate's acquisitions and retains its saved snapshot/backing.
`Cancel` releases runtime paths/sources; `Discard` additionally releases saved
state. `Begin` retries a disposable unlinked candidate. Linked active/rollback
owners refuse cancellation/reinitialization and arena destruction. Initial
resource readiness is **not** permission to play: envelope support, unresolved
order, unknown FX, source preparation, publication and transport have independent
requirements. The prototype never changes their behavior.

`SourceService` is an explicit external contract, simulated by the host fake.
Acquisition must pin a compatible immutable asset/format generation and own its
cache-key path. `SourceLease` is movable and cloneable only through checked
Retain; source slots cannot be reused while any candidate, active, rollback or
sounding-voice pin remains. Root/Fine Tune live per instrument, outside the PCM
key, so two tunings share one compatible source. No code here prepares actual
PCM or opens real Samples. One virtual Acquire call is not a measured SD work
quantum; a real adapter must coordinate the existing asynchronous job/refill
owner and report checked close/quarantine failures.

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

The BTT SourceService adapter is still outside this change. Next map SourceLease
to producer tokens and define Pending/Ready admission with the single existing
async preparation job after active refills, including cancellation, job ownership,
checked close/quarantine and source-change enforcement. A borrowed PreparePath
view or replaceable rollback mask is still not a candidate ownership credential.
The backend must outlive all SourceLeases.

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

Allocation scopes include setup/destructor teardown, fixed backing ownership,
all candidate acquisition/release, and simulated handoff. Fixture vectors, fake
disk bytes, report I/O and framework registration are excluded. The fake source
catalog contains metadata/pins only, not a second set of PCM buffers. Host tests
exercise the real path allocator's record/byte limits and first-fit fragmentation.
Target layout/frame/link measurements remain distinct from host memory figures.
No physical SD/stack/heap, realtime deadline, or FAT/power-loss claim follows.

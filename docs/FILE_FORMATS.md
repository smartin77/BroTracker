# BroTracker File Formats

BroTracker uses three related formats for tunes, projects and project distribution.

The BTT/BTP/BTM sections are a **BTT Foundation documentation draft** refining
[D0032](ARCHITECTURE_DECISIONS.md#d0032---brotracker-file-and-container-formats).
The ownership draft is complete; the BTT v1 proposal below awaits review.
Its layout and limits are proposed, not approved or implemented. BTP/ZIP BTM
details remain open. Existing working-session formats below retain their
separately documented behavior.

## Project/data workspace and host paths

The **Teensy SD card is always BroTracker's project/data workspace**. Tune/project
and Sample references below belong to that storage domain. ArkOS/dArkOS and
Windows are UI hosts; their application directory and process working directory
are not the BroTracker project workspace.

Host runtime configuration is separate: `<executable-directory>/config/config.ini`,
resolved independently of the process working directory on both hosts. Windows
delivery is portable; its location is not a project-storage root. Existing host
logs and diagnostic evidence retain their documented paths; see
[host path/deployment boundaries](PLATFORM_DEPLOYMENT.md#application-location).

This workspace decision does not imply that the Teensy SD card is mounted as a
host drive or that file browsing and module/tune saving are implemented. Existing
SD WAV inspection/preparation and the [explicit working-session foundation](#working-session-separate-from-tunesprojects)
are separate bounded operations. BTT/BTP/BTM descriptions below remain design
contracts, not proof of implemented tune/project persistence.

## BTT — BroTracker Tune

BTT is the lightweight tune file, named `<TuneName>.btt`. It owns:

- the tune name and BPM;
- patterns;
- instrument definitions and their saved parameters, including Sample Root Note
  and Fine Tune;
- future tune-scoped settings.

Instrument settings belong to the tune even if the user never saves a separate
instrument preset. BroTracker and BTX application preferences remain outside BTT.

BTT stores references to external samples and, in future, external instruments
or modules without embedding their assets. A standalone BTT is portable within
the same system only while its original stored paths remain resolvable. Moving
the BTT alone does not collect its dependencies or guarantee portability to
another storage layout or system.

Stored paths and saved parameters are distinct from runtime resolution and
validation. For missing or invalid sample references and unavailable external
instruments, preserve and display the original path/reference and saved values.
Mark a missing sample as **not located**; make other resolution/validation
failures and unsupported settings visible. Any effective runtime fallback must
be distinguishable from the original saved value and must not overwrite it.

Loading, playback and ordinary saving must preserve unresolved references,
instrument definitions and saved settings, including invalid or unsupported
values. They must not silently clear, normalize or substitute saved intent.
The user must be able to relink an unavailable reference explicitly; runtime
resolution alone does not rewrite the stored reference.

### BTT v1 proposal — awaiting review

This is a proposed binary envelope and initial Teensy loading profile, not a
runtime implementation or an upgrade to `current.btsession`. All limits and
new field assignments below require review. No asset PCM, waveform cache,
application preferences, transport state or working/published history is stored.

#### Tune model, identity and order

A nonzero 16-byte opaque tune ID is independent of the UTF-8 name and filename.
Rename and ordinary Save retain it. Creation generates a new ID; Save As/copy
identity policy remains a product decision. The name is authoritative tune data;
filename sanitization and automatic rename behavior need UI policy.

Patterns and instruments have separate nonzero uint32 ID namespaces, scoped to
the tune. IDs survive reordering and ordinary saves; they are not array indices,
display numbers, runtime path-pool IDs or session lineage. Two persisted next-ID
counters allocate monotonically and must exceed every defined or referenced ID
in their namespace. Deletion never reuses an ID; exhaustion blocks allocation
rather than wrapping. Explicit replacement can retain an instrument ID.

The order list is a sequence of pattern IDs, with repetition permitted: patterns
17 and 42 plus order `[17, 42, 17]` play pattern 17 twice without duplicating it.
Store each pattern definition exactly once under its stable ID; order entries
contain references, not embedded copies. The pattern-count limit bounds distinct
definitions, independently of the longer order-list limit.
Each entry means one full pattern, first through last stored row. An empty order
means no song sequence; unused patterns remain stored. End-of-list is the stored
sequence boundary. Whether transport stops or loops, restart position, cross-
pattern voices/NOTE OFF, Pattern Break/Jump, ranges and per-order transforms are
open product choices; v1 introduces no hidden loop or jump opcodes. Existing
single-pattern looping is not evidence that song sequencing is implemented.

Instrument reference 0 in a cell means no instrument update. Other values name
stable instrument IDs. A reference to an absent instrument or pattern is retained
and reported as unresolved, never rebound by position. An unresolved order entry
blocks song playback until resolved; an unavailable instrument has no playable
binding. Defined but unsupported instrument types are retained similarly.

#### Envelope and addressable directory

Integers are unsigned little-endian unless stated otherwise; offsets are absolute
file byte offsets. No native struct dumps, compression or implicit alignment.
The 32-byte header is:

| Offset | Bytes | Proposed meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `BTT1`, distinct from `BTM1`, BTSS and BTDP |
| 4 | 2 | Major version, 1 |
| 6 | 2 | Minor version, 0 |
| 8 | 4 | Directory offset, exactly 32 in v1 |
| 12 | 2 | Directory entry count |
| 14 | 2 | Directory entry size, exactly 24 |
| 16 | 4 | Exact total file bytes, no trailing bytes |
| 20 | 4 | Whole-file CRC-32/ISO-HDLC, excluding bytes 20..23 |
| 24 | 4 | Required envelope feature bits, zero for v1.0 |
| 28 | 4 | Reserved zero |

Each 24-byte directory entry contains, in order: four ASCII section-type bytes,
uint32 object ID, uint32 offset, uint32 length, uint32 payload CRC, uint32 flags.
Flag bit 0 means required for interpretation; all other bits must be zero in
v1.0. Unknown flag bits are an unsupported capability, not permission to ignore
them. Payload CRC covers exactly that section's bytes. Both CRCs use reflected
polynomial `0xEDB88320`, initial/final XOR `0xFFFFFFFF`; they detect accidental
damage, not malicious changes. The directory is covered by the whole-file CRC.

Sections follow the directory, contiguous in ascending offset order, without
overlap, gaps or padding. Directory order matches physical order. Require unique
`(type, object ID)` pairs, exactly one `TUNE` and `ORDR` with ID 0, one or more
`PATT` sections and zero or more `INST` sections with nonzero IDs. These four types
are required sections. Writers place `TUNE`, `ORDR`, `INST`, then `PATT` before
optional descriptive/extension sections; readers address objects through the
directory rather than assuming their positions. Unknown optional sections have
opaque payloads; their type, ID, flags and bytes must survive ordinary saves.

Reject unsupported major versions. A later minor version is readable only if
the envelope structure is unchanged and all required features, sections and
fields are understood. Preserve its minor version on Save; never silently
downgrade. Future incompatible envelope structures require a major change.

#### Section fields and cell encoding

Known sections contain a sequence of TLVs: uint16 tag, uint8 encoding, uint8
flags, uint32 byte length, then exactly that many value bytes. TLV flag bit 0
means required; other bits are unsupported capability bits. Tags are unique
within a section; order is insignificant but preserved when possible. TLVs must
consume the section exactly. Encodings are 1=uint8, 2=uint16, 3=uint32,
4=16-byte ID, 5=UTF-8 without NUL, 6=opaque bytes. Numeric lengths must match
their encoding; text must be valid UTF-8. Unknown encodings remain opaque for
optional unknown tags. No terminators are stored. Required fields below must
occur exactly once with the stated encoding and required flag.

| Section | Required tags and values |
| --- | --- |
| `TUNE` | 1: ID (encoding 4); 2: name (5); 3: BPM hundredths (3); 4: channel count (1); 5: next instrument ID (3); 6: next pattern ID (3) |
| `ORDR` | 1: order (6), consecutive uint32 pattern IDs; length divisible by 4, each ID nonzero |
| `PATT` | 1: row count (2); 2: cell payload (6), sized by its codec; 3: pattern codec selector (2) |
| `INST` | 1: instrument type (2), 1 Sample / 2 MIDI / 3 Sine Synth |

Each logical row-major cell starts with uint8 note, uint8 NOTE OFF timing and
uint32 stable instrument ID, followed by **all three FX slots**. The earlier
six-byte-cell-only proposal is replaced by the layouts below. FX-only cells are
stored even when note is empty and instrument ID is zero. FX contain numeric
effect IDs and numeric values, never display mnemonic strings. Per the
[general FX draft](BT_GENERAL_EFFECT_DRAFT.md), keep the accepted three-character
mnemonic and two-hex-digit compact UI value. Command ID assignments, units and
execution semantics are not finalized by these storage experiments.

Notes reuse 24..127 pitched, 254 OFF, 255 empty. Timing is 0 arrival or 1 end of
position, retained even on non-OFF cells. Other note/timing codes are structurally
invalid for this cell encoding. BPM belongs only to `TUNE`, with supported
runtime range 3200..30000; it is not repeated in patterns. Nonzero BPM outside
that range is preserved as unsupported and blocks playback; zero is structurally
invalid. Channel and row counts must be nonzero; counts above the profile are
capacity failures.

#### Pattern layout alternatives and bounded access

Proposed fixed-width baseline: append three `(uint16 effect ID, uint8 value)`
pairs to the six-byte note/timing/instrument prefix: **15 bytes per cell** with
explicit little-endian encoding and no ABI padding. ID 0/value 0 denotes an empty
slot; any nonzero ID with value 0 is a present effect and must survive unchanged.
ID 0 with nonzero value is malformed under this proposed encoding. Nonzero
unknown IDs and their values remain saved data, reported as unsupported and
never executed. Slot order is preserved even if all three are populated.

The compared compact option uses `(uint8 ID, uint8 value)` pairs, giving 12-byte
cells. Reserving ID 0 for empty leaves 255 usable numeric IDs, versus 65,535 in
the 16-bit option. A separate presence mask could allow all 256 narrow IDs but
would make the narrow fixed cell 13 bytes and still leave a small ID space; that
variant is not measured here. Neither proposed ID width assigns commands to
numbers. Widening later needs a distinct required codec/schema, not truncation
or an escape that an old reader silently interprets as an ordinary command.
The 16-bit baseline spends 3 extra bytes per cell (1,536 per 64×8 pattern) to
leave ID space for future commands/import mappings. Values remain uint8 in both
experiments, matching compact `00..FF` representation without settling each
command's legal range. Wider/detail operands would require a separately designed
extension and preservation contract; the UI design does not force every future
internal operand to eight bits.

Compare one simple sparse alternative: a uint16 populated-cell count followed
by records `(uint16 linear cell index, fixed cell)`, in strictly increasing index
order. Index is row × tune channel count + channel. Omitted cells reconstruct as
note 255, timing 0, instrument 0 and three empty FX slots. Only exactly that
default cell is omitted; FX-only cells, present zero-valued effects, instrument-
only updates and retained nondefault OFF timing must all be included. Reject
duplicates, descending/out-of-range indices, default-valued records and length
mismatches. There are no inter-pattern dictionaries, delta chains, compression
history or dependencies on the preceding pattern. Both encodings decode into
the same bounded logical buffer and preserve numeric unknown FX.

The host experiment uses tentative codec selectors 1=fixed8, 2=sparse8,
3=fixed16, 4=sparse16. These are experimental labels awaiting format review,
not an approved registry. An unknown required codec prevents playable pattern
decoding; keep the opaque section recoverable without treating it as empty or
silently rewriting it as a supported codec. A full-tune loader must reject
unsupported required interpretation before replacing the current tune, per the
versioning rules. Known codec plus unknown FX IDs is instead preservable data,
not structural corruption.

For N cells, K nondefault cells and fixed stride S, fixed payload size is N×S;
sparse size is 2+K×(2+S). Both have 28 section bytes for required row-count,
payload-header and codec TLVs. A directory entry adds a separate 24 bytes per
pattern. Sparse wins below roughly 86% occupancy for 12-byte cells or 88% for
15-byte cells; dense sparse data costs more than fixed. Recommendation pending
review: use fixed16 as the correctness and capacity baseline and permit sparse16
per pattern when smaller. Encoding choice changes storage, not logical identity,
slot semantics or decoded-buffer capacity. Do not adopt narrow IDs solely from
size results before reviewing command-space requirements.

Propose a seekable, bounded reader with file size and `ReadAt(offset, buffer,
length)` operations, plus section-limited cursors. Directory lookup by `(type,
stable ID)` returns a checked span; tune fields, instrument sections/resource
references, order ranges and individual patterns each have independent readers.
Read one `PATT` by ID without decoding preceding patterns or retaining the BTT in
RAM. A small fixed directory can be indexed in RAM or rescanned with a bounded
entry buffer; neither approach needs pattern bodies resident. Order references
can be read in bounded ranges instead of retaining all 1,024 entries. Keep
metadata/opaque bytes in owned backing storage, not every descriptive TLV in RAM.

Initial stopped Load still scans the whole file for envelope integrity and all
required structural checks, through bounded scratch memory, before acceptance.
That scan need not materialize patterns or decode them in playback order.
Subsequent direct pattern access uses the retained validated index/backing and
revalidates its section CRC. Whole-file CRC must not be mistaken for a need to
allocate the whole file or decode preceding patterns on each access. Immutable
backing/source-change safeguards remain required.

Plan to load tune settings, instrument definitions and original resource
references ahead of playback, with a stable-ID-to-runtime-slot map. Loading a
definition does **not** prepare its source: resolving paths, validating WAVs,
reserving source capacity and preparing streaming buffers are distinct steps
with distinct failure/status reporting. Missing/unsupported instruments remain
defined. Sample PCM stays external and uses the existing streaming source model;
neither BTT patterns nor definition preload embeds or fully loads sample audio.

`INST` optional known tags are 2: resource reference (encoding 6), 3: Sample Root
Note (2), 4: Sample Fine Tune in positive cents (2), 5: MIDI channel (2), 6: name
(5), 7: description (5). Sample writers include tags 2, 3 and 4; MIDI writers
include tag 5. For a known Sample, absent Root Note resolves to 60 (C-3) and
absent Fine Tune resolves to 0 (00), independently. Normal Save writes those
resolved defaults into whichever tuning tags were missing, even if the resource
is unavailable. This is the explicit exception to preserving field absence;
it never replaces a present value. An absent MIDI channel has effective default
1; its absence remains saved absence. Explicit Root 0 is a saved unsupported
value, not an absent-field sentinel. Supported ranges are Root 24..127, Fine Tune
0..99 and MIDI channel 1..16. uint16 values outside those ranges survive unchanged;
they disable the affected binding with a visible unsupported-setting status.
Display the original saved value separately from runtime availability/status;
unsupported values are not treated as absent and are not defaulted on Save.
Settings inappropriate to the stored type are also retained and reported.
Unknown type codes preserve the whole instrument without constructing a binding.
Future settings use new TLVs; v1 defines no synth patch or external state schema.

Resource tag 2 is one uint8 base followed by the remaining UTF-8 path/reference
bytes, without NUL. Base 0 is SD-workspace-root-relative: e.g.
`Samples/Kick.wav` resolves from `/` on the Teensy SD workspace, independent of
the BTT's folder. Base 1 is project-relative: `Samples/Kick.wav` resolves from
the parent directory of the BTT inside its BTP directory. That directory is the
project root; nesting the BTT elsewhere is not inferred or searched upwards.
Thus base is stored per reference, not guessed from extension, host cwd or a
mounted host path. Standalone writers use base 0; project collection explicitly
copies samples and rewrites selected references to base 1. Moving a standalone
BTT does not rewrite base-0 references. Base-1 references require an explicit BTP
load context; opening them standalone reports an unavailable base.

Paths must resolve within their declared base. Runtime rejects leading slash,
traversal, invalid components, unsupported characters, excessive resolved length
or unknown base codes without changing saved bytes. A reference with no base byte
is malformed; a base with an empty path is retained as an invalid resource.
The current ASCII 255-byte path validator applies only when making a runtime
Sample binding. Valid UTF-8 that this backend cannot resolve remains saved and
visible. Future external instruments/modules may use additional base codes and
settings; their assets are not embedded by this proposal.

#### Bounded loading, preservation and failure behavior

Initial profile, all limits awaiting review: file at most 1,048,576 bytes; at most 256 directory
entries, 32 patterns, 99 instruments, 1,024 order entries, 8 channels and 64 rows
per pattern. Each section is at most 65,536 bytes and contains at most 256 TLVs;
name is 1..255 UTF-8 bytes. Resource payloads may exceed the runtime path limit
within the section bound and remain unbound. These are explicit initial support
limits, not claims about available firmware RAM or permanent product maxima.
Exceeding a profile/budget returns a capacity error without partial load.

Loading is stopped foreground work under the SD read guard. First check header,
checked offset/length arithmetic, counts, complete directory, section bounds,
TLV framing, required fields, unique IDs, ID counters and both levels of CRC.
Stream CRC checks through a proposed 512-byte scratch buffer, including unknown
sections; do not allocate from untrusted lengths. Only after complete validation
and reservation of candidate storage may a candidate become current. Missing
resources and unsupported settings do not make structurally valid data corrupt.
Bad CRC, truncation, overlapping ranges, duplicate fields/IDs, missing required
fields, malformed known encodings/lengths or malformed cells reject the load.
An understood but incorrect encoding for a known tag is a structural error;
the optional unknown-encoding exception below is preserved as unsupported.
Unknown required sections/TLVs/features reject as unsupported, separately from
corruption. Every rejection, I/O error or capacity failure retains the current
tune, edits and bindings. Loading never automatically starts playback.

Use a lossless saved-document layer separate from effective runtime definitions.
Retain unknown optional TLVs (including header and value), unknown optional
sections, unsupported values and unresolved references. Editing one understood
field replaces only that field; ordinary Save must not serialize just the
runtime projection. Insert only absent known Sample tuning defaults as specified
above. No clamping, default insertion over present saved values, path
normalization, ID renumbering or stripping extensions is permitted implicitly.
Known optional tags with an unknown encoding are retained as unsupported rather
than decoded; required tags cannot be interpreted that way and reject the load.

Large opaque data can be copied through bounded buffers from a retained immutable
backing snapshot, with CRC revalidation before promotion. A naked filename/offset
into a replaceable source is insufficient. Candidate load must establish owned
backing storage or an equivalent guaranteed immutable source before acceptance;
if that cannot be guaranteed within budgets, reject without replacing the tune.
Backing loss/change later blocks Save visibly, rather than dropping recoverable
bytes. Exact backing ownership (RAM versus SD snapshot) needs implementation
review; the preservation guarantee is required for either approach.

Save uses the existing exclusive-SD-lease and candidate/previous recovery model,
adapted to streamed BTT data: create an exclusive sibling candidate, check every
write/sync/close, reopen and fully validate/compare it, then retain the old valid
canonical as `.previous` before promoting the candidate. Promotion failure rolls
back where possible; rollback failure retains `.previous` and reports recovery
required. Never truncate the previous valid file, remove existing recovery
markers automatically or treat an uncommitted candidate as committed. Failure
must preserve the previous valid bytes, even if recovery must use `.previous`.
Retained backups are reported. FAT power-loss atomicity is not promised. No SD
load/save I/O runs while Playing or in an audio callback/ISR.

#### Transport boundaries: requirements and current guards

Confirmed device IDLE with no pending transport command satisfies the stopped
precondition for LOAD/SAVE screen entry; no fresh STOP exchange is required.
From Playing, Paused or any other state that is not confirmed stopped, entry
requires STOP and the device's STOP acknowledgement before proceeding to storage
readiness and any required session save, then screen activation. Sending STOP
alone does not confirm stopped transport. Failed or timed-out STOP must leave
entry blocked. Loading or saving a Tune, Project or Module
requires confirmed stopped playback at every entry point, including commands,
shortcuts, import/package routes and future APIs outside that screen. Paused is
not stopped. Recheck transport and pending-operation guards at dispatch and on
the device; a screen-entry check alone cannot guard a later operation.

| Operation | Playback boundary |
| --- | --- |
| Tune / Project / Module Load or Save | Stopped only; future requirement for BTT/BTP/ZIP BTM, not implemented support |
| Sample loading / instrument loading | May continue playback only through supported live operations and their existing guards; no blanket permission for preset formats not yet implemented |
| Sample saving | Unavailable while Playing; future requirement, no standalone Sample Save operation implemented |
| Instrument saving | During-playback permission remains open pending processing, snapshot consistency and SD I/O cost analysis; no standalone preset Save implemented |

Current code review (static inspection, not a runtime test):

- `BringUpControls::Request` / `Tick` in `src/ui/bringup_controls.cpp` send STOP
  on Pattern LOAD/SAVE entry from Playing, Paused or Done, wait for `Stopped()`
  and `MidiRecoveryStopped()` (acknowledged IDLE, no pending transport), then wait
  for successful session Save before showing the screen. Already confirmed IDLE
  proceeds directly to session Save without a new STOP exchange, satisfying the
  stopped precondition. Other unconfirmed states currently block entry rather
  than initiating STOP; extending that entry coordination remains future work. The
  retained-work recovery review route also opens without this entry handshake;
  it is a recovery presentation, not authorization for file operations. Review
  that route separately to ensure its presentation never bypasses operation-level
  guards; confirmed IDLE itself requires no additional STOP handshake.
- `BringUpSerial::RequestPatternLoad`, `RequestFile(PatternSave)` and pattern
  listing require confirmed IDLE/no pending transport independently of screen
  entry. Firmware `StorageBlock` / `StorageControlBlock` guard load readiness;
  `SavePattern` additionally rejects transport other than Stopped. These protect
  existing `.btdev` Pattern operations, not future full-tune persistence.
- Working-session Save is different: current firmware returns `deferred-playing`
  and retains RAM intent while Playing, and can persist on settled Pause/Stop.
  This temporary-session policy does not authorize Tune/Project/Module Save while
  Playing or Paused. Shared `SdAccess` read/write leases enforce exclusivity,
  not transport state; future handlers need their own stopped-state guards.
- Live Instrument entry preserves transport; WAV browser/preparation and existing
  instrument updates use supported live visits, pending-operation/recovery,
  source-capacity and refill/headroom guards. Failure retains the old binding.
  Keep updates session intent; it is not a standalone instrument-file Save.

[BTX Controls](BTX_CONTROLS.md#pattern-loadsave) describes the existing Pattern
screen and [live Instrument controls](BTX_CONTROLS.md#instrument) describe the
playback exception. Neither promises BTT/BTP/ZIP BTM loading. The general
screen-entry wording in [UI screen map](UI_SCREEN_MAP.md) must be read with its
live Instrument exception and the shortcuts above. D0030 in
[Architecture Decisions](ARCHITECTURE_DECISIONS.md#d0030---loading-a-new-tune-during-playback)
still calls for immediate playback when opening a new tune during playback.
That historical direction conflicts with this stopped-only scope and requires
later reconciliation; it is not authorization to implement live tune loading.

Defer analysis of seamless Tune loading during playback until PSRAM is available.
PSRAM is a prerequisite for considering that feature, not proof it is feasible
or permission to enable it. Later analysis must address candidate-state memory
alongside the active tune, source preparation and SD scheduling, safe handoff of
definitions/patterns/voices, and playback continuity including internal audio and
external MIDI. No live Tune loading implementation is part of BTT v1 foundation.

#### Reuse, gaps and memory consequences

The proposal is grounded in these current representations:

- `src/core/tune.h` already has title, BPM hundredths, channel count and a vector
  of patterns. `pattern.h` has a uint16 display number, uint8 length and vectors
  of channels; neither supplies stable IDs, order or tune-owned instruments.
  `tune_loader.cpp` is a whole-file development JSON reader that skips unknown
  keys; it is not a bounded or lossless BTT reader.
- `core/event.h`, `note.h`, `tempo.h` and `playback/row_events.h` supply note,
  tempo and NOTE OFF semantics and the 64×8 `RealtimePattern`. Conversion can
  reuse them, translating stable instrument IDs through a checked ID-to-runtime-
  slot map; Event's uint8 instrument cannot hold an on-disk uint32 ID directly.
- `bringup/instrument_definition.h` supplies Sample/MIDI/Synth definitions and
  availability separately. Its 99 slots use array-position identities;
  Append/Replace validate resources and resolve tuning defaults. It cannot own
  raw unsupported BTT fields. `sample_tuning.h` can supply effective defaults
  and range checks, not lossless storage of original values.
- `bringup/working_session.h` writes temporary `BTM1` v3..v6 with one working
  Pattern/BPM, definitions, view/mute and optional lineage. Restore initializes
  working and published from that one pattern. Legacy BTWS has two revisions,
  not two song patterns. None stores tune identity/name, a pattern collection,
  order, stable IDs or opaque extensions. Its full-buffer decoder and strict
  instrument validation cannot be used as the BTT document model.
- `bringup/pattern_file.h` supplies explicit LE encoding and CRC semantics, and
  `pattern_file_store.h` / `working_session.h` supply recovery logic. The current
  CRC helper skips bytes 20..23, so section CRC needs a separate generic path.
  `firmware/teensy/BroTracker/sd_pattern_storage.h` supplies guarded FsFile I/O,
  exclusive creation, sync/close and reopen comparison; its whole-buffer API
  needs bounded seek/read/copy operations. `runtime/storage.cpp` is only an
  initialization stub. `resource_path.h` owns validated runtime paths, not raw
  saved references (8,192 string bytes and 256 entries are shared with live and
  rollback owners).

Current `Event`, `RealtimePattern` and temporary session records have no three-FX
payload or independent FX presence representation. They cannot roundtrip this
proposed data. A lossless saved-pattern representation and eventual runtime
projection are new work; none is connected to application or firmware here.

#### Host-only pattern measurements

Native C++17 experiment: `tests/btt_pattern_layout.h` and
`tests/test_btt_pattern_layout.cpp`, using the existing test framework in an
isolated `BroTrackerBttPatternLayoutTests` CMake/CTest target. Measurements below
are from a Release GNU 16.1.0 Windows x64 build. Reproduce without building host
deliveries or firmware:

```powershell
cmake -S . -B build/btt-pattern-layout/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DBROTRACKER_BUILD_WINDOWS_TERMINAL=OFF -DBROTRACKER_BUILD_ARKOS_UI=OFF -DBROTRACKER_BUILD_FONT_PREVIEW=OFF
cmake --build build/btt-pattern-layout/cmake --target BroTrackerBttPatternLayoutTests
ctest --test-dir build/btt-pattern-layout/cmake -R '^BroTrackerBttPatternLayoutTests$' --output-on-failure
```

Measured **complete PATT section bytes**, including its 28-byte TLV overhead
but excluding its separate directory entry:

| Rows × channels | Fixture | Fixed8 | Sparse8 | Fixed16 | Sparse16 |
| --- | --- | ---: | ---: | ---: | ---: |
| 32 × 8 | Empty | 3,100 | 30 | 3,868 | 30 |
| 32 × 8 | Sparse: 8 populated cells | 3,100 | 142 | 3,868 | 166 |
| 32 × 8 | All 256 cells populated | 3,100 | 3,614 | 3,868 | 4,382 |
| 64 × 8 | Empty | 6,172 | 30 | 7,708 | 30 |
| 64 × 8 | Sparse: 16 populated cells | 6,172 | 254 | 7,708 | 302 |
| 64 × 8 | All 512 cells populated | 6,172 | 7,198 | 7,708 | 8,734 |

Sparse/dense fixtures include three populated FX slots, an explicit zero operand
and numeric unknown IDs. Sparse fixtures include an FX-only cell. Additional
wide-ID tests preserve IDs 1,024 and 65,535, empty slots and present zero values;
narrow encoding refuses those wider IDs instead of truncating. Generated BTT
envelopes use non-sequential pattern IDs 17 and 9,001, instrument ID 7,001, order
`[17, 9001, 17]`, and 60,000 bytes of unrelated optional data before the second
pattern. Direct access is tested in both directions with reads confined to the
selected pattern span. Truncation, malformed TLVs/cells, CRC damage, duplicate
IDs, overlapping sections, bad sparse counts/indices, unsupported codecs and
directory/decoded-cell capacity limits are exercised.

Host measurements: a decoded 64×8 structure-of-arrays pattern reserves 7,680
cell bytes plus rows/channels and ABI padding, **7,684 bytes** total, even for a
32-row or sparse pattern. Two such buffers cost **15,368 bytes**. A packed active
32×8 cell payload is 3,840 bytes, but the experiment deliberately reserves the
maximum capacity. Explicit decoder workspace is **529 bytes** (512-byte CRC block
plus 17-byte record buffer); a temporary logical Cell is 20 bytes on this ABI.
Scalar locals, call frames and C runtime FILE state are additional; this is not
a measured stack high-water mark. The reader object itself is 48 bytes. There
are zero counted C++ heap allocations during index loading and pattern decode;
fixture generation/reporting use dynamic storage outside the measured access.
No access operation allocates a file-sized buffer. File-backed reads are capped
at 512 bytes, and a target-only read window rejects preceding-pattern access.

Account separately for indexing and non-pattern state: the maximal 256-entry
wire directory is 6,144 bytes; the measured fixed Index is 6,148 including count.
Only 144 wire bytes are needed by the six-entry fixture, but the experiment
reserves the full index. A resident 1,024-entry uint32 order costs 4,096 bytes;
range-reading it avoids that full residency. Thirty-two maximum fixed16 patterns
would consume 245,760 cell bytes (246,656 with section TLVs) if all retained;
current/next buffering avoids requiring that bank in RAM. TUNE/INST TLVs and
large metadata remain separately bounded in backing storage. Loaded definitions,
ID maps, runtime path ownership, prepared sources and rollback copies need
additional admission budgets. For context only, this host ABI measures the
current `InstrumentDefinition` at 32 bytes, 99-slot `InstrumentDefinitions` at
3,376, shared `ResourcePathPool` at 10,240, old `RealtimePattern` at 1,538 and
`WorkingSession` at 6,504. These runtime types neither budget nor preserve the
complete proposed BTT model, and host pointer sizes are not Teensy pointer sizes.

Local generated fixtures, `measurements.csv`, `memory.txt` and test/build logs
live under ignored `build/btt-pattern-layout/`. The CSV records bytes/read calls,
allocation counts and 100-iteration mean host decode times per case. The decoder
uses an intentionally simple CRC pass plus validation and decode passes with
many small seek/read calls. Timings include those host file operations, are
affected by filesystem caching and are **not Teensy SD throughput, latency or
prefetch deadline evidence**. This experiment validates layout and bounded
access, not the full BTT schema, lossless rewriter, source preparation or player.

#### Proposed current/next-pattern buffering and deferred validation

Keep one current validated pattern buffer pinned and fill a separate next buffer
in foreground work. A repeated order ID can reuse a clean resident definition;
never store a second definition merely because order repeats it. SD reads,
CRC checks and decoding occur outside the realtime audio callback/ISR. Audio
sample refill has priority over pattern prefetch; reads and decode work need
bounded scheduling/quanta and cancellation before this can become a device path.
Tune/Project/Module Load/Save remain stopped-only; prefetch here means accessing
another pattern in the **same already loaded tune**, not opening another tune
during playback. Confirmed device IDLE/no pending transport remains sufficient
for the stopped precondition, with Sample/instrument exceptions as documented.

A transition requires a complete validated target and prepared playable sources;
preloading an instrument definition alone does not meet that condition. Unknown
FX are retained without execution. Future jumps can invalidate sequential
prefetch and require target lookup/cancellation. Dirty edited patterns cannot
simply be evicted: a separately budgeted edited-pattern store/backing policy and
consistent save snapshot are prerequisites. FX runtime state (effect memory,
slides, delayed events, loops and channel/voice state) may outlive a buffer and
must remain separate from saved cell storage. Do not clear it merely on reuse.

On-device memory admission, active/candidate coexistence, prefetch deadlines,
storage contention and failure/late-transition behavior remain unvalidated.
Whether to block Start, stop at a safe boundary or report another explicit
failure requires design and device evidence; do not silently use stale data,
skip a pattern or promise gapless playback. No firmware pattern streaming or FX
execution is implemented by the experiment. Seamless **whole-tune** loading
during playback stays deferred until PSRAM is available as a prerequisite;
pattern buffering is a distinct design question. Tune ID and session cache
lineage still require an explicit lifecycle mapping rather than aliasing.

#### Open decisions and first validation step

Review the proposed profile limits, ID/copy lifecycle, editable name/path policy,
sequence transport/transition semantics, future effects and pattern lengths,
external-state schemas, project cache lifecycle and RAM/backing strategy before
approving v1. Changing the profile must not silently discard unsupported data.

The host-only pattern layout/access experiment above is complete; codec/ID-width
choices and profile limits still await review. The recommended next step is a
full bounded envelope/TLV validator and lossless rewriter behind seekable
reader/writer interfaces, without UI or device integration. Extend the measured
file-backed pattern path rather than requiring an in-memory whole-file reader.
Keep unknown optional pattern fields/codecs and FX data recoverable. Use a fixture with
two patterns, repeated order, stable instrument IDs, a missing sample and an
unsupported tuning value plus unknown optional data. Require byte-identical
no-edit output for a fixture whose Sample tuning defaults are already explicit.
Then change one known field, such as BPM, rewrite, reopen and assert that unknown
optional TLVs/sections, unresolved references and explicit unsupported saved
values survive unchanged; only the edited field and derived lengths/offsets/CRCs
may change. Use a separate fixture with absent Sample Root Note and/or Fine Tune:
normal Save must insert C-3/00 only for the missing tags, retain any present
unsupported tag verbatim, and produce stable no-edit output on the next rewrite.
This default-insertion case is not expected to be byte-identical to its input.
Reject corrupt CRCs, truncation, duplicate IDs and overlapping
sections without changing a sentinel destination. Inject candidate write failure
in a fake store and assert the previous valid bytes survive. This validates the
foundation before attempting runtime projection or UI Load/Save. The current
host experiment does not implement that full validator/rewriter or save recovery.

## BTP — BroTracker Project

BTP is the native BroTracker project format.

A BTP project is a directory containing its BTT, copies of all samples used by
the project, and a separate project waveform cache. BTP does not imply a second
`.btp` tune file alongside the BTT.

A typical project may look like:

```text
MyTune/
    MyTune.btt
    Samples/
        Kick.wav
        Snare.wav
        Bass.wav
    WaveformCache/   # rebuildable derived data; illustrative directory name
```

The project directory is associated with the tune/project name.

When the tune/project is renamed, the associated project directory should be renamed accordingly automatically.

All samples used by the project are copied into the project directory. The
project's BTT uses project-relative references such as `Samples/Kick.wav` for
those copies. This makes sample storage self-contained and removes dependencies
on the samples' original locations. Unavailable samples must remain visible as
unresolved references; collection must not silently drop them or claim a
complete project. References to external instruments or modules do not imply
that their assets or required hardware have been collected.

The project waveform cache is rebuildable derived data, separate from the BTT
and source samples. Its absence or invalidation must not lose tune or instrument
settings; visualization can be rebuilt from available samples. Cache layout and
generation are future work. The implemented
[terminal waveform cache](INSTRUMENT_FOUNDATION.md#persistent-terminal-waveform-cache)
is host-local and keyed by durable working-session lineage; it is not yet this
project cache. Its application preferences remain outside BTT.

### Addressable File Structure

BTP's BTT shall support addressable tune data. The following is a conceptual
loading goal, not a separate BTP file layout or a binary schema.

The tune data should be organized so that the information required for normal operation can be loaded first.

Runtime-relevant information should therefore appear before optional descriptive information.

Conceptually:

```text
[essential project data]
[runtime-relevant data]
[instrument/sample references]
[additional project data]
[optional metadata]
[tune description]
[instrument descriptions]
```

The exact binary representation is not yet defined.

The purpose of this organization is to avoid loading unnecessary information into device memory.

Optional information may be read independently from the TF card when required.

### Descriptive Metadata

BTP's BTT may contain descriptive metadata such as:

- tune description;
- instrument description;
- MIDI instrument information;
- external hardware information;
- bank/program information;
- short notes describing external instrument configuration.

Instrument descriptions are particularly useful for MIDI instruments because the sound itself may exist entirely outside BroTracker.

For example, a creator may document which external synthesizer was used, which MIDI channel it uses, and which bank/program or other relevant settings are required.

Descriptions should not be unnecessarily constrained by the memory requirements of the realtime portion of the project.

### Future External Instrument State

BroTracker plans to allow a Tune or instrument to preserve the state required by an external hardware instrument.

The stored External Instrument State should not be tied to one specific MIDI technology. Different devices may use different mechanisms to capture and restore their state.

Examples include:

- manufacturer-specific SysEx patch or state dumps
- MIDI-CI Property Exchange Device State
- other future state mechanisms supported by the device

For example, a Novation Bass Station II instrument could use a manufacturer-specific SysEx patch dump. A compatible modern MIDI device could instead provide its state through MIDI-CI Property Exchange.

When the Tune is loaded again with compatible hardware connected, BroTracker could restore the stored instrument state before playback begins. Normal playback would then continue using notes, CC and other realtime MIDI messages.

The amount of state that can be captured and restored depends on the capabilities of each device. BroTracker therefore cannot guarantee complete state restoration for every external MIDI instrument.

The External Instrument State storage format has not yet been designed.

The format should remain flexible enough to support different state mechanisms without requiring the BroTracker Tune format to be redesigned for each new MIDI technology.

## BTM — BroTracker Module

Future BTM is a single-file container for a complete BTP project.

BTM uses ZIP as its container format.

Conceptually:

```text
MyTune.btm
    |
    +-- ZIP
         |
         +-- MyTune/
              |
              +-- MyTune.btt
              |
              +-- Samples/
              |    +-- Kick.wav
              |    +-- Snare.wav
              |    +-- Bass.wav
              |
              +-- WaveformCache/ (rebuildable; packaging policy open)
```

A BTM therefore contains a complete BTP project.

Whether derived waveform caches are included in the future package remains
open; they are not required to recover the tune and its collected samples.

BTM does not introduce a separate playback representation. It is a portable container used for:

- transfer;
- backup;
- sharing;
- distribution;
- archiving.

A BTM should be extractable into a normal BTP project directory without changing the project's musical or runtime data.

## Format Relationship

The three formats serve different purposes:

```text
BTT
<TuneName>.btt: tune name, BPM, patterns, instruments and saved parameters
External asset references; assets are not embedded


BTP
Native project directory
BTT + copied samples with project-relative references + separate waveform cache


BTM
Future portable module container
ZIP containing complete BTP project; derived-cache packaging policy open
```

The BroTracker realtime core does not depend on any of these external container formats directly. File loading and conversion are responsible for producing the internal data representation required by the core.

## Future Specification

The BTT v1 layout, versioning, encodings and integrity rules above are a proposal
awaiting review, not a finalized specification. BTP/ZIP BTM packaging details
and runtime implementation remain open. The temporary session layouts below do
not specify these future formats.

These details shall be specified separately once the internal data model and storage requirements have stabilized.

## Working session: separate from tunes/projects

Teensy owns `/BroTracker/current.btsession`. Normal Windows and ArkOS session
save/exit, automatic stopped saves and boot restore use the temporary BTM below.
Hosts are UI clients; definitions, playback and session authority stay on Teensy.
Startup/reconnect never replays a configured development file over a valid session.

### Temporary BTM session version 1

This bounded development format is **not** the future ZIP BTM described above.
The existing filename is retained to reuse recovery markers and installed workflows.
All integers are little-endian. The maximum file length is 8027 bytes.

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `BTM1` (temporary format discriminator) |
| 4 | 2 | Version 1 |
| 6 | 2 | Header length 24 |
| 8 | 4 | Embedded Pattern record length |
| 12 | 4 | Definition count N, 0..99 |
| 16 | 4 | Reserved zero |
| 20 | 4 | CRC-32/ISO-HDLC, omitting bytes 20..23 |
| 24 | variable | One BTSS-layout Pattern record: notes, timing, raw slots and the single session BPM |
| after Pattern | N ? 65 | Consecutive definitions: Type byte (1 Sample, 2 MIDI, 3 Sine Synth), 64-byte zero-padded resource |
| after definitions | 8 | Row, channel, field, header, mute mask, three reserved zeros |

The embedded record's tempo is the sole BTM BPM, not a standalone Pattern BPM.
Save captures current working intent. Restore initializes both published baseline
and working state from this one Pattern and BPM; previous publication history is
not persisted. All definitions, including unused ones, retain their array position
(raw 0..N-1/display 01..N). Type and SD-relative WAV reference are all the current
development definition model carries; MIDI/Synth have no resource payload. Sample
paths are 1..63 ASCII letters/digits/slash/dot/underscore/hyphen, with no leading
slash or `..`; non-Sample resource fields must be empty. No WAV PCM is embedded.
Future parameters require a format version change.

Exact length, version, reserved bytes, CRC, Pattern and every definition validate
before destination mutation. Malformed definitions invalidate the whole session.
Missing/unprepared WAVs do not invalidate definitions: retain slots, references and
notes, omit unavailable playback bindings, and never substitute another instrument.
Sample assets are prepared by their saved SD-relative WAV path rather than saved
slot, using the shared streamed-source owner. This includes picker paths under
`BroTracker/Samples/` as well as legacy development paths under `Samples/`. Missing,
unsupported or source-capacity-exhausted WAVs retain definitions but no playable
binding. No BTM layout/version change is needed.
The one trailing undefined slot remains selectable and can remain referenced in a
session; persistence does not create a definition for it or clear its reference.

Legacy single BTSS and BTWS v1/v2 remain readable. They have no definitions and
therefore use the original five development definitions. BTWS retains its old two
records/tempos on legacy decode; the next successful save migrates to temporary BTM
using working intent and its BPM. Legacy files cannot recover previously RAM-only
appended definitions. Candidate/previous safeguards below apply unchanged, including
fallback to a validated previous file and never promoting an uncommitted candidate.
Cursor and acknowledged mute state are retained; restore is stopped. No host config,
voice phase, playback position, gain or external instrument state is persisted.
The legacy tooling-only `RESTORE` command remains single-BTSS-only; normal UI restore
is boot plus authoritative readback, not that retired command.

### Standalone Pattern contract (future import/export)

A standalone Pattern carries notes, timing and instrument slot numbers. BPM and
definitions belong to the destination project. Import keeps a reference whenever
that destination slot is defined, even if its instrument differs or its Sample WAV
is unavailable. If the destination slot is undefined (including the sole trailing
undefined slot), clear **only** the instrument reference. Preserve notes and timing,
and use the destination project's BPM. No instrument matching, substitution or
creation is implied. Standalone Pattern import/export is not implemented here;
existing development `.btdev` operations retain their temporary behavior.

### Legacy working-session envelope

All integers are unsigned little-endian; no native struct dumps. Historical BTWS maximum 3152 bytes (v2); v1 maximum 3144 bytes.
New writes use temporary BTM above; the following describes legacy decoding.

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `BTWS` |
| 4 | 2 | Version 2; readers also accept 1 |
| 6 | 2 | Header length 24 |
| 8 | 4 | Published BTSS record length |
| 12 | 4 | Working BTSS record length |
| 16 | 4 | Reserved zero |
| 20 | 4 | CRC-32/ISO-HDLC of envelope header/payload, omitting bytes 20..23 |
| 24 | variable | Published BTSS record, then working BTSS record |
| after records | 8 (v2 only) | Row, channel, field, header, mute mask, three reserved zeros |

For legacy BTWS, both records and the complete envelope validate before destination mutation. Each
record is bounded to 1560 bytes. Unsupported versions, reserved fields, excessive
length, truncation, trailing bytes, bad CRC, notes, timings, dimensions or tempo
are rejected. Old single-record BTSS sessions remain readable as identical
published/working state; they contain no separate unpublished edits to recover.

### Temporary pattern records and files

`/BroTracker/tune.btdraft` is a retired temporary tune workflow. Existing files
are left untouched; BTX has no Load/Save/Publish/Restore shortcuts for it, and
firmware rejects DRAFTSAVE/DRAFTLOAD. BTSS remains documented for legacy readers.
Pattern Load/Save now targets `.btdev` files; Save As remains future work.

Both draft BTSS and development BTDP records have this version-1 layout (temporary
BTM embeds this layout and uses its tempo as the single session BPM):

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `BTSS` (draft/legacy session) or `BTDP` (development) |
| 4 | 2 | Version 1 |
| 6 | 2 | Header length 24 |
| 8 | 1 | Active rows 1..64 |
| 9 | 1 | Active channels 1..8 |
| 10 | 2 | Reserved zero |
| 12 | 4 | Tempo in hundredths of BPM, 3200..30000 |
| 16 | 4 | Payload length: rows ? channels ? 3 |
| 20 | 4 | CRC-32/ISO-HDLC, omitting bytes 20..23 |
| 24 | variable | Row-major raw note/instrument/OFF-timing triples |

CRC uses reflected polynomial 0xEDB88320, initial/final XOR 0xFFFFFFFF. Notes are
24..127, 254 OFF, 255 empty; all raw instrument bytes are preserved (255 means no
update). OFF timing is 0 arrival / 1 end-of-position, including non-OFF cells.
Exact file length is required. Inactive cells return to empty defaults. Binding
validation on application can still reject a structurally valid file without
changing live work.

### Replacement, recovery and realtime limits

Each writer acquires the existing exclusive SD lease, serializes bounded data,
writes an O_EXCL `.candidate`, checks write/sync/close and byte-for-byte reopen
verification, renames the canonical file to `.previous`, then promotes the candidate.
Failure preserves or rolls back the previous committed copy where the API permits.
A retained backup or rollback failure is reported explicitly. Existing candidate/
previous files block writes instead of being silently deleted. Boot prefers a
validated canonical session, otherwise a validated previous copy with a warning;
it never assumes a candidate was committed. Manual recovery must archive all
three files before choosing a valid copy. FAT/card power-loss atomicity is not
promised; actual failure injection remains a physical validation item. Failed or
ambiguous boot recovery blocks automatic/explicit session writes until files are
archived and repaired, so a fallback fixture cannot erase recoverable work.

No persistence SD I/O runs while Playing. Fixed device RAM retains working state;
Pause/Stop saves after control/preview/MIDI queues settle. A playing explicit Save
freezes one bounded tune snapshot and reports pending until Pause/Stop completes
the write. Session exit while Playing reports deferred persistence and preserves
playback. Sudden power loss can lose changes not yet safely saved.

### Pattern Load screen listing

BTX lists the existing `/BroTracker/development/` directory, not a new storage
layout. A listed filename has a 1..24 ASCII alphanumeric/underscore/hyphen stem
and exact `.btdev` extension. Each regular file must pass the existing BTDP
version-1 length/dimensions/tempo/note/OFF-timing/CRC validator. This includes the
four existing fixtures and arbitrary compatible filenames. Directories, malformed
records, other extensions and temporary replacement files are omitted. Instrument
binding compatibility is still checked by the existing application validator.

Firmware advertises `BTSTORE1 LISTCAP 1`. While stopped and storage/control queues
are ready, `BTSTORE1 LIST 1 request offset` returns up to eight
`BTSTORE1 FILE 1 request filename` lines followed by
`BTSTORE1 LISTEND 1 request next_offset done` (done is 0/1), or
`BTSTORE1 LISTERROR 1 request reason`. Request IDs are nonzero uint32; offsets are
opaque aligned directory byte positions, initially zero. A page scans at most 64
entries under the existing read lease and can be empty with more entries remaining.
The host accepts only the matching request, commits a page only on its complete
end marker, and times out without replaying commands. Left refreshes the first
page; directory edits during browsing require a refresh. Listing never creates or
writes files and is rejected during playback.

Selected files use the existing `BTSTORE1 READY 1` / `BTSTORE1 LOAD 1 filename`
path, independent of startup fallback settings. Validation precedes application;
the host waits for the application acknowledgement and authoritative baseline /
working readback before returning with a reset editor cursor. Known rejected
loads preserve the current work. Uncertain application/readback keeps the existing
recovery barrier instead of claiming that the device did not change. No pattern
Save, session format change, cursor persistence or startup migration is added.

### Development selection and protocol

`[development] startup_pattern` is the startup fallback only when the session is
confirmed missing/invalid, and never a reconnect replay. Names accept 1..24 ASCII
alphanumeric/underscore/hyphen characters, optionally exact `.btdev`. No paths,
quotes, whitespace, traversal, truncation or other extensions. Exact unquoted
`none` or empty uses the configured blank-pattern length at 12500; `none.btdev`
names a real file. Invalid config names warn and use the configured blank length.
A valid session always wins. Named file contents determine dimensions and tempo.
Restart BTX to read changed config; syntax and installed settings are preserved.

Firmware advertises `BTSTORE1 CAP 1`, `READYCAP 1` and `SESSIONCAP 2`. Hosts require
the last capability for working-session actions; older SAVE behavior
must not be mistaken for saving unpublished edits.

| Command | Effect |
| --- | --- |
| `BTSTORE1 SAVE 1` | Save working session, or explicitly defer while Playing |
| `BTSTORE1 LOAD 1 filename` | Explicit development-file load through the same validation/barrier |
| `BTSTORE1 READY 1` | Bounded stopped-storage readiness negotiation |
| `BTSTORE1 RESTORE 1` | Legacy single-BTSS session tooling only; not normal startup or a UI shortcut |
| `BTSTORE1 SEED 1 N` | Explicit create-if-absent development provisioning, N=1..4 |
| `BTSTORE1 MIGRATE 1 N` | Explicit verified migration of legacy theme/drums/midi/midi_hold names |
| `BTSTORE1 REPLACE 1 3` | Explicit narrow patern3 CH5 development migration |

Existing patern1..4 files remain unchanged by normal launch/reconnect. Provisioning
and migration never run automatically. No uncertain LOAD or SAVE is replayed.

## Future MIDI routing and file-data boundary

The [approved MIDI backend direction](MIDI_FOUNDATION.md#approved-direction-shared-midi-scheduling-and-selectable-backends)
keeps tracker data independent of backend and destination selection. Planned host
settings request Teensy-validated, acknowledged routing; destination/port/virtual
cable is separate from MIDI channel, and protocol/UMP capabilities are separate
from transport. No new config keys, persisted routing fields or session/file-format
changes are implemented by this plan. Existing session instrument references do
not establish endpoint availability or restore external synth state.

## Explicit boot SD availability (host warning)

SD-capable development firmware advertises `BTSTORE1 SDCAP 1` and sends one
`BTSTORE1 SD 1 available` or `BTSTORE1 SD 1 unavailable` line in HELLO discovery.
The source is exclusively the retained result of the single boot-time
`SD.begin(BUILTIN_SDCARD)` call (`g_storage_backend.ready`), before session
loading. This reports initialization success, not physical card-presence sensing.
The discovery FIFO reserves two additional lines.

Hosts accept this status only following the capability on the current connection,
clear it to unknown on disconnect, and latch unavailable until reconnection.
There is no hot mounting, repair or automatic card recovery. Legacy/unsupported,
pending or absent status remains unknown. Session recovery errors, missing or
invalid sessions, missing samples/patterns, READY timeouts and file-operation
errors never establish unavailable SD. Session formats are unchanged. Updated
firmware must be uploaded separately to provide this authoritative status.

### Pattern Save to its source file

`BTSTORE1 PATTERNSAVECAP 1` advertises the stopped-only command
`BTSTORE1 PATTERNSAVE 1 filename.btdev`. Filename validation and directory rules
match Pattern Load. The result label is `pattern-save-filename.btdev` in the
existing `BTSTORE1 RESULT 1 label result revision` envelope (revision 0).
The host associates a source only after browser Load application and baseline /
working readback synchronization, and sends Save only after working edits and
tempo synchronize. Firmware saves its working pattern, not the published baseline,
using the unchanged BTDP development encoder. It never submits a pattern or
changes transport. Save uses exclusive `.candidate`, checked write/sync/close and
reopen verification, then canonical-to-`.previous` / candidate-to-canonical rename,
rollback on failed promotion, and previous cleanup. Existing candidate or previous
files require explicit recovery. `backup-retained` is not reported as clean success.
This does not change BTDP, BTWS or any session-persistence behavior.

### Startup precedence and automatic working-session scheduling

Production Teensy keeps the RTC in UTC. The SD callback adds the active
terminal's current local UTC offset once when packing classic FAT calendar
fields. Before an offset arrives, and after a reboot until the next update,
the callback uses a UTC calendar. FAT stores no timezone or DST identity
and its modification timestamp has two-second granularity. exFAT uses a UTC
calendar on this path. The SdFat callback does not write exFAT UTC-offset
metadata, so the packed fields alone cannot establish an absolute instant in
another reader. Existing directory entries are never rewritten by clock
synchronization. The session payload carries no wall-clock time.
See [clock exchange and quality](LOGGING.md#teensy-wall-clock-exchange).

A valid restored session wins over host startup configuration. On confirmed missing /
invalid / unsupported session, BTX applies `[development] startup_pattern` once;
`none` uses `[btx] new_pattern_length`. `BTSTORE1 NEWCAP 1` advertises
`BTSTORE1 NEW 1 rows` (16, 32, 64), a blank eight-channel 125.00-BPM pattern applied
through the same acknowledged application/readback flow as Load, result label `new`.
No automatic START or reconnect replay. Valid sessions saved since boot are advertised
as valid to later host instances, so config does not replace them.
Invalid canonical session bytes are preserved at `current.btsession.invalid` before
fallback persistence. Existing quarantine/interrupted-write markers block further
automatic overwrites. No session or pattern format changes.
Working/tempo changes schedule a session write independent of UI. Storage/transport
barriers leave it pending. Failed writes retain pending intent with a failure latch;
another mutation/request rearms it. No busy-loop I/O retry. The firmware no longer
accepts DRAFTSAVE/DRAFTLOAD; old tune.btdraft files are left untouched. Internal
publication/readback recovery remains available via explicit retained-work review.

## Instrument foundation session snapshot (2026-11-09)

`current.btsession` previously held BTWS v1: two BTSS records, published baseline
and working intent, each with cells, NOTE_OFF metadata, dimensions and tempo.
It did not store channel mutes or the host edit frame. BTWS v2 retains those
records and appends eight CRC-covered bytes: row, channel, field (0 Note / 1
Instrument), header flag, authoritative mute mask and three reserved zero bytes.
BTWS v1 and legacy single BTSS files load with default cursor and unmuted channels.
Coordinates are validated/clamped against existing dimensions, never resized.

`BTSESSION1 CAP 1` enables `BTSTORE1 SAVE 2 row channel field header` through the
same readiness/synchronization and atomic candidate/previous session writer.
`BTSESSION1 VIEW 1 row channel field header` advertises saved host view on HELLO;
a surviving host retains its current context. Geometry is main-owned opaque session
metadata and never goes to the realtime player. Explicit save/exit captures the
host view; automatic musical saves retain the last captured view.

Mute is captured from acknowledged audio-owner state after host mute coordination
settles. At boot, the restored mask uses the existing SubmitMute slot and audio
block application/STATE acknowledgment, with no START. HELLO/reconnect never applies
a saved mute or overwrites the running device; existing uncertain mute readback and
retained working-draft recovery remain authoritative. Browsing instruments neither
saves nor changes mutes or musical data. Physical validation remains pending.

## Sample WAV picker protocol

`BTINS1 WAVCAP 1` advertises stopped-only Sample Load and path preparation.
Legacy `BTSTORE1 WAVLIST 1 request offset` pages the fixed `/BroTracker/Samples/` SD
directory, returning up to eight `BTSTORE1 WAVFILE 1 request filename` lines and
`BTSTORE1 WAVEND 1 request next_offset done`, or `BTSTORE1 WAVERROR 1 request reason`.
The existing read lease, 64-entry scan limit, aligned opaque directory offsets,
matching request IDs, complete-page acceptance and timeouts apply. Listing filters
bounded regular-file names, not WAV contents; content validation and prefix read
occur at confirmed definition commit so a failed selection can report its error.
No directories/files are created by listing or preparation.

The path stays in the host Instrument draft until normal `BTINS1 DEFINE` is
confirmed. New Sample definitions require a supported, prepared WAV. The stopped
foreground uses the existing RIFF validator and streaming engine; only the bounded
binding/definition append runs with audio excluded. Temporary BTM persists the
same path and re-prepares it at boot. Valid saved definitions survive missing or
unsupported WAVs without renumbering or fallback playback. The current bounded
source cache has 16 entries including the three reserved development sources.
Successful stopped replacements release unused dynamic entries, retaining any
source referenced by another binding or a retained voice snapshot. Live source
addresses never move. Candidates require a spare entry until installation succeeds;
a cache whose entries are all retained can still report capacity exhaustion.
Read/format/capacity failures never delete saved definitions or the old binding.

### Existing Sample path relinking

The Instrument UI can now relink a saved Sample path using a stopped Teensy
transaction advertised by `BTINS1 RELINKCAP 1`. An explicit old/new-path prompt
precedes staging a different resource. `BTINS1 RELINK 1 request slot 1 resource`
prepares the candidate before replacing the existing Sample definition and binding;
`BTINS1 RELINKED 1 request result count` confirms the outcome without changing count.
Failed candidates preserve the old saved path and playable binding/unavailable
state. Pattern slot references never change as part of relinking. Successful
changes schedule the existing temporary BTM writer. The BTM layout and version
are unchanged: only the resource bytes in that same definition record differ.


### Hierarchical Sample WAV browser (temporary protocol v2)

`BTINS1 WAVBROWSECAP 1` adds `BTSTORE1 WAVOPEN 2 request offset fallback directory`.
The directory is an absolute SD path (root `/`), at most 63 bytes, using existing
resource path characters. Requests have a nonzero monotonically increasing identity
and a FAT cursor aligned to 32 bytes. Only the initial offset zero may request
fallback. Teensy tests the requested directory and readable parents through root;
manual navigation never silently falls back. Browsing creates no files/directories.

A successful page returns `BTSTORE1 WAVDIR 2 request confirmed_directory`,
`BTSTORE1 WAVINFO 2 request fallback_reason` (0 none, 1 preferred directory missing,
2 preferred directory unreadable), up to eight `BTSTORE1 WAVITEM 2 request D name`
or `... F name`, and `BTSTORE1 WAVEND 2 request next_offset done scanned_entries`.
Each page inspects at most 64 physical entries under the existing SD read guard.
Directories and eligible regular WAV filenames are included. The host accumulates
all correlated pages before replacing the visible confirmed directory/list; it
adds `..` below root. Failed navigation preserves that prior list and selection.

`BTSTORE1 WAVERROR 2 request result` distinguishes missing (1), SD read error (2),
not a directory (3), busy (4), and invalid path (5). An empty successful directory
has zero scanned entries; a nonempty directory without eligible WAV files has a
separate UI message. Filename eligibility does not imply supported WAV contents.
Selection joins the confirmed directory and filename into a full SD-relative
resource, enforcing the existing 63-byte limit before staging. Content validation,
replacement confirmation and transactional definition installation are unchanged.
Legacy WAVLIST v1 remains available to old hosts; new hosts require WAVBROWSECAP
for directory navigation. BTM storage and saved path semantics are unchanged.


### Sample Browser metadata listing (temporary protocol v3)

`BTINS1 WAVBROWSECAP 2` advertises `BTSTORE1 WAVOPEN 3` with the same directory,
request identity, FAT cursor and fallback rules as v2. Replies use version 3.
Current BTX keeps Sample Browser startup policy on the host: exact listings of
`/Samples`, then `/BroTracker`, then `/`, advancing only on missing (result 1).
It sends `fallback=0`, accepts empty successful pages, and publishes the initial
successful page directly. Other errors are surfaced; explicit navigation never
uses this startup chain. The legacy firmware parent-fallback flag remains available
for older clients.
Each WAV `WAVITEM` is followed by `BTSTORE1 WAVMETA 3 request page_index status
format channels bits sample_rate`. `status` is the existing `WavBufferStatus`
ordinal from the playback validator; other fields are the actual metadata read
before success/rejection. The host requires one matching metadata record per WAV
before accepting the page. Unsupported and invalid WAVs stay in the list. All
case variants of `.wav` are listed; unrelated files are omitted. Directory entries
and WAV names can contain up to 255 filename bytes. Selecting a supported WAV
still enforces the existing filename and 63-byte resource-path validation.

Current variable-rate firmware fills batches of at most eight entries, scanning
at most 64 directory entries per batch. Directory access and WAV inspection are
cooperative: each foreground turn performs at most one operation/bounded read.
The legacy synchronous adapter still stops after one WAV. BTX publishes each
correlated batch immediately in scan order, without a full-list sort; later batches
append while preserving the cursor and scroll. The footer counts only this
directory's WAV entries (`NN samples read`), including unsupported WAVs, alongside
metadata/errors. Leaving or changing folders invalidates older request replies.
It uses the existing bounded header/chunk validator under the shared SD read lease,
never reading PCM for browser metadata. Highlight changes use the listing's cached
metadata and perform no SD I/O. Missing/read-failed WAVs receive a rejected result;
folder failures retain the previous confirmed directory/list. Only validated
success can stage a selection; definition commit still prepares again. The same
single footer line shows metadata, the approved rejection reason, or an empty/
navigation status. Legacy v1/v2 requests and replies remain supported by firmware;
new metadata-aware hosts require WAVBROWSECAP 2. BTM storage is unchanged.


## Bounded sample paths and BTM version 2

Current path limits, shared resource ownership and the negotiated protocol are
specified in [SAMPLE_PATH_STORAGE.md](SAMPLE_PATH_STORAGE.md). BTPATH1 CAP 1
advertises the 255-byte path limit and 8192-byte string arena. WAVOPEN/WAVDIR and
metadata/listing replies use version 4; GET/STATE facts use version 3;
DEFINE/RELINK/REPLACE/EDIT and audition PLAY use version 2 for encoded path tokens.
Legacy versions retain their original 63-byte, whitespace-free tokens.

The session magic remains `BTM1`; byte 4 is **2** for this legacy version. Header size remains 24,
with embedded Pattern byte count at offset 8, definition count at offset 12,
unique path count at offset 16 and the existing whole-file CRC at offset 20.
The embedded BTSS record follows the header. Each definition uses three bytes:
Type and a little-endian uint16 path-table index (zero for non-Sample, otherwise
one-based). The unique path table follows: little-endian uint16 byte length,
full ASCII SD-relative path bytes, then NUL. Identical paths are written once.
The same eight view/mute/reserved bytes terminate the file. No host resource ID,
address, streaming handle or request identity is persisted. The bounded serializer
capacity is 10279 bytes; the path strings themselves cannot exceed the 8192-byte
arena budget. Restore validates the complete record before changing live state.
Resource pressure returns `resource-capacity` and preserves the current state.

BTM version 1 (fixed 65-byte definition records), BTWS v1/v2 and single BTSS reads
remain supported. Candidate/previous atomic save and recovery rules are unchanged.
There is no separate implemented instrument-file browser/format to migrate; saved
instrument definitions in existing sessions retain their slot numbers and paths.

## BTM version 3 MIDI channels

BTM1 version 3 retains the version 2 header, three-byte definition records,
path table, trailer and checksum. For MIDI records (Type 2), the uint16
previously reserved for a zero path index contains the channel, 1?16.
Sample and Synth records retain their existing interpretation.

BTM1 v1/v2 records contained Type and a path or path index. BTWS/BTSS held
only Pattern/session state and reconstructed development definitions. None
encoded the MIDI output channel, which came from a separate terminal setting.
All of these legacy MIDI definitions migrate to channel 12, the prior built-in
default. Saved custom terminal channels cannot be recovered from those files.
The next normal save writes v3. Future Tune and Project formats must preserve
the same per-instrument channel.

## BTM version 4 working-session lineage

BTM1 v4 keeps the v3 layout and appends a CRC-covered 32-character lowercase
hex lineage after the eight-byte view/mute trailer. Other session versions
remain readable. An empty legacy lineage is established once in RAM, using
the boot entropy when available. A capable BTX can send
`BTSESSION1 CLAIM 1 <lineage>` if the board advertises an empty identity.
Teensy accepts only the first valid proposal for the current working session.
Claims do not trigger SD writes. The normal session writer marks the identity
persisted after promoting the verified candidate to canonical storage.

`BTSESSION1 LINEAGE 1 <persisted> <lineage|->` follows transport STATUS and
HELLO and replies to CLAIM. The flag is 0 until canonical promotion or when
no ID is available. Validated boot recovery restores the saved identity.
Autosave, explicit SAVE and ordinary Pattern Load preserve it. Missing or
rejected session content starts without a saved lineage. No project/tune
lifecycle is implemented. The terminal uses the persisted flag to select
its waveform namespace (see [Instrument Foundation](INSTRUMENT_FOUNDATION.md#persistent-terminal-waveform-cache)).

## BTM versions 5 and 6 Sample tuning

A session containing a Sample instrument uses v5 without lineage or v6 with
lineage. Definition records grow from three to five bytes. The two appended
bytes are the Sample Root Note (raw 24–127) and Fine Tune (0–99 cents). Non-Sample
records have zero in both bytes. The existing path table, trailer, MIDI channel
and CRC rules apply. Versions 1–4 and legacy BTWS/BTSS restore Sample tuning to
C-3 and 00 because those formats lack tuning fields. A zero Root Note byte
in a tuning record resolves to C-3 and retains the stored Fine Tune.

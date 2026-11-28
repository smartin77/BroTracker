# BroTracker File Formats

BroTracker uses three related formats for tunes, projects and project distribution.

The BTT/BTP/BTM sections are a **BTT Foundation documentation draft** refining
[D0032](ARCHITECTURE_DECISIONS.md#d0032---brotracker-file-and-container-formats).
The ownership draft is complete; the BTT v1 proposal below awaits review.
The primary developer firmware has a narrow stopped-only load projection;
see [the production transaction](#stopped-only-developer-btt-load-2026-11-27).
Its layout and limits remain provisional; developer loading is experimental. The isolated
host validator/rewriter subset below is implemented; BTP/ZIP BTM
details remain open. Existing working-session formats below retain their
separately documented behavior.

## Stopped-only developer BTT load (2026-11-27)

`teensy41_dev` enables `BROTRACKER_BTT_STOPPED_LOAD`. This is a developer command
against the provisional schema, with no BTX UI, song sequencing, prefetch, FX
execution or BTT Save. Earlier isolated-foundation descriptions below describe
their original measurement scope; this section defines the added production use.

Send ASCII lines terminated by LF:

```text
BTBTT1 LOAD 303 Tunes/example.btt
BTBTT1 STATUS
BTBTT1 CANCEL
```

The nonzero decimal stable pattern ID is mandatory; the order list never chooses
it implicitly. Paths are SD-root-relative conservative ASCII FAT paths, at most
255 bytes, including internal spaces. No traversal, truncation or host paths.
The production output must already be configured and audio initialization
acknowledged. Confirmed IDLE alone does not bypass pending control/ACK, MIDI,
publication/readback, storage, live-visit or audition exclusion. Playing and Paused
reject without staging or changing transport. LOAD never sends STOP or START.
Other commands cannot mutate the baseline while staging or awaiting its ACK.

`BTBTT1 STATE 1 <phase> <reason> <selected-ID> <BPM-hundredths> <omitted> <retained>`
reports the latest attempt. Phases are 0 Empty, 1 Open, 2 Copy, 3 Seal, 4 Validate,
5 Scan, 6 Prepare, 7 Project, 8 Ready, 9 Submitted, 10 Cleanup, 11 Complete,
12 Failed. Only Complete/`ok` establishes a successful load. Cleanup may remain
pending under quarantine; it never implies that the new tune replaced the old.
`committed-old-backing-cleanup-pending` explicitly means the new tune is already
committed and only retirement of its predecessor remains. `retained=1` can also
describe the old successful tune after a later rejected attempt. CANCEL is
cooperative before submission; a submitted input stays frozen until the audio
ACK and cannot be cancelled. STATUS is available while work is pending.

The selected pattern supports the existing note, instrument-update and NOTE OFF
timing projection, 1..64 rows, 1..8 channels, and BPM 3200..30000. Every nonempty
FX slot in that pattern rejects as `initial-pattern-fx-unsupported`, including
FX-only cells and present zero operands. All three FX slots of all patterns stay
in saved backing. FX in an unselected pattern do not prevent this single-pattern
projection. The player still loops the selected pattern; the saved order is not
executed. First-pass and loop-continuation musical compatibility are preflighted
with the real command preparer before publication.

Supported definitions receive compact runtime slots through explicit stable-ID
maps, independently for active and staged owners. No stable ID is interpreted as
an array index. Sample root and fine defaults are independently 60/C-3 and 0/00
only when absent. Saved unsupported values/encodings, missing paths, unavailable
types and inappropriate parameters remain unchanged in Candidate saved metadata
and the original bytes. Unrepresentable unused definitions are omitted from the
runtime table and counted in STATE; required ones reject publication. Supported
unused Sample definitions remain present but NotPrepared, without PCM acquisition.
Runtime Sample level uses the existing 80 percent preparation convention.
MIDI channels and the existing sine Synth projection use current runtime support.

The full accepted BTT is retained byte-for-byte: tune settings/name/ID, definitions,
order, unselected patterns, all FX and unknown optional sections/TLVs. The active
Candidate pins this backing and required source generations. Saved intent is not
rewritten to match defaults or runtime availability. A rejected request leaves
the original input and the previous working state intact.

### SD snapshot and media policy

This developer command requires sole Teensy ownership of the mounted SD and a
stable card for the complete retained lifetime. No host mass-storage/MTP writer,
second controller, card removal/replacement or out-of-band mutation is supported.
Firmware does not write WAV sources. Prepared Sample generations pin producer
metadata/PCM prefixes, not the rest of a mutable physical WAV; external edits to
WAV files during the retained lifetime violate this explicit policy. No claim of
detecting same-size external edits or arbitrary card replacement is made.

An arbitrary input FsFile is **not** used as an immutable Reader. The coordinator
copies at most 1 MiB in <=512-byte chunks to a new `O_EXCL` registry file in the
reserved `/BroTracker/.btt-load/` namespace. It checks source size/read/write,
source close, snapshot sync/close/reopen and full BTT validation before admission.
The existing registry seals that write-once backing; all mutations in its namespace
must pass through it. No original file is removed, renamed or overwritten.
The namespace uses process-domain/slot/generation names. Leftovers after reset
are never permitted as arbitrary LOAD inputs, including FAT case aliases, and
are not imported, scanned or deleted; collisions fail explicitly. A later explicit
LOAD advances the in-process generation without overwriting the colliding file.

Cancelled/failed snapshots and superseded owned snapshots use checked close and
Discard only after all reader/voice/source owners release them. Failed closes or
removals retain ownership and block reuse until checked cleanup succeeds. An
active snapshot remains on SD after reset; it is not a saved session or recovery
record. An original file can be changed later only under the exclusive media
policy; its sealed snapshot remains the accepted saved input.

### Publication, sessions and verification boundaries

The real foreground coordinator uses the existing Candidate/SdSourceService and
singleton path pool/producer PCM storage. Refills run first. Source preparation
advances at most one owned job action per foreground turn; no blocking prepare
loop exists. Full envelope validation and initial materialization are bounded
stopped foreground operations, not a concurrent-playback timing guarantee.
Live preparation and audition acknowledge only their consumed/discarded legacy
completion. Neither BTT nor a foreign caller acknowledges unfinished work.

The existing pattern publication identity, frozen storage and audio request/ACK
FIFO now carry an optional frozen complete binding table. Transactional player
Configure applies pattern/BPM/bindings at one stopped audio boundary; failure
retains the prior configuration. Existing output gain/mute policy is restored.
Foreground definitions, availability, full-tune ownership and readback synchronize
at the matching ACK before further command/readback consumption. The old owner
stays pinned until that point and until the producer publishes detached voices.

While a BTT is retained, legacy storage/definition/edit commands and autosave are
blocked. The production dispatcher uses a shared, fully parsed command policy;
protocol namespace prefixes are not permission to mutate state. Safe status/fact
queries (including DEVICE, TEMPO, MUTE, VOL, GAIN, MIDI, instrument facts, clock
and streaming diagnostics) remain available during staging and retention. Pattern
and waveform readback/credit operations are available once settled; they are
blocked while staging/submitted/cleanup because an independent transfer can
conflict with admission or frozen publication. Explicit transport commands remain
available after retention, and are blocked while a load is busy.

BTX reconnect also needs operations beyond read-only queries. Once settled,
HELLO performs its existing connection/discovery reset, audio SET confirms the
locked output route and applies the application channel-swap preference, MIDI
delay GET/SET uses the existing stopped/queue/cleanup guards, and clock GET/SET
synchronizes RTC/timezone metadata. These setup operations do not edit tune data
or authorize legacy persistence. HELLO and setup writes are blocked while busy.
A valid session-lineage CLAIM receives `BTSESSION1 LINEAGE 1 0 -` without claiming
an identity or changing saved-session state. Volume/mute/gain SET, tune/instrument/
pattern mutations, audition/visits and all legacy BTSTORE commands (including
readiness/listing) remain blocked. Reconnect never starts playback.

The successful handoff clears the legacy saved lineage (`0 -`) in RAM,
without touching any session/recovery file. Reboot still restores the preexisting
legacy session, not the BTT. UI editing, retained-work recovery, BTT dirty state,
lossless Save and returning to a legacy session without reboot remain future work.

Target memory/map/lifecycle evidence, host fake-SD tests and read-only device
evidence belong to `build/btt-stopped-load/`. The coordinator is one static RAM2
object with two Candidate owners, one validation Document, one decoded Pattern,
one realtime projection and candidate definitions/bindings. It adds no player,
path pool, PCM cache or current/next pattern bank. All large persistent objects
remain off the CPU stack. Compiler stack frames and linker gaps are not measured
runtime heap/stack/IRQ peaks. The original-card preflight was blocked on private
staging and exact state restoration; the dedicated-card verification below
supersedes that blocker for the explicitly isolated test only.

The 2026-11-27 production-load preflight in
`build/btt-production-load-verification/` discovered Teensy serial 17681760 and
captured checksum-verified published/working readbacks (32 rows, 8 channels,
155.00 BPM), six instrument definitions and unchanged final IDLE/status facts.
This is a readback capture, not a restoration backup. No mounted SD workspace
was available. The inspected firmware/tools cannot export and round-trip the
complete current session plus byte-preserved session/recovery files before the
first upload. Existing preview/publication tools can change session autosave
intent; the legacy session Save stores the working pattern and does not preserve
an independently different published pattern. Historical restoration HEX/ELF
artifacts exist, but do not prove restoration of current RAM or unchanged SD.
The run therefore stopped before audio setup, upload, fixture staging or LOAD.
No standalone diagnostic was repeated and no physical production-load success
is claimed. A new staging image alone cannot close this pre-upload restoration
gap; establish the non-destructive state/file round-trip route first.

The same preflight's fresh unoptimized host regression exposed a coordinator
destruction-order defect: Candidate source leases outlived their SdSourceService.
Declaring the service before the candidates fixes reverse-order destruction.
The existing stopped-load suite now passes in Debug as well as Release; this
host lifetime correction does not establish physical load/restoration success.

### Dedicated-card production verification (2026-11-28)

The owner authorized reset and a dedicated new 32 GB card, replacing the exact
original-card volatile/session restoration prerequisite for this isolated run.
The original card was not accessed. After read-only volume/disk identification,
two native fixtures were exclusively staged in `K:\BTT-b0c00c11a2`, reopened,
production-validated and hash-matched to host references. The healthy FAT32 volume
has 31,263,490,048 bytes capacity; Windows exposes its USB reader identity, not a
verified SanDisk CID. Safe flush/lock/dismount/eject passed, then the owner
confirmed powered-off installation in Teensy serial 17681760.

The first real LOAD passed, but a subsequent invalid load exposed real SdFat's
existing-directory behavior: `mkdir` fails if its final component already exists.
The old fake incorrectly returned success. The focused fix creates a missing
namespace, checks that an existing path is a directory, and reuses the persistent
input handle so a failed namespace close remains owned for checked Cleanup.
No preexisting file is removed/overwritten. The stricter fake reproduces the
failure; regressions cover existing directories, foreign-file preservation,
regular-file rejection and close quarantine. All 15 stopped-load cases and 14
affected CTest entries pass; current ordinary `teensy41_dev` builds pass.

On the corrected production image, the retained earlier snapshot caused the
expected `snapshot-create-collision-or-io` refusal before validation (BPM 0).
The next explicit LOAD used a fresh generation and reached matching publication
ACK 1 plus Complete/`ok`. Pattern 503 (not order-first 17) read back as 8 rows,
8 channels, notes 60..67 and BPM 137.50. Saved sine IDs `[701,29]` correspond to
observed runtime slots `[0,1]` under the inspected mapping; direct stable-map
inspection is not exposed by the protocol. Both instrument slots report Ready
Synth, with no Sample dependency. Reconnect/audio setup and an explicit lineage
claim remained IDLE and returned `0 -`; published/working readback stayed exact.
The private FX fixture failed as `initial-pattern-fx-unsupported`; settled Failed
with retained=1 and unchanged six readbacks/instrument facts verifies rejection
and coordinator cleanup, not replacement of the accepted tune.

Reported streamed I/O/stale-read errors and allocation failures were zero; no
sample voices were active. Audio block usage was current 1/peak 3, not a runtime
heap/stack/IRQ watermark. The same corrected normal firmware was finally uploaded
and its build identity, board identity, IDLE and Empty loader verified. No START,
STOP, Save or automatic playback occurred. Test inputs and sealed successful
snapshots remain on the dedicated card as evidence; reset ends volatile ownership
but is not checked in-process retirement or a deletion proof. No uncertain
snapshot cleanup was attempted. Original-card restoration, audible playback,
full runtime memory admission and power-loss recovery are not established.
After serial closure, the owner confirmed powered-off reinstallation of the
original card; the tools issued no further device commands.
Full serial/image/test/cleanup evidence and limits are in
`build/btt-production-load-verification/card-20261128-b0c00c11a2/REPORT.md`.

Source-verified HEAD versus stopped-load mapped `teensy41_dev` (compile/link only):

| Metric | Reviewed baseline | Stopped load |
| --- | ---: | ---: |
| ITCM banks | 8 | 8 |
| RAM1 static | 157,888 | 158,944 |
| RAM1 local/stack linker gap | 104,256 | 103,200 |
| RAM2 static | 166,688 | 202,368 |
| RAM2 heap linker interval | 357,600 | 321,920 |

The complete coordinator is 35,672 bytes (35,680 after RAM2 alignment). Its
simultaneous state plus a 2,048-byte allowance is 37,720 bytes, below the 65,536
compile-time ceiling. With the earlier **unproven** 32 KiB stack/IRQ and 64 KiB
heap allowances, linker remainder is 70,432 and 256,384 bytes respectively.
No extra 64 KiB arena is allocated: the measured coordinator itself owns all its
fixed members. Compiler frames for Begin/Step/Cleanup are 48/160/32 bytes;
the existing Configure and foreground musical preparer still use bounded
temporary metadata. Full call chains/SD/library/IRQ peaks remain unmeasured.

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

#### Bounded FsFile Save backend (2026-11-28)

`tools/btt/fs_save.{h,cpp}` implements the native, synchronous storage portion
of Save. It is compiled/linked for Teensy and tested through the actual adapter
against fake FsFile faults; it has no serial command, screen or production Save
integration. `FsSave::Run` requires the complete accepted `Document`, its matching
live sealed `FsBackings` token, a destination and disposable caller-owned
`Document`/`Workspace` scratch. It pins the immutable source for the operation.
It never serializes the runtime pattern/instrument projection, changes tune
identity, or settles the open Save As/copy identity policy.

The adapter reuses `Save`/`Rewrite`, including source-fingerprint revalidation,
absent tuning-default insertion and exact intended-output comparison. It fully
validates an existing canonical first, exclusively creates `<destination>.candidate`,
checks positional writes, sync and close, then reopens, validates and compares.
The verified read handle must close successfully before canonical -> `.previous`
and candidate -> canonical promotion. A failed promotion attempts rollback;
failed rollback retains exact old bytes in `.previous` and verified new bytes
in `.candidate` for explicit recovery. Successful promotion removes only its own
backup; failed removal reports `BackupRetained`. Existing candidate/previous
paths block without deletion. Outcomes distinguish `Saved`, `BackupRetained`,
`PromotionFailed`, `RollbackFailed`, `CleanupRequired`, collision, busy and other
failure; `detail()` retains the foundation result. `promoted()` and artifact
flags distinguish committed data from failed work, independently of cleanup.

`Cleanup` retries checked close/removal or rollback of owned artifacts; it never
deletes the sole previous valid file. Pending resources block reuse. The owner
must retain this state and registry until `CanDestroy()` succeeds. Scratch pins
are released before an admitted Run returns; they are not a new active document.
There is no destructor-driven filesystem cleanup: the caller must meet that
lifetime rule before FsFile's own library destructor can run. Failed exclusive
open grants no deletion rights over an existing file. Ordinary failed close and
SdFat's wrapper-open/inner-closed failure shape are both exercised.

Destination paths use the conservative FAT policy from `ValidSamplePath` and
are limited to 245 bytes so both sibling suffixes remain within 255. Traversal,
leading slash, repeated separators, trailing space/dot and short-name `~` aliases
are rejected. Case-insensitive component-prefix checks exclude `.btt-prototype`,
`.btt-load` and the configured backing namespace. Because the only admitted
sources belong to that registry, destination/source aliasing is excluded without
another pathname pool. The sibling names share the same validated parent.

The caller must establish and maintain confirmed stopped/settled transport,
no pending publication, audition or competing storage/path mutations throughout
Run and Cleanup. The readiness boolean attests to this precondition; it does
not implement transport checks. Each SD action uses existing `SdAccess`
exclusion, including source reads; an outer write lease must not enclose Run.
No SD operations are allowed in audio callbacks or interrupts. FAT power-loss
atomicity and runtime stack/heap/IRQ admission are not established here.

Evidence: `build/btt-save-backend/REPORT.md`, Debug/Release logs, exact fault
outcomes, actual ARM compile/link maps/layouts, stack frames and allocation audit.
ARM state is 912 bytes; a separate comparison Document (6,192) and Workspace
(1,136) bring isolated aligned RAM2 reservation to 8,256 bytes. Existing source
Document/registry/pins are reused; no decoded pattern, path pool, PCM cache,
player or whole-file buffer is added. This unintegrated backend adds no normal
production reservation. Explicitly rooted diagnostic code adds 976 ITCM bytes
without changing the eight-bank allocation. Repeated saves count zero adapter
C++ allocations; fake STL allocations are recorded separately. These static
figures are not observed hardware stack high-water or timing measurements.

Next: wire this adapter into the retained full-document owner behind the existing
stopped/settled storage gates, provide explicit recovery reporting/lifetime and
completion handling, and only then add the separately scoped production Save
entry point. Preserve saved tune identity until product identity policy is decided.

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

#### Host-only envelope/schema foundation

`tools/btt/foundation.h` and `.cpp` implement an isolated native C++17 saved
document validator, direct pattern materializer and lossless streamed rewriter.
Only isolated BTT host tests and compile/link diagnostics link these sources. This does not enable
application/firmware storage, UI, transport, playback prefetch or FX execution.
The format, codec numbers, profile limits and device admission remain provisional.
Fixed16 is the baseline; the existing experimental Fixed8/Sparse8/Sparse16
layouts also have structural coverage. No compression or encoding selection is
performed, and rewrites retain each pattern's original encoding.

Implemented acceptance checks cover the header, exact contiguous directory,
both CRC levels, IDs/counters, required sections/fields, all TLV framing and
duplicate tags (including unknown tags), known encoding lengths, UTF-8 without
NUL, order references, note/timing/FX structure and sparse-record rules.
Every pattern definition is scanned sequentially through reusable scratch;
acceptance retains no decoded pattern bank or complete tune/file buffer.
Definitions occur once, while repeated order entries stay stable-ID references.
Unresolved nonzero references below their namespace's next-ID counter are valid
saved data. Zero order IDs and references at/above the counter are invalid;
instrument ID zero remains no update. Unknown required data rejects acceptance
as `Unsupported`; malformed structure, capacity and I/O failures have separate
results. A later minor version survives unchanged when required schema is known.

`Status` separately reports unsupported saved interpretation, unresolved pattern
and instrument IDs, and unchecked resources. No resource is resolved through the
host filesystem, and unchecked does not mean located or playable. Missing Sample
bindings, SD/BTP context, resource validation and effective runtime defaults need
a future resource layer. Unknown FX numbers are retained in all three slots,
including FX-only cells and explicit zero operands; no FX command registry or
execution meaning is assigned here. Unsupported BPM/tuning/type values and
inappropriate parameters remain saved data. Known optional tags with an unknown
encoding remain opaque and present, so saving does not overwrite them with a
default. Valid UTF-8 paths, including invalid runtime paths/bases, remain raw.

The `Reader` contract requires owned, immutable, seekable backing for its entire
lifetime. `Document` retains a `ReaderOwner` pin (fixed prototype control or compatibility
host shared pointer), never a naked filename or borrowed byte array. Implementations must guarantee immutability;
`const` alone cannot freeze external storage. Registry-owned FsFile backing and
the stopped loader are described below; the separate Save adapter above remains
unintegrated. The host test reader owns its file handle and denies write
sharing on Windows; its POSIX test fallback assumes exclusive test ownership.
Adapters must give all wrappers of the same backing the same identity, and the
rewriter rejects a writer with the source identity before writing. It checks the
source whole CRC against its saved acceptance fingerprint again (including
saved size), so substituting another valid file also blocks saving. Recoverable bytes are
never dropped. Source and candidate storage must remain distinct through save.

`Validate`, `Materialize` and `Save` take caller-owned candidate state. On failure
the candidate may be partially filled and must be discarded; acceptance/ready
flags stay false. Callers must never pass active state as a candidate. `Rewrite`
may leave partial temporary bytes, which are disposable. `Save` requires an
abstract store to finish/sync/close, reopen with owned immutable backing, fully
validate, compare exact intended rewritten bytes, then promote. The original
document/backing remains available. The store's promotion contract retains the
previous valid destination on failure. The original fake-store tests establish
abstract failure ordering. The bounded FsFile Save backend above now supplies
exclusive creation, SD leases and checked `.previous`/rollback with actual-adapter
fault tests and target links. Physical FAT/power-loss recovery remains unvalidated.

No-edit rewrites are byte-identical when Sample tuning fields are explicit,
including explicit defaults and unsupported values. A BPM edit changes only
that field and dependent CRCs. Each absent Sample Root Note/Fine Tune field is
independently appended as uint16 60 (C-3)/0 (00); present fields are never
substituted. Default insertion can change section lengths/offsets and bytes and
is rejected if it exceeds section/file/TLV capacity. Unknown optional sections
and complete TLVs are copied through a 512-byte buffer from the stable source.

Measured GNU 16.1.0 Release Windows x64 ABI: `Document` is 6,208 bytes (fixed
256-entry index plus backing owner/state), `Workspace` 1,136 bytes (512-byte block,
512-byte duplicate-tag array, eight known spans and counters), and one decoded
`Pattern` **7,684 bytes**, or **15,368 bytes for two buffers**. Naturally aligned
structure-of-arrays storage holds 7,680 logical cell bytes plus four bytes for
rows, channels and readiness. The temporary `Cell` remains 20 bytes; its padding
is never retained per cell. Compile-time size and alignment checks guard the
compact buffer budget, and tests cover all fields/defaults and reused sparse
candidates. No packed structs, unaligned accesses or on-disk changes are used.
These are fixed capacities, independent of file size within the provisional limits. Validation
and rewriting need 7,344 bytes of named caller state and no decoded pattern;
materialization adds one pattern for 15,028 bytes. Save adds a second candidate
index/state and a 512-byte comparison array: 14,064 bytes of named buffers/state.
Those totals exclude scalar locals, compiler call frames, backing control blocks
and CRT FILE state, and are not process RSS or stack high-water measurements.
No recursion or variable-size stack arrays are used. The saved GCC
`foundation-audit.su` reports per-function stack frames, including optimized
clones; it is not a complete call-chain/virtual-backend stack bound.

The 2/32-pattern by 8/65,536-byte optional-section matrix spans files from 15,781
to 313,269 bytes without changing resident types. Each 64x8 Fixed16 direct access
uses **36 backing reads** with **515 logical cursor/TLV accesses**, independent
of unrelated pattern/optional payload counts. Reads are capped at 512 bytes and
the test confines direct access to the selected pattern span. Logical access
counts mean TLV-header scans plus cursor requests (cells, sparse indices and
UTF-8 bytes); scalar/random reads are counted as backing calls, not cursor calls.
Whole validation uses 138..1,956 backing calls; rewriting uses 158..2,560 in this
matrix. Increased bytes cost I/O and CRC work, not proportional resident data.
Validate/materialize/rewrite have **zero counted C++ new/new[] allocations**.
File setup, fixture generation, reports and shared ownership creation are outside
those scopes. Successful fake Save includes one backend allocation for its
reopened File/control block; its injected wrong-document generator is measured
separately and does allocate fixture vectors. Fixture generation intentionally
uses full envelope vectors; `fixture-memory.csv` records their live capacities
and conservative reallocation-overlap bounds, separate from operation memory.
None of these host numbers establishes Teensy RAM admission or SD deadlines.

Reproduce only the host tests (no delivery/firmware build):

```powershell
cmake -S . -B build/btt-foundation/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DBROTRACKER_BUILD_WINDOWS_TERMINAL=OFF -DBROTRACKER_BUILD_ARKOS_UI=OFF -DBROTRACKER_BUILD_FONT_PREVIEW=OFF
cmake --build build/btt-foundation/cmake --target BroTrackerBttFoundationTests BroTrackerBttPatternLayoutTests
ctest --test-dir build/btt-foundation/cmake -R '^BroTrackerBtt(Foundation|PatternLayout)Tests$' --output-on-failure
```

`build/btt-foundation/` contains generated valid/malformed fixtures, the actual
complete patch, test/build logs, memory/read-call CSVs and stack audit. Coverage
includes non-sequential IDs 17/9001 with repeated order, missing Sample references,
unsupported tuning and optional data; exact rewrites/BPM edits/default insertion;
large opaque sections/TLVs; malformed envelopes/schema/sparse data and capacity
boundaries; every small-fixture backing read/write failure, including failures
during pattern materialization; and fake finish/reopen/validation/comparison/
promotion failures retaining the prior document. Remaining work includes format
review, real immutable storage adapters, device memory/source admission,
resource availability, SD transaction/recovery, runtime projection, prefetch,
FX execution and separately guarded UI/transport integration.

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

#### Teensy 4.1 without PSRAM: compile/link memory assessment

Production baseline: `feature/dev` at `2fd2c413a8207d0f711463086431483590d6d8ce`,
`pio run -e teensy41_dev`, Teensy platform 5.2.0, Teensyduino 1.62,
ARM GCC 15.2.1, Cortex-M7 hard-float, GNU C++17, `-O2`, no exceptions/RTTI.
This records the initial target-memory feasibility assessment, not BTT runtime
integration or final admission. The later backing prototype below supersedes its
owner costs and resolves the four-call compile blocker; production is unchanged.
Reproduction tools live in `tools/teensy_diagnostics/btt_admission/`; maps, ELF
section/symbol reports, compiler probes, stack frames, lifecycle CSV and report
are saved under `build/btt-teensy-admission/`.

| Production linker evidence | Bytes / placement |
| --- | --- |
| RAM1 static `.data` + `.bss` | 79,552 + 78,272 = **157,824**, ending `0x20026880` |
| ITCM code/exidx reported by size tool | **238,940**, eight 32 KiB banks = 262,144 |
| ITCM padding reported | **23,204**; another bank costs DTCM 32,768 |
| DTCM stack top / link-time gap | `0x20040000` / **104,320** bytes above static data |
| RAM2 `.bss.dma` | **166,688**, `0x20200000..0x20228b20` |
| RAM2 heap interval | `0x20228b20..0x20280000`: **357,600** bytes before runtime allocations |
| External RAM section | **0** bytes; no PSRAM budget used |
| Actual flash image / remaining | **331,776 / 7,794,688** bytes from linker symbols |

The installed linker places normal code in ITCM, normal globals and even normal
read-only tables in DTCM, `FLASHMEM` code in flash and `DMAMEM` in RAM2. RAM1 is
shared ITCM/DTCM, not a separate 512 KiB data budget. The size tool omits a
four-byte orphan `.fini` from its code arithmetic: physical ITCM end is 238,944
and padding 23,200; its flash free count is correspondingly four bytes larger
than the linker-symbol result above. The 32 KiB bank assignment is unchanged.
The diagnostic map/stack build has identical ELF section sizes and addresses to
the production baseline. All gaps are linker capacity, not observed free stack
or heap after initialization/playback.

Existing Sample storage is retained: nine 8,192-byte rings (eight channels plus
audition), sixteen 4,096-byte source prefixes and one 4,096-byte staging buffer
total **143,360 RAM2 bytes**. AudioMemory(8) reserves another 2,080 bytes; other
DMA/USB/audio objects/alignment account for the remaining 21,248. Do not borrow
these buffers or change refill priority. RAM1 already includes the 12,160-byte
selected control/player, 4,704-byte streaming metadata with sixteen `FsFile`s,
2,184-byte active instrument definitions, 5,308-byte WorkingSession (including
its own definitions and two legacy patterns), 5,168-byte live-visit rollback
snapshot, 10,509-byte session serialization store, 10,240-byte shared path pool,
3,088-byte preview readback and other publication/active/storage/snapshot copies.
These have different publication, undo, recovery and session lifetimes. This
assessment removes none of them or their static/boot binding tables.

Target compiler layout (not host ABI): `Document` **6,188**, `Workspace` **1,136**,
`Pattern` **7,684**, two patterns **15,368**, temporary `Cell` 20 bytes. Compact,
naturally aligned storage still preserves three FX slots. `shared_ptr` is eight
bytes; `FsFile` 80; existing SDClass/SdFs 1,236/1,228 are already in the baseline.
A hypothetical virtual FsFile backing owner is 92 bytes, its toolchain-specific
`allocate_shared`-style combined control/object 104, and writer owner 88. These
shapes have no adapter implementation. Standalone validator state is 7,324 bytes,
one-pattern materialization state 15,008, and Save's two documents/workspace plus
512-byte comparison array 14,024, before backend/scalar/call-stack costs.

Proposed ownership plan: at most **64 KiB of additional RAM2** for explicit
lifecycle arenas, **128 bytes RAM1** for owner/handoff state. Keep active document,
two clean current/next patterns and a bounded stable-ID map resident; reuse one
workspace for sequential validation/prefetch/rewrite under foreground ownership.
Retain unknown bytes in owned immutable SD backing, not RAM. Keep cold schema/SD
operations candidates for flash placement; do not move audio hot paths. DMA
staging needs 32-byte alignment and reviewed cache maintenance. Initialize
DMAMEM NOLOAD storage explicitly and construct/destruct owners safely.

The separate link-only reservation probe roots the minimally ABI-adapted
foundation against production objects, with that arena and owner reservation.
ITCM grows **8,256 bytes**, retains eight banks and has 14,948 reported padding
(14,944 physical). RAM1 static becomes **157,952**, leaving **104,192** stack gap;
RAM2 static becomes **232,224**, leaving **292,064** heap interval. No BTT adapter
or runtime calls are added; additional adapter/allocator code remains unmeasured.

Lifecycle increments below are above the complete production baseline, not
phases to sum together. They conservatively cover replacing an already loaded
BTT document; the first load without an old BTT owner may need less. Each includes
a separate proposed 2,048-byte arena guard:

| Phase | Increment before guard | With guard | Key concurrent ownership |
| --- | ---: | ---: | --- |
| Stopped validation | 30,968 | **33,016** | Old document/current/next/map/backing; candidate document/backing; workspace and bounded I/O |
| Stopped initial-load projection | 56,182 | **58,230** | Validation owners plus one candidate BTT pattern, one legacy runtime projection, definitions/bindings, candidate path arena/map |
| Playback current/next | 24,676 | **26,724** | One document, two patterns, ID map, backing, workspace and I/O/scheduler allowance |
| Stopped rewrite/reopen verification | 31,056 | **33,104** | Keep active owners; one candidate index, workspace, output/reopened backing; no candidate definitions or decoded bank |

Thus the proposed 64 KiB arena has **7,306 bytes** beyond its largest guarded
phase. Only one workspace is concurrent; validation and materialization reuse
it. Stopped load conservatively retains both old buffers and allocates a third
candidate; it does not assume the old next pattern is clean/evictable. Save scans
candidate definitions without materializing a pattern. Its 512-byte comparison
array remains within the stack reserve, not counted again as arena storage.
Existing session serialization and BTT work cannot run simultaneously if a later
implementation overlays scratch, but this budget credits no existing scratch
reclamation, including `g_storage_pattern` or the legacy session byte store.

Candidate projection reserves target-sized definitions 2,184, bindings 2,776,
path-pool shape 10,240 and two 99-entry eight-byte ID maps (active/candidate).
The 1,024-byte I/O staging, 64-byte store/scheduler and 2,048-byte guard are
proposed allowances, not measured adapter requirements. Known type sizes are
measured; instance counts/placement/lifetime reuse are a derived design scenario.
Today's singleton path pool has only 8,192 string bytes/256 entries, shared by
active, session, rollback and streaming owners. Copies retain paths; they do not
duplicate strings. A second pool cannot be used by current two-byte path refs
without an explicit context/commit design. Even 99 distinct 255-byte paths exceed
one pool; preserve raw saved paths in backing and reject binding admission rather
than truncate. The sixteen source prefixes/handles are a separate resource
limit: old pinned sources cannot be recycled for a new candidate before commit.
Use free admitted slots or report capacity/unavailability; do not silently add
a duplicate Sample cache or discard rollback resources.

Interface blockers and stack evidence:

- At the initial assessment, four `std::min` calls failed on this ARM ABI because
  unsigned int and uint32_t (unsigned long) differ. These calls are now fixed in
  the actual foundation; the admission tool compiles it directly, without its
  former generated replacement shim. Historical adapted-copy evidence remains
  in the original assessment artifacts.
- `shared_ptr` construction/control blocks allocate even though operation tests
  allocate none after backing setup. With exceptions disabled, ordinary allocator
  exhaustion is not a recoverable candidate-capacity path. Prefer a bounded
  foreground backing-slot owner or explicitly fallible fixed control-block pool;
  count readers, writers, verification and rollback ownership before publishing.
  Virtual I/O itself adds a four-byte vptr per object, not a hidden file buffer;
  it can stay foreground-only. No FILE/test dependencies need reach firmware.
- `FsFile` ownership alone does not freeze SD contents. Use isolated immutable
  snapshots/generation identities and prevent writers from targeting a live
  backing, with per-operation SD leases that do not pin out existing session
  writes. Rewrite source and destination must remain distinct until owners release
  the old backing. Resource paths remain Teensy SD-domain references.
- The target frame probe reports Validate 176, Materialize 40, Rewrite 688, Save
  88 and comparison write 536 bytes. Existing linked RenderBlock is **10,032**,
  initialization 7,224, ReplaceBinding 5,672, StorageCommand 3,800, session decode
  2,696 and ServiceSerial 1,552. Some can nest or be interrupted; these are not
  stack high-water bounds. Prebuilt libc/allocator frames, IRQ nesting and FPU
  exception stacking remain unknown. Do not put documents/patterns on the stack.

Reserve **32 KiB DTCM stack** and **64 KiB RAM2 runtime heap** as unproven policy
allowances. The reservation link then leaves **71,424 RAM1 bytes** and **226,528
RAM2 bytes** unassigned. Allowing one further ITCM bank still leaves **38,656
RAM1 bytes** after that stack allowance. Those are explicit conditional margins,
not measured live headroom. Runtime heap peak/fragmentation, full stack/IRQ bound,
real backing owner and path/source coexistence remain admission blockers. Dirty
pattern retention and future FX runtime state also require separate bounds.

Conclusion: compact BTT storage has substantial static capacity room without
PSRAM, but **reliable device admission is not yet established**. The isolated fixed-capacity FsFile immutable backing/arena prototype described
below implements that assessment's next step. Resource/candidate lifetimes still
need admission review before device integration; later authorized on-target stack/heap high-water diagnostics
are still required to validate the provisional reserves. Compile/link evidence
proves no SD latency/prefetch deadline, FAT/power-loss recovery or physical behavior.

#### Isolated bounded backing/arena prototype

`tools/btt/arena.h`, `backing.h` and `fs_backend.h` implement developer-only
ownership/I/O, with the same registry/arena logic exercised by a fake host disk.
No production Load/Save, session handler, UI or player uses it. The real
foundation now has the four explicit uint32_t `std::min` template arguments and
compiles directly with production Teensy flags, no exceptions/RTTI. The admission
tool no longer generates a replacement source copy.

Concrete policy: exclusive-create **write-once generation files** under
`/BroTracker/.btt-prototype/`, then check sync/close and reopen read-only before
sealing. This is not a complete snapshot/recovery subsystem: no arbitrary path
imports, renames, recovery scanning or automatic deletion of old files. The
caller must initialize SD and provision the private directory; FsBackend defaults
to unavailable. Existing-file collisions fail without touching those bytes.

Three fixed in-place slots cover active backing, candidate input/output and a
spare for verification or failed-close cleanup. Creation is explicitly fallible;
there is no fallback/control-block heap allocation or exhaustion abort.
`ReaderOwner` copies pin an existing control, including Document copies and failed
candidates. Slot identity includes registry domain and generation, not pathname;
both counters stop before reuse on wrap. Old tokens cannot access a reused slot,
even after reconstructing a pool at the same address. Writers are move-only;
sealed files refuse write opens through any prototype wrapper. Rewrite also
rejects alternate wrappers reporting the source identity before writing.

All prototype mutations/deletion go through the registry; raw FsFile backend
methods are private to it. A pinned reader or live writer blocks deletion/reuse.
Last release checks close; failure quarantines the slot and is exposed through
`Inspect`. Explicit `Discard` retries checked close/removal; failure retains the
registered bytes. Slot/arena exhaustion and failed acquisitions leave current
owners and previous valid bytes intact. The caller must discard failed document
candidates explicitly; holding their pins is intentional ownership, not a leak.

FsFile handles remain idle between operations. Every open, bounded seek/read/
write, resize, sync, close and deletion uses the existing SD arbitration, with no
permanent global lock. Transfers are at most 512 bytes; sizes are checked against
1 MiB and reopened size must match tracked size. There are no FILE/test dependencies
in the ARM adapter. **Immutability enforcement covers prototype entry points**;
future production integration must exclude all other writers/deleters/renames
from that private namespace and handle card/media replacement. An open FsFile
alone cannot guarantee immutability. Fake I/O and compile/link checks do not prove
physical SD behavior, latency, cache coherency or durable saves.

The arena has sixteen fixed object/destructor records and 32-byte-aligned,
caller-owned storage. Checked byte/record capacity and alignment precede noexcept
placement construction. Destruction invalidates handles; explicit Reset reclaims
the bump region, without compacting holes. Reset preflights destruction guards
and refuses a pinned/open/quarantined pool before changing any objects. Destroy
documents/pins and writers, resolve uncertain closes, then destroy/reset the pool.
Arena metadata must outlive Object handles; the arena destructor does not silently
invalidate live objects. The diagnostic initializer places metadata in the first
aligned part of RAM2, with documents/workspace/patterns in the remaining bytes.
Only small handles/scalars are on its CPU stack. All lifecycle/refcount operations
are foreground-only; there is no concurrent ISR owner API.

ARM GCC 15.2.1 target sizes: fixed three-slot FsFile pool **672 bytes**, arena
metadata **272**, token **12**, movable output **20**, ReaderOwner **12** and
Document **6,192**. Workspace stays **1,136**, Pattern **7,684**, two patterns
**15,368**. All three FX slots remain naturally aligned; no packed structs or
wire-format changes. The empty compatibility shared-pointer field costs eight
target bytes inside ReaderOwner but never allocates on the bounded path; legacy
host fixtures may still explicitly create shared controls. Target standalone
validation state is 7,328 bytes, one-pattern materialization state 15,012, Save's
two documents/workspace/comparison array 14,032, before pool/arena/scalar frames.
Host sizes are separately recorded (ReaderOwner 24, Document 6,208, arena 544,
fake-backed pool 496); these are not target admission numbers.

Fresh isolated reservation link roots the actual foundation, real FsFile adapter
and arena initializer against production baseline objects, with the same 64 KiB
RAM2 and 128-byte RAM1 reservations. ITCM code/exidx is **255,132** bytes (eight
banks), leaving **7,012** reported padding / **7,008** physical padding. RAM1 static
is **157,952**, stack interval **104,192**; RAM2 static **232,224**, heap interval
**292,064**. External RAM remains zero. No diagnostic function is called by
production. Actual adapter/control code is included, replacing hypothetical owner
shapes; additional resource projection code is still unmeasured.

Updated lifecycle increments keep the entire existing production state resident:

| Phase | With 2,048-byte arena guard |
| --- | ---: |
| Stopped validation with old document/current/next retained | 33,761 |
| Stopped initial-load projection (third candidate pattern, definitions/bindings/path/map reservations) | **58,975** |
| Playback current/next plus bounded foreground workspace/I/O | 27,569 |
| Stopped rewrite/reopen verification | 33,781 |

Pool/arena/backend metadata are included inside the 64 KiB RAM2 reservation;
fixed controls replace per-backing heap control blocks. Largest guarded phase
leaves **6,561 bytes** in that arena. These are distinct phases, not additive;
no baseline Sample/session/publication/rollback storage is reclaimed. Candidate
path-pool and sample-source coexistence are still proposed, not implemented by
this prototype. The unproven 32 KiB stack and 64 KiB runtime-heap allowances leave
71,424 RAM1 / 226,528 RAM2 bytes unassigned; one more ITCM bank reduces RAM1 to
38,656. Current target frames include Validate 176, Materialize 48, Rewrite 688,
Save 96 and comparison write 536 bytes, plus adapter/library calls. This is not
a full stack/IRQ bound or runtime heap measurement. Reliable admission remains open.

Host verification covers arena bytes/record/alignment exhaustion, copy/move/release,
stale generations, source aliases, SD arbitration conflicts, failed closes and
1,186 individual seek/read/write/resize/sync/close/open positions through bounded
Save. Allocation accounting includes arena/control/owner creation and lifecycle,
plus Validate/Materialize/Rewrite/Save: **zero counted C++ heap allocations** on
the bounded path. Fixture generation, fake disk storage, reporting and framework
registration are excluded. Existing BTT layout/schema tests still cover unknown
optional data, all FX slots and independent tuning defaults; bounded-owner tests
also verify byte-identity, BPM edits and tuning values. Fake promotion failures
retain previous valid bytes; this is not FAT/power-loss recovery validation.

Reproduce using `tools/btt/README.md`. Generated tests, allocation CSV, ARM logs,
real-source stack reports, reservation map, lifecycle budget, complete actual
diff and whitespace result are under `build/btt-backing-prototype/`. Remaining
integration blockers: namespace/media policy in production, candidate path-pool/
sample-source coexistence, physical stack/heap peaks, and separately designed
prefetch, FX execution and FAT/power-loss recovery. The candidate resource
contract below completes the next isolated step; production adoption is separate.

#### Isolated candidate resource admission

`tools/btt/resource_admission.{h,cpp}` implements a bounded developer-only
preparation/rollback boundary. It uses the **real singleton ResourcePathPool and
ResourcePathRef**; a second pool cannot transparently replace the context-free
two-byte IDs. Active, rollback, session, voice/source and candidate owners share
the existing 8,192 string bytes and 256 records. Only additional references to
required runtime paths are acquired; saved paths remain spans in immutable BTT
backing. No production paths, streaming buffers, bindings or publication APIs
are changed, and there is no second ring/prefix cache.

`InspectInstrument` exposes a 32-byte saved view after section CRC/schema checks,
using reusable Workspace scratch. It retains stable uint32 instrument ID, section
and resource spans, original uint16 Root/Fine/MIDI values and separate presence/
understood flags. Opaque encodings, names/descriptions and unknown TLVs remain
recoverable from the complete section in the retained backing. Candidate owns a
backing pin independently of the original Document. Cancelling/failed runtime
admission retains saved metadata and original bytes; Discard releases that saved
owner. Revalidating its backing recreates a Document for lossless rewriting.

All definitions are scanned sequentially without acquiring paths or sources.
Preparation is limited to one explicitly named initial pattern: all its nonzero
instrument updates, plus explicit per-channel incoming instruments. A pitched
note with no known instrument, or a reference to an absent ID, blocks readiness
without positional rebinding. Later patterns and prefetch remain separate.
Even a valid initial pattern can exceed the producer's available source slots;
that returns capacity rather than loading a bank or evicting old resources.

Absent Sample tuning defaults independently to Root 60/C-3 and Fine 0/00.
Present unsupported values remain saved and visible (including 0 and uint16
values beyond runtime range); they disable affected required bindings. MIDI has
effective default channel 1 only when absent. Unknown types, inappropriate
parameters and unknown optional encodings are distinct unsupported settings.
An unknown-encoding field must be displayed from its opaque saved bytes, not as
the uninitialized numeric interpretation. Unsupported unused definitions remain
stored and do not trigger eager Sample preparation.

Path status distinguishes absent reference, invalid runtime syntax, excessive
runtime length, unavailable base/context, unsupported encoding and temporary
pool capacity. Only base-0 SD-root references are admitted; base 1 awaits an
explicit project context. No host filesystem lookup, normalization, path
truncation or saved-value substitution occurs. A well-formed UTF-8 saved path
can remain unsupported by the runtime ASCII path validator. Missing Sample,
temporary source capacity, path capacity, unsupported source format and I/O have
separate source statuses. Saved structural validity is independent of these.

Each Step acquires at most one path or one source lease, permitting cancellation
after every acquisition. Any preparation failure releases only that candidate's
runtime acquisitions. Sharing compatible resources increments real ownership;
no active/rollback/voice source or path is evicted. The external SourceService
contract pins an immutable asset/format generation and owns its cache-key path.
Root/Fine Tune remain per-definition parameters, so identical PCM paths with
different tuning share one source. A changed asset/format cannot reuse a still-
pinned incompatible slot. The host fake implements this contract with metadata
only; it neither opens physical WAVs nor allocates PCM storage. One Acquire call
does not establish a physical SD scheduling quantum.

`StoppedExchange` simulates explicit old/current/candidate ownership: Commit
requires the caller's simulated stopped assertion and resource-ready candidate;
Rollback swaps current/previous; ReleasePrevious drops only that previous owner.
SourceLease clones represent sounding voices and survive old-owner release.
Linked states refuse Cancel/Begin and arena destruction until released/detached.
Owners/backend contexts must outlive their pins and exchange. No actual audio
handoff, STOP acknowledgement, MIDI action or production publication occurs.
Resource-ready is **not playback admission**: saved unsupported FX/order data,
runtime interpretation, transport and audio resource readiness remain separate.

Production `SdSampleStreaming` now has an independent generation-checked
candidate-pin API alongside its unchanged rollback mask, bindings and published
pattern/audition voice protection. `PreparedGeneration(path)` snapshots identity;
`AcquireCandidatePin`, `RetainCandidatePin`, `ReleaseCandidatePin` and
`AccessCandidatePin` check source generation and ownership. `PreparationCapacity`
reports full/exhausted new-source capacity without eviction. Reset refuses live
pins and retains the nonwrapping source sequence across reinitialization.
See [the exact API and reclaim/reset contract](SD_SAMPLE_STREAMING.md#candidate-source-pins-2026-11-27).
Real fake-SD tests cover all reclaim/discard paths, failed/cancelled preparation,
multiple owners, stale reuse, reference/source capacity and ownership cycles;
diff, results and actual firmware RAM delta are in `build/btt-source-pins/`.
The developer-only `tools/btt/sd_source_service.{h,cpp}` now maps SourceLease to
these pins and integrates Pending/Ready admission with a checked owned job.
Candidate Step polls without advancing I/O or losing earlier leases/paths;
adapter Service is the sole job driver after active refills. Job tickets prevent
foreign/stale step/cancel/result consumption, and completion acquires a pin before
returning to reclamation callers. Cancel returns Busy until checked cleanup, Io
for quarantine, and Ok after acknowledgement; CanDestroy refuses outstanding
cleanup. Foreign legacy results require explicit `AcknowledgePreparation()` by
their caller before an owned job can start. Production handlers are unchanged.
See [the complete adapter lifecycle](SD_SAMPLE_STREAMING.md#developer-btt-sourceservice-adapter-2026-11-27).
Pins preserve prepared identity/format/path/PCM state, not immutable physical SD
contents or playback admission. Root/Fine Tune remain per-instrument parameters.

Measured ARM GCC 15.2.1 sizes: saved view **32 bytes**, Definition **60**,
Candidate **5,976**, two candidates **11,952**, SourceLease **12**, SourceToken
**8**, StoppedExchange **8**, path scratch **256**. A candidate includes all 99
saved views, effective parameters, path refs and source leases; no additional
definitions/bindings/ID map or second path arena is needed for this prototype.
Large documents/candidates/pattern scratch are arena-owned, off the CPU stack.
The actual ARM adapter is **28 bytes**, replacing the old unknown allowance.
Target Begin/Step frames are 88/40 bytes before callees; frame sizes are not a
complete stack/IRQ bound. The path singleton's 10,240 bytes already belong to
the production baseline and are not added again as candidate storage.

The revised lifecycle model retains the full existing production state, two
current/next patterns and old/candidate owners during stopped preparation:

| Phase | Additional RAM2 state including 2,048-byte guard |
| --- | ---: |
| Stopped saved validation with active resource owner | 38,973 |
| Stopped initial candidate preparation/projection | **54,427** |
| Current/next playback with retained resource owner | 32,781 |
| Stopped rewrite/verification with active resource owner | 38,993 |

The largest phase leaves **11,109 bytes** within the proposed 64 KiB arena.
These are alternative phases, not additive. Measured Candidate structures replace
the prior hypothetical duplicate path pool/definitions/bindings/maps. The model
still reserves 1,538 bytes for a future legacy runtime projection; the former
1,024-byte facade allowance is replaced by the measured 28-byte adapter.
Producer job metadata is already in the production baseline, not counted twice.
The earlier synchronous fake-source experiment measured 296 bytes of host source
metadata with no PCM. Its historical simultaneous arena use was **36,340 bytes**,
including two Documents, two
Candidates, one pattern scratch, Workspace, path scratch and fixed fake-backed
owner pool; the 544-byte host arena metadata and source context are outside that
region. The complete host harness context is another 992 bytes (arena metadata,
fake source metadata, handles/tokens), for 37,332 bytes of named simultaneous
live state plus caller frames; the backing arena reserves 65,536 bytes regardless
of occupancy. That historical host Candidate was 7,176 bytes / two 14,352.
Current host Candidate is 7,184 bytes and the real adapter is 40 bytes; these are
not target sizes. The measured fake-source harness remains a separate experiment
from the real-producer fixture and its shared PCM storage.

Actual source and lifecycle probe compile with production Cortex-M7 hard-float
GNU C++17 flags, exceptions/RTTI disabled, outside production targets. Before the
cold placement below, the initial adapter reservation link used **nine ITCM banks** with
30,628 reported padding bytes (30,624 physical). This additional bank costs
32,768 DTCM bytes. Isolated RAM1 static/gap are 158,016/71,360; RAM2 static/heap
interval 232,224/292,064. Production stays at eight banks: RAM1 static/gap
157,888/104,256, a +32-byte static delta from the fresh pre-adapter build;
RAM2 static remains 166,688. Producer sizeof is 4,760 (+24 bytes before alignment).
Existing Sample storage remains 143,360 bytes. The prior unproven stack/heap
allowances leave 38,592 RAM1 and 226,528 RAM2; one further code bank leaves 5,824
RAM1 after the 32 KiB stack allowance. No reliable physical admission is claimed.

Host checks cover shared/distinct paths and scan lifetime, real path record/byte
exhaustion and fragmentation/reuse, pinned old source capacity, missing/invalid
references, explicit unsupported tuning, independent defaults, compatible-source
sharing across tuning and incompatible generations, failure/cancellation after
each acquisition, simulated commit/rollback and old/voice release, 100 repeated
cycles, saved-backing recovery and lossless save after runtime failure. A 99-
definition fixture prepares only the explicit initial requirement. Measured
setup, acquisitions and destructor teardown have **zero counted C++ heap
allocations**; fixture generation, fake disk bytes, reports and framework setup
are excluded. Existing BTT and real-path/streaming regression suites also run.

See `tools/btt/RESOURCE_ADMISSION.md` for contracts/commands. Complete diff,
tests, allocation/memory evidence, target compile/frame/link logs, map and report
from the synchronous fake-source experiment are under `build/btt-resource-admission/`.
Real producer adapter tests, counted allocation scopes (zero C++ allocations,
excluding fake-SD container/string work), ARM compile/frame/link and updated
lifecycle evidence are under `build/btt-source-adapter/`. Tests cover Pending
retention, tuning/source sharing, foreign jobs, cancelled/stale completions,
quarantine retry, capacity, refill priority and repeated ownership cycles.
Physical stack/heap peaks, namespace/media enforcement, production scheduler/
publication and Tune Load/Save integration, prefetch, FX execution and
FAT/power-loss recovery remain separate integration blockers.

#### Cold BTT code placement (2026-11-27)

`tools/btt/code_placement.h` supplies `BTT_FLASHMEM(name)`: on Teensy 4 it uses
the core's `.flashmem*` XIP-flash convention with `noinline`, and on hosts it
expands to nothing. Stable per-operation section names keep template COMDAT
and concrete functions in compatible sections. Ordinary firmware does not link
developer BTT code merely to measure it.

The selected infrequent foreground operations are Validate, InspectInstrument,
Rewrite and Save; rewrite-only Sink/Copy/Emit/Wire/CompareWriter::write helpers;
and Backings Create/Discard/Write/Resize/Seal with its FsBackend Resize helper.
The final map places their emitted bodies, including the FsBackend Resize
`.isra.0` clone, in flash at `0x6000...`. `noinline` also removes duplicated
mutation/rewrite bodies from ITCM wrappers without removing any retained function.
Output write/resize/Finish wrappers remain in ITCM and call the flash bodies.

Shared Read/Scalar/Utf8/Fields/Crc/Cells helpers, Find and Materialize stay in
ITCM for foreground pattern access. This intentionally leaves part of the
validation call graph in ITCM; moving an entry point does not move its callees.
Candidate admission, source leases, the asynchronous SdSourceService and its
producer preparation/refill work also retain their placement: they may run while
playing. No audio callback, renderer, refill priority or producer code is changed.
New BTT cross-region veneers occupy 32 ITCM bytes and 64 flash bytes, already
included in the measured totals.

Before/after isolated links retain the same **74 unique roots** (77 root flags
including duplicates), identical production objects/libraries and flags, the
128-byte RAM1 owner and 65,536-byte RAM2 arena. Every retained root is present in
both final ELFs. Layouts and lifecycle phase sizes are unchanged.

| Isolated full-BTT metric | Before | After cold placement |
| --- | ---: | ---: |
| ITCM text plus exception index | 264,284 | 255,996 |
| ITCM banks | 9 | **8** |
| Flash `.text.code` | 3,336 | 10,680 |
| RAM1 static | 158,016 | 158,016 |
| RAM1 local/stack link gap | 71,360 | **104,128** |
| RAM2 static including arena | 232,224 | 232,224 |
| RAM2 heap interval | 292,064 | 292,064 |

Net ITCM reduction is **8,288 bytes**, restoring **32,768 DTCM bytes** by returning
one bank. The eight-bank link has 6,148 reported padding bytes (6,144 physical).
After the still-unproven 32 KiB stack and 64 KiB heap allowances, conditional
margins are 71,360 RAM1 and 226,528 RAM2 bytes; another ITCM bank would reduce that
RAM1 margin to 38,592. Peak proposed arena phase remains 54,427 bytes including
its guard, leaving 11,109 of the 65,536-byte arena. EXTMEM remains zero.

Production `teensy41_dev` remains separate: eight banks, RAM1 static 157,888,
local/stack link gap 104,256, RAM2 static 166,688 and heap interval 357,600.
Its linked code/data section bytes and hot-function placement are unchanged.
Seven focused BTT/streaming/control suites pass; host placement expands to
nothing. Complete diff, tests, before/after maps, disassembly, section/symbol
comparisons, shared-input hashes and memory reports are under
`build/btt-code-placement/`.

Flash placement does not establish playback timing or final device admission.
Physical instruction-cache/SD effects, complete stack/IRQ and heap peaks,
source-change enforcement and production publication/Tune Load/Save integration
remain unmeasured or separate work. Future runtime pattern prefetch must review
the shared call graph and cache costs before extending these annotations.

#### Opt-in physical BTT diagnostic (first failure corrected; stopped-only verification passed)

The separate opt-in [physical BTT/SD diagnostic](../tools/btt/DEVICE_DIAGNOSTIC.md)
is prepared as `teensy41_btt_diagnostic`. The first physical run on 2026-11-27
passed SD init, WAV write/reopen/compare, source BTT validation and stable pattern
access, then failed no-edit rewrite/reopen validation (code 4). Explicit cleanup
later failed at the old sticky-error check (309), with zero path/backing refs.
Those aggregate codes do not identify the failed I/O call or prove a handle leak.
Logs are `device-monitor-261127-212130.log` and `device-monitor-261127-212741.log`
under `logs/`, uploaded source SHA
`8707d1b2e1ce7fb2b9b82ad1a2d7b7269bcfd3a2dfe96a067ac6d940adc238e6`.
**The corrected physical run passed both cases, 16 async cycles and final suite
cleanup on 2026-11-27, using fresh run ID `iofix-261127-b-8d351f`. Normal
firmware was restored and its serial build identity verified.**
It waits for `BTTDIAG RUN` before SD initialization/tests/writes and exclusively
creates retained private artifacts. Native-C++ fixtures cover two stable pattern
IDs, repeated order references, three FX slots, unknown optional data, explicit
unsupported tuning and independent absent defaults. The actual foundation,
FsFile Backings/arena and producer/source adapter perform rewrite, checked
sync/close/reopen/validation/byte comparison and asynchronous prepare/cancel/
share/release cycles. No tune publication, FX execution or audible playback is
included. Host fixture and full entry-point fake-SD regressions are separate
from the physical failure and successful corrected stopped-only run. Original preparation logs,
firmware identity, maps and
memory comparison are saved under `build/btt-device-diagnostic/`.

The diagnostic is a standalone USB-serial image with the same 64 KiB arena and
producer PCM Storage, not the full production application's concurrent state.
It reports operation durations, actual arena occupancy, known ownership counts
and current allocator/MSP boundary observations. Neither snapshots nor linker
gaps are peak runtime headroom. Live stack is never painted. Production Tune
Load/Save remains unchanged; the owner guide covers collisions, evidence capture,
cleanup failures and restoration of `teensy41_dev`.

The corrected FsBackend fills gaps and growth by bounded writes at logical EOF:
both installed SdFat FAT/exFAT seek implementations reject beyond-EOF seeks,
including the first Rewrite directory write at offset 32 on an empty output.
The corrected fake reproduces the old code-4 failure. Cleanup now inspects
current refs/writer/wrapper/quarantine independently of sticky Inspect() history;
genuine quarantine still blocks destruction, and first failure/per-slot history
survives cleanup. A fresh `BTT_DIAG_RUN_ID` selects a private child namespace for
each later build, without deleting, overwriting or renaming old evidence.
See `build/btt-device-io-fix/` for findings and verification. This is a demonstrated
software contract defect, not an observed identification of the physical I/O call.

Compile/link comparison: the corrected standalone image uses four ITCM banks, 24,960
RAM1 static bytes and 221,312 RAM2 static bytes, including the 65,536-byte arena
and 143,360-byte PCM Storage. Normal `teensy41_dev` remains at eight banks,
157,888 RAM1 static bytes and 166,688 RAM2 static bytes. Its ITCM bytes match the
pre-change image. The physical run observed arena peak 37,372 bytes and zero
post-cleanup owners; allocator/MSP snapshots do not measure runtime peaks. The
diagnostic's larger linker DTCM gap is not a measured stack reserve or evidence
that all production and candidate state fits together at runtime.

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

The host-only envelope/schema validator and lossless rewriter are complete for
the provisional subset above, including explicit-default identity roundtrips,
BPM edits, independent absent-default insertion, unknown/unresolved preservation,
corruption/capacity rejection and fake-store failures. This is not real SD save
recovery. Codec/ID-width choices and profile limits still await review. The bounded backing/arena prototype below completes the assessed next step.
Before production integration, resolve candidate path/source coexistence and
measure physical stack/heap peaks; UI, playback and recovery remain separate.

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

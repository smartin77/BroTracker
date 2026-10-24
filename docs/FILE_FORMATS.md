# BroTracker File Formats

BroTracker uses three related formats for tunes, projects and project distribution.

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

BTT is a lightweight BroTracker tune/module representation.

A BTT contains the tune and instrument information but may reference sample data stored externally.

Sample references depend on the physical storage location of the referenced samples.

If a referenced sample cannot be found when the BTT is loaded:

- the instrument definition remains available;
- the instrument parameters remain available;
- the missing sample is marked as **not located**;
- the user may be prompted to locate the missing sample.

BTT is therefore useful when sample data is intentionally maintained separately from the tune.

BTT does not need to contain a copy of every referenced sample.

## BTP — BroTracker Project

BTP is the native BroTracker project format.

A BTP project is stored as a directory containing the tune/project data and all samples used by that project.

A typical project may look like:

```text
MyTune/
    MyTune.btp
    Samples/
        Kick.wav
        Snare.wav
        Bass.wav
```

The project directory is associated with the tune/project name.

When the tune/project is renamed, the associated project directory should be renamed accordingly automatically.

All samples used by the project are copied into the project directory. This makes the project self-contained and removes dependencies on the original locations of imported or user-provided samples.

### Addressable File Structure

BTP shall use an addressable file structure.

The file shall be organized so that the information required for normal operation can be loaded first.

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

BTP may contain descriptive metadata such as:

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

BTM is a single-file container for a complete BTP project.

BTM uses ZIP as its container format.

Conceptually:

```text
MyTune.btm
    |
    +-- ZIP
         |
         +-- MyTune/
              |
              +-- MyTune.btp
              |
              +-- Samples/
                   +-- Kick.wav
                   +-- Snare.wav
                   +-- Bass.wav
```

A BTM therefore contains a complete BTP project.

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
Lightweight tune
External sample references


BTP
Native BroTracker project
Tune + project data + samples


BTM
Portable module container
ZIP containing complete BTP project
```

The BroTracker realtime core does not depend on any of these external container formats directly. File loading and conversion are responsible for producing the internal data representation required by the core.

## Future Specification

The exact binary layout, headers, versioning, offsets, field sizes, checksums and compatibility rules are intentionally not defined yet.

These details shall be specified separately once the internal data model and storage requirements have stabilized.

## Working session: separate from tunes/projects

Explicit Teensy-owned SD SAVE and RESTORE are implemented in the development
firmware. Development-file provisioning/loading and Windows startup readback have
been physically checked; session SAVE/RESTORE physical validation remains pending. See the [follow-ups](TODO_LIST.md#planned-session-storage-and-development-fixtures).
The canonical directory is `/BroTracker`, and the working file is
`/BroTracker/current.btsession`. This is not an explicitly saved BTT tune, BTP project or BTM module;
existing format semantics and their unresolved binary specifications stay unchanged.

The current Publish snapshot is one pattern: active row/channel dimensions, each
active cell's raw note (including empty/OFF) and raw instrument ID (including no
update), per-cell arrival/end-of-position NOTE OFF timing, and published tempo in
integer hundredths of BPM. The current version-2 publication includes tempo;
version-1 pattern-only compatibility is not a reason to omit published tempo from
persistence. Publish freezes coherent data and acknowledges device application.
Protocol transfer/revision IDs and checksums identify/validate transactions, not
additional musical content to restore as live request identities.

Session saving must capture this published baseline coherently, not the host draft,
live working preview or an independently changed live tempo. Mute mask, transport
position/running state, gains, host settings, MIDI channel negotiation/ownership and
other state outside Publish are excluded. Bindings, instrument definitions and
sample PCM/paths are not supplied by the current Publish snapshot. Version 1
references existing instrument IDs and validates against current bindings on
application; serializing pattern bytes alone does not make a session self-contained.

Use a versioned validated session format and safe replacement of the previous valid
copy; the implemented candidate/previous-file sequence and interrupted-write
limitations are described below. SD access obeys D0046 read/write exclusivity, with serialization/I/O
outside audio/timing ISRs and coherent fixed-memory handoff rather than ISR file work.

Three operations remain distinct: current **Restore** returns to the in-memory
published baseline; explicit **session restore** loads the validated working-session
file into a defined device-baseline/host-draft state without starting playback;
**module/tune load/save** is a separate explicit future operation with its own
sample/format contract. Session startup/shutdown INI keys remain planning only;
the explicit host development-file selector below is implemented separately.


### Temporary version-1 pattern files

This is a bounded development/session foundation, **not** a final BTT/BTP/BTM
format. Both kinds store one pattern and references to the existing raw instrument
IDs. They embed no Sample PCM or paths and do not establish instrument availability;
application still uses the current validated binding/reconstruction path. Incompatible
references can therefore reject RESTORE after file validation, without changing the
live pattern/tempo. The format is not a self-contained project.

All multibyte integers are unsigned little-endian; no native struct/padding dump:

| Byte offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | ASCII `BTDP` development or `BTSS` session (distinct, never interchangeable) |
| 4 | 2 | Version, exactly 1 |
| 6 | 2 | Header length, exactly 24 |
| 8 | 1 | Active rows, 1–64 |
| 9 | 1 | Active channels, 1–8 |
| 10 | 2 | Reserved, zero |
| 12 | 4 | Published tempo, 3200–30000 integer hundredths of BPM |
| 16 | 4 | Payload bytes, exactly rows × channels × 3 |
| 20 | 4 | CRC-32/ISO-HDLC (reflected polynomial 0xEDB88320, initial/final XOR 0xFFFFFFFF) |
| 24 | payload | Row-major note/instrument/OFF-timing triples |

CRC covers bytes 0–19 followed by the payload; bytes 20–23 are omitted. It detects
accidental corruption, not malicious modification. Notes are raw 24–127, 254 OFF,
255 empty. Instruments preserve all raw byte values, including 255/no update,
matching current publication validation. OFF timing is 0/arrival or 1/end-of-position
for every active cell, including ignored-but-valid timing on non-OFF cells. Exact
file length is mandatory: no trailing bytes, at most **1560 bytes**. Unsupported
kind/version, reserved bits, dimensions, tempo, invalid notes/timing, length and
CRC are rejected before destination pattern or tempo changes. Inactive cells are
reset to normal empty defaults; they are not file content.

### Explicit device operations and development files

In SD-enabled development firmware, `BTSTORE1 CAP 1` advertises the tooling protocol.
Shared host `[development] startup_pattern` is a **filename**, not a numbered enum.
Accepted names have a 1–24-byte stem of ASCII letters/digits/underscore/hyphen,
optionally followed by the exact lowercase suffix `.btdev` (maximum30 bytes with
suffix). No quotes are supported. Other dots/extensions, whitespace, separators,
non-ASCII, traversal and oversized names are rejected before transmission/SD access;
names are never truncated. Missing/empty config requests the clean default; invalid
config warns and uses that default without sending the invalid name.

Only exact unquoted `none` requests clean empty32×8 applied/published state at12500
through normal publication, with no SD access/write or automatic session restore.
The reserved token is recognized **before** extension normalization. Case is retained;
`NONE` is a filename, not the reserved token (SD lookup obeys filesystem case rules).

| Config value (unquoted) | Meaning |
| --- | --- |
| `none` | Clean empty32×8/125.00 BPM, no file |
| `patern4` | `/BroTracker/development/patern4.btdev` |
| `patern4.btdev` | Same file; extension is not appended twice |
| `none.btdev` | Load the actual `/BroTracker/development/none.btdev` file |

Every named load uses the same validated loader. File bytes determine dimensions,
raw notes/instruments, complete OFF data and tempo; no name-based content/tempo
inference or seed fallback is allowed. For example numeric `4` would mean `4.btdev`,
not `patern4.btdev`. Known numbered files are provisioning examples only; arbitrary
valid filenames are equally loadable. Normal startup has no descriptive/numeric
alias table and no dependency on provisioning definitions. Both applications submit
resolved config/default initialization; standalone unconfigured serial clients may
omit that application initialization.

Startup waits for final application ACK, authoritative baseline and installed working
recovery before editing/START. Pre-load captures cannot enter preview. Failure or
uncertainty stays blocked, without automatic retry, fallback, reconnect replay or
START. Correct the file/config and restart BTX. Once initial recovery completes,
ordinary reconnect preserves local-draft recovery without reinitializing the device.
No automatic seed, session restore/shutdown save or new storage UI is implemented.
Firmware's in-code boot fixture is unchanged; host startup replaces it explicitly.
Files are only created/loaded through the explicit commands below, in main context. Operations
require fully stopped initialized audio, no queued control work/ACKs, pending
publication/preview/readback, and settled MIDI queue/cleanup/USB transfers. Playing,
Paused, core fault or unresolved cleanup returns `busy-or-not-ready`; missing SD
initialization returns `sd-unavailable`. No interrupt exclusion spans SD I/O.

| Explicit command | Effect |
| --- | --- |
| `BTSTORE1 SEED 1 N` | Create paternN.btdev only if absent, N=1..4 |
| `BTSTORE1 MIGRATE 1 N` | Validate/copy legacy file to paternN.btdev, verify before removing legacy source |
| `BTSTORE1 LOAD 1 filename` | Validate filename, append .btdev only if absent, then load within fixed SD directory |
| `BTSTORE1 LOAD 1 none` | Empty 32×8 at 12500; no SD access/write |
| `BTSTORE1 REPLACE 1 3` | Explicit accepted CH5 bass update in existing patern3.btdev |
| `BTSTORE1 SAVE 1` | Save coherent device-owned published pattern + published tempo |
| `BTSTORE1 RESTORE 1` | Load `/BroTracker/current.btsession` through stopped publication |

Numbered files, all under `/BroTracker/development/`, all 32 rows/eight channels:

| Selector / file | Contents | Tempo |
| --- | --- | --- |
| 1 / patern1.btdev | Original CH1/CH2 sine theme; CH3–CH8 empty | 15500 |
| 2 / patern2.btdev | Same theme, CH3 raw60 at rows0/16/28, CH4 raw60 at rows0/12/28; CH5–CH8 empty | 15500 |
| 3 / patern3.btdev | Original theme plus accepted CH5 bass and OFFs, unchanged | 15500 |
| 4 / patern4.btdev | Pattern2 CH1–CH4 plus CH5 bass without any CH5 OFF; CH6–CH8 empty | 15500 |

Pattern2/4 Sample instruments are explicit only at row0: CH3 raw0 (display01),
CH4 raw3 (display04). Later triggers have raw255/no instrument update. Pattern3/4
CH5 notes at zero-based row/raw pitch are 0/36, 6/43, 8/48, 12/43, 16/41, 22/48,
24/41, 28/43; **every note explicitly selects raw1/display02**. Pattern3 has arrival
OFF/raw255 at rows3/7/11/15/19/23/27/31. Pattern4 has NO CH5 OFF events: those rows
and every other non-note CH5 cell are empty. All new fixture OFF metadata defaults
to Arrival, including empty cells. No mute mask, cursor or transport state is seeded.
Provisioning code creates files only on request; normal LOAD reads SD, not seeds.
New projects still default to 125.00 BPM. Existing bindings, gains, notes, timing,
voice policy and mute semantics are unchanged.

SAVE uses only main-owned publication sources synchronized after audio ACK, never
host/unpublished draft, working preview or separately adjusted live tempo. RESTORE
validates the entire candidate, stages it through the existing version-2 publication
validator, and freezes storage through the audio-owner ACK. Only success updates
applied/published pattern+tempo and existing readback sources. It stays stopped and
preserves the current session-only mute mask. Request/revision IDs are newly allocated
from the boot publication namespace, not loaded from disk. Existing in-memory
Restore/F4 remains distinct. No voices, MIDI ownership, PCM, transport position,
channel gains, negotiation or external state are persisted.

Replies are `BTSTORE1 RESULT 1 <operation> <status> <revision>`; SAVE/SEED/REPLACE/MIGRATE use revision
0. LOAD/RESTORE first send `BTSTORE1 SUBMITTED 1 <operation> <revision>`; this is not
success. Final `ok` follows normal publication ACK and readback synchronization;
`application-rejected` leaves baseline/tempo unchanged. Statuses also include
`missing`, `exists`, `invalid`, `unsupported`, `busy`, `io`, `recovery-required` and
`backup-retained`. Some low-level lookup failures cannot distinguish missing from
unreadable; neither changes live state. Disconnect/lost ACK can leave operation
outcome unknown: never automatically replay writes. Reconnect/read back explicitly.
There is no new protocol boot/session identity in this change.

### SD finalization, replacement and recovery limits

Uses the existing initialized SD/SdFat device and D0046 ownership guard. Fixed
`FsFile` objects avoid the allocating `SD.open()` wrapper. A complete exclusive
write lease includes candidate reopen/read verification; no other SD read/write
owner overlaps it. Short reads/writes fail without retry. Only canonical directories
are created if absent, preserving existing `/BroTracker` contents/logs; FAT/exFAT
case lookup follows the filesystem (do not rely on case-distinct directory names).
No Windows drive mount is assumed.

Create a sibling `.candidate` using `O_CREAT|O_EXCL`, check the entire write, `sync()`
and `close()`, then reopen and compare every byte with the validated encoding using
a 64-byte stack chunk. Existing development files return `exists` without writes.
The installed Teensy SdFat FAT and exFAT rename implementations create destinations
with `O_EXCL`; they do not provide overwrite-rename. Session SAVE therefore renames
current to `current.btsession.previous`, then candidate to current. On promotion
failure it attempts rollback; on success it removes previous. `backup-retained`
means the new current was promoted but removing previous failed. Failed write or
verification removes only this operation's candidate where possible; current stays
untouched before replacement. Existing candidate/previous paths block a new save
with `recovery-required` rather than deleting uncertain evidence.

These are checked API outcomes and best-effort rollback, **not atomic replacement,
power-loss safety or a guarantee under filesystem/card corruption**. A failed
rename can itself have partial filesystem effects. On rollback failure the previous
file is retained where the API permits; current may be missing. RESTORE reads only
the canonical file, not an implicit backup. For `recovery-required`, stop use and
inspect/copy the reserved candidate/previous/current files with an offline SD reader
before choosing a valid file or removing leftovers. No automatic recovery browser
or backup selection is implemented. Native fault fixtures model these paths; real
SD failure/power-interruption validation remains pending.

### Explicit MIDI-fixture bass update

`python tools/storage/device_storage.py replace-3 --port <discovered-port>`
issues only `BTSTORE1 REPLACE 1 3`. This deliberately narrow operation requires
an existing valid development file with dimensions 32×8 and tempo 15500; missing,
invalid or incompatible files are rejected. It reads the actual SD file, clears
all CH5 cells/OFF metadata and installs the bass part above, retaining every other
cell, metadata and tempo. It does not regenerate other channels from seed code.
Normal SEED continues to return `exists` without overwriting existing files.

The existing main-context, stopped/settled and shared SD ownership gates apply.
Replacement uses `patern3.btdev.candidate` and `patern3.btdev.previous` with the same
full-write/sync/close/reopen validation, rename/rollback, stale-file rejection and
failure reporting described for session SAVE. Current is not deleted first. No
atomicity or power-loss guarantee is added. Only this fixed MIDI path is supported;
there is no REPLACE 1/2/4/session/arbitrary-path command. The active/published
pattern is unchanged until a separate explicit `load-midi` succeeds.

Historical bass update (2026-10-24, before numbered migration): the accepted
bass definition and existing SD file were updated.
Firmware was built/uploaded after rediscovery, STOP/IDLE and CDC closure. Reviewed
tooling returned `replace-midi ok`; explicit LOAD and independent authoritative
readback checked all 256 cells, every OFF timing value and tempo 15500/15500.
All 224 non-CH5 cells matched a pre-upload SD-loaded snapshot exactly. CH1/CH2
matched the original theme; CH5 had eight notes, eight arrival OFFs and sixteen
empty cells. Final transport was IDLE; CDC released, no START. No operation targeted
theme.btdev, current.btsession, Samples or SD logs. Windows executable-relative
the then-current legacy selector for pattern3 and both host deliveries remained unchanged: this
change affects provisioning firmware/tooling/tests, not host application inputs.
Native tests cover replacement preservation, missing/invalid/busy cases, failed
writes/sync/promotion/rollback and retained normal SEED no-overwrite behavior.
This provisioning run verified readback only; the new bass fixture was not
auditioned during it. The user's separate 2026-10-24 Windows/MIDI-OX/WASP USB
playback and listening acceptance is recorded in [MIDI status](MIDI_FOUNDATION.md#user-confirmed-windows-usb-route-2026-10-24).
Exact offset/jitter/drift, H4MIDI/DIN/handheld routing and interrupted-write physical
checks remain unverified. Private captures are retained under ignored build storage.

### Numbered migration, provisioning and validation

Normal tooling LOAD uses `load --stem filename` with the same bounded validation
as host/firmware (the option accepts either a stem or `.btdev` filename). Numbered
SEED/MIGRATE/REPLACE commands are separate explicit provisioning helpers:

```powershell
python tools/storage/device_storage.py --list
python tools/storage/device_storage.py migrate-1 --port <discovered-port> --audio-output default
python tools/storage/device_storage.py migrate-2 --port <discovered-port>
python tools/storage/device_storage.py migrate-3 --port <discovered-port>
python tools/storage/device_storage.py migrate-4 --port <discovered-port>
python tools/storage/device_storage.py seed-2 --port <discovered-port>
python tools/storage/device_storage.py seed-4 --port <discovered-port>
python tools/storage/device_storage.py load --stem patern2 --port <discovered-port>
python tools/storage/device_storage.py load --stem patern4 --port <discovered-port>
python tools/storage/device_storage.py load --stem none --port <discovered-port>
```

Close BTX/monitors first; operations require stopped/settled state. No automatic
STOP, START or retry is hidden in the tool. Optional route activation is explicit.
Log files are exclusive-created; unknown outcome requires inspection before retry.
Legacy migration is an explicit fixed whitelist: theme.btdev→patern1.btdev,
drums.btdev→patern2.btdev, midi.btdev→patern3.btdev,
midi_hold.btdev→patern4.btdev. These legacy names are not normal selectors/paths.

Migration takes the shared exclusive SD lease, validates the entire bounded source,
requires32×8/15500, writes/syncs/closes a numbered candidate, reopens and verifies
exact bytes, promotes to an absent destination, then verifies destination again
before removing the legacy source. If a destination already exists, source is
removed only after byte-for-byte equality is verified; a different/unreadable
existing destination returns `exists` without overwrite or source removal. Missing
source returns `missing` (not proof of destination contents). Partial-write,
verification, promotion or removal failure preserves source where the API permits
and reports failure/recovery-required; candidate leftovers are not silently erased
on a later attempt. No atomic/power-loss guarantee. Session/Samples/logs are not
targets. Ordinary SEED remains no-overwrite; `exists` is not content verification.

Before firmware upload, preserve available authoritative working/pending pattern
and live/published tempo to local evidence, without Publish/session SAVE. An
unpublished host-only edit not delivered to Teensy cannot be inferred from a device
snapshot; preserve existing BTX logs as separate evidence. Do not copy screenshot
mute/cursor/transport state into a fixture. Build/upload requires explicit authority.
Then migrate existing files, seed only absent new files, LOAD/read back all256 cells,
complete OFF metadata and applied/published tempo. Validate clean `none` at12500,
then configured Windows startup selection4 at15500, stopped with BTX/CDC closed.
Native coverage includes exact variants, migration/no-overwrite/failure paths,
clean baseline publication, startup barrier and ordinary reconnect draft recovery.
Handheld installation and physical failure/power-loss validation remain separate.

### Historical numbered-selector delivery/readback (2026-10-24)

Before upload, the available authoritative device working draft (revision26,
32?8/15500, distinct from published revision2) was captured locally, including all
cells/OFF timing and live/published tempo. The existing BTX log was preserved.
There was no Publish or session SAVE; host-only edits not delivered to the device
are not claimed captured. After user closed BTX, STOP/IDLE and CDC release preceded
upload to the rediscovered Teensy.

Migration1/3 returned `ok`: existing original files preserved byte-for-byte under
numbered names and legacy sources removed only after verified migration. Migration2/4
reported missing sources; SEED2/4 created new numbered files (`ok`, not an existence
assumption). Explicit LOAD/readback of every numbered file verified all256 cells,
complete OFF metadata and applied/published15500. Pattern3's bass/OFFs stayed exact.
Clean `none` was separately verified as empty32?8/12500, with no SD write.

The refreshed deployed Windows BTX was checked first with `none`, then final
`startup_pattern=4`: both application readbacks matched all256 cells and their
tempos12500/15500, with installed working recovery and IDLE. Other executable-
relative Windows settings and AppData config were unchanged. Final BTX closed and
CDC released; no START, note transmission, session/Sample/log writes or handheld
installation. Windows9/9 and Eoan22/22 suites, ABI/checksums/config-preserving
installation checks passed. Physical checks are SD/USB/protocol readback, not an
audition of the new fixtures or a new audible-timing measurement. Earlier manual
Windows MIDI-OX/WASP listening acceptance remains topology-specific and unchanged.

### Historical validation before numbered naming correction (2026-10-24)

The development firmware was uploaded to the rediscovered Teensy after an explicit
STOP/IDLE check and CDC closure. Reviewed tooling created both SD fixture files
(`ok`, not an `exists` assumption), explicitly loaded each file, and read back all
256 active cells with applied/published tempo 15500. Theme CH1/CH2 and empty other
channels matched; MIDI fixture CH5 matched all eight raw-note/instrument-1 entries
and following-row arrival OFFs. No session SAVE/RESTORE was issued during setup;
`current.btsession` was not an operation target. Existing files were not overwritten.

The refreshed `deploy/windows/BroTrackerTerminal.exe` read its executable-relative
legacy `startup_pattern=midi`, sent legacy `BTSTORE1 LOAD 1 midi`, received application success,
and read back the complete matching baseline. It remained IDLE; no START was sent.
BTX closed normally and released CDC. Other runtime settings and the old AppData
config were preserved. Windows/ARM64 build/package, ABI, checksum and installation
results are recorded with delivery evidence; staging ARM64 is not handheld installation.

This is physical USB/SD/protocol readback, not external MIDI delivery, listening or
audible-synchronization validation. Real-card session replacement/rollback,
power-loss behavior, handheld startup selection and other pending manual checks
remain open. The subsequent editor/preview startup barrier correction is covered
by mock Tick/SyncEditor and host package tests, without a repeat physical run.
Private captures/build logs stay in ignored build storage.


### Generic filename LOAD validation

Normal firmware LOAD validates the entire selector before any lookup, builds a
fixed55-byte absolute path buffer and uses the existing bounded CRC-validated file
loader. `none` bypasses SD entirely and still uses the stopped publication path.
The validated reply label fits fixed40-byte storage through the audio-owner ACK;
no heap, ISR filesystem work, new PCM buffer or timing policy is introduced.
Invalid direct protocol/tool/API filenames are rejected, never shortened. Missing,
malformed or incompatible files leave current pattern/tempo unchanged. Final ACK,
authoritative baseline and installed working recovery still gate editing/START;
no autoSTART or ordinary reconnect replay. Existing patern1..4 files are not renamed,
migrated, replaced or reseeded by this correction. Provisioning remains explicit.

Native tests load `ambient_Take` and `none.btdev` with file-supplied16×3 dimensions,
12753 tempo and deferred OFF data; reject invalid/oversized paths before I/O and
exercise all four examples above with real host Tick/SyncEditor. Earlier numbered
migration results are retained as history, not current selector semantics.

On 2026-10-24 the updated development firmware was built/uploaded after STOP and
CDC closure. Explicit LOAD of existing `patern4` and `patern4.btdev` each matched
all256 cells, complete OFF metadata and applied/published15500 by authoritative
readback. The refreshed `deploy/windows/BroTrackerTerminal.exe`, configured with
`startup_pattern=patern4`, completed SD LOAD, baseline and editor/working recovery
with the same contents. No START was sent; BTX closed normally and released CDC.
Only that runtime selector changed; other Windows settings and AppData state were
preserved. No fixture/session writes, renaming or reseeding occurred.

Windows package tests passed9/9; Eoan ARM64 package tests passed22/22 with ABI,
artifact/checksum and config-preserving installation checks. ARM64 staging is not
handheld installation. Physical `none.btdev`/new arbitrary-name files were not
created; their loading semantics were tested natively. This run does not add an
external MIDI delivery, listening or audible-synchronization measurement.

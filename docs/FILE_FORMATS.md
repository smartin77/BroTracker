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
firmware; physical SD validation is pending. See the [follow-ups](TODO_LIST.md#planned-session-storage-and-development-fixtures).
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
sample/format contract. Proposed session/development INI keys are planning only,
not supported options or changes to the runnable example.


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
No host INI selection, automatic boot load/seed/restore, shutdown save, browser or
new UI shortcut is implemented. Startup remains the existing in-code sine theme.
Files are only created/loaded on explicit commands, in main context. Operations
require fully stopped initialized audio, no queued control work/ACKs, pending
publication/preview/readback, and settled MIDI queue/cleanup/USB transfers. Playing,
Paused, core fault or unresolved cleanup returns `busy-or-not-ready`; missing SD
initialization returns `sd-unavailable`. No interrupt exclusion spans SD I/O.

| Explicit command | Effect |
| --- | --- |
| `BTSTORE1 SEED 1 theme` | Create `/BroTracker/development/theme.btdev` only if absent |
| `BTSTORE1 SEED 1 midi` | Create `/BroTracker/development/midi.btdev` only if absent |
| `BTSTORE1 LOAD 1 theme` / `midi` | Read that SD file and submit validated stopped publication |
| `BTSTORE1 SAVE 1` | Save coherent device-owned published pattern + published tempo |
| `BTSTORE1 RESTORE 1` | Load `/BroTracker/current.btsession` through stopped publication |

`theme.btdev` is exactly the existing 32-row/eight-channel CH1+CH2 sine theme at
155.00 BPM; CH3–CH8 are empty. `midi.btdev` retains that theme/tempo and adds raw
instrument 1 (displayed MIDI 02) on CH5 at zero-based rows/raw notes:
2/64, 6/67, 10/71, 14/67, 18/69, 22/72, 26/69, 30/67. Every following row has an
explicit arrival OFF with no instrument update. Other channels remain empty.
Pattern2 is reserved for user-supplied CH3/CH4 drums: no substitute is created.
Seed code is provisioning only; LOAD reads/validates files, never regenerates them.
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

Replies are `BTSTORE1 RESULT 1 <operation> <status> <revision>`; SAVE/SEED use revision
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

### Provisioning and manual validation

No upload or physical SD write has been performed for this implementation. With
explicit upload authorization, close BTX/owned monitors, discover the connected
Teensy and run `pio run -e teensy41_dev -t upload`. Use an SD card with existing
assets intact. Then, with BTX closed, discover CDC and run from the repo root:

```powershell
python tools/storage/device_storage.py --list
python tools/storage/device_storage.py seed-theme --port <discovered-port> --audio-output default
python tools/storage/device_storage.py seed-midi --port <discovered-port>
python tools/storage/device_storage.py load-theme --port <discovered-port>
python tools/storage/device_storage.py save --port <discovered-port>
python tools/storage/device_storage.py restore --port <discovered-port>
```

`--audio-output` explicitly initializes/selects a route if needed (use bridge or
pt8211 instead to select that route); never starts playback. The tool does not
STOP a playing/paused device automatically: STOP in BTX first, close it, then run.
It opens one CDC handle, requires storage capability, has bounded response waits,
never retries/reseeds automatically and closes its handle on exit. Timeouts mean
unknown outcome, not proof no write occurred. Optional `--log <new-file>` preserves
local transcripts without overwriting existing evidence. Do not run alongside BTX.
After tooling closes CDC, reconnect BTX: existing v2 baseline and working-preview
readback must reflect the restored pattern+tempo, discarding/reconciling draft via
existing recovery. No runtime storage UI is added.

Native tests cover serialization, truncation/corruption/oversize/unsupported files,
missing/no-overwrite, shared SD exclusion, write/sync/reopen/rename/rollback failures,
published-versus-preview capture, stopped/paused/queued-work gates, mute retention,
validated publication and host snapshot readback. Actual main-loop interrupt-guard
placement and Teensy backend wiring are inspected/compiled, not hardware-tested.
Physical checks remain: create/read both SD files, repeat no-overwrite, SAVE/RESTORE
and reconnect BTX while stopped; verify prior files/logs remain intact. External
MIDI delivery/audio synchronization and existing manual/audio issues remain open.

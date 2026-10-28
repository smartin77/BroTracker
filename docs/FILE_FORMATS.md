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

Teensy owns `/BroTracker/current.btsession`. It stores the current published
baseline **and** working intent, including unpublished preview edits available on
the device, with independent tempos. Normal startup/reconnect never loads the
configured development file. Firmware restores a validated session at boot,
stopped; BTX synchronizes with the device. See [continuity and audio/storage
policy](RECONNECT_CONTINUITY.md).

This is a bounded one-pattern foundation, not BTT/BTP/BTM or a self-contained tune.
Raw instrument references use current bindings. No Sample PCM/paths, instrument
definitions, voice phase, playback position/running state, mute/gain/master volume,
MIDI route/ownership, external instrument state or host configuration is stored.
Unsent host-only edits cannot be reconstructed after a host crash.

### Working-session envelope

All integers are unsigned little-endian; no native struct dumps. Maximum 3144 bytes.

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `BTWS` |
| 4 | 2 | Version 1 |
| 6 | 2 | Header length 24 |
| 8 | 4 | Published BTSS record length |
| 12 | 4 | Working BTSS record length |
| 16 | 4 | Reserved zero |
| 20 | 4 | CRC-32/ISO-HDLC of envelope header/payload, omitting bytes 20..23 |
| 24 | variable | Published BTSS record, then working BTSS record |

Both records and the complete envelope validate before destination mutation. Each
record is bounded to 1560 bytes. Unsupported versions, reserved fields, excessive
length, truncation, trailing bytes, bad CRC, notes, timings, dimensions or tempo
are rejected. Old single-record BTSS sessions remain readable as identical
published/working state; they contain no separate unpublished edits to recover.

### Temporary pattern records and files

`/BroTracker/tune.btdraft` is the single explicitly saved tune. F5 / row A replaces
it safely; F4 / L1+Y loads it. Save captures working intent/applied tempo without
Publish; successful Load intentionally establishes both a new published baseline
and working copy. Automatic session updates and development-file loads never
write this file. Full Load/Save/Save As dialogs remain future work.

Both draft BTSS and development BTDP records have this version-1 layout:

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

### Development selection and protocol

`[development] startup_pattern` is used **only** by F3 / Select+Start. Names accept
1..24 ASCII alphanumeric/underscore/hyphen characters, optionally exact `.btdev`.
No paths, quotes, whitespace, traversal, truncation or other extensions. Only exact
unquoted `none` requests an empty 32?8 pattern at 12500 without SD access;
`none.btdev` names a real file. Invalid config disables explicit test load rather
than loading a fallback. Missing/empty config defaults to `none`. Restart BTX to
read changed configuration. Named file contents determine dimensions and tempo.

Firmware advertises `BTSTORE1 CAP 1`, `READYCAP 1` and `SESSIONCAP 2`. Hosts require
the last capability for working-session and draft actions; older SAVE behavior
must not be mistaken for saving unpublished edits.

| Command | Effect |
| --- | --- |
| `BTSTORE1 SAVE 1` | Save working session, or explicitly defer while Playing |
| `BTSTORE1 DRAFTSAVE 1` | Save working tune to tune.btdraft; bounded RAM defer while Playing |
| `BTSTORE1 DRAFTLOAD 1` | Validate draft file, apply through stopped publication, final audio-owner ACK |
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

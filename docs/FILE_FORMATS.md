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
host drive, or that file browsing, planned module/session persistence or new
storage operations are implemented. Existing SD WAV inspection/preparation and the
[planned session contract](#planned-working-session-separate-from-tunesprojects)
retain their documented implementation status. Format descriptions below remain
design contracts, not proof of implemented persistence.

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

## Planned working session: separate from tunes/projects

Future Teensy-owned working-session persistence is scheduled after external MIDI
validation and resulting fixes; see the [implementation milestones](TODO_LIST.md#planned-session-storage-and-development-fixtures).
Canonical SD directory `/BroTracker` and working file `/BroTracker/current.btsession`
are planned. This is not an explicitly saved BTT tune, BTP project or BTM module;
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
sample PCM/paths are not supplied by the current Publish snapshot. Resolve their
ownership, references and compatibility explicitly before save/load: serializing
pattern bytes alone does not make a session self-contained.

Use a versioned validated session format and safe replacement of the previous valid
copy; filesystem durability and interrupted-write recovery require implementation-
time review. SD access obeys D0046 read/write exclusivity, with serialization/I/O
outside audio/timing ISRs and coherent fixed-memory handoff rather than ISR file work.

Three operations remain distinct: current **Restore** returns to the in-memory
published baseline; future **session restore** loads the validated working-session
file into a defined device-baseline/host-draft state without starting playback;
**module/tune load/save** is a separate explicit future operation with its own
sample/format contract. Proposed session/development INI keys are planning only,
not supported options or changes to the runnable example.

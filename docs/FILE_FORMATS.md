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
Each WAV `WAVITEM` is followed by `BTSTORE1 WAVMETA 3 request page_index status
format channels bits sample_rate`. `status` is the existing `WavBufferStatus`
ordinal from the playback validator; other fields are the actual metadata read
before success/rejection. The host requires one matching metadata record per WAV
before accepting the page. Unsupported and invalid WAVs stay in the list. All
case variants of `.wav` are listed; unrelated files are omitted. Directory entries
and WAV names can contain up to 255 filename bytes. Selecting a supported WAV
still enforces the existing filename and 63-byte resource-path validation.

Teensy inspects at most one WAV and scans at most 64 directory entries per page.
It uses the existing bounded header/chunk validator under the shared SD read lease,
never reading PCM for browser metadata. Highlight changes use the listing's cached
metadata and perform no SD I/O. Missing/read-failed WAVs receive a rejected result;
folder failures retain the previous confirmed directory/list. Only validated
success can stage a selection; definition commit still prepares again. The same
single footer line shows metadata, the approved rejection reason, or an empty/
navigation status. Legacy v1/v2 requests and replies remain supported by firmware;
new metadata-aware hosts require WAVBROWSECAP 2. BTM storage is unchanged.

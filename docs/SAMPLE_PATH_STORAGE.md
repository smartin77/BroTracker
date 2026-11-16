# Bounded sample paths

BTX constructs paths and parent navigation from the confirmed directory listing.
Teensy validates every incoming path and performs SD operations. Both sides bound
full paths at **255 ASCII bytes excluding NUL**. Ordinary internal spaces in file
and folder names are supported, including `/Samples/Jungle riddims`. Empty,
traversal, repeated-separator, control/non-ASCII and reserved-punctuation components
are rejected; components cannot start/end with a space or end with a dot. Directory
paths are absolute; stored sample resources are SD-relative. The complete combined
sample path must fit, even if the directory and filename each fit separately.
Rejected paths are never truncated. WAV format support remains PCM16 mono 44100 Hz.

## Ownership and budget

One shared arena holds **8192 bytes**, including terminators, for distinct validated
resource-path strings. **256 entries** of eight bytes add **2048 metadata bytes**,
for **10240 bytes** of fixed shared storage. Allocation consumes each actual string
length plus NUL. Two-byte owning references retain entries across definition-table
copies, cached sources, preparation jobs and live rollback. Runtime definition
views expose pointers into the same stable arena for existing callers; they do not
own additional strings. Deduplication uses the exact validated full path.

Allocation reuses released metadata slots and free byte ranges. It does not move
retained strings. A path needs a contiguous free range and an entry, so fragmentation
or the entry limit can reject it before all bytes are occupied. Cache/voice and
rollback pins retain resources until their final owner releases them. Reclamation
respects active streaming descriptors and handles. Visit end releases the rollback
definitions and source pins. No audio callback performs path lookup, allocation,
reference management, file opens, reads or closes.

Preparation resolves a resource when opening a file. Subsequent bounded PCM reads
use the existing FsFile handle; there is no repeated path lookup per read. Source
capacity remains 16 and streaming buffers remain unchanged. New capacity failures
use result 9 (`Sample path storage full`), keeping the old definition, binding,
slot/count and source assignment. Session loading under resource pressure reports
`resource-capacity`, does not quarantine a valid file or activate partial state,
and does not automatically start playback. The global budget includes retained
session, rollback and in-flight candidates, not only the displayed instruments.

Protocol and temporary transaction/path buffers remain fixed-size. A path is at
most 256 bytes with NUL; an encoded command/reply line is bounded at 640 bytes.
The fixed TX FIFO has 51 entries of 640 payload bytes. These temporary copies are
separate from shared resource storage and are included in the firmware RAM report.

## Versioned protocol

`BTPATH1 CAP 1 255 8192` enables a single-token path encoding: `@` followed by two
hex digits per original byte. Embedded NUL, malformed hex, decoded over-limit
paths and invalid components are rejected. This makes spaces unambiguous and
caps worst-case token length at 511 bytes. Negotiated versions are:

| Exchange | Long-path version |
|---|---|
| WAVOPEN and WAVDIR/WAVINFO/WAVITEM/WAVMETA/WAVEND/WAVERROR | 4 |
| Instrument GET/STATE facts | 3 |
| DEFINE, RELINK, REPLACE, EDIT | 2 |
| Audition PLAY | 2 |

Listing filenames remain the remainder of WAVITEM, preserving ordinary spaces.
Numeric fields and cancellation/visit coordination are unchanged. Legacy clients
retain the old exchanges and 63-byte raw tokens; a legacy response cannot truncate
a long path. BTX rejects unsupported long/spaced writes on old firmware. Progressive
batches, sample counts, current-directory-only scans and refill-first scheduling
are unchanged. Browser headers wrap the full directory; Instrument resources and
replacement confirmation also retain the complete decoded path.

## Persistence

BTM version 2 stores one full string per unique resource, then compact per-slot
indices. IDs are file-local, not runtime arena IDs. Boot resolves saved paths
without BTX, prepares available resources and preserves valid unavailable Sample
definitions without substituting a sound. Existing BTM v1, BTWS and BTSS reads
remain supported. See [the session layout](FILE_FORMATS.md#bounded-sample-paths-and-btm-version-2).

# BTX host logging

Logging is BTX-owned ([D0058](ARCHITECTURE_DECISIONS.md#d0058---btx-logging-modes-and-startup-log-retention)).
No firmware logging, diagnostic formatting/frequency or audio-callback work changes.

## Configuration and recovery

Schema 1 retains its required `[btx]` fields and accepts optional settings:

```ini
[logging]
mode=debug
preserve_last=5
```

Built-ins are Basic/0; supported modes are `basic` and `debug`, retention 0..20.
Duplicate settings/sections, unknown keys, invalid modes/ranges reject the complete
file transactionally. Missing optional settings use built-ins. `--safe-config`
bypasses external config and uses Basic/0. Startup never creates/rewrites config.
The example opts into Debug/5. On explicit authorization, the Windows user file
was created (none previously existed) with schema 1, new-pattern length 32, Debug/5.

Windows config: `%APPDATA%\BroTracker\BroTracker Terminal\config.ini`.
Windows log: the same resolved SDL directory, `brotracker-terminal.log`.
ArkOS config: SDL preference path, normally
`$XDG_DATA_HOME/BroTracker/BroTracker Terminal/config.ini`, or
`$HOME/.local/share/BroTracker/BroTracker Terminal/config.ini`.
ArkOS log remains `/tmp/brotracker-arkos.log`; this is not permanent evidence storage.
Paths are independent of the launch working directory.

## Modes and timestamps

Basic includes startup UTC/local time, application/build information, configuration
resolution/effective mode, connection/session transitions, applied transport changes,
errors/faults/rejections/recovery and clean shutdown. Routine CELL/POS/PERF payloads,
per-edit diagnostics, periodic Windows audio statistics and other protocol detail
are excluded. Bounded storage suppresses unchanged waiting/error/state observations;
meaningful changes/recovery remain visible.

Debug includes Basic and existing detailed diagnostics. A one-time startup summary
uses platform APIs for OS/version, architecture, CPU count and physical memory,
plus SDL versions/backends. Existing USB/device identities and Windows audio
formats/buffer capabilities remain Debug details. Unavailable fields are explicit;
no unrelated inventory or sensitive environment dump is taken. Host resources do
not identify Teensy CPU/RAM/PSRAM or firmware build. Build date/time identifies the
host compilation, not a firmware revision.

Events carry UTC, local time with signed timezone offset and steady `mono_ms`.
Shared serial detail retains SDL `host_ms` with its separate application origin.
Compare like clocks; these are host observations, not exact physical device/audio
timing. Logging stays outside realtime audio processing. Host file writes are
serialized across workers. Formatting is bounded; oversized messages are omitted
with a diagnostic, rather than silently appearing complete.

## Startup retention

Debug with N>0 rotates once before opening current: `brotracker-terminal(1).log`
(or ArkOS `brotracker-arkos(1).log`) is newest, then `(2)` through `(N)`. Only this
logger's exact numbered names 1..20 are touched, never unrelated captures. Basic
or zero retention retains prior overwrite behavior. No current log means no shift
of existing archives within the configured limit. `BROTRACKER_APPEND_LOG=1` appends, skips rotation and logs the
decision in either mode. There is no runtime rotation or compression.

Preflight rejects nonregular archive entries. Rotation removes the oldest retained
archive, prunes exact-name archives beyond the limit, then renames descending:
(N-1) to (N), through (1) to (2), and current to (1). All renames stay in the same
directory; no copying or timestamp rewriting is used. Opening a new current log
is allowed only after successful rotation. Windows fixture tests verify unchanged
creation/access/last-write timestamps and file identity for moved current and archives.
Rotation failure uses append fallback and an actionable diagnostic, preserving an
unarchived current. Earlier archive moves/removals may already have completed;
rotation is not an atomic multi-file transaction and does not roll them back. If opening also fails, startup
continues with stderr and a Windows debugger notice. Archive validated evidence
separately before Basic/zero runs; numbered retention is not permanent archival.

## Validation status

Portable tests cover schema compatibility/defaults, full rollback/safe bypass,
filtering, repeated observations/recovery, UTC/timezone offsets, rotation order and
limits, unrelated-file preservation, append and failure fallback. Windows package
build/tests are required; manual Windows output/rotation checks and ArkOS build and
runtime validation remain pending. No postponed hardware scenario is run here.

1. Compare Basic and Debug logs, including rejected operations and clean shutdown.
2. With Debug/5, launch repeatedly and verify newest-first archives and unrelated
   file preservation; confirm an invalid archive/permissions condition preserves
   current with a diagnostic.
3. Verify append skips rotation and `--safe-config` selects Basic/0 without rewriting
   config. Archive evidence before testing overwrite behavior.

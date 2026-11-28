# Accepted-BTT export verification

The protocol and candidate/session distinction are authoritative in
[FILE_FORMATS.md](../../../docs/FILE_FORMATS.md#complete-accepted-btt-export-to-a-btx-candidate-2026-11-28).
The actual production exporter and real client run together in
`BroTrackerBttStoppedLoadTests`; use separate CMake Debug/Release artifact roots.

`run.py` is an explicit-step dedicated-card workflow, run from the checkout with
PlatformIO Python/pyserial and the existing UCRT64 native compiler on PATH:

1. `run.py prepare`: create fresh native validated input/expected bytes, a private
   nonce directory/path and an isolated fixed-path helper config under
   `build/btt-export`. Does not access a device. Outputs must not already exist.
2. `run.py preflight`: verify unique USB serial 17681760, confirmed IDLE, actual
   card CID 6f0303534441424310aa001504018b00, 61076480 sectors, FAT32/64-sector
   clusters. It confirms the existing output route (default bridge if not yet
   initialized); it never STOPs, LOADs or saves.
3. Build `pio run -c build/btt-export/helper.ini -e teensy41`, then
   `run.py upload-helper`, then `run.py stage`. The helper independently rechecks
   card identity, inventories legacy files/backing collisions, exclusive-creates
   only the fresh private fixture, and reads it back. It reuses the established
   stopped-save helper source with an expanded read-only collision inventory.
4. Build normal `pio run -e teensy41_dev`; `run.py upload-normal` restores that
   image. Any other explicit `upload-*` label also selects this normal image;
   use a fresh label for changed firmware and retain preceding evidence.
5. Build/package the actual Windows delivery using `tools/windows/build-package.ps1`.
   `run.py exercise` confirms board/card/firmware build facts, LOADs the private
   fixture, closes its own serial, and launches delivered Windows BTX with
   `--export-btt <fresh-evidence-directory> <expected-board> <expected-boot>`.
   BTX uses the actual BringUpSerial receiver/platform transport, writes raw RX
   and timed TX/RX hex evidence, validates the candidate and returns without
   STOP, session Save or BTT Save. The Python side checks exact bytes plus
   independent envelope and every section CRC, then verifies IDLE/serial close.
   `unavailable` is accepted for optional boot facts; fresh challenges on every
   transfer provide correlation, not a fabricated device boot UUID.
6. `run.py final`: recheck current normal firmware build identity, board/card,
   IDLE and close serial. No helper/diagnostic image may remain installed.

Each physical step creates a new `physical-<label>` directory exclusively. An
`exercise-*` label repeats the corrected export against the same task-owned
fixture while retaining an earlier attempt. Only an exclusive-create collision
positively identified by staging inventory or this task's recorded successful
LOAD permits another LOAD attempt; uncertain reads/writes/uploads never retry.
Historical private backing files are never removed to pass. No SD benchmark runs.
The expected dedicated-card identity is deliberate; a mismatch stops the workflow.

`memory.py prepare` archives HEAD read-only into an isolated source directory and
creates its baseline build config. Build with absolute `-c` plus that project
root; `memory.py measure` records production section/symbol/RAM/ITCM-bank deltas
and firmware hashes. It never uploads. Existing `baseline/` snapshots are the
initial on-disk images; the comparison uses the fresh isolated HEAD build.

The automation entry is built into both delivered BTX platforms. Physical export
is checked on Windows; this task makes no physical ArkOS or timing qualification
claim. Temporary candidates are not the internal current session. Import,
restore, migration, autosave, editing unlocks and project packaging remain outside
this tool and task.

After a successful run, `summarize.py <windows-btx-evidence-directory>` operates
entirely offline. It writes exclusive readable serial and JSON summaries, checks
raw/event identity, every credit/offset/chunk CRC/challenge, streaming exact bytes
against the expected input, whole CRC and completion, and confirms no tune/save/
transport mutation commands were sent by BTX.

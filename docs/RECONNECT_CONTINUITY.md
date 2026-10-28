# BTX reconnect continuity (2026-10-28)

Startup selection and reconnect recovery are different operations. The configured
`development.startup_pattern` loads once per BTX launch, behind the existing
STOP/storage-ready/load-result/baseline/working-readback barriers. Reconnect must
not replay that load, START, a session restore, or SD saves.

## Origin and limits

`firmware/teensy/BroTracker/pattern_bringup.cpp` initializes `g_active_pattern`
from `MakeDevelopmentPattern()` and `SelectDevelopmentFixture()` at boot, then
copies it into `g_working.working`. `src/bringup/pattern_fixture.h` populates the
BT theme in channels 1/2. `MakeStoredDevelopmentPattern(...,1)` in
`pattern_file_store.h` uses that same development pattern. The boot theme is not
proof that the device loaded the SD file patern1: reboot can produce those cells
without reading it. Firmware starts stopped. A USB interruption alone does not
necessarily reboot a separately powered device.

Published baseline, working/pending preview, transport and device revision IDs
are volatile. Power loss destroys that device RAM. SD files/session data survive
unless explicitly saved/replaced; they are not automatically the latest work.
Restarting BTX intentionally starts a new configured startup transaction; it is
not recovery of the previous host draft. Host exit/crash loses the new retained
RAM copy too. No session auto-load/save or persistence mechanism is added.

Previously `LocalPatternEditor::Sync(nullopt)` cleared the displayed editor and
preview recovery invalidated local intent on NEXT rollback. Reconnect then adopted
the verified boot snapshot. Correct checksum/readback proves what the device now
contains, not that it retained the previous pattern or loaded the configured file.
The earlier startup-success reporting fix only scoped the log to its transaction.

## Disconnect without a device reset

In the current variable-rate production firmware, `ServiceSerial()` detects
`!Serial`, clears bounded serial/telemetry/snapshot staging, cancels an SD-ready
wait, and calls publication/preview `NewSession()`. Those calls cancel only
unsubmitted staging, not already submitted frozen work or the applied patterns.
On loss of an established protocol connection it also resets the MIDI session
and attempts to enqueue STOP once. The submission result is ignored, so this is
not an unconditional stop acknowledgment if the queue is full. HELLO re-establishes
capabilities/state; it does not select a fixture or enqueue START.

`VariableRatePatternPlayer::Stop()` retains pattern/bindings, installs an accepted
pending working preview, resets voices/position to stopped row zero, and cancels
pending tempo. Thus a powered interruption need not lose cells, but uninterrupted
transport/phase is not promised even without a reboot. Read the actual state and
working/pending metadata after reconnect; never infer PLAYING or replay transport.
Legacy/native test environments have different MIDI/session handling and do not
establish the variable-rate physical USB behavior.

## Implemented host recovery boundary

`BringUpSerial` freezes the last editor draft and published baseline before its
normal disconnected Sync can clear them, including an edit immediately before
failed I/O. Fixed host storage retains pattern cells, OFF timing, dimensions and
last known working tempo. The editor stays visible and locked, without a live
position highlight. No realtime firmware callback or firmware memory is changed.

After complete published and working/pending readbacks, exact content and tempo
agreement releases the hold without a write. The log reports matching contents,
not proof of boot identity, phase continuity or uninterrupted playback. Protocol
revision counters are not a boot UUID. A fresh BTX without retained work still
adopts authoritative data according to normal startup policy.

A mismatch retains the old host draft **and** published baseline. The device may
still contain the boot fixture, but BTX does not silently substitute it in the
editor or claim successful recovery. Automatic preview, START and CONTINUE remain
blocked until explicit publication of the retained or locally restored pattern.
STOP remains available. BTX never replays START/CONTINUE.

- **F5 / mapped Publish (A in cell context):** explicitly publish the retained draft
  to device RAM using the existing full publication validator and fresh identity
  discovery, with retained tempo where tempo-aware publication is supported. It
  intentionally establishes a new published baseline, including unpublished host
  edits. This is not an SD save, session restore or automatic START. Stop/pause
  first; existing paused-publication capability checks apply. Exact publication
  readback is required before the hold clears. Different boot dimensions do not
  force a lossy preview merge.
- **F4 / mapped Restore (L1+Y):** restore the retained **local published baseline**,
  discarding local unpublished edits, as normal Restore does. It does not adopt
  the boot fixture or restore a session. During the recovery hold this is local
  only: no pattern, tempo or SD write is sent, and the hold remains. F5 Publish is
  still required to place that restored pattern back on a differing device.
  F5 is publication, never an SD/session Restore. No new control adopts boot data.

Incomplete/failed readback, rejected publication, lost replies and interrupted
recovery keep the retained copy and block playback restart. No uncertain command
is retried automatically. A later explicit F5 requests readback first while publication is uncertain,
without replaying it. F4 cannot change the retained draft during uncertain publication.
If capabilities/readbacks are unavailable, recovery remains blocked rather than
inventing continuity. Keep BTX open to retain this RAM-only copy.

This is pattern recovery, not whole-session or audio continuity: instrument/sample
availability must still pass device validation; voice phase, transport position,
mute/gain/session-master settings and external MIDI/device state are not restored
from this host pattern cache. No analog success or dArkOS hardware result follows
from host tests or Windows checks.

## Validation

Regression coverage: `tests/test_startup_storage.cpp`,
`tests/test_host_pattern_preview.cpp`, `tests/test_host_pattern_publication.cpp`
and the existing bring-up controls/serial suites. Tests distinguish initial startup
from reconnect; changed working data and unsent edits from boot data; matching
powered readback; missing/partial readback; repeated disconnect; local Restore
and RAM publication, different dimensions, rejected recovery, queued-publication
suspension even before editor Sync, retained verified tempo during uncertainty,
no automatic LOAD,
preview/publication, START or SD writes.

Hardware evidence and final build/package results are recorded with the review
artifacts in `diagnostics/reconnect/`. Hardware verification requires an explicit
baseline followed by user unplug/replug; a reconnect does not by itself prove reset.

## Windows hardware observation (2026-10-28, CET)

PT8211 was confirmed attached. The user approved replacing test RAM only; actual
config stayed `output=pt8211`, `startup_pattern=patern4`. No firmware was uploaded.
The existing and patched Windows clients were exercised stopped, with no START.

- 18:38:37: original BTX loaded patern4 once. Both complete readbacks had checksum
  **1585222843**. After user unplug/replug, 18:39:52 readbacks had boot-theme
  checksum **3187457427**, with no second LOAD. The initial read-only pre-BTX
  snapshot also had that boot-theme checksum. Revision/contents returning to boot
  state is consistent with reset/lost RAM; no electrical power/uptime measurement
  establishes the exact reset mechanism.
- Patched client startup at 18:42:47 loaded patern4. The user changed row 0/channel 1
  from note 60 to 61; at 18:43:46 preview 1 was ACCEPTED and APPLIED. This working
  edit remained unpublished before unplug.
- After the second user unplug/replug, at 18:44:57 both device readbacks again
  had checksum **3187457427**. BTX held the old draft/baseline rather than replacing
  the displayed cells. The user confirmed note 61/C#3 was still displayed.
- The user explicitly pressed **F5 Publish**. At 18:46:38 BTX sent the retained
  draft as `BTPUBLISH1 BEGIN 2 1 32 8 256 15500`. At 18:46:39 publication APPLIED;
  published and working readbacks had checksum **180298710**, and at 18:46:40
  recovery was confirmed. Comparing all 256 cells against startup verifies exactly
  the intended change: first cell `(60,2,0)` -> `(61,2,0)`. This published the edit
  to RAM, not an SD/session Restore. No second LOAD, START/CONTINUE, SD save/seed or
  session-restore command was issued. The client was closed cleanly, stopped.

The tested intermediate client offered a special F4 device-adoption option; it
was not exercised. Following the user's control clarification, that option was
removed: final F4 restores the retained local baseline, and F5 remains Publish.
Final regression tests cover that mapping and uncertain-publication read-only
retry. `hardware-tested-binary.txt` identifies the intermediate hardware-tested
executable separately from final package hashes.

Two optional patched launches hit a bounded Windows receive-buffer overflow during
startup while builds were active. It stayed locked behind the existing startup
barrier; no uncertain LOAD or START was replayed. Their logs were archived; a clean restart succeeded for the
edited-pattern hardware test. This task does not fix or characterize that buffer-overflow
condition; it remains a stress/reliability limitation.

No audible playback/phase continuity, separately powered USB-only interruption,
physical failure injection or dArkOS hardware run was verified. Unsaved edits
not yet transmitted and interrupted/rejected recovery are covered by host tests.
Evidence: `diagnostics/reconnect/hardware-before-btx.txt`,
`windows-baseline-reconnect.log`, `windows-fixed-before-unplug.log`,
`windows-fixed-after-unplug.log`, `windows-fixed-recovered.log`,
`hardware-comparison.json`, `windows-fixed-startup-overflow.log` and
`windows-final-startup-overflow.log`.

## Final build and packaging validation

Final native CTest: **7/7**; Windows package CTest: **10/10**; Eoan ARM64 CTest:
**23/23**. Final source hashes were checked unchanged after build. Production
`pio run -e teensy41_dev` passed; firmware sources/build selection are unchanged,
and no upload was needed or performed for this host-only fix. Windows package
matches its build executable. ArkOS ABI/dependency, offline/package identity/hash
and isolated temporary installer/config-preservation checks passed. Existing
Windows/manual-testing user configs retain their original hashes; the missing
ArkOS user config remains absent. Both package examples match the shared source.
No actual console installation was performed.

Final artifacts (repository-relative):

- `deploy/windows/BroTrackerTerminal.exe` and its existing DLL/assets package.
- `deploy/arkos/BroTrackerTerminal`, `BroTrackerAlsaBridge`, `launch.sh`, assets and
  `SHA256SUMS`.
- `.pio/build/teensy41_dev/firmware.hex` / `firmware.elf` (build only).
- Logs: `diagnostics/reconnect/tests-host-final.log`, `windows-package-final.log`,
  `arkos-package-final.log`, `firmware-build.log`; final artifact/source hashes in
  that directory. Full workspace patch: `diagnostics/reconnect/full.diff`.

The subsequent optional final-client startup loaded unedited patern4 into test
RAM before hitting the second receive-buffer overflow. Its later complete
published readback was **1585222843**, and the client exited cleanly stopped.
Thus note 61 was verified during the explicit publication test, but was not saved
to SD or left as the final test RAM pattern. No test client/serial capture remains
open. Changes are uncommitted in the current `master` working tree; no branch,
index or remote mutation was performed.

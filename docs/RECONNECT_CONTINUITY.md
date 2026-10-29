# BTX working session and reconnect continuity

Teensy owns the engine and SD workspace. BTX is a terminal: closing or losing CDC
must not enqueue STOP, reset MIDI ownership, or prevent the main loop from draining
accepted audio-owner notifications. Audio and connected hardware service continue
while Teensy remains powered. Physical USB removal can independently disrupt USB
MIDI/audio endpoints; CDC continuity is not a guarantee of external delivery.

## Current session and explicit files

`/BroTracker/current.btsession` preserves separate published and working patterns,
with separate published/applied tempos. Unpublished edits delivered through working
preview are session content. It is distinct from `/BroTracker/tune.btdraft`, which
changes only on explicit temporary Save. See [file formats](FILE_FORMATS.md#working-session-separate-from-tunesprojects).
Mute, gains, master volume, transport position, voice phase, host-only edits, routing,
Sample PCM and external instrument state are outside this bounded format.

Normal BTX launch reads the actual device baseline and working/pending readbacks.
It never loads `development.startup_pattern`, RESTOREs a file, or STARTs playback.
The device reads its latest validated session at boot and remains stopped. Missing
sessions leave the boot fixture available; corrupt/unrecoverable sessions are
reported explicitly, without overwriting the file or pretending recovery succeeded.
A valid previous copy can be read with a `backup-retained` warning. Interrupted
replacement files are retained for manual recovery and block subsequent writes.

F3 / Select+Start explicitly requests the configured development file; F4 / L1+Y
loads `tune.btdraft`. Both require STOP first. Read/CRC and binding/application
validation precede replacing the session. A successful load establishes a new RAM
published baseline and working copy; it never overwrites the saved draft file.
Missing/invalid reads retain current work. Uncertain application/readback remains
locked rather than replaying a possibly applied command. There are no dialogs.
F5 / row A saves the working tune, without publishing it. Shift+F5 / L1+A remain
explicit RAM Publish; Shift+F4 / L1+X remain in-memory Restore to the published
baseline. [Controls](BTX_CONTROLS.md) is the binding reference.

## Storage and audio priority

The existing SD backend performs synchronous writes/sync/verification/rename, whose
card latency has no realtime upper bound. It therefore performs **no persistence
I/O while Playing**. Fixed main-owned working/pending metadata retains the newest
accepted pattern in RAM. At settled Pause/Stop, after queued controls and audio
acknowledgments drain, main context saves the session promptly. A paused pending
preview is saved as working intent without applying it early or continuing playback.
No filesystem work runs in an ISR and no SD I/O runs under interrupt exclusion.

Temporary Save during playback freezes one bounded device RAM snapshot, reports
`deferred-playing` (not success), and writes that snapshot after Pause/Stop. Later
edits do not change the requested saved tune. Further deferred Save requests report
busy. Sudden power loss before persistence can lose RAM-only changes.

Regular exit waits for available host edits to reach verified working preview,
then requests a session save. Stopped/paused exit awaits its result. Playing exit
receives an explicit deferred result and leaves playback running; it cannot promise
a fresh disk write without sacrificing the audio policy. A bounded exit timeout,
disconnect or unsupported firmware is logged as unconfirmed/unavailable, never
successful persistence. Host-only edits lost in a crash are not recoverable.

## Reconnect protection

A surviving BTX retains its draft/baseline before disconnected Sync could clear it.
After complete baseline and working/pending readbacks, matching contents release
the hold without writing. The editor cursor is retained, including field/header
selection, and clamped to valid visible rows/channels when installing a context.

A playing edit can exist in BTX and device RAM while SD still contains an older
working pattern. Power loss then restores the older disk session, stopped. When
its published pattern/tempo matches the retained baseline, its working namespace
has restored zero revision IDs, and authoritative transport is IDLE, BTX makes
one automatic working-preview recovery attempt. It sends the retained draft as
working data and restores the retained verified working tempo where supported.
A full transfer requires complete checksum readback before editing and START/
CONTINUE unlock. The published baseline stays separate and the recovered edit
remains unpublished. No Publish, tune.btdraft write, fixture LOAD or START is sent.
This recovers available host RAM; it does not claim the edit survived on SD before
power loss. Normal stopped firmware persistence may subsequently save it.

Running/paused devices, advanced working revisions, differing published baselines
or dimensions, missing capabilities, rejected transfers and incomplete/failed
readbacks do not authorize a blind overwrite. BTX retains available work and
shows the recovery hold/failure. STOP remains available; a rejected/uncertain
recovery is not automatically retransmitted in a loop. A fresh connection can
perform new authoritative discovery. Revision counters alone are not boot UUIDs;
the matching baseline, verified working data and stopped-state checks are also
required. A newer working revision on a powered device is never overwritten
automatically with the retained host draft.

For an unresolved hold, Shift+F4 / L1+X restores the retained local published
baseline only. If that now matches the verified device, the hold can clear without
Publish. Shift+F5 / L1+A remains an explicit RAM publication option after stopping
or eligible pause, including deliberate replacement of different dimensions.
Uncertain publication is read back before explicit retry. Fresh BTX adopts actual
device state. Matching cells do not establish uninterrupted playback/phase.

## Validation scope

Automated coverage is in the storage, startup/storage, host preview/publication,
shared controls and Windows/ArkOS input suites. It covers validated replacement,
missing/corrupt/interrupted files, retained unpublished content, normal startup
without fixture LOAD/STOP/START, powered running reconnect without writes, exit
without STOP, and both Select+Start press orders with held-chord suppression.
Build/test and physical-test evidence for this change is recorded separately;
the observations below describe the **previous implementation**, whose F4/F5
bindings and automatic startup load have now been replaced.

## Normal-session-launch recovery gate correction (2026-10-29)

A subsequent physical failure was captured in the running deployed Windows BTX
before restart or session changes. Process path, package/build SHA256 and in-memory
PE/COFF headers matched the package from the prior fix (SHA256 prefix CC2CA3E1).
The startup log's compile date belongs to an unchanged translation unit and is not
sufficient whole-executable identity. The captured working draft was reconstructed
from verified readback and edit transactions and matched its logged checksum.

The device returned stopped at row/tick zero and acknowledged Enter/STOP, but
Space sent no START. BTX logged that automatic recovery had begun, yet sent no
working-preview transfer. A normal configured session launch uses
`initial_sync_ready_`; disconnect clears it. Recovery was still gated by
`StartupPatternReady()`, creating a circular wait: no transfer until ready, no
ready until transfer/readback. The prior physical test explicitly used F3 first,
which selected a different readiness path and masked this case.

Only an already-authorized automatic working recovery may now pass the preview
sending/start-transfer readiness gates. User START remains blocked until full
working readback and normal editor synchronization finish. No START, Publish,
fixture LOAD, draft-file save or relaxed device-authority check is added. STOP can
be acknowledged during the hold but does not substitute for verified recovery.

The exact regression starts BTX with configured test selection but never requests
F3. It resumes a saved unpublished working pattern, starts and advances playback,
edits back to the published content, simulates power loss restoring the older
working session, and verifies automatic recovery. Both preview-capability paths
check stopped transport at sample zero, retained cursor, unlocked editing, STOP
acknowledgment and subsequent user START through the transport coordinator.
Current evidence and build results are under `diagnostics/reconnect-current/`;
these supersede the earlier test's coverage claim for normal session launch.

Windows passed 10/10 tests and Eoan ARM64 passed 23/23 with ABI, offline package
identity and isolated config-preserving installer checks. Production firmware
built with unchanged HEX; no upload was performed. In the actual updated
`deploy/windows/BroTrackerTerminal.exe` (SHA256 prefix 2AD5B33F), BTX launched
normally from a session containing unpublished working edits, without F3. The
playing test edit returned a cell to the published baseline, while the disk
session still held the older unpublished note. After the user's power cycle,
BTX recovered the retained draft automatically at 19:54:27-19:54:29 CET.
Complete readbacks verified all 256 published and 256 working cells, OFF metadata
and tempo. Transport was stopped at row/tick zero before user input; recovery sent
no START, STOP, Publish, fixture LOAD or tune-file Save. Subsequent user Space
received STARTED before any Enter; Enter then received STOPPED and Space started
again. The user explicitly confirmed: Space works without needing Enter.
Test patterns and tune.btdraft were not written. Available original host/session
work was captured before the swap and the reconstructed host draft was checksum
verified and restored through working preview before temporary session testing.

## Playing-edit reconnect lock fix (2026-10-29)

The Windows observation log showed complete baseline/working readbacks, followed
by the old permanent mismatch hold. The baseline checksum was unchanged across
the playing-edit power loss; the volatile working edit differed from the restored
SD state. The old policy required explicit Publish to clear that hold. It also
reset the editor context on matching recovery, losing the cursor. Stale file-action
status could mask the reconnect explanation. These host paths now perform the
verified working-only recovery described above, preserve/clamp the cursor and
prioritize the recovery status. Firmware persistence/audio scheduling is unchanged.

Regression coverage includes playing edits and fresh boot with full/sparse preview
capability; unsent host changes; unpublished baseline separation; no automatic
START, Publish, fixture LOAD or draft-file save; editing and Start/Pause/Continue/
STOP after recovery; matching powered reconnect; cursor retention/clamping;
rejected/incomplete recovery with STOP; and protection of advanced device working
revisions. Current build/package and physical evidence is recorded under
`diagnostics/reconnect-playing/`. Physical and final build results are recorded
there separately from the prior implementation evidence below.

Windows CTest passed 10/10 and ARM64 Eoan CTest passed 23/23, including ABI,
offline/package identity and isolated config-preserving installer checks. An
additional recovery-readback regression also passed the Windows and ARM64 core suites. The
production Teensy build passed with the same HEX
as the installed firmware; no upload was required. The user performed the physical
Windows/PT8211 test with a note edited at row 5/channel 4 while Playing, then
removed/reconnected USB/power with BTX open. At 18:45:16 BTX began working-only
recovery and at 18:45:18 completed verified readback, stopped. Comparison of all
256 published cells and 256 working cells, OFF metadata and tempo confirmed that
the published baseline stayed unchanged and the retained edit was recovered as
unpublished work. The recovery segment sent no Publish, fixture LOAD, draft-file
Save, STOP or START. The user confirmed cursor retention, Space play/pause, then
editing and Enter/STOP without Restore/Publish. Logs independently show subsequent
edits at the retained row/channel, preview ACCEPTED/APPLIED and STOPPED.

The first synthetic follow-up edit was suppressed by normal focus-loss handling
while the user was replying in chat; Windows denied the test process foreground
activation. User testing with BTX focused confirmed editing. This was not a second
recovery lock. Physical failure injection, differing-baseline/dimension recovery,
separately powered physical USB removal, acoustic/peripheral continuity and actual
handheld operation remain unverified. Conservative holds for incompatible or
advanced device state are intentional; automatic recovery does not claim a boot
UUID or overwrite newer working revisions.

## Working-session validation (2026-10-28, CET)

Windows CTest passed **10/10**. ARM64 Eoan CTest passed **23/23**, with ABI,
offline dependency/package identity and isolated config-preserving installation
checks. The final logging adjustment is included in the refreshed deliveries.
Production `teensy41_dev` built and was uploaded to the discovered Teensy. Final
firmware HEX matches the physically tested upload after a comment-only rebuild.
Reported RAM1 variables: 61120 bytes; RAM2 variables: 296480 bytes. No PCM pool or
playing SD write was introduced.

Device protocol tests verified missing-file preservation, complete published and
working readbacks, unpublished Save without Publish, validated draft Load, a frozen
playing Save surviving later edits, powered CDC close/reopen while still Playing,
and Pause/Stop session persistence. PERF telemetry reported zero callback-budget
overruns during the exercise; this is not acoustic or external-peripheral proof.

The packaged Windows BTX was exercised with its real F3/F4/F5 handlers, working
preview, Space and window-close events sent only to the owned test process.
Window close persisted/deferred the session without sending STOP. Fresh BTX read
back the running device without LOAD/STOP/START and preserved its working cells.

The user unplugged/reconnected Teensy while BTX remained open. At 23:34:59 BTX
observed disconnect; at 23:35:05 it received `session-boot ok` and complete baseline
and working readbacks. All **256 published cells**, **256 working cells**, OFF
metadata and both 15500 tempos matched the prior persisted session. First cell
raw note **65** was published and **67** remained unpublished. BTX released its
matching-content hold; transport remained stopped, with no host LOAD/STOP/START.
An independent serial readback after graceful client exit confirmed both copies.
The user's power-cycle action and reset metadata are recorded; electrical power
and uptime were not independently measured.

Finally, an explicit F3 loaded `patern4`, verified baseline/working recovery, and
an explicit F5 saved it to tune.btdraft. BTX was reopened with the final package,
stopped, resuming that session. The unchanged Windows config still selects
`output=pt8211`, `startup_pattern=patern4`. Development `.btdev` files were never
seeded, renamed, replaced or written. The user authorized session/draft test-file
changes without backup or restoration. The draft/session test contents were thus
intentionally replaced; their earlier distinct states remain in local evidence.

Unverified: physical interrupted-write/card-failure injection; separately powered
physical USB cable removal; acoustic continuity; external peripheral delivery;
physical handheld Select/Start mapping, handheld playback and device installation.
Synthetic controller tests cover SDL BACK/START and verified raw bindings, both
press orders, held chords, full-release rearming and consumed Start.

Local evidence: `diagnostics/session-hardware/`, `session-windows-hardware/`,
`session-host-final-tests.log`, `session-arkos-final-verified.log`,
`session-firmware-final-verified.log`, final hashes/report/diff under `diagnostics/`.
Build artifacts: `deploy/windows/BroTrackerTerminal.exe`,
`deploy/arkos/BroTrackerTerminal` and its verified package/manifest,
`.pio/build/teensy41_dev/firmware.hex` and `firmware.elf`.
No stage, commit, push or merge was performed.

## Previous Windows reconnect implementation (2026-10-28, CET)


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

# ArkOS bridge-pop investigation closeout — 2026-10-21

Diagnostic development is **paused**. One completed physical A/B supports the
experimental 45-frame capture holdback; it is not a universally validated fix.
Holdback remains opt-in. Normal production defaults and audio behavior are unchanged.

## Verified automated evidence

| Exact run ID | Saved outcome | USB versus received PCM |
|---|---|---|
| `20261021T155911Z-offline-fga4lt6d` | FAILED: analysis timeout | Excluded from completed A/B; raw evidence retained |
| `20261021T160940Z-offline-nsulhgyp` | COMPLETED: baseline A | 991 differing stereo frames in 68 groups; all 991 match the 2,048-frame history in both streams |
| `20261021T162057Z-offline-f49vr6z6` | COMPLETED: 45-frame holdback B | Zero differences across the 5,419,228-frame captured range |

Baseline covers 5,433,385 stereo frames / 113,338 positive reads; holdback covers
5,419,228 frames / 113,188 positive reads. In both completed runs every immediate
read-return digest matches saved PCM, with complete read coverage and zero recorded
digest loss. Received and playback-accepted PCM are byte-identical. USB integrity
passes: baseline has 246,538 events / 123,269 pairs; holdback has 245,888 events /
122,944 pairs, no reported capture drops. Content alignment independently selects
offset zero. These comparisons cover the **diagnostics-on recording**, including
its BTX-active/off phases; diagnostics-off has no paired PCM/USB recording.

The completed runs use identical binaries and matching automatic workload and
condition readbacks (pattern, tempo, gains and available sample metadata).
Bridge SHA-256: `f52827275dd56465f25c351835d16f206d82d57151ee3d9d4693e752166cf773`.
BTX SHA-256: `58777263dca38a3f73a30416ba4979ad1e7aca09335dfd538ad3ffa660df993d`.
Unknown sample-binding/source-identity limitations in the original reports remain.

Holdback's **diagnostics-off** bridge log records one playback underrun and
256 discarded frames. This is a separate unresolved issue. COMPLETED denotes
completion of the experiment's technical requirements, not absence of every
playback fault or an audible-clean pass. The failed first attempt is not a third
completed result and has not been relabelled after offline review.

## User listening observations — separate from automated evidence

The user reports audible dull pops in baseline, more noticeable during the second
reported playback segment. Holdback sounded substantially cleaner, with one larger
pop near the end of the second reported segment. No precise timestamp or
unambiguous phase association was supplied. The larger pop cannot be attributed
to the recorded underrun as fact. The user's segment descriptions are not mapped
to diagnostics-on/off or BTX-active/off phases.

## Limits and paused work

The immediate read-return evidence supports stale bytes at that observation,
subject to noncryptographic digest collisions and common-mode limitations.
usbmon observes the HCD boundary, not guaranteed on-wire truth. The suspected
kernel publication-before-copy race and the running kernel's exact source
identity remain **unconfirmed**. One completed A/B is finite evidence; the clean
holdback recording does not establish clean analog output or universal reliability.
Further diagnostics and playback-underrun investigation are pending during the
pause. No kernel/firmware changes, automatic workaround activation or new tests
are authorized by this closeout.

The next planned task, not implemented here, is a minimal sine synth foundation
replacing the current beeper: a 440 Hz reference note, gentle attack around
10–20 ms, smooth decay to zero, approximately one-second total duration. Resolve
the project's octave naming before assigning an octave label to that note.

## Verified preservation set (ignored, local)

The existing last-three archive was located and reused rather than duplicating
its raw data. It retains all three original directory trees, existing analyses,
failed-attempt records, logs and per-run artifact identities: **194 members /
172 regular files**. Every original member was hashed. Completed-run raw PCM,
USB payload reconstruction/content alignment and read digests were independently
checked against the saved analyses. No original was moved, overwritten or deleted.

- Original archive: `_backyard/bridge-clicks-last-three-20261021.tar.gz`
  SHA-256: `f1aa261cd488a38c17914628ab69c69393b58ad6bc3739d1371c2fa53376fbc4`
- Matching tooling/build context: `_backyard/bridge-pop-closeout-20261021/tooling-context.tar.gz`
  SHA-256: `22a00cf8701cfac6ce959c2e24023b60759d83ab8adcffe94036cd7276f1b721`
  All 52 members re-read and hashed. Includes matching source/tooling, ARM64 bridge
  and BTX binaries, deployment fingerprints/assets, existing build/ABI identities,
  pre-closeout documentation snapshots and the original-member verification manifest.
- Backup index: `_backyard/bridge-pop-closeout-20261021/backup-index.json`.
  Verification details/per-file hashes: sibling `verification.json`.
  Final closeout documentation is retained in sibling `closeout-documents/`.

Keep both archive components and the preservation directory together under
`_backyard/`; the index refers to the existing archive one directory above it.
Raw captures, WAVs, archives, logs and generated review exports remain ignored
and outside the proposed Git changes. This tracked report is a concise index,
not a replacement for original evidence. No physical tests or builds were run
for this closeout.

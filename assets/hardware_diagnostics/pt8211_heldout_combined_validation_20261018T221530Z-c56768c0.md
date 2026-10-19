# Frozen loading classifier: combined held-out validation

**Evidence limit:** prior attached report/raw/manifests were removed by the user. Attached outcomes below are previously reported results, not revalidated evidence. The three new detached captures are fully verified. The attached 806-cycle raw record and manifest cannot be independently checked from this export.

| Condition/attempt | Expected signature | Prediction | UNKNOWN | Evidence |
|---|---|---|---|---|
| attached 1 | PT8211_LIKE | PT8211_LIKE | False | Prior conversation only; raw deleted |
| attached 2 | PT8211_LIKE | PT8211_LIKE | False | Prior conversation only; raw deleted |
| attached 3 | PT8211_LIKE | UNKNOWN | True | Prior conversation only; raw deleted |
| detached 1 | UNLOADED | UNLOADED | False | Raw verified now |
| detached 2 | UNLOADED | UNLOADED | False | Raw verified now |
| detached 3 | UNLOADED | UNLOADED | False | Raw verified now |

Frozen fingerprints match attached-session snapshot. Calibration remains 13; none of the new captures is a calibration input. No tuning or replacement captures.

## Per-feature margins and reasons

M=median, C=p05/p95 central envelope, H=every-trial hard envelope. Margins in cycles relative to the expected signature. Negative margin rejects that signature. They are not probabilities.

| Condition/attempt | Feature | M | C | H | Rejection reason |
|---|---|---|---|---|---|
| attached 1 (prior report only) | pin7_target0 | 8 | 8 | 8 | none previously reported |
| attached 1 (prior report only) | pin7_target1 | 8 | 8 | 8 | none previously reported |
| attached 1 (prior report only) | pin20_target0 | 8 | 8 | 8 | none previously reported |
| attached 1 (prior report only) | pin20_target1 | 8 | 8 | 8 | none previously reported |
| attached 1 (prior report only) | pin21_target0 | 8 | 8 | 8 | none previously reported |
| attached 1 (prior report only) | pin21_target1 | 8 | 8 | 8 | none previously reported |
| attached 2 (prior report only) | pin7_target0 | 8 | 8 | 8 | none previously reported |
| attached 2 (prior report only) | pin7_target1 | 8 | 8 | 8 | none previously reported |
| attached 2 (prior report only) | pin20_target0 | 8 | 8 | 8 | none previously reported |
| attached 2 (prior report only) | pin20_target1 | 8 | 8 | 8 | none previously reported |
| attached 2 (prior report only) | pin21_target0 | 8 | 8 | 8 | none previously reported |
| attached 2 (prior report only) | pin21_target1 | 8 | 8 | 8 | none previously reported |
| attached 3 (prior report only) | pin7_target0 | 8 | 8 | 11 | none previously reported |
| attached 3 (prior report only) | pin7_target1 | 8 | 8 | 8 | none previously reported |
| attached 3 (prior report only) | pin20_target0 | 8 | 8 | 8 | none previously reported |
| attached 3 (prior report only) | pin20_target1 | 8 | 8 | 8 | none previously reported |
| attached 3 (prior report only) | pin21_target0 | 8 | 8 | -47 | hard raw bound (806 < 853), unverified now |
| attached 3 (prior report only) | pin21_target1 | 8 | 8 | 8 | none previously reported |
| detached 1 | pin7_target0 | 8.0 | 8 | 8 | none |
| detached 1 | pin7_target1 | 8.0 | 8 | 8 | none |
| detached 1 | pin20_target0 | 8.0 | 8 | 8 | none |
| detached 1 | pin20_target1 | 8.0 | 8 | 8 | none |
| detached 1 | pin21_target0 | 8.0 | 8 | 8 | none |
| detached 1 | pin21_target1 | 8.0 | 8 | 8 | none |
| detached 2 | pin7_target0 | 8.0 | 8 | 8 | none |
| detached 2 | pin7_target1 | 8.0 | 8 | 8 | none |
| detached 2 | pin20_target0 | 8.0 | 9 | 8 | none |
| detached 2 | pin20_target1 | 8.0 | 8 | 8 | none |
| detached 2 | pin21_target0 | 8.0 | 8 | 8 | none |
| detached 2 | pin21_target1 | 8.0 | 8 | 8 | none |
| detached 3 | pin7_target0 | 8.0 | 8 | 9 | none |
| detached 3 | pin7_target1 | 8.0 | 9 | 9 | none |
| detached 3 | pin20_target0 | 8.0 | 9 | 8 | none |
| detached 3 | pin20_target1 | 8.0 | 9 | 8 | none |
| detached 3 | pin21_target0 | 8.0 | 8 | 8 | none |
| detached 3 | pin21_target1 | 8.0 | 8 | 8 | none |

## New detached attempts

- `assets\hardware_diagnostics\pt8211_detached\20261018T221153.351814Z-c3bdde66`; cold power-up (operator reported); 387856 bytes; {'meta': 1, 'pin': 384, 'control': 100, 'cadence': 3444, 'end': 1}; PASS: sequences, per-record CRCs, cumulative CRC/count, raw SHA, final newline, decoded results, summary, ACK log and identical firmware metadata.; classifier reasons: [].
- `assets\hardware_diagnostics\pt8211_detached\20261018T221246.746733Z-fb5f505e`; software reset/boot-only; 387856 bytes; {'meta': 1, 'pin': 384, 'control': 100, 'cadence': 3444, 'end': 1}; PASS: sequences, per-record CRCs, cumulative CRC/count, raw SHA, final newline, decoded results, summary, ACK log and identical firmware metadata.; classifier reasons: [].
- `assets\hardware_diagnostics\pt8211_detached\20261018T221328.971752Z-24c3b8fb`; software reset/boot-only; 387856 bytes; {'meta': 1, 'pin': 384, 'control': 100, 'cadence': 3444, 'end': 1}; PASS: sequences, per-record CRCs, cumulative CRC/count, raw SHA, final newline, decoded results, summary, ACK log and identical firmware metadata.; classifier reasons: [].

## Hard raw-trial guard sensitivity

- Frozen classification remains unchanged. Every new detached feature passes median, p05/p95 and hard raw bounds, with no rejection reasons.
- The earlier reported attached UNKNOWN had passing median and central envelopes but one 806-cycle raw value below 853. The +8 median/central margins did not override the -47 raw margin. Original evidence is unavailable for rechecking.
- For n=64, p05 uses sorted index floor(63*0.05)=3. A single low outlier (1/64 trials) can leave median/p05/p95 within profile yet fail the every-trial guard.
- The hard guard deliberately trades sensitivity to rare extremes for fail-closed behavior: one trial outside the finite envelope makes the whole run UNKNOWN, even with robust center agreement. Across six groups there are 384 opportunities per run for such a guard rejection.
- UNKNOWN is not UNLOADED or proof of absence/DAC failure. Do not tune the band from this held-out outcome or discard the outlier. Any future rule revision needs a new version and fresh prospective validation.
- No general accuracy estimate is supported by six small same-setup held-out runs, especially with the attached raw evidence deleted.

## Missing evidence

- `assets\hardware_diagnostics\pt8211_heldout_attached_validation_20261018T215857Z-b0a0e4c7.json`
- `assets\hardware_diagnostics\pt8211_attached\20261018T215700.434997Z-194bfe2f\raw.jsonl`
- `assets\hardware_diagnostics\pt8211_attached\20261018T215700.434997Z-194bfe2f\manifest.json`
- `assets\hardware_diagnostics\pt8211_attached\20261018T215735.599162Z-ee13663a\raw.jsonl`
- `assets\hardware_diagnostics\pt8211_attached\20261018T215735.599162Z-ee13663a\manifest.json`
- `assets\hardware_diagnostics\pt8211_attached\20261018T215809.161405Z-93ee62f3\raw.jsonl`
- `assets\hardware_diagnostics\pt8211_attached\20261018T215809.161405Z-93ee62f3\manifest.json`

No firmware/profile/classifier edits, rebuilds, programming, production changes or Git writes. Exactly three new detached attempts; all preserved outside calibration. No outcome-dependent retries.

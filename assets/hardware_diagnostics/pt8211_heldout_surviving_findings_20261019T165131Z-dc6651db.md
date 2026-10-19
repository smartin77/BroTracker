# Held-out findings from surviving and recovered evidence

No captures or tuning. Surviving data classified with frozen tooling; supplied archive partially inspected without full restoration. Archive document instructions were not executed.

| Run | Expected | Prediction | Evidence | Minimum margin |
|---|---|---|---|---|
| attached 1 | PT8211_LIKE | PT8211_LIKE | Raw evidence supplied in recovery ZIP; directly revalidated now | 8.0 cycles |
| attached 2 | PT8211_LIKE | PT8211_LIKE | Raw evidence supplied in recovery ZIP; directly revalidated now | 8.0 cycles |
| attached 3 | PT8211_LIKE | UNKNOWN | Previously reported; raw/manifest unavailable | GPIO21 down raw margin -47 (reported) |
| detached 1 | UNLOADED | UNLOADED | Surviving workspace raw evidence; directly revalidated now | 8.0 cycles |
| detached 2 | UNLOADED | UNLOADED | Surviving workspace raw evidence; directly revalidated now | 8.0 cycles |
| detached 3 | UNLOADED | UNLOADED | Surviving workspace raw evidence; directly revalidated now | 8.0 cycles |

## Detached feature margins

M=median, C=p05/p95 envelope, H=every-trial hard envelope. Margins are to UNLOADED boundaries, not probabilities.

| Run | Feature | Median | M margin | C margin | H margin | Rejection |
|---|---|---|---|---|---|---|
| 1 | pin7_target0 | 485.0 | 8.0 | 8 | 8 | none |
| 1 | pin7_target1 | 170.0 | 8.0 | 8 | 8 | none |
| 1 | pin20_target0 | 485.0 | 8.0 | 8 | 8 | none |
| 1 | pin20_target1 | 170.0 | 8.0 | 8 | 8 | none |
| 1 | pin21_target0 | 629.0 | 8.0 | 8 | 8 | none |
| 1 | pin21_target1 | 198.0 | 8.0 | 8 | 8 | none |
| 2 | pin7_target0 | 492.0 | 8.0 | 8 | 8 | none |
| 2 | pin7_target1 | 170.0 | 8.0 | 8 | 8 | none |
| 2 | pin20_target0 | 485.0 | 8.0 | 9 | 8 | none |
| 2 | pin20_target1 | 170.0 | 8.0 | 8 | 8 | none |
| 2 | pin21_target0 | 629.0 | 8.0 | 8 | 8 | none |
| 2 | pin21_target1 | 198.0 | 8.0 | 8 | 8 | none |
| 3 | pin7_target0 | 492.0 | 8.0 | 8 | 9 | none |
| 3 | pin7_target1 | 170.0 | 8.0 | 9 | 9 | none |
| 3 | pin20_target0 | 485.0 | 8.0 | 9 | 8 | none |
| 3 | pin20_target1 | 170.0 | 8.0 | 9 | 8 | none |
| 3 | pin21_target0 | 629.0 | 8.0 | 8 | 8 | none |
| 3 | pin21_target1 | 198.0 | 8.0 | 8 | 8 | none |

## Integrity results

- `assets\hardware_diagnostics\pt8211_detached\20261018T221153.351814Z-c3bdde66`: PASS: raw SHA, every sequence and record CRC, cumulative CRC/count, complete framing, exact decoded results and recomputed summary, expected firmware metadata. ACK write log verified
- `assets\hardware_diagnostics\pt8211_detached\20261018T221246.746733Z-fb5f505e`: PASS: raw SHA, every sequence and record CRC, cumulative CRC/count, complete framing, exact decoded results and recomputed summary, expected firmware metadata. ACK write log verified
- `assets\hardware_diagnostics\pt8211_detached\20261018T221328.971752Z-24c3b8fb`: PASS: raw SHA, every sequence and record CRC, cumulative CRC/count, complete framing, exact decoded results and recomputed summary, expected firmware metadata. ACK write log verified
- `assets\hardware_diagnostics\pt8211_attached\20261018T215700.434997Z-194bfe2f`: PASS: raw SHA, every sequence and record CRC, cumulative CRC/count, complete framing, exact decoded results and recomputed summary, expected firmware metadata. ACK write log verified
- `assets\hardware_diagnostics\pt8211_attached\20261018T215735.599162Z-ee13663a`: PASS: raw SHA, every sequence and record CRC, cumulative CRC/count, complete framing, exact decoded results and recomputed summary, expected firmware metadata. Unavailable in supplied recovery archive; not required for raw CRC/sequence validation

## Interpretation

- All three detached runs are UNLOADED with no rejection reasons and minimum margins of eight cycles. Every feature passes median, p05/p95 and raw-trial guards.
- Attached runs 1/2 are now directly verified from recovery raw evidence, rather than solely previously reported.
- Attached run 3 remains UNKNOWN: previously reported GPIO21 down raw trial 806 cycles, below 853, giving a -47-cycle raw margin despite +8 median/central margins. Its raw and manifest remain unavailable; no reclassification or pass is claimed.
- The hard every-trial guard can reject one extreme while median/p05/p95 pass. It is fail-closed loading-signature behavior, not proof of physical absence or DAC failure.

No captures, profile/classifier/firmware tuning, production changes or Git writes. Existing datasets, recovery ZIP and previous reports/export preserved.

# PT8211 protocol-2 four-condition comparison

Cross-topology analysis explicitly authorized by the user ONLY for this four-condition report. Existing probe.py same-topology safeguards are unchanged. Expected condition/topology labels independently verified.

Explicit selected datasets in both named reports, de-duplicated by path, plus three explicit new attached ADuM3160 paths. No directory globbing, failed runs or unrelated datasets.

| Pin | Pull | Attached direct median [range] | Detached direct median [range] | Attached ADuM3160 median [range] | Detached ADuM3160 median [range] | Pooled A/D gap |
|---|---|---|---|---|---|---|
| pin7 | down | 717.0 [716-727] | 485.0 [484-491] | 718.0 [717-728] | 491.0 [484-496] | 220 |
| pin7 | up | 198.0 [197-200] | 170.0 [168-171] | 198.0 [197-200] | 170.0 [168-171] | 26 |
| pin20 | down | 745.0 [745-748] | 485.0 [484-515] | 745.0 [745-748] | 485.0 [484-516] | 229 |
| pin20 | up | 198.0 [197-200] | 170.0 [168-171] | 198.0 [197-200] | 170.0 [168-171] | 26 |
| pin21 | down | 861.0 [861-864] | 629.0 [629-632] | 861.0 [861-864] | 629.0 [629-632] | 229 |
| pin21 | up | 226.0 [226-229] | 198.0 [197-200] | 226.0 [226-229] | 198.0 [197-200] | 26 |

All table values are cycles at 600 MHz. Full percentiles, means, standard deviations, histograms and overlap masses are in JSON. Zero timeouts and already-at-target initial states; all settled observations follow the selected pull.

## Topology shifts and boot variation

- pin7_target0: ADuM-direct pooled median shifts attached=1.0, detached=6.0 cycles.
  - attached_none: boot medians [717.0, 717.0, 717.0, 717.0]; cold p95 721, software p95 721 cycles.
  - attached_adum3160: boot medians [717.0, 717.0, 724.0]; cold p95 723, software p95 724 cycles.
  - detached_none: boot medians [485.0, 485.0, 485.0]; cold p95 491, software p95 489 cycles.
  - detached_adum3160: boot medians [492.0, 492.0, 485.0]; cold p95 492, software p95 492 cycles.
- pin7_target1: ADuM-direct pooled median shifts attached=0.0, detached=0.0 cycles.
  - attached_none: boot medians [198.0, 198.0, 198.0, 198.0]; cold p95 200, software p95 200 cycles.
  - attached_adum3160: boot medians [198.0, 198.0, 198.0]; cold p95 200, software p95 200 cycles.
  - detached_none: boot medians [170.0, 170.0, 170.0]; cold p95 171, software p95 170 cycles.
  - detached_adum3160: boot medians [170.0, 170.0, 170.0]; cold p95 170, software p95 171 cycles.
- pin20_target0: ADuM-direct pooled median shifts attached=0.0, detached=0.0 cycles.
  - attached_none: boot medians [745.0, 745.0, 745.0, 745.0]; cold p95 747, software p95 748 cycles.
  - attached_adum3160: boot medians [745.0, 745.0, 745.0]; cold p95 748, software p95 747 cycles.
  - detached_none: boot medians [485.0, 485.0, 485.0]; cold p95 514, software p95 487 cycles.
  - detached_adum3160: boot medians [485.0, 485.0, 485.0]; cold p95 513, software p95 487 cycles.
- pin20_target1: ADuM-direct pooled median shifts attached=0.0, detached=0.0 cycles.
  - attached_none: boot medians [198.0, 198.0, 198.0, 198.0]; cold p95 200, software p95 200 cycles.
  - attached_adum3160: boot medians [198.0, 198.0, 198.0]; cold p95 200, software p95 200 cycles.
  - detached_none: boot medians [170.0, 170.0, 170.0]; cold p95 171, software p95 170 cycles.
  - detached_adum3160: boot medians [170.0, 170.0, 170.0]; cold p95 170, software p95 171 cycles.
- pin21_target0: ADuM-direct pooled median shifts attached=0.0, detached=0.0 cycles.
  - attached_none: boot medians [861.0, 861.0, 861.0, 861.0]; cold p95 863, software p95 864 cycles.
  - attached_adum3160: boot medians [861.0, 861.0, 861.0]; cold p95 864, software p95 864 cycles.
  - detached_none: boot medians [629.0, 629.0, 629.0]; cold p95 631, software p95 632 cycles.
  - detached_adum3160: boot medians [629.0, 629.0, 629.0]; cold p95 632, software p95 632 cycles.
- pin21_target1: ADuM-direct pooled median shifts attached=0.0, detached=0.0 cycles.
  - attached_none: boot medians [226.0, 226.0, 226.0, 226.0]; cold p95 229, software p95 229 cycles.
  - attached_adum3160: boot medians [226.0, 226.0, 226.0]; cold p95 228, software p95 229 cycles.
  - detached_none: boot medians [198.0, 198.0, 198.0]; cold p95 200, software p95 200 cycles.
  - detached_adum3160: boot medians [198.0, 198.0, 198.0]; cold p95 200, software p95 200 cycles.

## Run validation

- attached, none, software reset: `assets\hardware_diagnostics\pt8211_attached\20261018T205239.548877Z-13ee2d9b`; 387856 bytes/3930 records. PASS: all sequences/per-record CRCs; cumulative CRC/count; raw SHA; exact decoded results; recomputed summary; measurement order/control timestamps; ACK log; identical firmware metadata.
- attached, none, software reset: `assets\hardware_diagnostics\pt8211_attached\20261018T205318.757431Z-12c8ba57`; 387856 bytes/3930 records. PASS: all sequences/per-record CRCs; cumulative CRC/count; raw SHA; exact decoded results; recomputed summary; measurement order/control timestamps; ACK log; identical firmware metadata.
- attached, none, software reset: `assets\hardware_diagnostics\pt8211_attached\20261018T205357.062029Z-f15e692a`; 387856 bytes/3930 records. PASS: all sequences/per-record CRCs; cumulative CRC/count; raw SHA; exact decoded results; recomputed summary; measurement order/control timestamps; ACK log; identical firmware metadata.
- attached, none, cold power-up (operator reported): `assets\hardware_diagnostics\pt8211_attached\20261018T210730.652396Z-fd65f4b4`; 387856 bytes/3930 records. PASS: all sequences/per-record CRCs; cumulative CRC/count; raw SHA; exact decoded results; recomputed summary; measurement order/control timestamps; ACK log; identical firmware metadata.
- detached, none, cold power-up (operator reported): `assets\hardware_diagnostics\pt8211_detached\20261018T211202.422679Z-b513ae3e`; 387856 bytes/3930 records. PASS: all sequences/per-record CRCs; cumulative CRC/count; raw SHA; exact decoded results; recomputed summary; measurement order/control timestamps; ACK log; identical firmware metadata.
- detached, none, software reset: `assets\hardware_diagnostics\pt8211_detached\20261018T211237.372223Z-dac20f4d`; 387856 bytes/3930 records. PASS: all sequences/per-record CRCs; cumulative CRC/count; raw SHA; exact decoded results; recomputed summary; measurement order/control timestamps; ACK log; identical firmware metadata.
- detached, none, software reset: `assets\hardware_diagnostics\pt8211_detached\20261018T211316.759098Z-94e9a48c`; 387856 bytes/3930 records. PASS: all sequences/per-record CRCs; cumulative CRC/count; raw SHA; exact decoded results; recomputed summary; measurement order/control timestamps; ACK log; identical firmware metadata.
- detached, adum3160, cold power-up (operator reported): `assets\hardware_diagnostics\pt8211_detached\20261018T212105.020506Z-50618547`; 387765 bytes/3929 records. PASS: all sequences/per-record CRCs; cumulative CRC/count; raw SHA; exact decoded results; recomputed summary; measurement order/control timestamps; ACK log; identical firmware metadata.
- detached, adum3160, software reset/boot-only: `assets\hardware_diagnostics\pt8211_detached\20261018T212143.422401Z-e0756d27`; 387856 bytes/3930 records. PASS: all sequences/per-record CRCs; cumulative CRC/count; raw SHA; exact decoded results; recomputed summary; measurement order/control timestamps; ACK log; identical firmware metadata.
- detached, adum3160, software reset/boot-only: `assets\hardware_diagnostics\pt8211_detached\20261018T212219.792664Z-80fd4dcd`; 387856 bytes/3930 records. PASS: all sequences/per-record CRCs; cumulative CRC/count; raw SHA; exact decoded results; recomputed summary; measurement order/control timestamps; ACK log; identical firmware metadata.
- attached, adum3160, cold power-up (operator reported): `assets\hardware_diagnostics\pt8211_attached\20261018T213549.838008Z-eec93f24`; 387856 bytes/3930 records. PASS: all sequences/per-record CRCs; cumulative CRC/count; raw SHA; exact decoded results; recomputed summary; measurement order/control timestamps; ACK log; identical firmware metadata.
- attached, adum3160, software reset/boot-only: `assets\hardware_diagnostics\pt8211_attached\20261018T213623.702761Z-6a10f51b`; 387856 bytes/3930 records. PASS: all sequences/per-record CRCs; cumulative CRC/count; raw SHA; exact decoded results; recomputed summary; measurement order/control timestamps; ACK log; identical firmware metadata.
- attached, adum3160, software reset/boot-only: `assets\hardware_diagnostics\pt8211_attached\20261018T213657.978728Z-3a50cddb`; 387765 bytes/3929 records. PASS: all sequences/per-record CRCs; cumulative CRC/count; raw SHA; exact decoded results; recomputed summary; measurement order/control timestamps; ACK log; identical firmware metadata.

No new transport or reset failures. Firmware write counters and stream CRCs are recorded per run in JSON.

## Limits

- Range separation and histogram overlap are descriptive for this finite dataset, not population guarantees or automatic detection thresholds.
- Cycle timings contain software overhead and GPIO threshold/pull variation. Any topology shift cannot be attributed uniquely to isolation versus power/environment.
- Only one cold boot per condition/topology limits cold-start inference. Boot median variation and distribution tails are included separately.
- Direct attached has four boots (256 trials per pin/direction); other conditions have three (192 each). Pooled attached has 448 trials and pooled detached 384 per pin/direction.
- A one-callback difference in the fixed control window is allowed only when end counts and cumulative CRC match; it is not missing wire data.

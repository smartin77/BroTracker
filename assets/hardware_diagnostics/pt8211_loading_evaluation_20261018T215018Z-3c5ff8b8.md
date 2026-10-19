# Experimental PT8211 loading classifier evaluation

In-sample calibration replay, not independent accuracy validation. Manual labels used only for scoring after classification.

| Run | Manual | Predicted | Primary / cross-check / minimum margin, cycles |
|---|---|---|---|
| 20261018T205239.548877Z-13ee2d9b | attached (none) | PT8211_LIKE | 8.0 / 8.0 / 8.0 |
| 20261018T205318.757431Z-12c8ba57 | attached (none) | PT8211_LIKE | 8.0 / 8.0 / 8.0 |
| 20261018T205357.062029Z-f15e692a | attached (none) | PT8211_LIKE | 8.0 / 8.0 / 8.0 |
| 20261018T210730.652396Z-fd65f4b4 | attached (none) | PT8211_LIKE | 8.0 / 8.0 / 8.0 |
| 20261018T211202.422679Z-b513ae3e | detached (none) | UNLOADED | 8.0 / 8.0 / 8.0 |
| 20261018T211237.372223Z-dac20f4d | detached (none) | UNLOADED | 8.0 / 8.0 / 8.0 |
| 20261018T211316.759098Z-94e9a48c | detached (none) | UNLOADED | 8.0 / 8.0 / 8.0 |
| 20261018T212105.020506Z-50618547 | detached (adum3160) | UNLOADED | 8.0 / 8.0 / 8.0 |
| 20261018T212143.422401Z-e0756d27 | detached (adum3160) | UNLOADED | 8.0 / 8.0 / 8.0 |
| 20261018T212219.792664Z-80fd4dcd | detached (adum3160) | UNLOADED | 8.0 / 8.0 / 8.0 |
| 20261018T213549.838008Z-eec93f24 | attached (adum3160) | PT8211_LIKE | 8.0 / 8.0 / 8.0 |
| 20261018T213623.702761Z-6a10f51b | attached (adum3160) | PT8211_LIKE | 8.0 / 8.0 / 8.0 |
| 20261018T213657.978728Z-3a50cddb | attached (adum3160) | PT8211_LIKE | 8.0 / 8.0 / 8.0 |

Matched 13/13 manual labels in calibration replay.

## Proposed decision bands

Largest within-label, per-feature between-boot median spread (7 cycles) plus one engineering guard cycle. Not a confidence interval.

M = median band; C = allowed p05/p95 envelope; H = hard envelope for every raw trial. All bounds inclusive, in cycles.

| Feature | UNLOADED: M; C; H | PT8211_LIKE: M; C; H |
|---|---|---|
| pin7_target0 | 477..500; 476..500; 476..504 | 709..732; 708..732; 708..736 |
| pin7_target1 | 162..178; 160..179; 160..179 | 190..206; 189..208; 189..208 |
| pin20_target0 | 477..493; 476..522; 476..524 | 737..753; 737..756; 737..756 |
| pin20_target1 | 162..178; 160..179; 160..179 | 190..206; 189..208; 189..208 |
| pin21_target0 | 621..637; 621..640; 621..640 | 853..869; 853..872; 853..872 |
| pin21_target1 | 190..206; 189..208; 189..208 | 218..234; 218..237; 218..237 |

All three pull-down primary gates must agree on one class; all three pull-up cross-checks must confirm that class. Any disagreement or quality/profile failure is UNKNOWN.

Margin = minimum distance to any applicable M/C/H boundary. A boundary value has zero margin and is accepted; a negative margin rejects that signature. Margins are not probabilities. UNKNOWN covers gaps, outside bands, disagreement and quality/profile failures.

Calibration identity: a9179c5f093605fd51845dbfa8c064604089083de9e3930fcb4805c6504fb805 at 600000000 Hz; complete metadata must match.

## Limits

- Experimental electrical loading signature of this exact Teensy/module/wiring and firmware, not PT8211 chip identity, power or DAC health.
- Unknown third-party loads, other boards, wiring lengths, temperature/supply drift and other firmware versions are not validated.
- Eight-cycle engineering padding is not a confidence interval; false-positive/false-negative rates outside these calibration runs are unknown.
- Audio Shield recognition remains pending physical hardware. Manual override and production integration remain future work.

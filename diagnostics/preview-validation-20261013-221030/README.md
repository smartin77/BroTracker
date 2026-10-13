# Working-preview validation evidence

Original supplied/current logs and pre-test executable were copied before any run.
manifest.json records source revision, initial modifications, expected firmware
context and exact tested executable hash. No Teensy build/upload occurred.
The firmware protocol exposes no build identity, so the environment is expected,
not independently proven from a binary hash.

measure-preview.ps1 and direct-serial-timing.json record the exclusive COM3 test.
working-before.json / working-after.json compare equal for all 48 cells and
OFF timing. Published data was read but never changed; original working data was
restored, then playback stopped. timing-summary.json correlates COMMIT, ACCEPTED,
APPLIED and next received POS. Receive timestamps are host observations; they do
not measure the exact audio callback or audible onset.

BTX scripts attempted targeted Windows-message injection but foreground focus
could not be acquired; no keys were injected. The hidden-window attempt required
explicit normal window-close messages to release its port. Both logs were saved.
Do not claim direct serial STOP as physical/SDL Enter hardware validation.

These captures are evidence, not disposable review/build artifacts. Archive them
separately when cleaning. The normal AppData log is overwritten at next launch.

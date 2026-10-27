# Audio Shield isolated smoke test

One Teensy 4.1 and one Teensy 4.x SGTL5000 Audio Shield, with **Rev D2
(fixed address) as the priority**. No added PSRAM, Shield memory, SD card,
potentiometer, input, BTX or host audio application is needed. At the current
amplitude 0.20 / headphone volume 0.50, the user confirmed clean, separated L/R
tones without audible noise/clicks and increased level versus the faulty LINEIN
route. This is a headphone smoke-test observation, not calibrated analog testing
or production-backend validation.

From the repository root:

```powershell
pio run -e teensy41_audio_shield_test
```

For a later, explicitly requested manual upload, followed by optional diagnostics:

```powershell
pio run -e teensy41_audio_shield_test -t upload
pio device monitor -e teensy41_audio_shield_test
```

Initial preparation did not upload firmware. The default remains
`teensy41_dev`; this environment compiles only its own `main.cpp`, uses USB serial
without USB audio/MIDI, and does not inherit production USB preparation.

## Connections and listening

Use the Shield's **headphone jack with ordinary stereo headphones**. Start with
headphones off your ears. The signal peak is 0.20 (about -14 dBFS) and headphone
volume is explicitly 0.50; perceived loudness still depends on the headphones.
The line outputs are explicitly muted and are not the listening connection for
this test. `volume()` controls headphones only: line output would require its own
`lineOutLevel()` and `unmuteLineout()` configuration and suitable grounded
line-level equipment. Do not use the headphone jack as a grounded amplifier or
oscilloscope connection: its sleeve is HP_VGND, not system GND. See the
[schematics and codec pin definitions](../../../docs/AUDIO_SHIELD_SMOKE_TEST.md#primary-references).

1. Disconnect **all power** before changing modules. Remove PT8211; verify the
   Shield is a Teensy 4.x revision, orientation, headers, 3.3V and common GND.
   Prefer directly stacked short headers; keep any I2S wires short.
2. Power Teensy. No serial connection is required for the sequence. After codec
   initialization and initial silence, expect a low 440 Hz tone on **left only**,
   silence, a higher 880 Hz tone on **right only**, then longer silence; repeat.
3. Each gate is 250 ms, with 10 ms attack/release. The silent phases are 512 ms
   and 1512 ms measured from noteOff, allowing 12 ms for release. These are
   foreground scheduling targets, not measured physical timing guarantees.
4. Optionally open the serial monitor (115200; close BTX first). The startup
   banner is retained until the first connection and drained in bounded chunks.
   Record each control-call return value, board revision, wiring, headphone model,
   channel separation, silence, pops/noise and failures. Serial messages do not
   verify analog audio. To repeat the banner/initialization, reset or power-cycle.
5. If any checked control call fails, no tone gates run and there is no automatic
   retry. Power down and inspect the hardware before repeating. Even a fully true
   banner still requires listening. An absent-board comparison, if wanted, must
   also be performed with all power removed before detachment/reattachment.

## Implementation boundaries

The static graph is two sine generators, two envelopes and `AudioOutputI2S`,
connected to independent L/R ports. `AudioControlSGTL5000` uses `setAddress(LOW)`
(7-bit 0x0A), `enable()` retaining its DAC route, headphone volume,
headphone unmute and line mute. Failed configuration leaves the graph silent;
additional mute attempts are best effort. No ADC/input object is instantiated.

All I2C calls, the library's initialization delays and banner formatting run once
in foreground setup with audio interrupts enabled. The loop has no delay, wait
for Serial, storage access or catch-up bursts. Serial writes are limited to
available space and 64 bytes per pass. Only short PJRC audio parameter updates
use `AudioNoInterrupts`; I2C/printing never runs inside that exclusion or an ISR.

Audio objects/connections and diagnostics use fixed storage. `AudioMemory(16)`
is a static internal RAM2 pool, not heap allocation; PJRC's I2S DMA buffer is also
internal RAM2. Constructing the hardware output before `AudioMemory` gives it
update ownership and avoids AudioStream's dynamically allocated fallback timer.
Audio blocks come from that bounded pool. No application `new`, `malloc`,
Arduino String, EXTMEM or external-memory API is used. Library-wide build logs
may compile SD/SerialFlash objects because `Audio.h` includes them; the test does
not initialize or use them. This smoke graph does not replace tracker scheduling.

See [research, pin conflicts, older revisions, dual-board limitations and build
evidence](../../../docs/AUDIO_SHIELD_SMOKE_TEST.md).

## Listening-level follow-up (2026-10-27)

Raised amplitude to 0.20 (about -14 dBFS) and headphone volume to 0.50.
The isolated environment built and uploaded to Teensy serial 17681760 on COM4.
An initial upload/capture attempt was blocked by an occupied COM4; retry succeeded
after the user closed the monitor. Captured startup reports `enable()=true`,
`muteLineout=true`, `headphoneSelect(DAC)=true`, `volume(0.50)=true` and
`unmuteHeadphone=true`, with `audio UNVERIFIED`. Source/banner consistency and
`git diff --check` passed; sequence, ramps, initialization guards and production
routing are unchanged. The user subsequently confirmed the lower tone on the
left and the higher tone on the right, but reported that this run was still very
quiet. This is a user listening observation, not a measured output level.

## Higher-level follow-up (2026-10-27)

Current tone amplitude is 0.80 (about -1.9 dBFS); headphone volume is 0.80.
The previous source used volume 0.50; this follow-up explicitly sets the requested
0.80. Sequence, ramps, line-out mute, initialization checks and production routing
are unchanged. New loudness and channel separation require user listening.
Keep headphones off your ears initially.

Build/upload passed for `teensy41_audio_shield_test` on COM4 (serial 17681760).
Captured initialization reports `enable()=true`, `muteLineout=true`,
`headphoneSelect(DAC)=true`, `volume(0.80)=true`, `unmuteHeadphone=true`, and
`amplitude=0.80`, with `audio UNVERIFIED`. Focused source/banner consistency
and unchanged-behavior checks plus `git diff --check` passed. The capture port
was closed after reading; no new audible result is claimed.

## Route and peak diagnostic (2026-10-27)

This diagnostic intentionally retains the current `headphoneSelect(AUDIO_HEADPHONE_DAC)`
call, amplitude 0.80 and headphone volume 0.80. **Keep headphones disconnected**
for this diagnostic. The installed library's selector sets bit6 despite its DAC
name/comment; actual readback below confirms LINEIN bypass. The route is not
corrected here, and the listening procedure above does not establish output level.

Two static AudioAnalyzePeak objects branch from the envelope outputs before I2S.
Foreground sampling discards the first 50ms of each phase and reports maxima
through 200ms, for three cycles / twelve windows. Values are normalized peaks
in millionths; `reads` counts successful analyzer reads, not guaranteed block
counts. `reads=0` means no blocks arrived, **not a measured zero**. Idle envelopes
stop transmitting; the I2S output zero-fills missing channels. Timing is observed
foreground timing, not a physical output measurement. Analyzer objects remain
in the fixed graph; recording/reporting stops after the third cycle.

Eighteen registers are read once after codec setup. Each transaction writes only
the two-byte register-address pointer, then reads two data bytes; it never writes
a register value. Pointer enqueue/status failures, short reads and missing bytes
are explicitly errors with no decoded value. Successful zero is distinct from
failure. There are no retries or later register reads. I2C, formatting and serial
work remain foreground-only, with audio interrupts running. The complete report
is retained in a fixed 8192-byte buffer for late Serial attachment and drained in
at most 64-byte chunks alongside the original banner. `DIAG COMPLETE` marks the
end; reset/re-upload is required to obtain a fresh report after it is consumed.

Build/upload of `teensy41_audio_shield_test` succeeded on COM4, serial 17681760.
Complete serial capture reached `DIAG COMPLETE`. Observations:

| Readback | Observed | Comparison |
| --- | --- | --- |
| CHIP_ANA_CTRL 0x0024 | 0x0166 | Predicted: SELECT_HP=1/LINEIN, HP unmuted, line-out muted |
| HP volume 0x0022 | 0x1919 | Predicted: -0.5dB on each side |
| DAC volume / mute | 0x3C3C / L=0,R=0 | Predicted: 0dB, both unmuted |
| SSS / clock / I2S | 0x0010 / 0x0004 / 0x0030 | All predicted values matched |
| Digital / analog power | 0x0073 / 0x40FF | Both predicted values matched |
| CHIP_ID | 0xA011 | Part A0, revision11; does not establish PCB identity |
| Analog status | 0x0017 | No L/R or common short flag at this one-time read; PLL_LOCK=1 |
| Active left / right peaks | 0.800012 / 0.799982..0.800012 | Expected approximately 0.80; all six active windows matched |
| Inactive channels / silence | NO_BLOCKS | Consistent with idle-envelope behavior and I2S zero-fill |

No register transfer errors occurred. All twelve windows were 50..200ms. Audio
pool high-water was 2/16 blocks in this run. Static image sizes: flash 66556 bytes,
RAM1 variables 21376 + code 44136 + padding 21400 = 86912 bytes, RAM2 variables 17088.
These are linker figures and an observed pool maximum, not a full timing budget.

A local host harness compiled the actual firmware source against fake Audio/Wire
interfaces and passed four I2C failure cases, valid-zero handling, route/gain
decoding, eighteen one-time pointer reads and twelve reports with no buffer
overflow. Captured-output checks matched the predicted registers and active
peaks; configuration/sequence statements, production sources and diff whitespace
checks passed. The harness does not emulate I2S or analog hardware.

The result confirms a digital signal near the intended level and LINEIN selection
in the codec. It does not verify the I2S waveform, analog voltage, headphone/jack
compatibility or audible output, nor prove that leakage caused earlier faint
tones. No listening was requested, no route fix was applied, and no production
firmware was altered. Route correction is outside this diagnostic step.

## Corrected DAC route verification (2026-10-27)

Supersedes the earlier intentionally faulty-route diagnostic. Removed
`headphoneSelect(AUDIO_HEADPHONE_DAC)` and its return-value check/banner field.
Installed Teensyduino 1.62 `enable()` writes ANA_CTRL=0x0036 (SELECT_HP=0/DAC).
`muteLineout()` sets only bit8; headphone volume/unmute clear bit4 and preserve
bit6. Retain that route, with no selector workaround or library patch. Reduce
amplitude to 0.08 and headphone volume to 0.25 before upload; sequence, ramps,
initialization checks for remaining calls, line-out mute and production routing
are unchanged. Actual route/mute claims come only from successful readback;
failed ANA_CTRL reads explicitly report UNKNOWN for route and mute bits.

The isolated build/upload passed on COM4, serial 17681760, with headphones kept
disconnected for capture. Complete diagnostic output reports:

- ANA_CTRL=0x0126: SELECT_HP=0/DAC, MUTE_HP=0, MUTE_LO=1.
- HP_CTRL=0x6060: both sides -36.0dB; DAC_VOL=0x3C3C: both 0dB, unmuted.
- SSS=0x0010, clock=0x0004, I2S=0x0030, digital/analog power=0x0073/0x40FF.
- All six active windows peak at 0.079989; inactive channels and silent windows
  report NO_BLOCKS (I2S zero-fill), not measured zero.
- Eighteen register reads without transfer errors, all twelve 50..200ms windows,
  DIAG COMPLETE after three cycles, pool high-water 2/16.

Focused host checks passed for the four read-error cases, legitimate zero,
UNKNOWN route on failed read, retained enable route, reduced gain, one-time
reads, twelve reports and retained buffer bounds. The mock codec intentionally
has no selector method, so the actual source compiles without that API.
Captured-output checks confirm predicted registers/peaks; configuration-preservation,
production-source and diff whitespace checks passed. Serial port closed after
capture. Analog audio remains unverified until the user's listening test; start
with headphones off the ears. Older records above retain their original settings
and observations and are not descriptions of the current route.

## Corrected-route listening-level increase (2026-10-27)

Current amplitude is 0.20 (about -14 dBFS) and headphone volume is 0.50.
The corrected DAC route, line-out mute, stereo sequence, ramps and diagnostics
are unchanged. Historical sections above retain their original settings/results.

The isolated build/upload passed on COM4, serial 17681760. Complete capture:
ANA_CTRL=0x0126 / SELECT_HP=0 (DAC), MUTE_HP=0, MUTE_LO=1; HP_CTRL=0x4040
(-20.0dB per side), DAC_VOL=0x3C3C (0dB), both DAC channels unmuted.
All six active windows peaked at 0.200018; inactive/silent paths had NO_BLOCKS,
not measured zero. All eighteen register reads succeeded; twelve windows over
three cycles completed with pool high-water 2/16. Analog audio remains unverified.

Focused host checks passed for read errors, unknown route on failure, valid zero,
gain/route decode, one-time reads and bounded reporting. Source comparison proved
only the four requested value/banner substitutions; capture consistency and
`git diff --check` passed. Serial port closed. User confirmation of loudness and
stereo separation remains pending; keep headphones off the ears initially.

## Confirmed headphone listening result (2026-10-27)

Current source independently specifies amplitude 0.20 and codec headphone volume
0.50. The retained `diagnostics/audio-shield/corrected-level-serial.txt` capture
reports the same settings, active pre-I2S peaks 0.200018, HP_CTRL 0x4040 (-20dB),
DAC_VOL 0x3C3C (0dB), and ANA_CTRL 0x0126 (DAC selected, headphones unmuted,
line-out muted), with all twelve windows completed and no register-read errors.
These confirm configured values and digital/register observations, not analog
voltage or objectively measured noise, distortion or channel isolation.

The user subsequently confirmed that the lower tone plays only in the left
headphone channel, the higher tone only in the right, sound is clean with no
audible noise or clicks, and level increased compared with the faulty LINEIN
route. This supersedes pending-listening statements in the earlier run records.
No line-out, amplifier, production audio, older Shield or dual-Shield test is
established. No analog output voltage has been measured.

For an external amplifier, prefer the separate LINE OUT L/G/R contacts and a
properly wired stereo jack/cable; do not connect a grounded amplifier directly
to the headphone jack. See the [connection investigation](../../../docs/AUDIO_SHIELD_AMPLIFIER.md).

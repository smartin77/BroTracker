# Production Audio Shield headphone output

Implemented for review on `feature/audio-shield`, 2026-10-27. One Rev D2 SGTL5000
Shield on Teensy 4.1, no optional PSRAM/Shield memory, input or SD socket needed.
The user confirmed successful Windows BTX production headphone playback at volume
0.50. The new 0.80 image is build-only in this task; ArkOS hardware validation
remains open. The earlier
[isolated headphone observation](AUDIO_SHIELD_SMOKE_TEST.md) is separate evidence.

## Configuration and current default

The firmware default remains **USB_BRIDGE**, and the shared example remains
`output=bridge`. No default change is proposed. Both Windows and ArkOS BTX use
the same parser and executable-relative `config/config.ini`. To choose Shield
headphones explicitly, merge this field into the user's existing configuration:

```ini
[audio]
output=audio_shield
```

Accepted explicit lowercase values: `bridge`, `pt8211`, `audio_shield`. Whitespace
is trimmed. Empty/missing means no override: retain an active selection, otherwise
use the firmware default. Invalid nonempty values warn and use that same existing
no-override behavior; do not rely on invalid settings with attached hardware.
No setting is written automatically. `--safe-config` also uses existing defaults.

Rebuild both BTX and firmware for the new value; old firmware rejects the command
rather than selecting another module. Restart BTX to reload its config. A changed
device route or failed codec initialization requires a **Teensy restart**; changing
config/reconnecting/restarting BTX does not reconstruct the hardware driver.
No autoSTART, automatic module detection or priority hierarchy is added. Physically
attach **PT8211 or Shield, never both on the shared I2S pins**. All power must be
off before changing modules. Explicit bridge remains available with either module
installed, subject to the documented MQS/optional-SPI pin overlap.

The protocol extends the existing BTAUDIO1 choice, not its version:

```text
BTAUDIO1 SET 1 audio_shield
BTAUDIO1 SELECTED 1 audio_shield
```

SELECTED means the codec control calls and checked register state completed; it
does not prove the physical PCB identity, actual I2S/analog waveform or audibility.
Host START stays gated on acknowledgment. An unsupported/failed selection returns
an explicit BTAUDIO1 ERROR; there is no host retry/fallback during that session.
The host stores the full route name, including across reconnect discovery.
ArkOS bridge-only diagnostic mode rejects an explicit hardware route rather than
silently overriding it. This option selects a Teensy route, not a host sound card.

## Graph, gain and lifecycle

The existing engine produces **mono PCM**. `ProductionAudioOutput` connects source
port 0 to both I2S output ports 0/1, so both headphone channels carry the same audio.
This is not a new stereo engine or the isolated test's alternating tone graph.
The Shield route constructs AudioOutputI2S only; PT8211 and MQS are not constructed.
USB descriptors/serial/MIDI remain present, but Shield adds no source-to-USB PCM
patch. Existing PT8211 two-channel duplication and bridge MQS-plus-two-USB-patches
are retained. Existing device master volume and channel gains remain engine-side.

Hardware uses fixed placement storage large enough for one selected driver and
three patch objects. The codec object is static; only Shield selection calls it.
The existing **AudioMemory(8)** internal RAM2 pool is unchanged. Hardware is
constructed before the pool so it owns updates without a heap fallback timer.
No EXTMEM, heap allocation, new sample buffers or extra-memory dependency is added.

The original PRIMASK/BASEPRI activation assembly is preserved in the small
`production_audio_guard` boundary, allowing host tests of the actual graph without
substituting the graph itself. Hardware, patches and pool activate under that
bounded guard. The route is reserved for the boot. Interrupt masks are restored
**before** any I2C or `enable()` delays. Initialization runs in foreground serial
selection, not the audio ISR. Pattern audio remains gated by `g_audio_ready` until
success; transport rejects failed audio activation. Legacy START also checks the
selection result before starting its sample sequence.

Shield configuration: address LOW/0x0A, `enable()`, line-out mute, headphone
`volume(0.80)` (**-0.5dB per side**, register 0x1919), explicit headphone unmute.
The codec DAC remains 0dB (0x3C3C). This is a fixed hardware setting,
not a new INI volume parameter. It is higher than the successful isolated and
Windows production tests at 0.50; do not copy the smoke oscillator amplitude into
the engine. Keep headphones off the ears initially for a later 0.80 listening test.
Installed Teensyduino 1.62 `control_sgtl5000.h` converts volume using
`n * 129 + 0.499f`; `control_sgtl5000.cpp::volumeInteger()` then uses `128-n`.
For 0.80 this gives 103 then 25 (0x19) in each channel: 0x1919,
12 - 0.5 * 25 = -0.5dB. No engine signal level changes.

Installed Teensyduino 1.62's `headphoneSelect(AUDIO_HEADPHONE_DAC)` reverses SELECT_HP
relative to its name/comment. Production **does not call that method**. It retains
enable's SELECT_HP=0/DAC and checks readback. See [source and data-sheet evidence](AUDIO_SHIELD_SMOKE_TEST.md).
Seven one-time reads verify headphone source/mutes, headphone gain, DAC gain/mutes,
I2S-to-DAC routing, clock and I2S format. Pointer-write status, byte count and
missing-byte failures are explicit errors, never decoded as zero or success.
Reads do not identify the PCB and are not analog measurements.
The installed enable() boolean checks its first power write, not every later
initialization write. The seven readbacks strengthen specific route/gain/format
checks; they are not exhaustive power-state or analog-output verification.

Failures are sticky for that boot, with stage-specific errors:
`shield-enable-failed`, `shield-line-mute-failed`, `shield-volume-failed`,
`shield-unmute-failed`, `shield-readback-failed`, `shield-config-mismatch`.
Same/default requests return the original failure without reinitializing or
retrying. Another route returns `restart-required`. After enable succeeded,
failure attempts headphone/line mute as best effort; it does not claim mute
delivery. I2S may continue clocks/silence after failure, but no alternative driver
is instantiated and no playback is armed. There is no unsafe automatic fallback
to PT8211, MQS/bridge or an assumed attached module. STOP after unavailable audio
may also report audio-not-ready rather than an applied audio-owner acknowledgment.

## Pins and external connections

Rev D2 uses TX7, LRCLK20, BCLK21, MCLK23, SDA18/SCL19, 3.3V and GND. RX8 is not
used by this output graph. Pins7/20/21 overlap PT8211; the formats are different.
Shield optional SD/memory SPI nets overlap bridge MQS 10/12; optional peripherals
are not initialized by the Shield output. Teensy's existing built-in SDIO sample
preparation is independent and retained by the development environment.

Only headphone output is enabled. Its sleeve is **HP_VGND**, not system ground:
do not join it to GND or directly connect it to a grounded amplifier, mixer or PC.
The [separate line-output connection investigation](AUDIO_SHIELD_AMPLIFIER.md)
remains preparation only; line-out stays muted. No analog input, codec DSP effect,
dual-adaptor, older Shield, arbitrary I2C bus or shared-module support is added.

## Review-first manual headphone check

The earlier 0.50 image was uploaded; the new 0.80 image has not been uploaded.
For a separately authorized later user listening test:

1. Keep the single Rev D2 attached, PT8211 absent, headphones disconnected during
   upload. Build the normal `teensy41_dev` image; use rebuilt BTX with explicit
   `output=audio_shield`. Do not launch another control client on the serial port.
2. Boot/restart Teensy into the reviewed production image. Confirm configuration
   logging and `BTAUDIO1 SELECTED 1 audio_shield`, with no Shield setup errors.
   A handshake/ACK alone is not audible success. START should not be automatic.
3. With headphones initially off the ears, explicitly START a known pattern and
   check that the **same mono program** reaches both channels. Record subjective
   level, noise/clicks, STOP/pause behavior and sample/Synth content separately.
4. Reconnect BTX with Teensy still powered: same route must acknowledge without
   codec reinitialization or automatic START. Test physical resets separately;
   existing startup-pattern/recovery policy is unchanged.
5. Failure injection/absent-Shield testing, if later authorized, requires all power
   off before hardware changes. Expect explicit initialization/readback failure,
   no START and no fallback. Reboot after correcting hardware. No grounded analog
   measurements or amplifier connection are part of this check.

## Validation and limits

- Builds passed: `teensy41_dev`, `teensy41_pattern`, legacy `teensy41`, isolated
  `teensy41_pt8211_test` and `teensy41_audio_shield_test` using Teensy platform 5.2.0,
  framework 1.162.0 / Teensyduino 1.62 and GCC 15.2.1. Existing unrelated development
  filename indentation warning remains. A pre-existing legacy include-scope issue
  was fixed by keeping the pattern-only headers inside their feature guard.
- All **7/7 host CTest suites passed**. The new ProductionAudioTests compile the
  actual production graph/lifecycle with fake PJRC objects/bus and a substituted
  interrupt boundary. They cover each route's exact patches, no other hardware
  constructors, unchanged default, idempotent reconnect, conservative gain,
  foreground-only control, five read-error modes, seven readback mismatch cases,
  each control-call failure and sticky no-retry/no-fallback behavior.
- Host config/protocol tests cover audio_shield, full name retention, default
  acknowledgment, reconnect without START, error gating, and the existing routes.
  The example fits the 4096-byte parser bound even with CRLF.
- Windows BTX built successfully in `diagnostics/audio-shield/windows-build`;
  it was not run. The Eoan ARM64 build and **23/23 CTest suites passed**, including
  the new production-route tests. ABI/dependency checks passed. Package verification
  caught CRLF template bytes; the template was normalized to LF and final package
  verification passed: template equality, manifest/source hashes, ARM64 identities
  and isolated installer/user-config preservation checks. Local deploy/arkos
  artifacts were refreshed by tools/arkos/prepare-package.sh; existing user config
  was not touched.
- Development image: flash code 142160 + data 40956 + headers 8368 = 191484 bytes;
  RAM1 variables 52640 + code 138824 + padding 25016, with 307808 bytes local headroom;
  RAM2 variables 296480 bytes. External-RAM section **0 bytes**. These are static
  link figures, not measured realtime/stack performance. Sample storage is the
  unchanged existing development workload, not new Shield memory.

Windows production headphone playback at 0.50 was confirmed by the user.
ArkOS hardware validation, 0.80 listening, failure/backpressure/reconnect under
hardware load, electrical levels and timing remain unverified. Production firmware upload and
stopped codec selection were performed; no START, pattern storage command or
line-out/amplifier test was performed. Local packages were prepared; no ArkOS
installation was performed on the connected console.

## Production volume 0.50 validation (2026-10-27)

Raised only the fixed production headphone volume from 0.25 to 0.50 and its
readback expectation from 0x6060 to 0x4040 (-20dB both channels). Updated the
focused tests, example comments and package copies. DAC selection, line-out mute,
initialization/error policy, PT8211/bridge routes and engine levels are unchanged.

Validation passed:

- Focused production-route test target, including gain/readback, mutual exclusion
  and sticky failures; **10/10 Windows package CTest suites**, **23/23 Eoan ARM64
  suites**, ABI/dependency, hashes and installer/config-preservation checks.
- Production `teensy41_dev` built/uploaded on COM4, serial 17681760. Only HELLO and
  `BTAUDIO1 SET 1 audio_shield` were sent after upload. Capture received IDLE and
  `BTAUDIO1 SELECTED 1 audio_shield`, followed by stopped position reports. This
  firmware acknowledgment follows successful checks of HP gain 0x4040, DAC route,
  HP unmute and line-out mute. It is **not a raw-register dump or an audible test**.
- Both package templates match `config/config.ini.example`. Existing Windows,
  ArkOS and manual-testing user configurations were checked for preservation.
  Serial capture was closed; no playback was started.

Artifacts (repository-relative):

- Firmware: `.pio/build/teensy41_dev/firmware.hex` and `firmware.elf` in that directory.
- Windows runnable package: `deploy/windows/BroTrackerTerminal.exe` with DLLs/assets;
  built through `tools/windows/build-package.ps1` using
  `diagnostics/audio-shield/windows-build`.
- ArkOS runnable package: `deploy/arkos/BroTrackerTerminal`, `BroTrackerAlsaBridge`,
  `launch.sh`, assets and `SHA256SUMS`; built/verified by
  `tools/arkos/prepare-package.sh` in the existing Eoan root.
- Control capture: `diagnostics/audio-shield/production-volume050-serial.txt`.

Select with `[audio] output=audio_shield` in the existing executable-relative
`config/config.ini`; restart BTX after changing it. Do not replace a user config
with the template. The default remains bridge. Audio remains mono on both
headphone channels. Keep headphones off the ears initially for user listening;
no audible loudness or stereo-separation result is claimed by these checks.

## Windows listening observation and production volume 0.80 (2026-10-27)

The user reports successful Windows BTX production playback through the Audio
Shield headphone output at fixed codec volume 0.50. The preceding source and
control capture establish that configuration and successful checked selection;
the listening result is the user's observation, not an analog measurement.
No measured distortion, noise floor, line-out, amplifier or ArkOS result follows.
The earlier isolated left/right tones are separate from production mono playback.

Current source raises only the fixed headphone setting to 0.80 and expected
CHIP_ANA_HP_CTRL to 0x1919. DAC route, line-out mute, failure handling, engine
levels and PT8211/bridge behavior are unchanged. No firmware upload or package
installation is performed for this change. New 0.80 audibility remains untested.

The front-panel connector idea is a future investigation recorded in
[AUDIO_SHIELD_AMPLIFIER.md](AUDIO_SHIELD_AMPLIFIER.md#future-front-panel-sourcing-and-wiring-investigation).

Validation for the 0.80 change: production `teensy41_dev` build passed; 7/7
native host suites, 10/10 Windows package suites and 23/23 Eoan ARM64 suites
passed. Both BTX packages were rebuilt through the existing packaging scripts.
ABI/dependency checks, package hashes/template equality and isolated temporary
installer/config-preservation checks passed. Existing user config hashes were
unchanged (including the absent ArkOS user config remaining absent). No package
was installed onto hardware. Full logs and diffs are in
`diagnostics/audio-shield/production080-*`; artifacts use the paths listed above.

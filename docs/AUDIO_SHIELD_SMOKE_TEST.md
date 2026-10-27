# Audio Shield smoke test and integration preparation

Scope: isolated stereo output on Teensy 4.1 without optional memory, prioritizing
the fixed-address **Rev D2** SGTL5000 board. See the
[build and manual listening procedure](../firmware/teensy/audio_shield_test/README.md).
The user confirmed clean lower-tone-left / higher-tone-right headphone output,
without audible noise/clicks, at current amplitude 0.20 / headphone volume 0.50,
with higher level than the faulty LINEIN route. This is an isolated headphone
listening observation; line-out, amplifier, production audio, older revisions and
dual-board configurations remain untested. This isolated environment remains
separate from the new [production headphone route](AUDIO_SHIELD_OUTPUT.md); its
production hardware validation is still pending. See [external amplifier preparation](AUDIO_SHIELD_AMPLIFIER.md).

## Pin allocation and conflicts

Directions are relative to Teensy. The following is derived from the PJRC Rev
D/D2 schematics and installed Teensyduino 1.62 `output_i2s.cpp` IMXRT1062 branch.

| Signal | Teensy 4.1 pin | Test use / conflict |
| --- | --- | --- |
| MCLK | 23, output | I2S master clock, approximately 11.2896 MHz |
| BCLK | 21, output | I2S bit clock, approximately 2.8224 MHz (64 Fs) |
| LRCLK | 20, output | Stereo frame clock, approximately 44.1 kHz |
| Shield DIN | 7, output | Stereo data from Teensy; output ports 0/1 are L/R |
| Shield DOUT | 8, input if enabled | Wired on the Shield but not used by this output-only graph |
| SDA / SCL | 18 / 19 | Wire I2C codec control; other devices need compatible addresses/loading |
| Power | 3.3V / GND | Shield supply and common digital/system ground |
| Optional volume | 15 / A1 | Not read; no potentiometer required |
| Shield SD | CS10, MOSI11, MISO12, SCK13 | Not initialized; separate from Teensy 4.1 built-in SDIO socket |
| Shield SPI memory | CS6, MOSI11, MISO12, SCK13 | Not populated/initialized by this test |

PT8211 uses pins 7/20/21 too, but a different wire protocol. Treat modules as
alternative physical configurations, power down to swap, and do not instantiate
both output drivers on SAI1. The production bridge uses MQS pins 10/12, which also
overlap the Shield's optional SD/SPI connections; this test does not instantiate
MQS or touch those pins. Existing LED13 use can overlap Shield SPI SCK if that
optional bus is enabled later; this test does not drive the LED. No other
application pin drivers are compiled into this environment. External wiring not
described in the repository still needs a physical pin audit; connected hardware
cannot be established from a successful build.

Production `ProductionAudioOutput::Select` remains unchanged: PT8211, or MQS as
hardware update owner plus USB PCM for bridge, with fixed storage and existing
route retention. The default remains `teensy41_dev`; this isolated environment
does not inherit it. No host negotiation or new output selection is added.

## Initialization and detection limits

The installed `AudioControlSGTL5000` defaults to 0x0A; `setAddress(LOW/HIGH)` selects
**0x0A/0x2A**, not an arbitrary address and not a physical address change. The
test explicitly selects LOW. In the normal Teensy 4.1 slave-codec path,
`enable()` checks the first CHIP_ANA_POWER write, returns false if it fails, then
does many unchecked register writes and returns true. CHIP_ID reading is
commented out. A true result is not complete register validation, codec identity
verification, correct clocks, or working DAC/headphone output.

The test reports that limited return value separately from the subsequent
headphone-routing, headphone-volume, headphone-unmute and line-mute bools.
`volume()` itself does not propagate its internal unmute result, so the test also
calls `unmuteHeadphone()` explicitly. Any false checked result disables tone
gating; mute cleanup is best effort, not a confirmed safe-state acknowledgment.
Successful I2C transactions do not identify the PCB revision or distinguish
multiple same-address devices. No bus scanner, presence auto-selection, chip-ID
claim or audio-feedback measurement is implemented. Listening is still required.

Headphones and line-out share the DAC stream but have separate analog controls.
This test configures headphones and mutes line-out. HP_VGND must not be joined to
system ground; the board schematics show the jack sleeve connected to HPVGND.
Use the actual line-out header for any later grounded amplifier/capture test.
References: [codec API implementation][control], [header][control-header],
[SGTL5000 data sheet, pin definitions p.3 and serial interface][datasheet].

## Older revisions and dual-adaptor preparation

| Revision | Single-board compatibility | Address/dual implications |
| --- | --- | --- |
| D2 (20-pin SGTL5000) | Teensy 4.0/4.1 layout; priority here | Fixed 0x0A; no address-selection pads. Two D2 boards on the same unmodified Wire bus cannot be independently controlled. |
| D (32-pin SGTL5000) | Teensy 4.x layout | Default 0x0A; cut/rebridge address pads to select 0x2A for a second codec. Single-board default-address operation is source-compatible, physically untested here. |
| B/C | Teensy 3.x layout, not direct Teensy 4.1 stacking | Address/data modification pads exist; requires short rewiring or a suitable adaptor to Teensy 4.x pin mapping. C adds MCLK series resistance. |
| A | Legacy Teensy 3.x layout | Predates B's added address/data solder pads; do not assume the documented dual-board jumper procedure applies. |

For a wired B/C adaptor on Teensy 4.1, map signals by name, not header position:
Shield 3.x MCLK11 -> Teensy23, BCLK9 -> 21, LRCLK23 -> 20, DIN22 -> 7,
DOUT13 -> 8 (if input is needed); SDA18/SCL19 stay the same. Optional 3.x
SPI MOSI7/SCK14 need Teensy11/13; MISO12 and CS10/6 keep their numbers.
These mappings do not constitute tested compatibility or a recommended new build.
Revision/pad differences follow [PJRC's revision history][shield] and
[D2][d2-schematic], [D][d-schematic], [C][c-schematic] schematics.

PJRC's dual example uses **AudioOutputI2SQuad**, two SGTL5000 controls at LOW/HIGH,
and two stereo data lanes sharing MCLK23/BCLK21/LRCLK20:

| Audio path on Teensy 4.1 | First codec | Second codec |
| --- | --- | --- |
| Output, I2SQuad ports 0/1 and 2/3 | Teensy7 -> DIN | Teensy32 -> DIN |
| Optional quad input | DOUT -> Teensy8 | DOUT -> Teensy6 |
| Control | SDA18/SCL19, 0x0A | Same bus, hardware-addressed 0x2A |

Second-board DIN must be isolated from its normal pin7 trace and rewired to 32;
for quad input, isolate its normal pin8 DOUT trace and reroute to 6 so codec
outputs are never tied together. Share supply/ground and keep clocks/data short.
The older Rev D modification discussion also addresses removing pin6 memory-CS
pull-ups when that pin becomes audio input. Do not use optional SPI memory CS6
at the same time. Two stacked SD sockets also share CS10: independently accessing
both needs separate CS wiring; using the Teensy built-in SDIO socket avoids
that particular shared-CS arrangement. None of these modifications is made here.

Primary evidence is the installed [QuadChannelOutput example][quad-example],
the Teensy 4 branches in [output_i2s_quad.cpp][quad-out] and
[input_i2s_quad.cpp][quad-in], and
[Paul Stoffregen's actual Rev D/Teensy 4.1 wiring report, posts 20/21][quad-forum].
The old SparkFun tutorial linked by PJRC returned HTTP 403 during research;
its Teensy 3.x pin numbers must not be copied to Teensy 4.1. The source driver
and PJRC author's Teensy 4.1 report establish the mapping above.

One D2 at 0x0A plus an address-modified D at 0x2A is an architectural candidate,
not verified BroTracker support. Two D2 boards need a different independently
controlled I2C arrangement (such as bus isolation/multiplexing) and driver work:
this control class hardcodes `Wire`. Merely constructing two objects or calling
`setAddress(HIGH)` cannot readdress D2 hardware. No such design is implemented.

Quad I2S is **not TDM**: it sends two stereo frames on separate data pins.
PJRC's [TDM driver][tdm] instead emits a 256-bit multichannel frame for compatible
codecs such as CS42448. SGTL5000's supported serial modes are I2S/left/right
justified and PCM; the existing SGTL configuration does not select arbitrary
TDM slots. Do not substitute AudioOutputTDM or promise dual-D2 addressing via TDM.
Both approaches consume SAI resources; they cannot be mixed with the current
PT8211/I2S drivers on the same peripheral without a separately reviewed design.

## Optional memory and future flow

Baseline output uses internal RAM only. Teensy 4.1 underside QSPI expansion pads
are distinct from the Shield's optional SPI memory footprint. The Teensy can
accept one or two 8 MiB PSRAM chips (one belongs on the smaller pads), or supported
flash on the larger footprint. RAM1 is TCM for hot state/code; RAM2 holds DMA
buffers. EXTMEM refers to optional Teensy QSPI PSRAM, not Shield SPI RAM.
See [Teensy memory documentation][teensy41], [PSRAM hardware][psram] and the
existing [BroTracker memory policy](TEENSY_MEMORY_ARCHITECTURE.md).

Future QSPI PSRAM could retain larger samples/wavetables, expand bounded refill
caches, or hold long delay/reverb histories. Preserve internal audio buffers,
hot DSP/voice state, fixed ownership and foreground SD refill. Measure cache
misses, bandwidth, DMA reachability/coherency and worst-case combined traffic
before making throughput promises. More memory is not more CPU or higher codec
resolution; optional partitions/budgets remain a separate design decision.

Shield W25Q128 flash is persistent SPI clip storage (SerialFlash playback); its
alternative 23LC1024 is 128 KiB SPI SRAM, usable by the Audio Library's external
delay object. Neither is Teensy's memory-mapped PSRAM or automatically enlarges
AudioMemory. Foreground staging/caching may improve future flow, but SPI latency,
bus sharing and bounded transfers need measurement. Do not introduce blocking
filesystem/SPI work into BroTracker's realtime render path or require either chip
for baseline stereo output. Relevant objects:
[play_serialflash_raw.cpp][flash-player], [effect_delay_ext.cpp][external-delay].

## Validation

Verified 2026-10-27 on `feature/audio-shield`:

- `pio run -e teensy41_audio_shield_test -e teensy41_pt8211_test -e teensy41_dev`:
  all three passed with Teensy platform 5.2.0, framework 1.162.0 and GCC 15.2.1.
  The production build retains an existing misleading-indentation warning in
  `src/bringup/development_pattern_selection.h:41`; no production fix is included.
- Rebuilt all six configured host test targets and ran
  `ctest --test-dir build --output-on-failure`: **6/6 passed**. This includes
  pattern/preview/publication, output-selection and storage regression coverage;
  it does not simulate or validate analog Shield behavior.
- Source/config isolation check: default still `teensy41_dev`, new environment
  has no inheritance, selects only its sibling `main.cpp`, USB_SERIAL only;
  production and existing PT8211 sources have no diff. Selector Python syntax
  and `git diff --check` passed.
- ELF symbols contain no production routing, AudioOutputPT8211, AudioOutputMQS,
  AudioOutputUSB or AudioInputI2S objects; `.bss.extram` is **0 bytes**.
  Audio pool `setup::data` is **4160 bytes at 0x20200000** (16 blocks including
  metadata); I2S DMA TX buffer is **512 bytes at 0x20201040**. Both are internal
  RAM2. The fixed startup text buffer is 1024 bytes in RAM1.

The Shield image's linker size report (bytes):

| Region | Usage | Remaining as reported by teensy_size |
| --- | --- | --- |
| Flash | code 45600 + data 9400 + headers 8484 = **63484** | 8062980 for files |
| RAM1 | variables 12032 + code 42280 + padding 23256 = **77568** | 446720 for local variables |
| RAM2 | variables **17088** | 507200 for heap headroom (not an allocation requirement) |

These are static link figures, not measured stack, realtime CPU or pool maxima.
Initial validation did not upload firmware or perform listening/electrical tests.
A later [listening-level follow-up](../firmware/teensy/audio_shield_test/README.md#listening-level-follow-up-2026-10-27)
built/uploaded the isolated image and captured true control-call results. The user subsequently confirmed L/R separation but very quiet output at those
previous settings. The [higher-level follow-up](../firmware/teensy/audio_shield_test/README.md#higher-level-follow-up-2026-10-27)
sets amplitude/headphone volume to 0.80/0.80; new loudness still requires listening.
Older-board and dual-board behavior remain unverified.

The subsequent [route/peak diagnostic](../firmware/teensy/audio_shield_test/README.md#route-and-peak-diagnostic-2026-10-27)
built/uploaded an instrumented isolated image without changing levels or routing.
Readback confirmed `CHIP_ANA_CTRL=0x0166` / LINEIN, despite the call named DAC
selection; pre-I2S active peaks were approximately 0.80 over three cycles. The
suspected route remains deliberately uncorrected. Register and digital-peak
observations establish neither analog output voltage nor headphone compatibility.

## Primary references

Research checked 2026-10-27 against PJRC pages, schematics, the installed framework
(`framework-arduinoteensy` 1.162.0 / Teensyduino 1.62), its Audio objects/examples,
and the linked upstream sources. Upstream master can change; the installed code
is the code used for the build.

- [PJRC Audio Library overview](https://www.pjrc.com/teensy/td_libs_Audio.html),
  [connections/memory](https://www.pjrc.com/teensy/td_libs_AudioConnection.html),
  [processor/interrupts](https://www.pjrc.com/teensy/td_libs_AudioProcessorUsage.html).
- [Design Tool object reference](https://www.pjrc.com/teensy/gui/):
  AudioOutputI2S, AudioControlSGTL5000, AudioOutputI2SQuad, AudioInputI2SQuad,
  AudioOutputTDM, sine and envelope; [underlying help source][gui].
- [I2S output implementation][i2s], [AudioStream pool/update ownership][stream].

[shield]: https://www.pjrc.com/store/teensy3_audio.html
[teensy41]: https://www.pjrc.com/store/teensy41.html#audio
[psram]: https://www.pjrc.com/store/psram.html
[datasheet]: https://www.pjrc.com/teensy/SGTL5000.pdf
[d2-schematic]: https://www.pjrc.com/store/schematic_audio4b.png
[d-schematic]: https://www.pjrc.com/store/schematic_audio4.png
[c-schematic]: https://www.pjrc.com/store/schematic_audio3.png
[control]: https://github.com/PaulStoffregen/Audio/blob/master/control_sgtl5000.cpp#L499-L591
[control-header]: https://github.com/PaulStoffregen/Audio/blob/master/control_sgtl5000.h
[i2s]: https://github.com/PaulStoffregen/Audio/blob/master/output_i2s.cpp
[quad-out]: https://github.com/PaulStoffregen/Audio/blob/master/output_i2s_quad.cpp#L84-L104
[quad-in]: https://github.com/PaulStoffregen/Audio/blob/master/input_i2s_quad.cpp#L76-L103
[quad-example]: https://github.com/PaulStoffregen/Audio/blob/master/examples/HardwareTesting/SGTL5000/QuadChannelOutput/QuadChannelOutput.ino
[quad-forum]: https://forum.pjrc.com/index.php?threads/61123/#post-242057
[tdm]: https://github.com/PaulStoffregen/Audio/blob/master/output_tdm.cpp
[gui]: https://github.com/PaulStoffregen/Audio/blob/master/gui/index.html
[stream]: https://github.com/PaulStoffregen/cores/blob/master/teensy4/AudioStream.cpp#L64-L99
[flash-player]: https://github.com/PaulStoffregen/Audio/blob/master/play_serialflash_raw.cpp
[external-delay]: https://github.com/PaulStoffregen/Audio/blob/master/effect_delay_ext.cpp

## Corrected-route follow-up (2026-10-27)

The [corrected DAC-route verification](../firmware/teensy/audio_shield_test/README.md#corrected-dac-route-verification-2026-10-27)
removes the installed library's faulty selector call, retaining enable's route.
Amplitude/headphone volume were lowered to 0.08/0.25 before isolated build/upload.
Register readback now confirms 0x0126: DAC selected, headphones unmuted, line-out
muted. HP gain is -36dB, DAC gain 0dB; all active peaks are 0.079989 over three
cycles. Failed route reads report UNKNOWN. Analog output remains unverified;
prior diagnostic sections above describe the historical LINEIN configuration.

## Current corrected-route listening settings (2026-10-27)

A subsequent [listening-level increase](../firmware/teensy/audio_shield_test/README.md#corrected-route-listening-level-increase-2026-10-27)
sets current amplitude/headphone volume to 0.20/0.50. Isolated build/upload and
focused checks passed. Readback retained DAC route (SELECT_HP=0), headphones
unmuted and line-out muted (ANA_CTRL=0x0126). HP gain is -20dB, DAC gain 0dB;
active peaks were 0.200018 across all six active windows. The complete twelve-window
capture finished without register errors. The user subsequently confirmed clean separated headphone tones and increased
level versus LINEIN bypass; see the confirmed result below. Earlier sections are
historical validation records.

## Confirmed headphone result (2026-10-27)

The actual current source specifies amplitude 0.20 and headphone volume 0.50;
`diagnostics/audio-shield/corrected-level-serial.txt` independently records those
values, pre-I2S peak 0.200018, HP_CTRL 0x4040, DAC_VOL 0x3C3C and ANA_CTRL 0x0126
(SELECT_HP=0, MUTE_HP=0, MUTE_LO=1). The user's listening report establishes
lower tone only left, higher tone only right, clean sound with no audible noise
or clicks, and increased level compared with the faulty LINEIN route. It does not
measure voltage, distortion, noise floor, or isolation. No line-out/amplifier,
production, older/dual-Shield test occurred. See [connection options and limits](AUDIO_SHIELD_AMPLIFIER.md).

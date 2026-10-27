# Rev D2 output to an external amplifier

Documentation/investigation only, 2026-10-27. No firmware or hardware changes,
upload, new capture or amplifier operation was performed for this investigation.

## Evidence and scope

Reviewed the uncommitted isolated environment, firmware, documentation and the
existing `diagnostics/audio-shield/corrected-level-serial.txt` capture. Current
source amplitude is **0.20**, headphone volume **0.50**. The capture separately
records these settings, pre-I2S active peaks **0.200018**, headphone register
**0x4040** (-20dB both sides), DAC register **0x3C3C** (0dB), and analog control
**0x0126**: DAC selected, headphones unmuted, **line-out muted**.

The user confirmed lower tone only left, higher tone only right, clean sound
without audible noise or clicks, and increased level compared with the faulty
LINEIN route. This is a successful isolated headphone routing/listening check.
It is not a measured analog level, frequency, noise floor, distortion or crosstalk
specification. No line-out, external amplifier, production audio, older revision
or dual-Shield result follows from it. The latest listening report did not
independently restate the headphone model; do not broaden the earlier headset
observations into a connector-compatibility claim.

## Headphone jack versus line output

Rev D2's codec pin2 HPVGND feeds the headphone jack sleeve; codec pins4/1 feed
headphone left/right. HPVGND is an actively maintained non-zero virtual ground,
not system GND. The data sheet explicitly forbids grounding it, even unused.
An ordinary stereo cable into equipment whose sleeve is grounded can join it to
Teensy GND through USB, a PC, mixer, supply or chassis. That can load/short the
common driver and cause incorrect operation or damage; this is not merely a
ground-loop-noise concern. Floating battery-powered equipment is not a general
exception: charging, USB and other cables may change its grounding. Never defeat
protective earth or tie HPVGND to system GND. [Codec pin definitions, p.3][codec]
and [official Rev D2 schematic][schematic].

The separate LINEOUT signals are intended for high-impedance line inputs. The
board schematic includes **2.2uF coupling capacitors** between codec LINEOUT pins
and the line header. Use the header side of those capacitors, not bare codec pins.
Line-output ground is system/analog ground, not HPVGND. Normal line-level
ground-loop noise may still need investigation after selecting proper wiring.
References: [schematic][schematic], [data sheet electrical characteristics p.6
and line-output description pp.15-16][codec].

## Exactly which board contacts

Use PJRC's [Rev D2 back-side photograph][back] with its text upright: the analog
header area is at the top. The three adjacent contacts labelled **LINE OUT L G R**
are the output group; the nearby **LINE IN R L** contacts are inputs. This is a
back-side view: positions reverse when viewing the opposite face. Select contacts
by their silk labels and schematic nets, not an assumed photograph orientation.

| Rev D2 contact | Schematic origin | Proposed ordinary stereo TRS jack |
| --- | --- | --- |
| LINE OUT L | SGTL5000 pin7 LINEOUTL, through 2.2uF capacitor | Tip |
| LINE OUT R | SGTL5000 pin6 LINEOUTR, through 2.2uF capacitor | Ring |
| LINE OUT G | Board ground at analog header | Sleeve |

**Do not use the L/R/VGND pads beside the headphone jack.** Their ground-like
contact is the virtual-ground node. The board has no second factory-fitted
3.5mm line-output jack in the [official front photo][front]. Its analog header
footprint is shown unpopulated. PJRC describes an AC97-compatible line-in/out
header and allows direct soldered wires. That does not prove the user's board is
unpopulated or accessible when stacked. A header, soldered wires, or an already
installed compatible connector is needed to bring those nets to a cable; an
ordinary plug cannot access bare plated holes. [PJRC board documentation,
Signals to Teensy / line-in-out header][board].

## Connection proposals, not tested arrangements

**Preferred:** solder a suitable header into the line-output footprint (if absent)
and use a matching plug/cable to a panel-mounted stereo 3.5mm TRS socket. Wire
L->tip, R->ring, G->sleeve. A socket breakout with solder terminals or screws can
simplify the enclosure end, but does not eliminate the Shield-side connection
work. Verify the socket data sheet: switched sockets have extra contacts.
Provide insulation, short shielded audio wiring and cable strain relief.

Alternatively, solder a short stereo cable/pigtail directly to those three board
contacts, ending in a 3.5mm TRS plug or female socket. Direct wires are compact
but less serviceable. Existing fitted headers may allow a solder-free removable
harness; generic loose jumpers are poor permanent strain relief. Do not modify
the factory headphone jack or bypass the onboard coupling capacitors.

This mapping assumes a **stereo unbalanced AUX/LINE input**. A 3.5mm connector
can instead be mono balanced, microphone, TRRS or carry plug-in power. A mixer
with two mono inputs needs the appropriate stereo breakout, not a stereo signal
inserted into one balanced TRS input. An amplifier requiring power-level speaker
drive or a passive speaker cannot be driven directly from LINEOUT.

**Conditional alternative:** a verified stereo transformer audio isolator between
the headphone jack and a high-impedance amplifier input may avoid soldering.
Its two primary circuits must reference only the headphone-side tip/ring/sleeve;
the secondary and its ground must be galvanically isolated from that sleeve,
USB/system ground and any conductive enclosure connection. Both channels and
all shields/grounds must be checked. A device advertised as a noise suppressor,
TRRS adaptor or capacitor-coupled cable is not proof of this topology: shared
sleeves can still short HPVGND. A powered/USB isolator may introduce another path.

PJRC's [audio ground isolator description][isolator] specifies magnetic coupling,
DC ground-current blocking and high-impedance line-input use. That supports the
principle, not certification of an arbitrary product or the user's wiring. Verify
the particular device's schematic/specification and disconnected continuity
before selecting it; continuity alone does not establish signal/DC ratings or
performance. Transformer loading, insertion loss, bass response, distortion,
headphone control level and residual DC across a primary must be suitable. Do
not attach headphones/speakers to an isolator intended for line-input loads.
This alternative has not been tested here and is not automatically safe merely
because the product has two jacks. Prefer the Shield's dedicated LINEOUT path.

## Software for a later line-out smoke test

No change is made now: current firmware deliberately mutes line-out.
For a separately implemented line-output test, retain normal `enable()` and the
verified I2S->DAC path, initialize quietly, and explicitly configure line level
before `unmuteLineout()`. Consider muting headphones for that line-only test.
Retain conservative tones/ramps and verify register readback before listening.

- `volume(0.50)` changes headphone analog gain only. It does **not** set line-out
  amplitude. `dacVolume()` is digital attenuation upstream of both outputs.
- `lineOutLevel(n)` adjusts line-output swing. The installed library clamps
  **13..31**; a larger number is a **lower** output, unlike headphone volume.
  Proposed conservative initial setting: **31**, subject to amplifier sensitivity.
  Library reference values are about 1.16Vpp at 31 and 1.29Vpp at default 29 for a
  full-scale sine. With amplitude 0.20 and 0dB DAC gain, the 31 estimate is about
  0.232Vpp/82mVrms. These are engineering estimates, not measured board voltage
  or guaranteed loudness. Do not confuse the headphone's -20dB with line gain.
- Check `unmuteLineout()`'s return and readback: ANA_CTRL bit8 MUTE_LO must clear;
  SELECT_HP bit6 remains 0 for the retained headphone DAC route. If headphones
  are deliberately muted, bit4 must be 1. Verify DAC mute bits clear and DAC_VOL
  remains 0x3C3C if retaining the current digital gain.
- For level 31, `CHIP_LINE_OUT_VOL & 0x1F1F` should be 0x1F1F; current capture has
  0x1D1D (29) while muted. `lineOutLevel()` returns a register-valued result,
  not comprehensive verification; its internal read/modify path does not expose
  every transfer error. Preserve explicit error-aware readback.

Implementation checked against installed Teensyduino 1.62 `control_sgtl5000.h`
(volume, line mute) and `.cpp` (enable, lineOutLevel, dacVolume). Upstream
[control implementation lineOutLevel section][line-code] and
[header mute methods][mute-code] provide precise corresponding references;
[Design Tool SGTL5000 control help][help] documents the separate output controls.
Upstream master may differ from the installed version.

## Information required before selecting the connector

Need the amplifier make/model and exact input label/pinout/sensitivity; whether
it is stereo AUX/LINE, balanced, microphone or something else; its power supply,
earth and USB/PC connections; and the complete planned Teensy power/USB path.
Also need clear views of the actual Shield's LINE OUT area, headers and stacking
clearance, any existing modifications, and whether the user prefers soldered
pigtail versus a detachable panel jack. If using an isolator, need its exact
model and documented topology/ratings. The official photo cannot establish the
user's physical assembly. Do not make or test connections on the basis of an
unverified header orientation or assumed amplifier ground.

[board]: https://www.pjrc.com/store/teensy3_audio.html
[schematic]: https://www.pjrc.com/store/schematic_audio4b.png
[back]: https://www.pjrc.com/store/teensy4b_audio_back.jpg
[front]: https://www.pjrc.com/store/teensy4b_audio_front.jpg
[codec]: https://www.pjrc.com/teensy/SGTL5000.pdf
[isolator]: https://www.pjrc.com/store/audio_ground_isolator.html
[line-code]: https://github.com/PaulStoffregen/Audio/blob/master/control_sgtl5000.cpp#L664-L698
[mute-code]: https://github.com/PaulStoffregen/Audio/blob/master/control_sgtl5000.h#L45-L50
[help]: https://www.pjrc.com/teensy/gui/?info=AudioControlSGTL5000

## Future front-panel sourcing and wiring investigation

Future task only: investigate a suitable PCB-mount male AC'97-compatible header
for the actual Rev D2 Shield and a PC front panel with two audio jacks and two
USB ports. The intended convenience is line input/output and USB connections;
this is not a tested design, selected product or instruction to buy hardware.

Before proposing any adapter or connection:

- Inspect the actual Shield contacts and proposed male header: verify pinout,
  pitch, row/connector spacing, orientation, keying, PCB fit and soldering needs
  against the official schematic and the part's mechanical drawing.
- Verify the actual panel's AC'97 wiring, sleeve/return connections, jack
  functions and any switching/detection contacts. Two jacks may serve headphones
  and microphone rather than LINE IN/LINE OUT. HD Audio wiring is not assumed
  equivalent. Establish whether a separate LINEOUT jack is more appropriate.
- The existing Teensy USB-host connection is a **SINGLE-ROW header**. A typical
  double-row PC USB plug is not assumed directly compatible. Verify both ends'
  pinout, voltages, data wiring, connector pitch/spacing, keying, ground/shield
  connections, VBUS direction, available current, protection and power/backfeed
  requirements before designing an adapter. Determine how two panel USB ports
  would be supported, including whether an active hub is needed; do not assume
  passive splitting or direct mechanical compatibility.
- Obtain the actual panel model/photos, wiring diagram and connector dimensions,
  plus amplifier input/grounding and intended USB power connections. Until those
  are verified, no wiring or adapter recommendation is validated.

Never connect headphone HP_VGND to system ground. This investigation does not
change the currently muted production LINEOUT or enable audio input/USB-host
features. Windows production headphone playback at volume 0.50 was successful
according to the user; this supplies no evidence about panel, line-out or amplifier
operation, measured distortion/noise floor, or ArkOS hardware behavior.

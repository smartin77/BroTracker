# BroTracker general FX design and MOD/XM import

Design proposal for native BroTracker FX. Amiga ProTracker MOD and FastTracker II XM provide classic tracker semantics and import references. Dirtywave M8 provides a second source of inspiration for layered FX, parameter changes and performance sequencing. Native authoring and musical behavior take priority. Imports should preserve playable behavior wherever practical and report approximations. Reverse export is outside this design. All proposed BroTracker mnemonics are provisional.

Each FX mnemonic has exactly three meaningful characters, with no padding or four-character exceptions. Its compact value has exactly two hexadecimal digits. The gap in `VOL 40` is UI presentation only. Internal IDs, operand storage, and command-specific units and ranges remain open. A command can have a detail editor for values such as fractional tempo. Three general-purpose FX slots per position are the current baseline. Native effects can act on existing playback state without a new note. Source volume-column placement can be an import convention, and source timing and evaluation order determine import execution order.

## Native FX and the Dirtywave M8

The M8 comparison uses the manual’s relative/absolute, sequencer and instrument FX sections. BroTracker candidates use the shared scheduler and the Pattern, channel and arrangement model. Define state scope, timing and retrigger behavior per operation. [Dirtywave M8 Operation Manual, v6.5.2, pp. 69–75](https://cdn.shopify.com/s/files/1/0455/0485/6229/files/m8_operation_manual_v20260421.pdf?v=1776791699#page=76)

| Musical operation | M8 reference | BroTracker candidate |
|---|---|---|
| Arpeggio | `ARP` cycles the base note and two semitone offsets. `ARC` configures mode and speed. | `ARP` cycles pitches relative to the active note. Define cadence, phase and reset on BroTracker’s timeline. Convert imported tick cycling explicitly. |
| Volume and pan | The M8’s parameter FX distinguish absolute values and relative changes. Its instrument `VOL` offsets instrument volume. | `VOL` sets playback gain and `PAN` sets stereo position. `VSL` changes gain over time and `PAS` moves stereo position over time. Define slide cadence, bounds, state lifetime and interaction with instrument defaults. |
| Pitch slides | M8 `PSL` sets portamento in ticks. `PBN` moves pitch continuously up or down. | `PRT` glides a sounding note to a target pitch without retriggering. `PUP` and `PDN` move pitch in the chosen direction without a destination note. |
| Note delay | M8 `DEL` delays a phrase row or the table playhead. | `DEL` schedules the channel’s note trigger and associated instrument action later in the row. Other row events keep their own timing. Import may need source-specific handling for coupled effects. |
| Retrigger | M8 `RET` can repeat at an interval with gain changes or fire once after a delay. | `RTR` repeats a note trigger on the channel at scheduled offsets. Specify which voice state resets. A combined retrigger and volume transformation is a separate candidate for XM `Rxy`. |
| Arrangement jumps | M8 `HOP` acts on phrases or tables. `SNG` moves a track relative to its song row. | `JMP` directs the arrangement to an order-list entry. `BRK` selects the destination Pattern row at a transition. Define when the transition takes effect and how simultaneous commands resolve. |

### Additional native candidates

- **Trigger probability.** Consider `PRB` as a mnemonic for gating a note trigger by probability. Define repeatable random evaluation and how attached events follow a skipped trigger. M8 `CHA` can gate commands according to their position in the phrase or table.
- **Conditional triggers.** Allow a note to trigger on selected Pattern passes, with explicit counter scope and reset rules. M8 `NTH` uses phrase or table loop count.
- **Microtiming.** Place individual events within a row on the shared scheduler timeline. Define the offset range, boundary rules and ordering when events coincide. Note delay is one application.
- **Parameter automation.** Schedule absolute changes, relative changes or ramps to named playback parameters without requiring a note. M8 instrument FX and tables illustrate parameter sequencing. Define target scope, interpolation and reset for BroTracker.

### Existing OFF and timed termination

BroTracker already has a note-field `OFF`. Its default arrival setting stops the active channel note at row start. Its row-end setting schedules the stop at the boundary into the next row, ahead of new triggers at that boundary. This overlaps with the purpose of M8 `KIL`, which stops the playing instrument after a tick delay. In-row termination could extend BroTracker’s existing `OFF` timing.

MOD/XM `ECx` sets channel volume to zero at a scheduled tick. A later volume command may restore the sound if the voice remains active. Voice termination ends that active note. XM `Kxx` releases the key at a scheduled tick and can allow the instrument’s envelope and fadeout to continue. An XM key-release mapping depends on compatible instrument behavior. [BroTracker row preparation](../src/core/playback/variable_rate_sample_commands.h) · [Deferred row-end stop](../src/core/playback/variable_rate_pattern_player.h) · [Dirtywave M8 Operation Manual, p. 70](https://cdn.shopify.com/s/files/1/0455/0485/6229/files/m8_operation_manual_v20260421.pdf?v=1776791699#page=77)

## MOD and XM import

The first columns in the tables describe source commands and source values. The BroTracker column shows candidate operations in compact notation. Placeholders such as `xx`, `xy` and `0x` illustrate the display and require a defined conversion from the source operand.

**Assessment key** — **Direct** means a close native representation is possible once the named operation exists. **Conditional** means timing, memory, instrument behavior or arrangement must match. **Defer** means report the effect until a suitable native operation exists. These are design assessments, independent of current implementation.

## Main effect column

| Amiga MOD | FT2 XM | BroTracker | Assessment and expected differences |
|---|---|---|---|
| `0xy` | `0xy` | `ARP xy` | Conditional. Preserve the two semitone offsets and tick cycling. FT2 arpeggio phase can differ. Source `000` is empty. |
| `1xx` | `1xx` | `PUP xx` | Conditional. Convert pitch units and tick cadence. XM linear and Amiga period modes produce different trajectories. |
| `2xx` | `2xx` | `PDN xx` | Conditional. Same issues as pitch up, with independent source memory where applicable. |
| `3xx` | `3xx` | `PRT xx` | Conditional. Target comes from the note. Preserve glide without retrigger and source memory. |
| `4xy` | `4xy` | `VIB xy` | Conditional. Preserve speed, depth, phase, waveform and any per-nibble memory. |
| `5xy` | `5xy` | `PRT` + `VSL xy` | Conditional. Portamento uses the earlier speed. Expand only when the combined operation's order and memory survive. |
| `6xy` | `6xy` | `VIB` + `VSL xy` | Conditional. Vibrato uses remembered settings. Apply both with source cadence and precedence. |
| `7xy` | `7xy` | `TRM xy` | Conditional. Waveform, phase and volume scale can alter the sound. |
| Dialect-specific `8xx` | `8xx` | `PAN xx` | Direct for XM if pan law and range align. Recognize MOD panning only in a source dialect that defines it. |
| `9xx` | `9xx` | `OFS xx` | Conditional. Decode the source offset and its memory, then convert to native sample frames. Handle converted samples and out-of-range offsets explicitly. |
| `Axy` | `Axy` | `VSL xy` | Conditional. Tick cadence, precedence with both nibbles set, and zero memory vary. |
| `Bxx` | `Bxx` | `JMP xx` | Conditional. Translate source order index to the imported arrangement, including repeated patterns and relocated targets. |
| `Cxx` | `Cxx` | `VOL xx` | Direct with matching channel gain semantics. The source range is hex `00–40`, and `C00` sets silence. If BroTracker adopts a linear native `00–FF` gain scale covering the same range, source `C40` maps to `VOL FF`. |
| `Dxx` | `Dxx` | `BRK xx` | Conditional. Decode decimal digits, remap through splits, and preserve behavior with simultaneous position jumps. `D31` targets decimal row 31, displayed as `BRK 1F` if the native operand is hex. |
| `Fxx` | `Fxx` | `BPM xx` or timing conversion | Conditional. Source speed controls legacy ticks per row. Decode tempo, speed and special values using the source profile. `F20` and `F00` require explicit cases. |

## Extended E commands

| Amiga MOD | FT2 XM | BroTracker | Assessment and expected differences |
|---|---|---|---|
| `E0x` | — | — | Defer. Amiga hardware filter changes the mix. A native filter approximation would need a defined response and scope. |
| `E1x` | `E1x` | `FUP 0x` | Conditional. Fine pitch up on the initial tick, with format-specific units and memory. |
| `E2x` | `E2x` | `FDN 0x` | Conditional. Fine pitch down with the same constraints. |
| `E3x` | `E3x` | `GLS 0x` | Conditional. Glissando changes how portamento is quantized. |
| `E4x` | `E4x` | `VWF 0x` | Conditional. Vibrato waveform and retrigger behavior affect phase. |
| `E5x` | `E5x` | `FIN xx` | Conditional. Convert source fine-tune encoding and honor when it takes effect. |
| `E6x` | `E6x` | `LOP 0x` | Conditional. Preserve loop begin, count and channel precedence. A loop spanning a split pattern needs arrangement support. |
| `E7x` | `E7x` | `TWF 0x` | Conditional. Tremolo waveform and phase can differ by dialect. |
| Dialect-specific `E8x` | — | `PAN xx` | Conditional extension. Interpret only when supported by the selected MOD dialect. |
| `E9x` | `E9x` | `RTR xx` | Conditional. Convert retrigger interval and preserve the initial-trigger rule. |
| `EAx` | `EAx` | `FVU 0x` | Conditional. One-shot volume increase with source gain scale and memory. |
| `EBx` | `EBx` | `FVD 0x` | Conditional. One-shot volume decrease with source gain scale and memory. |
| `ECx` | `ECx` | `MUT xx` | Conditional. Timed zero channel volume. Validate whether a later volume command can restore the voice. A voice kill would be an approximation. |
| `EDx` | `EDx` | `DEL xx` | Conditional. Timed note or instrument action. Preserve zero, beyond-row delays and interaction with other commands. |
| `EEx` | `EEx` | `RDL 0x` | Conditional. Extend the row with the source repeat rules. Duplicating rows can introduce unwanted notes or effect resets. |
| `EFx` | — | — | Defer. ProTracker inverts sample-loop data during playback, potentially affecting other voices that use the same sample. |

## XM-specific effect commands

| FT2 XM | BroTracker | Assessment and expected differences |
|---|---|---|
| `Gxx` | `GVL xx` | Direct if the native song gain has matching scope and scale. |
| `Hxy` | `GVS xy` | Conditional. Global volume slide has tick behavior, memory and cross-channel precedence. |
| `Kxx` | `KOF xx` | Conditional. Timed key release follows XM envelope and fadeout behavior. Map ordinary XM key-off notes through the native note-release path where behavior matches. |
| `Lxx` | `ENV xx` | Conditional. Requires an equivalent envelope-position operation and compatible envelopes. |
| `Pxy` | `PAS xy` | Conditional. Convert pan range, cadence and memory. |
| `Rxy` | `RVR xy` | Conditional. Retrigger interval and volume transformation travel together. FT2 execution details can affect the result. |
| `Txy` | `TMR xy` | Conditional. Preserve alternating audible/silent lengths and phase rules. |
| `X1x` | `XUP 0x` | Conditional. Extra-fine pitch up uses a separate scale from `E1x`. |
| `X2x` | `XDN 0x` | Conditional. Extra-fine pitch down uses a separate scale from `E2x`. |

Treat OpenMPT's XM extensions as separately identified inputs. Unknown commands get a diagnostic entry with their source position and raw data when available.

## XM volume column

The values below are source byte ranges. The importer decodes the operation before mapping it to a native operand.

| XM source | Operation | BroTracker | Caveat |
|---|---|---|---|
| `00` | Empty | Empty | Distinct from zero gain. |
| `10–50` | Set volume | `VOL xx` | Subtract hex `10` to decode the source level `00–40`, then convert to the native gain scale. For a linear native `00–FF` scale covering the same range, source byte `50` maps to `VOL FF`. Preserve precedence against the main effect. |
| `6x` / `7x` | Volume slide down / up | `VSL xy` | Decode direction and source tick behavior. |
| `8x` / `9x` | Fine volume slide down / up | `FVD 0x` / `FVU 0x` | Initial-tick action. Preserve column-specific semantics. |
| `Ax` / `Bx` | Vibrato speed / depth | `VSP 0x` / `VDP 0x` | Updating speed and applying vibrato are distinct operations. Verify activation when the main column also contains vibrato. |
| `Cx` | Set pan | `PAN xx` | Translate FT2's coarse pan positions. |
| `Dx` / `Ex` | Pan slide left / right | `PAS xy` | Decode direction, scale and tick behavior. |
| `Fx` | Tone portamento | `PRT xx` | Decode the volume-column speed scale and preserve interaction with the main column. |

## Differences to expect after import

1. **Timing and LPB.** Keep 96 internal ticks per row, 4 rows per quarter and 384 PPQ. A source effect tick at position k in a row with S uniformly spaced source ticks maps to k × 96 / S internal ticks if row duration is preserved. At speed 6, `ED3` lands on tick 48, so the proposed native display would be `DEL 30` if its unit is one internal tick. Specify rational scheduling or quantization for non-integral results and test cumulative error. In classic CIA timing, a legacy tick lasts 2.5 / T seconds at tempo T. Preserving S such ticks in one native row implies native BPM = 6 × T / S. Tempo 125 and speed 3 therefore map to 250 BPM, if the native range and transition handling allow it. The formula assumes constant tick duration inside the row. VBlank timing and mid-row tempo application need separate handling. Speed also changes the number of slide and retrigger steps, so matching row duration alone is insufficient.
2. **State and arrangement.** Track effect memory by source dialect, channel and operation. Distinguish a remembered operand from an active modulation and a one-shot row command. Test pattern reuse with different incoming memory or speed, backward jumps, loop counts, pattern boundaries and `Bxx` plus `Dxx` on the same row. Where conversion depends on context, emit separate native pattern instances if feasible or report the limitation.
3. **Instruments and sample playback.** XM instruments can map notes to different samples and carry volume and pan envelopes, fadeout, auto-vibrato and forward or ping-pong loops. Those features influence effects even if every command is decoded. Sample-rate conversion, pitch law, pan law, loop boundaries and gain staging can also change sound. XM modules with more channels than the current eight-channel target require an explicit acceptance or overflow policy.
4. **Ordering and capacity.** Keep source operands until source column precedence and effect phases are understood. Expansion of a combined command can consume multiple slots. Preserve the logical operation when feasible and report overflow by order, row, channel and column. Avoid silently overwriting another command. Prefer native operations with value beyond import, including timed note delay, retrigger, pitch glide and sample offset.
5. **Coverage.** Report recognized, mapped, approximated and unsupported occurrences per module, with examples and locations. A percentage of mapped occurrences is only one measure. Compare row durations, note triggers, arrangement flow and representative audio against a chosen reference player. One missing jump, loop or key-off can matter more than many correctly mapped volume settings. Treat 96% as an aspiration until a representative MOD/XM corpus and acceptance criteria exist.

## Implementation priorities for later consideration

1. Define native volume, pan, note delay, termination, sample offset and arrangement behavior for authoring. Use 4-channel ProTracker/CIA MOD as the first import profile and report timing or behavior approximations.
2. Add shared native pitch, slide, modulation and retrigger behavior with explicit source conversion and effect-memory fixtures. Treat VBlank and hardware-specific semantics as independently measurable coverage.
3. Add FT2 XM with the volume column, both frequency modes and instrument support. Reuse shared native operations where semantics align. Introduce a bounded native operation only when it serves BroTracker or demonstrably improves important imports.

## References

- [BroTracker Effect Event Architecture](https://github.com/smartin77/BroTracker/blob/feature/instrument-screen/docs/EFFECT_EVENT_ARCHITECTURE.md)
- [BroTracker Architecture Decisions](https://github.com/smartin77/BroTracker/blob/feature/instrument-screen/docs/ARCHITECTURE_DECISIONS.md)
- [BroTracker Scheduler](https://github.com/smartin77/BroTracker/blob/feature/instrument-screen/docs/SCHEDULER.md)
- [ProTracker 2.3D built-in help and command reference](https://github.com/echolevel/Protracker-2.3D-Helpfile-Manual)
- [FastTracker II 2.04 XM format specification](https://modland.com/pub/documents/format_documentation/FastTracker%202%20v2.04%20%28.xm%29.html)
- [OpenMPT effect reference](https://wiki.openmpt.org/Manual:_Effect_Reference)
- [OpenMPT compatible playback](https://wiki.openmpt.org/Manual:_Compatible_Playback)
- [Dirtywave M8 Operation Manual, v6.5.2](https://cdn.shopify.com/s/files/1/0455/0485/6229/files/m8_operation_manual_v20260421.pdf?v=1776791699)

# BroTracker MOD and XM effect import

Design proposal. Native BroTracker authoring and playback are the priority. The import goal is to preserve as much playable musical behavior as practical from Amiga ProTracker MOD and FastTracker II XM. Reverse export is outside this design. The mnemonics below are proposed BroTracker names, and their presence in the table indicates a design candidate.

Use three-character command names and a two-digit hexadecimal operand in the Pattern View detail, for example `VOL 40`. Each command defines its operand's unit and range. Use command-specific detail for any value that exceeds one byte or needs a different display, such as a tempo with decimals. Keep storage encoding open. Three general-purpose slots are the current design baseline. Source volume-column placement can be an import convention. Source timing and evaluation order determine execution order.

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
| `Bxx` | `Bxx` | `HOP xx` | Conditional. Translate source order index to the imported arrangement, including repeated patterns and relocated targets. |
| `Cxx` | `Cxx` | `VOL xx` | Direct with matching channel gain semantics. Source range is `00–40` hex. `C00` sets silence. |
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
| `Pxy` | `PSL xy` | Conditional. Convert pan range, cadence and memory. |
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
| `10–50` | Set volume | `VOL 00–40` | Subtract hex `10`; preserve precedence against the main effect. |
| `6x` / `7x` | Volume slide down / up | `VSL xy` | Decode direction and source tick behavior. |
| `8x` / `9x` | Fine volume slide down / up | `FVD 0x` / `FVU 0x` | Initial-tick action. Preserve column-specific semantics. |
| `Ax` / `Bx` | Vibrato speed / depth | `VSP 0x` / `VDP 0x` | Updating speed and applying vibrato are distinct operations. Verify activation when the main column also contains vibrato. |
| `Cx` | Set pan | `PAN xx` | Translate FT2's coarse pan positions. |
| `Dx` / `Ex` | Pan slide left / right | `PSL xy` | Decode direction, scale and tick behavior. |
| `Fx` | Tone portamento | `PRT xx` | Decode the volume-column speed scale and preserve interaction with the main column. |

## Differences to expect after import

1. **Timing and LPB.** Keep 96 internal ticks per row, 4 rows per quarter and 384 PPQ. A source effect tick at position k in a row with S uniformly spaced source ticks maps to k × 96 / S internal ticks if row duration is preserved. At speed 6, `ED3` lands on tick 48, so the proposed native display would be `DEL 30` if its unit is one internal tick. Specify rational scheduling or quantization for non-integral results and test cumulative error. In classic CIA timing, a legacy tick lasts 2.5 / T seconds at tempo T. Preserving S such ticks in one native row implies native BPM = 6 × T / S. Tempo 125 and speed 3 therefore map to 250 BPM, if the native range and transition handling allow it. The formula assumes constant tick duration inside the row. VBlank timing and mid-row tempo application need separate handling. Speed also changes the number of slide and retrigger steps, so matching row duration alone is insufficient.
2. **State and arrangement.** Track effect memory by source dialect, channel and operation. Distinguish a remembered operand from an active modulation and a one-shot row command. Test pattern reuse with different incoming memory or speed, backward jumps, loop counts, pattern boundaries and `Bxx` plus `Dxx` on the same row. Where conversion depends on context, emit separate native pattern instances if feasible or report the limitation.
3. **Instruments and sample playback.** XM instruments can map notes to different samples and carry volume and pan envelopes, fadeout, auto-vibrato and forward or ping-pong loops. Those features influence effects even if every command is decoded. Sample-rate conversion, pitch law, pan law, loop boundaries and gain staging can also change sound. XM modules with more channels than the current eight-channel target require an explicit acceptance or overflow policy.
4. **Ordering and capacity.** Keep source operands until source column precedence and effect phases are understood. Expansion of a combined command can consume multiple slots. Preserve the logical operation when feasible and report overflow by order, row, channel and column. Avoid silently overwriting another command. Prefer native operations with value beyond import, including timed note delay, retrigger, pitch glide and sample offset.
5. **Coverage.** Report recognized, mapped, approximated and unsupported occurrences per module, with examples and locations. A percentage of mapped occurrences is only one measure. Compare row durations, note triggers, arrangement flow and representative audio against a chosen reference player. One missing jump, loop or key-off can matter more than many correctly mapped volume settings. Treat 96% as an aspiration until a representative MOD/XM corpus and acceptance criteria exist.

## Implementation priorities for later consideration

1. Start with 4-channel ProTracker/CIA MOD and establish source-profile selection and import reporting. Prioritize volume, pan where applicable, note delay, cut, offset and arrangement control.
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
- [Dirtywave M8 manual](https://dirtywave.com/pages/resources-downloads)

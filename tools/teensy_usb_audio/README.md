# Teensy4 USB transmit shortage zero-fill fix

The Teensy4 core declares the packet buffer as `uint16_t[]`, but `len` counts
four-byte stereo frames. The default build applies only this change:

```diff
-memset(usb_audio_transmit_buffer + len, 0, num * 4);
+memset((uint32_t *)usb_audio_transmit_buffer + len, 0, num * 4);
```

This uses the same frame addressing as the adjacent packet-copy operation.
It preserves valid prefix bytes, clears both shortage channels and leaves no
stale tail. Packet lengths/cadence and all other core code remain unchanged.
It fixes corruption during shortages; it does **not** establish or resolve
repeated starvation or whole-block omissions.

## Reproducible build

`platformio.ini` registers `prepare_core.py` as a Teensy41 pre-script. It checks
framework package name/version `framework-arduinoteensy 1.162.0` and the exact
original Teensy4 `usb_audio.cpp` SHA-256:

`32cce87877e82d69739169390dddf9c6847efb18b3d9c99dfc5b1f2fa2971033`

Unknown versions or changed source fail with an explicit error. Review an
upstream change before updating these guards; do not bypass them. No packages
are installed by the script and no installed framework file is written.

PlatformIO's source middleware substitutes only this translation unit with
`.pio/build/teensy41/core-patches/usb_audio.cpp`. Original headers and other core
sources remain in use. The build prints the replacement path and writes
`core-patches/receipt.json` with original/patched hashes. The generated copy is
recreated as needed after a clean build; it is not a manually maintained fork.

```sh
pio run -e teensy41
```

Artifacts: `.pio/build/teensy41/firmware.hex` and `firmware.elf`. Build alone
never flashes the device. Existing IDLE boot, USB descriptors, BTTEST1 and
clock/audio ownership are not changed.

## Host regression

Using the existing Windows compiler and installed framework:

```powershell
python tools/teensy_usb_audio/check_zero_fill.py --framework "$env:USERPROFILE/.platformio/packages/framework-arduinoteensy" --build build/teensy-usb-zero-fill-review/tests
```

An alternate compiler can be supplied with `--cxx`. No external Python modules
are required. The test extracts the actual operation from the generated patched
core and compiles it beside the original negative control. All copied lengths
0..44 and 0..45 are tested: empty, full and partially filled packets, even/odd
counts, preserved prefix, all-zero shortage channels, no stale tail, and guards
before/after each packet. It also verifies rejection of unknown version and
changed source. Expected:91 patched cases pass,87 original cases fail.

Physical shortage/cold-boot behavior still needs a separately authorized flash
and hardware check; the build and byte-level tests do not prove starvation is
resolved.

## Opt-in 128-packet pre-DMA trace

Select PlatformIO environment `teensy41_usb_trace` in VS Code for the diagnostic
firmware build/flash. The default `teensy41` environment has no trace hooks.
Both environments use the same validated framework/source guards and isolated
core replacement; trace output is generated under
`.pio/build/teensy41_usb_trace/core-patches/`. No installed core is modified.

An explicit successful BTTEST1 START arms static storage for the next 128 USB
transmit packets (including initial silence). Each packet stores its exact
post-fill/pre-DMA payload, byte length, copied stereo-frame count, shortage
flag, sequence, MCU microseconds, cycle counter, USB AudioStream update-entry
count and discarded block-pair count. Counters are cumulative; timestamps and
counters wrap at 32 bits. This does not change AudioStream update ownership.
Storage is below 32 KiB, allocated statically. The IRQ path only copies bounded
payloads and metadata: no heap, serial/SD writes, locks or waits.

Storage freezes at 128 packets or an early STOP, completion or playback error.
The main loop sends bounded `USBTRACE1` lines only when playback is inactive and
CDC has space. Natural completion therefore also permits retrieval (after the
existing completion blinks). Wait at least ten seconds after STOP/completion
before EXIT, and check for `USBTRACE1 END` in the collected log. An unretrieved
trace is retained across another START (`BUSY`); after its END, the next START
can arm a fresh session. HELLO replays retained data after CDC reconnect. Replay
can produce duplicate lines; identify packets by session/index, payload by
session/index/offset. Session numbering resets when the MCU resets.

BroTracker Diagnostics persists these lines in `usb-tx-trace.log`, the normal
collected BTX log and `events.tsv` as `usb_tx_trace_observation`. Timeline times
are host collector observations, not packet timestamps. Correlate START/STOP,
trace session/arm_us and packet MCU timestamps with WAV recording start/end and
bridge instance events in the same diagnostic directory. Packet payloads are
hexadecimal byte fragments (`D`, offset `o`); `P` contains packet metadata.
Normal Ports behavior and protocol command/reply bytes are unchanged.

This is **pre-DMA evidence**, not proof of on-wire data. A correct saved payload
with corrupt host PCM leaves DMA/USB transport/host capture as possibilities.
The short trace may capture only initial silence; it cannot by itself establish
why repeated starvation or whole-block loss occurs.

Host checks (no firmware build or hardware access):

```powershell
python tools/teensy_usb_audio/check_trace.py --framework "$env:USERPROFILE/.platformio/packages/framework-arduinoteensy" --build build/usb-tx-trace-review/tests
```

The test compiles actual generated fill/update/transmit functions with host
peripheral stubs, checks exact packet data and counters, freeze/early STOP,
replay, CDC backpressure, reconstruction and trace-disabled compilation.

Collection: after separately building/flashing the opt-in firmware, cold boot
with Teensy directly attached; launch BroTracker Diagnostics; L1/B START and
play; R1/X STOP and wait ten seconds; unplug/replug while BTX stays open; wait
for CDC/audio recovery; L1/B START and play; R1/X STOP and wait ten seconds;
R1/X EXIT. Upload the complete persistent diagnostic directory, including WAVs,
`events.tsv` and `usb-tx-trace.log`. Do not initiate another START until the prior
trace has been retrieved.

## USB lifecycle history (trace environment only)

The isolated replacement now also validates `cores/teensy4/usb.c` SHA-256
`8cb03e83e90527e5574c7977bc8957edbc16f27c2b210c7949a507158f8bf78c`
from framework 1.162.0 before generating its instrumented copy. Ordinary builds
still replace only `usb_audio.cpp` for the zero-fill correction.

History begins at USB initialization, independently of START. Its constexpr
initialization avoids losing early evidence when C++ constructors run later. Static storage
retains the first 32 events and most recent 96 (3,076 bytes), with sequence
numbers and an explicit omitted-event count. STOP/completion dump a stable
snapshot as `USBLIFE1` before the existing packet dump. Replay uses the same
snapshot. A fresh retrieved START session takes a new snapshot when stopped.
The dump adds another 3,076-byte static snapshot; no dynamic allocation is used.
History can omit intermediate transitions; it is not a complete bus analyzer.
Microseconds/sequence counters wrap at 32 bits.

`USBLIFE1 E` fields are `seq`, MCU `us`, numeric `type`, and values `a,b,c`:

| Type | Event | a | b | c |
| --- | --- | --- | --- | --- |
| 1 | USB init | 0 | 0 | 0 |
| 2 | bus reset observed | configuration | TX alternate setting | PORTSC snapshot |
| 3 | configuration request | old configuration | requested configuration | 0 |
| 4/5 | TX/RX alternate request | old alternate | requested alternate | 0 |
| 6 | audio configure entry | TX alternate | high-speed flag | 0 |
| 7 | TX state change after fill | TX alternate plus shortage=256, first block=512, second block=1024 | remaining first-block offset | copied frames in upper 16 bits, requested frames in lower 16 |
| 9 | replacement-block allocation failure | channel (0 left, 1 right) | first-block offset | update-entry count |
| 8 | update input/queue state change | TX alternate plus left input=256, right input=512, first block=1024, second block=2048 | first-block offset | cumulative update-entry count |

Lifecycle requests are recorded **before** the corresponding original state
change; they do not mean a transfer has completed. Every original operation,
packet cadence, audio setting and block ownership rule is preserved. IRQ hooks
only append bounded scalar records under a short interrupt-state-preserving
critical section. The main loop snapshots history and emits lines when stopped.
No Serial, SD, formatting, allocation or waiting occurs inside the IRQ hooks.

### Software re-enumeration limitation

This installed Teensy4 core exposes `usb_init()` but no supported runtime USB
detach/reattach API. Its controller startup code has no runtime teardown contract
for live CDC/MIDI/audio transfers and AudioStream queues. Calling it again or
writing controller registers would be an unvalidated experiment. No USB reinit
action or full-board reset has been added, and no Ports button was repurposed.
Thus a no-unplug before/after reinit comparison is currently unavailable. A
successful future supported reinit would demonstrate recovery, not identify
which side originally produced the bad state.

### Trace-file persistence

Diagnostics continuously retains the shared log and now reconstructs
`usb-tx-trace.log` from its complete final log, including both `USBTRACE1` and
`USBLIFE1` raw lines. This covers a final polling gap or split incremental line.
`events.tsv` includes live trace observations and a `trace_final_snapshot` event.
`collector.log` records collector revision `usb-lifecycle-v1`;
`diagnostics-launcher.sha256` records the running script hash when available.
The existing installer copies this script, preserving earlier launcher backups.

The supplied archive has packet traces in `btx-final.log` but no separate trace
file or trace-observation events, and contains no deployed script/hash. This is
consistent with an older deployed collector, but its exact script or failure
cannot be established from that archive alone.

User-operated commands (not executed as part of this task):

```sh
pio run -e teensy41_usb_trace
pio run -e teensy41_usb_trace -t upload
```

On ArkOS, from the existing checkout after transferring the reviewed scripts:

```sh
cd "$HOME/BroTracker"
bash tools/arkos/install-port.sh
```

Collection available now: cold boot with powered Teensy attached; launch
BroTracker Diagnostics; L1/B START/play; R1/X STOP; wait ten seconds; R1/X EXIT.
Upload the complete diagnostic directory and check for `USBLIFE1 END` and
`USBTRACE1 END`. Do not label this as a USB reinit experiment: no supported
software reinit action is available in this core.

Additional host regression:

```powershell
python tools/teensy_usb_audio/check_lifecycle.py --framework "$env:USERPROFILE/.platformio/packages/framework-arduinoteensy" --build build/usb-lifecycle-review/history-tests
```

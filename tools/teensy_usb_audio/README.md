# Teensy4 USB transmit shortage zero-fill fix

The Teensy4 core declares the packet buffer as `uint16_t[]`, but `len` counts
four-byte stereo frames. The sole applied change is:

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

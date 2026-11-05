# Warning-task delivery and physical validation

## Owner-reported Windows confirmation (2026-11-04)

The owner confirmed these two observations in Windows BTX:

- Without Teensy connected, the correct disconnected warning appeared.
- After connecting Teensy, the warning disappeared and the application worked.

This confirms only that reported Windows connection workflow. It does not validate
SD-unavailable presentation, a blocked card, handheld behavior, or every reconnect
and synchronization failure path.

An earlier report said the SD-unavailable warning did not appear in a card-absent
scenario, with no cause or installed firmware identity established. In the latest
owner report, the SD warning is confirmed OK in the tested scenario. This does not
establish validation of blocked/unresponsive SD cards, which have **not** been tested.

At the time of that report, dArkOS had been updated with git pull and
`tools/arkos/install-port.sh`, but a warning-task ARM64 delivery had not yet been
built. That update therefore did not establish validation of the new warning
implementation on dArkOS. The ARM64 delivery was subsequently built and refreshed successfully on 2026-11-04;
owner transfer/install and physical ArkOS/dArkOS validation remain pending.

## Firmware with explicit boot SD status

The existing development build is PlatformIO environment `teensy41_dev`:

- `.pio/build/teensy41_dev/firmware.hex` (upload image).
- `.pio/build/teensy41_dev/firmware.elf` (development executable/debug image).
- Prior successful compile log: `diagnostics/blocking-warnings/firmware-build.log`.

The existing ELF contains `BTSTORE1 SDCAP 1`, `BTSTORE1 SD 1 available` and
`BTSTORE1 SD 1 unavailable`. Source is
`firmware/teensy/BroTracker/pattern_bringup.cpp`: HELLO advertises the capability
and reports `g_storage_backend.ready`, retained from the single boot-time
`SD.begin(BUILTIN_SDCARD)` result before session loading. Neither missing sessions
nor arbitrary later I/O failures are classified as unavailable SD.

This detects boot-time initialization failure **when initialization returns
false**. A later blocked/unresponsive card, or an initialization call that does
not return, is a separate unimplemented/unverified case. No card-health monitor,
watchdog, hot mounting, repair or automatic recovery was added. The latest owner confirmation applies only to the tested SD-warning scenario;
blocked/unresponsive-card behavior is not inferred from it.

Owner-controlled upload, from the repository root after closing BTX/other serial
clients:

```powershell
pio run -e teensy41_dev -t upload
```

This is the existing PlatformIO build-and-upload command. It is provided for the
owner only; no upload, serial connection, hardware operation or new firmware
change was performed for this delivery.

## Handheld delivery and installation

The normal delivery directory is `deploy/arkos`, with `BroTrackerTerminal`,
`BroTrackerAlsaBridge`, `launch.sh`, the existing manifest/identity metadata and
config example. Warning resources retain their runtime layout:

- `assets/fonts/terminus/ter-u12n.bdf`, `ter-u14n.bdf`, `ter-u14b.bdf`.
- `assets/fonts/terminus/OFL-bitmap.txt` and `SOURCES.md`.
- `assets/translations/en_US.ini`.
- `assets/icons/font-preview-warning.svg`.
- `licenses/NanoSVG.txt`.

The existing installer now explicitly installs those resources, creates/checks
the required subdirectories, and preserves `config/config.ini`. The existing
isolated installer check verifies installed resource bytes and user configuration
preservation; it writes only to a temporary fixture, never /roms or a handheld.

After the owner reviews and transfers the complete package through their normal
Git workflow, run on ArkOS/dArkOS from the existing repository checkout:

```sh
git pull
bash tools/arkos/install-port.sh
bash "/roms/ports/BroTracker Terminal.sh"
```

The installer uses the `deploy/arkos` package from that checkout. It does not build
code on the handheld. The installed application is `/roms/ports/brotracker`, with
the same assets and license paths beneath it. Existing user config is not created
or overwritten. Physical validation and SD behavior remain the owner's next step.

## Build and check scope

Use the existing Eoan ARM64 compiler/root and packaging scripts. To refresh only
the two shipped binaries while retaining existing ABI/package/isolated-installer
checks, from WSL/Linux at the repository root:

```sh
sudo update-binfmts --enable qemu-aarch64
BROTRACKER_ARKOS_DELIVERY_ONLY=1 bash tools/arkos/prepare-package.sh
```

The flag skips the broad CMake build and CTest suite; the default workflow is
unchanged. CPU emulation is used for the established compiler/root and small
existing diagnostic-info/ABI checks, never a presentation loop. There is no new
installer, packaging framework, screenshot test or physical deployment here.
The delivery-only build passed for both shipped targets. Existing Eoan ABI/
dependency checks, complete-package checks, and isolated installer/config-
preservation checks passed. No full test suite or presentation run was performed.
Final build/check results and the complete task diff are in
`diagnostics/warning-arm64-delivery/`.

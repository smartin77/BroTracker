# BroTracker Terminal (BTX) on ArkOS Eoan ARM64

The ArkOS host application is **BroTracker Terminal**; **BTX** is communication
shorthand (D0053), not a separate application name. Windows uses the same target
name in its own build/package; ArkOS deploys to `deploy/arkos/`.

The display test targets the console's Ubuntu 19.10 (Eoan) runtime:
glibc 2.30, libstdc++6 9.2.1-9ubuntu2, and system SDL2 2.0.10.
Build inside a minimal Eoan ARM64 root under QEMU user emulation in WSL2.
No Docker, console compiler, copied console sysroot, or bundled SDL2 is needed.

## One-time setup in Ubuntu WSL

Keep the build root in WSL's Linux filesystem. The checkout may stay on D:.
These commands use the signed Eoan release archive, not Jammy packages for
the ARM64 target. Do not add newer distribution sources to the build root.

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends debootstrap ubuntu-keyring qemu-user-static binfmt-support
export BROTRACKER_EOAN_ROOT="$HOME/brotracker-eoan-arm64"

sudo debootstrap --arch=arm64 --foreign --variant=minbase \
  --components=main,universe eoan "$BROTRACKER_EOAN_ROOT" \
  https://old-releases.ubuntu.com/ubuntu/
sudo cp /usr/bin/qemu-aarch64-static "$BROTRACKER_EOAN_ROOT/usr/bin/"
sudo update-binfmts --enable qemu-aarch64
sudo chroot "$BROTRACKER_EOAN_ROOT" /debootstrap/debootstrap --second-stage

printf '%s\n' 'deb https://old-releases.ubuntu.com/ubuntu/ eoan main universe' |
  sudo tee "$BROTRACKER_EOAN_ROOT/etc/apt/sources.list"
sudo cp /etc/resolv.conf "$BROTRACKER_EOAN_ROOT/etc/resolv.conf"

cd /mnt/d/dev/smartin77/BroTracker
bash tools/arkos/run-in-eoan.sh apt-get update
bash tools/arkos/run-in-eoan.sh apt-get install -y --no-install-recommends \
  g++ make cmake pkg-config libsdl2-dev libasound2-dev file binutils
```

The runner uses a private mount namespace; /workspace is the read-only
checkout, with /proc and /dev available only for the lifetime of the command.
It clears inherited host environment variables. QEMU must be registered with
binfmt_misc so ARM64 subprocesses execute inside chroot.

## Build and check

```sh
bash tools/arkos/run-in-eoan.sh
```

This enables the ArkOS terminal and standalone ALSA bridge, builds all enabled
CMake targets in /build/brotracker and runs CTest. The existing
`BROTRACKER_BUILD_ARKOS_UI` option now creates `BroTrackerTerminal`; enable only
one platform terminal option in each build tree.
The repository's CMake features work with Eoan's 3.13.4; the minimum is 3.13.
No newer target libc or SDL2 is installed to obtain CMake.

The script requires the matching compiler/runtime/development package versions,
selects Eoan's pkg-config SDL2 files, and checks BroTrackerTerminal for:

- AArch64 ELF and /lib/ld-linux-aarch64.so.1 interpreter;
- dynamic system SDL2 dependency, expected system libraries, and no RPATH/RUNPATH;
- required GLIBC <= 2.30, GLIBCXX <= 3.4.28, and CXXABI <= 1.3.12;
- dependency and relocation resolution with ldd -r in the Eoan root.

The freshly built `BroTrackerAlsaBridge` is checked similarly for system
`libasound.so.2`, matching symbol versions and relocations. Only after all
checks pass does it stage both executables, launch.sh, font
descriptor plus its required brotracker.bfm bitmap data, and test tune at
/package/arkos inside the build root. The dependency report
stays in /build/brotracker/arkos-dependencies.txt. No SDL2 library is packaged.
These are build/ABI checks, not proof that the console display or input works.

The old aarch64-toolchain.cmake is not used by this method: compilation runs
with Eoan's ARM64 GCC under emulation, with no cross-toolchain/sysroot override.

## Copy the checked package into the repository

After a successful build, from WSL in the checkout:

```sh
mkdir -p deploy/arkos
cp -a "$BROTRACKER_EOAN_ROOT/package/arkos/." deploy/arkos/
# After the new package has passed checks and been copied:
rm -f -- deploy/arkos/BroTrackerArkOSUI
```

The package includes the current working-tree sources, including any uncommitted
edits. Review the source and package together before a later commit and
repository transfer. Committing or pushing is not part of the build scripts.
When later adding the package to Git from Windows, record executable modes for
BroTrackerTerminal, BroTrackerAlsaBridge and launch.sh with git update-index --chmod=+x after git add.
The LF attributes also cover the packaged launcher and BTF descriptor.

On the console, run from the transferred checkout:

```sh
bash deploy/arkos/launch.sh
```

The launcher selects its own directory for relative assets. The diagnostic
client writes /tmp/brotracker-arkos.log. Repository deployment is a bring-up
step; the canonical installed application root remains /opt/BroTracker.

PlatformIO remains dedicated to Teensy firmware. No VS Code task is involved.

## First Eoan verification (2026-09-26)

All five CMake targets built with CMake 3.13.4 and GCC 9.2.1; CTest passed.
The UI executable requires GLIBC_2.17, GLIBCXX up to 3.4.21, and CXXABI up
to 1.3.9. Its interpreter is /lib/ld-linux-aarch64.so.1; direct dependencies
are libSDL2-2.0.so.0, libstdc++.so.6, libgcc_s.so.1, libc.so.6, and
ld-linux-aarch64.so.1. ldd -r resolved all dependencies and relocations in
the matching Eoan root.

A five-second SDL_VIDEODRIVER=dummy smoke test loaded the packaged assets,
presented the first framebuffer, entered the event loop, and shut down
normally on the timeout's exit event (timeout status 124). The first attempt
exposed the missing brotracker.bfm asset; packaging now includes it.

This build included the then-uncommitted ui/arkos/main.cpp diagnostics,
subsequently committed as 2cfb46d.
The source file was not modified by the build. No console hardware test or
repository transfer has been performed.

## PortMaster UI plus live audio

`install-port.sh` installs the checked `BroTrackerTerminal`,
`BroTrackerAlsaBridge`, launcher and assets (both executables mode 0755).
Package generation builds/tests and verifies both binaries; ALSA is not linked
into the terminal. No EmulationStation process or system audio settings are changed.

The Ports launcher starts the UI and one bridge supervisor together. Initial
startup and runtime recovery use a capped exponential backoff of 2, 4, 8,
then 16 seconds between failed attempts. Attempts continue while BTX is open,
including a prolonged Teensy absence. Each attempt runs a fresh `--auto`
discovery with current numeric PCM addresses and unchanged identity/ambiguity
checks. Only one bridge runs at a time; it closes its PCMs and is reaped before
rediscovery. The bridge's `Queue capacity:` line marks PCM configuration;
a subsequent exit switches logging to runtime recovery and resets the backoff.
Bridge diagnostics record successful configuration, selected endpoints and
failures. Audio recovery never sends START or changes firmware playback state.

The launcher truncates `/tmp/brotracker-arkos.log` once and appends its own and
the bridge's diagnostics. `BROTRACKER_APPEND_LOG=1` tells the UI to append;
a direct UI invocation without that flag still starts a fresh log. On UI exit
or launcher TERM/INT/HUP, the supervisor stops/reaps its bridge or retry sleep,
then the launcher stops/reaps the UI before `pm_finish`. Normal UI exit status
is preserved; launcher signals return 128 plus the signal number.

Run `bash tools/arkos/check-port-launcher.sh` for isolated stub-process checks
of initial retries, runtime recovery after prolonged absence, duplicate prevention,
shared logs, missing audio,
exit-code preservation and child cleanup before `pm_finish`. It temporarily
uses the standard log path and restores the previous log afterwards. Real
EmulationStation handoff, audio and USB hotplug still need R36H testing.

## Update an existing ArkOS installation

After transferring the reviewed checkout/package through the repository, run
these commands **on ArkOS from that checkout** (no compiler or firmware upload):

```sh
bash tools/arkos/install-port.sh
# Launch via the BroTracker Terminal Ports entry, or test its exact launch path:
bash "/roms/ports/BroTracker Terminal.sh"
# After exiting, inspect the shared diagnostics:
cat /tmp/brotracker-arkos.log
```

The source launcher is `tools/arkos/port/BroTracker Terminal.sh`, installed as
`/roms/ports/BroTracker Terminal.sh`. The application directory remains
`/roms/ports/brotracker/`. The installer saves unique backups of any existing
new or legacy `BroTracker.sh` launcher, installs the six packaged application
files and new launcher, then removes the legacy launcher only after successful
installation. Repeated updates leave one active BroTracker Ports entry and
preserve previous backups. It also removes only the obsolete application
executable `/roms/ports/brotracker/BroTrackerArkOSUI`.
It rejects symlink/non-file destinations before writing. User data, extra
files and previous launcher backups remain in place. Direct display/CDC
checks without the audio supervisor can use `bash deploy/arkos/launch.sh`.

Automatic audio capture accepts `BroTracker USB audio`, legacy
`Teensy MIDI/Audio`, or legacy ALSA ID `MIDIAudio`. Numeric card/device/subdevice
addresses come from current enumeration and post-open identity checks remain
mandatory. After USB reconnect, the supervisor rediscovers audio automatically
while BTX stays open; CDC reconnect remains independent and never replays START.

On hardware, verify L1/B START/restart, R1/X STOP-and-stay then EXIT, audio,
controller mapping and EmulationStation audio handoff. Local checks:

```sh
bash tools/arkos/check-install-port.sh
bash tools/arkos/check-port-launcher.sh
```

Both use isolated temporary installations/stubs, never the handheld. The
launcher check temporarily uses and restores `/tmp/brotracker-arkos.log`.

## Read-only R36H USB diagnosis and recovery test

The PC cannot determine the handheld's post-reboot hub/USB state. Before
unplugging anything, collect the following on R36H over SSH; repeat after
unplug/replug. These commands do not reset USB or change drivers/settings:

```sh
date
lsusb
# Inspect the actual USB device identity, independent of cached names:
for dev in /sys/bus/usb/devices/*; do
    [ -r "$dev/idVendor" ] && [ -r "$dev/idProduct" ] || continue
    [ "$(cat "$dev/idVendor"):$(cat "$dev/idProduct")" = 16c0:048a ] || continue
    echo "USB device: $dev"
    for field in product serial authorized; do
        [ ! -r "$dev/$field" ] || { printf '%s: ' "$field"; cat "$dev/$field"; }
    done
    ls -l "$dev"/"$(basename "$dev")":* 2>/dev/null
 done
cat /proc/asound/cards
arecord -l
aplay -l
for node in /sys/class/tty/ttyACM* /sys/class/sound/card*; do
    [ -e "$node" ] || continue
    printf '%s -> ' "$node"; readlink -f "$node/device"
done
ls -l /dev/ttyACM* /dev/snd/* 2>/dev/null
id
# May require read permission; do not change system logging configuration:
dmesg | tail -n 100
cat /tmp/brotracker-arkos.log
```

No `16c0:048a` device in `lsusb` or USB sysfs means failure before BTX
can discover it. USB present but no associated ttyACM/capture PCM points to
interface enumeration/binding; correlate sysfs ancestry and kernel messages.
If the associated interfaces exist, compare their permissions and identities
with CDC open/handshake and bridge candidate/open errors in the shared log.
A busy Rockchip output or ambiguous capture identity is a distinct audio error.
Do not conclude that the reboot problem and audio recovery have the same cause.

After a separately reviewed transfer, update and launch on the handheld:

```sh
bash tools/arkos/install-port.sh
bash "/roms/ports/BroTracker Terminal.sh"
```

1. Reboot ArkOS with Teensy attached; collect the read-only snapshot before
   any unplug. Launch BTX and check initial CDC/audio discovery. If absent,
   unplug/replug and collect a second snapshot to locate the failed layer.
2. With audio configured, START using L1/B. Unplug Teensy while BTX stays open.
   Confirm responsive controls, CDC disconnect and logged runtime audio recovery.
3. Leave it absent for several minutes: attempts must continue at the capped
   interval, with one supervisor and at most one bridge. Reconnect; confirm
   fresh PCM names, handshake and audio configuration without automatic START.
   Press L1/B explicitly and verify live audio returns without restarting BTX.
4. Repeat unplug/replug, then exit during a recovery backoff using R1/X or
   window close. Confirm cleanup messages, `pm_finish`, and no remaining bridge
   or supervisor (`ps -ef | grep -E 'BroTracker|audio_supervisor'`).

These are pending physical checks, not evidence of post-reboot USB detection
or subjective audio quality. Audio clicks and USB reset/power fixes are outside
this change.

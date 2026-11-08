# BroTracker Terminal (BTX) on ArkOS Eoan ARM64

The ArkOS host application is **BroTracker Terminal**; **BTX** is communication
shorthand (D0053), not a separate application name. Windows uses the same target
name in its own build/package; ArkOS deploys to `deploy/arkos/`.

The display test targets the console's Ubuntu 19.10 (Eoan) runtime:
glibc 2.30, libstdc++6 9.2.1-9ubuntu2, and system SDL2 2.0.10.
Build inside a minimal Eoan ARM64 root under QEMU user emulation in WSL2.
No Docker, console compiler, copied console sysroot, or bundled SDL2 is needed.

## Development MIDI connection launcher

The existing `tools/arkos/install-port.sh` installer also installs
**Ports > BroTracker MIDI Connect**. Run it manually before BroTracker Terminal.
It starts one detached, user-owned route keeper; Teensy and H4MIDI can arrive in
either order, before or after launch. Reopening the entry shows the existing
keeper's current status rather than starting another. No boot service is installed.
It requires Python 3 and `aconnect` from alsa-utils; prepare these while online.
`amidi` (also alsa-utils) supplies read-only identity listings when the installed
`aconnect` omits `card=`. Missing `amidi` reduces USB visibility; routing can continue.
No Wi-Fi/SSH or running BTX is required during use.

The launcher shows a compact system-console summary for up to ten seconds:
device discovery, ALSA route, USB observation and fresh BTX MIDI health remain
separate. Positive values request ANSI bold green; errors, unavailable and unknown
values request bold red, with explicit words for use without color (`NO_COLOR`
disables it). The console may show intensity rather than a heavier font.
Cleanup is complete only with fresh, valid MIDI health; otherwise a false cleanup
flag is shown as unknown. A reported pending/uncertain cleanup remains visible.
The MIDI row is omitted when BTX health is unknown (for example, BTX is closed
or its telemetry is stale). Known health/faults and pending cleanup stay visible;
route, device and USB observations are independent and remain displayed.
An existing route never establishes external MIDI delivery. Details remain in
the existing launch/keeper logs rather than being dumped onto the small screen.
A failed launch/ensure command is shown separately in red above any valid status
snapshot, with its exit code and the runtime path to `brotracker-midi-connect-launch.log`.
Any new button press on the R36H's `GO-Super Gamepad` (or `retrogame_joypad`)
returns early, using read-only Linux input events without grabbing the controller
or starting a mapping daemon.
Queued launch events and button releases are ignored. If the input device is
missing or inaccessible, the screen reports that buttons are unavailable and
uses the timeout. The 2026-11-02 R36H physical test confirmed readable spacing,
red/green values, the approximately ten-second timeout and early return with
A/B/X/Y and L/R shoulder buttons. The user saw no heavier bold font. Analog stick
movement is not supported or required. The older `retrogame_joypad` name remains
covered by fixtures; it was not physically verified in this run.
The same offline run passed PT8211 audio, H4MIDI LED activity and audible WASP
playback with an unpowered OTG hub and externally powered H4MIDI. H4MIDI stayed
connected while Teensy and Wi-Fi were swapped for playback and SSH respectively;
this does not validate all three devices together or establish a USB failure cause.

The tool discovers the exact ALSA sequencer port names `BroTracker USB audio MIDI 1`
and `H4MIDI-WC MIDI 1`, connects only source to destination, and verifies the route.
Only the expected kernel ALSA clients are accepted, with direction/identity
rechecked before connecting. USB path, bus/device number and sysfs incarnation are
recorded where available. An existing subscription is a no-op. States are missing,
ambiguous, connecting, connected and unavailable; **connected means subscription
verified, not delivered MIDI**. BTX health is a separate observation, never a route
acknowledgement or permission for the keeper to control playback.

The keeper observes every two seconds, with five connection attempts per stable
device/port identity and capped 2/4/8/16-second backoff. After exhaustion it keeps
observing without repeated connection attempts; device/port changes, observed
removal/reappearance, or loss after a verified connection permit a new bounded
attempt sequence. Each aconnect command has a five-second timeout. Numeric ALSA
addresses are rediscovered; unrelated subscriptions are never changed. It never
opens CDC, sends MIDI notes or STOP/START, resets USB, changes system/BTX config,
or loads/saves sessions. It neither clears firmware fault 8 nor initiates its
explicit STOP/cleanup/START recovery. BTX exit does not stop the keeper or its route.

USB attribution uses the sequencer's `card=` when provided. On the handheld format
without that field, exact supported raw-MIDI names/directions from `amidi -l` are
joined to actual control character devices, `/sys/dev/char` and sound-card/raw-MIDI
ancestry. Proven control-device aliases are collapsed; multiple physical matches,
missing ancestry or identities changing during the read remain reduced visibility.
Client/card-number equality and USB VID/PID alone never establish identity.

Read-only kernel-ring observations attribute new USB errors to supported paths
and the current device incarnation. The first readable ring is a baseline, not a
new fault. New errors and lifecycle tokens are retained in bounded memory while
ALSA/sysfs discovery catches up. A verified incarnation receives its deferred
errors only when path, device number and lifecycle ordering establish the match;
old-device errors are not transferred to a replacement. Missing or conflicting
evidence stays explicitly reduced visibility, including exhausted history buffers.
Disconnect/replacement records racing a device snapshot prevent attribution to
that stale incarnation. Missing mappings/history, unreadable logs or missing dmesg
are explicitly reduced visibility. This is not USB packet tracing or an H4MIDI
delivery acknowledgement. A latched error remains unavailable for that incarnation
even if its subscription is present: reconnecting a route is not USB repair.
Physical USB/power/kernel failures can exceed what this tool can recover.

The console shows the result for ten seconds (or until a gamepad button), then
returns to Ports. The detached keeper is intended to continue, but external
Ports cleanup can terminate it; see the dArkOS limitation below. State transitions/
errors are timestamped with wall/monotonic time, boot, process and device identity in
`/tmp/brotracker-<uid>/brotracker-midi-connect.log` (128 KiB, three rotating backups,
named `brotracker-midi-connect(1).log` through `(3).log`, following BTX naming). Duplicate
unchanged observations are not logged. `midi-connect-status.json` is atomic current
status, with stale/dead/previous-boot keeper checks; it is not a delivery receipt.
`brotracker-midi-connect-launch.log` holds each console invocation separately,
retaining three previous launches with the same parenthesized naming. Run
`python3 -B /roms/ports/brotracker/midi-connect.py --status` for read-only status.
The process lock is held for the keeper lifetime, including concurrent launches.
To stop monitoring deliberately, identify its current PID from a fresh status and
send TERM to that keeper only; the intended subscription is left intact. No broad
route teardown, USB reset or playback command is performed at shutdown. After a
reboot, launch the Ports entry again. Updating installed helper files does not
replace an already-running keeper; stop that keeper explicitly before upgrading.

All MIDI status, health and lock files use the same private `brotracker-<uid>`
directory, outside the checkout. A valid absolute `TMPDIR` replaces `/tmp` for
the helper, MIDI Connect launcher and BTX; use the same environment for all three.
The unrelated BroTracker Diagnostics collector keeps its existing persistent
`$HOME/BroTracker/diagnostics/` location; it has no dependency on this MIDI runtime
directory. These MIDI files, like `/tmp/brotracker-arkos.log`, may disappear on
reboot. Collect logs before rebooting. Existing legacy logs/state under `~/BroTracker` are moved
without replacement into a unique `legacy-*` archive in the new runtime directory
on the next keeper start. Active legacy keeper/BTX locks block migration; close
both before upgrading all cooperating files together. For migration alone use
`python3 -B /roms/ports/brotracker/midi-connect.py --migrate-legacy`; it starts no
keeper and changes no ALSA route. Project/configuration files are never moved.

### dArkOS Ports lifetime limitation (2026-11-03)

On the inspected Debian 13 dArkOS installation, the EmulationStation game-end
hook `~/.emulationstation/scripts/game-end/batt_warning_stop.sh` runs
`sudo killall python3` when `~/.config/.NOVERBALBATTERYWARNING` is absent. The
2026-11-03 bounded process/signal capture matched sender PID 132609 (`killall
python3`), parent PID 132603 (`sudo killall python3`) and the hook's shell back
to EmulationStation. Keeper PID 132492 received that sender's SIGTERM and exited.
Thus a battery-warning cleanup intended for another Python program also kills
the keeper; no Python exception or missing-port exit was involved in that run.
The same broad cleanup also targets ordinary Python observers.
The keeper now sets its main thread's Linux name to `btx-midi-keep` (13 ASCII
bytes) using `PR_SET_NAME`, and checks it with `PR_GET_NAME` before publishing
readiness or making any route attempt. The launcher verifies the live kernel name
and fresh PID/start-time/boot status before confirming startup or reusing an
existing keeper. A concurrent launch waits up to two seconds for initialization;
failed/unconfirmed naming is an explicit launch error, with details in the log.
Old unnamed keepers require an explicit stop/relaunch after upgrade. No battery
setting or system hook is changed. PID-targeted SIGTERM retains normal shutdown;
this is a distinct process identity, not a signal-ignore or restart workaround.
On dArkOS, physical verification on 2026-11-03 confirmed timeout and B-button
return, reuse of the same keeper PID, and fresh status beyond thirty seconds
after Ports return. Explicit PID-targeted TERM stopped it and released the lock.
After relaunch, Teensy arrival after Ports return established the supported route;
the user confirmed PT8211 audio, H4MIDI flashes and audible WASP playback with
the unpowered OTG hub, externally powered H4MIDI and Wi-Fi absent. The same named
keeper remained fresh more than 100 seconds after BTX exit and the captured
`killall python3` hook. No new H4MIDI USB error was reported in this run.
ArkOS verification of this process-name change remains pending. This single
dArkOS run does not establish general USB stability or simultaneous Wi-Fi use.
Check fresh BTX keeper status rather than assuming detachment guarantees survival.
An ALSA subscription can remain after keeper exit, so earlier confirmed offline
playback does not prove persistent monitoring. A missing Teensy while it is
physically disconnected is not evidence for the earlier playback failure or for
the keeper's termination. Scoping the OS hook to the battery-warning process
remains a useful upstream improvement, independent of the keeper's own identity.

For physical verification, initially leave Teensy disconnected: keep the existing
battery-warning setting, return from Ports by timeout and early button press, and
verify the same named PID and advancing status timestamps after at least thirty
seconds and repeated launches. Check explicit PID-targeted TERM and relaunch too.
Then prepare offline logs and swap Wi-Fi for Teensy while H4MIDI stays connected;
verify discovery after Ports return, user-started PT8211/WASP playback and keeper
survival after BTX exits. Do not connect all three devices just for this check.

This is the temporary H4MIDI-only route authority. A future BroTracker MIDI host
must explicitly take over this ownership rather than run a competing route writer.

### BTX health/status exchange

ArkOS BTX displays route subscription state, attributed USB-path errors and Teensy
MIDI health separately. Windows retains the shared firmware-health display without
an ArkOS route-keeper connection. Neither display proves H4MIDI/DIN/WASP delivery.
Start the keeper through Ports explicitly; BTX does not start or replace it.

The keeper atomically publishes `midi-connect-status.btx`, a bounded four-line v1
observation consumed by BTX, alongside its existing JSON status. Header:
`BTMIDIROUTE 1 pid process_start boot_id monotonic_ms state subscription`;
following lines are `detail ...`, `usb ...`, `visibility ...`. Text cannot inject
extra lines. BTX rejects malformed, future-dated, dead/reused-owner, prior-boot or
older-than-20-second observations. A missing/stopped keeper is shown without
blocking editing, transport, file operations or internal audio.

BTX alone atomically publishes `btx-midi-health.state`, at most once per second:
`BTMIDIHEALTH 1 pid process_start boot_id monotonic_ms connected supported known configured fault cleanup reason firmware_age_ms transport`.
The keeper rejects dead/reused-owner, prior-boot, malformed and older-than-three-
second publications, and also expires the underlying firmware observation at
three seconds including its age before publication. Firmware STATUS replies renew
same-state health without logging every reply. No extra CDC query/owner is added.
BTX removes its observation on orderly exit; a crash expires through owner/freshness
checks. With BTX closed, firmware health is unknown while route maintenance continues.

The BTX health-publisher lock prevents competing writers from overwriting that
file; it is not general BTX single-instance or serial-port exclusion. The keeper
never opens CDC. Published health and errors are observations, not commands.
Keeper logs add BTX owner/health/cleanup transitions but not every heartbeat.
BTX logs route/USB visibility transitions. No status-file field can issue transport,
change the session or alter an unrelated ALSA route.

Fault 8 is a Teensy USB completion stall, separate from Linux H4MIDI URB errors.
Current firmware permits recovery only at an explicit stopped START boundary after
cancellation/cleanup. BTX shows `STOP; wait STOPPED + cleanup=0; then START` while
cleanup is uncertain or transport running/paused; once stopped and clean, it shows
`START requests recovery`. Missing USB must be restored first. Cleanup completion
alone never clears a latched fault. No hidden STOP/START, automatic playback restart,
USB reset or firmware command was introduced. If cleanup cannot finish, MIDI remains
uncertain while internal playback remains usable. See [MIDI recovery policy](../../docs/MIDI_FOUNDATION.md#midi-output-failure-isolation-2026-10-22).

Run `python3 -B tools/arkos/check-midi-connect.py` and
`bash tools/arkos/check-install-port.sh` for isolated checks. Visible console
feedback and MIDI delivery on ArkOS and dArkOS remain physical checks.

## Current handheld controls

Use [BTX_CONTROLS.md](../../docs/BTX_CONTROLS.md), the single current desktop and
handheld reference, including mapping requirements, dated user confirmations,
validation limits and reconnect-hold Restore/Publish semantics. It supersedes
legacy bindings in historical observations below.

## Installed runtime configuration (ArkOS and dArkOS)

BTX reads `/roms/ports/brotracker/config/config.ini` directly, resolved relative to
the installed executable with SDL_GetBasePath. Launching from another shell working
directory does not change it. Windows likewise reads `config/config.ini` beside
BroTrackerTerminal.exe (`deploy/windows/config/config.ini` in this checkout).
There is no SDL preference, XDG or HOME fallback for runtime config. Startup logs
the resolved absolute path and load/error status. Missing config uses built-ins;
the example is never loaded automatically. Restart BTX after editing.

Run as the normal Ports user using the existing writable installation; no sudo,
chmod 777 or unrelated permission changes are needed. Installation/update copies
only the example and preserves an existing `config/config.ini` byte-for-byte.
The console's existing `output=pt8211` and `row_numbering=h0` file is used directly.
Logging and diagnostic recordings keep their existing locations; the per-user
listening evidence directory is independent of the installed config directory.

Merge into the existing file without removing [btx], [logging], [editing] or
other existing settings (keep required schema fields):

```ini
[audio]
output=bridge
```

`bridge`, `pt8211` and `audio_shield` are accepted lowercase values, with trimmed whitespace.
The Shield route requires rebuilt BTX/firmware; see [production headphone output](../../docs/AUDIO_SHIELD_OUTPUT.md).
An explicit value selects the Teensy route, not the handheld playback device.
Missing/empty values keep the active route or use firmware USB_BRIDGE on a fresh
Teensy boot. Invalid values warn and ignore the audio override. Restart BTX to
reload; changing an already activated route also requires restarting Teensy.
Reconnect resends the preference but never starts playback or resets an unchanged
route. The acknowledged route must match; a different locked route is rejected
with restart-required. PT8211 is manual only; no presence detection is enabled.

Optional development MIDI compensation uses the same shared setup as Windows:

```ini
[audio.bridge]
midi_delay_ms=25
```

Add this owner-selected handheld test value to the existing installed
`/roms/ports/brotracker/config/config.ini`, preserving other values. Missing means
zero; examples/defaults stay zero. Only whole integers 0..160 are accepted.
Restart BTX after editing. Updated Teensy firmware is required for nonzero delay;
only confirmed initialized Bridge applies it (PT8211/Shield acknowledge zero).
BTX verifies the current value first, then asks for user STOP/cleanup if a change
is needed. There is no automatic STOP/reset/playback. Logs/UI show configured and
acknowledged effective milliseconds separately; failures block START/CONTINUE,
while PAUSE/STOP remain available. See [protocol and activation](../../docs/MIDI_FOUNDATION.md#configuration-and-acknowledged-setup).

Packages/installations carry `config/config.ini.example` as an example
inside the application tree. Neither launcher nor installer creates or overwrites
the actual runtime config. Merge the example manually if a user file exists.
`--safe-config` on direct invocation bypasses the file; the direct launcher
forwards arguments. Shared Linux tests are not physical handheld tests: ArkOS
and dArkOS hardware checks remain separate and pending for this integration.

### Local configuration verification, 2026-10-19

The existing Eoan ARM64 QEMU root was available; all 10 CTest cases and the
ARM64 interpreter/dependency/symbol-version/relocation checks passed. The rebuilt terminal
loaded both bridge and pt8211 preferences headlessly, logged the then-current SDL
paths (superseded by executable-relative config above) and preserved all test INI
bytes (including btx/logging/editing settings).
Launcher/installer fixture tests passed then-current preference-path export/fallback and
preservation checks. No new package was staged and no handheld was contacted.
ArkOS physical bridge/listening/reconnect checks remain pending. dArkOS physical
checks remain independently pending; local ABI/host tests validate neither device.
The connected Windows Teensy passed serial bridge acknowledgment, START/STOP and
telemetry/reconnect checks; those are not handheld or listening results.

## One-time serial access setup on ArkOS/dArkOS

For the user running Ports (usually `ark`), if the CDC port is owned by
`root:dialout` with mode `0660`, grant membership once:

```sh
sudo usermod -aG dialout <runtime-user>
```

Replace `<runtime-user>` with the actual account, for example `ark`. Restart
existing login/desktop sessions or reboot so they inherit the new groups.
Do not run BTX as root or change device permissions. The installer only advises;
when installing with sudo it uses `SUDO_USER`, not root. Set
`BROTRACKER_RUNTIME_USER=ark` explicitly if installing for another account.

BTX distinguishes a detected Teensy with serial access denied from USB absence.
It logs the port and setup guidance once per changed failure and keeps retrying.
Missing USB enumeration is a separate problem: group membership cannot make a
USB device absent from sysfs appear, or create missing CDC/ALSA interfaces.

Observed on dArkOS: `/dev/ttyACM0` was `root:dialout`, mode `0660`, and `ark`
lacked membership. Adding `ark` to dialout and rebooting restored serial access.
With the hub setup, a separate boot-time issue still left Teensy absent from
sysfs, CDC and ALSA until physical reconnect. After reconnect BTX reached IDLE
and playback was clean; a subsequent L1 restart caused one audible click with
no pauses. Boot enumeration and intermittent clicks remain unresolved; the
click's cause is unknown.

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

## Build, check and prepare Git delivery

From WSL/Linux in the repository:

```sh
sudo update-binfmts --enable qemu-aarch64
bash tools/arkos/prepare-package.sh
```

This is the complete delivery command: it builds/tests in the established Eoan
root, checks ABI/dependencies, refreshes **deploy/arkos**, verifies the packaged
executables against the build, refreshes artifact identity metadata, writes
SHA256SUMS, and tests clean installation plus user-config preservation. It does
not stage, commit, push, SSH, install on a handheld or upload firmware. Existing
bridge diagnostic metadata is refreshed for the new binary identities; no
physical audio diagnostics are run.

`bash tools/arkos/run-in-eoan.sh` alone is compilation/testing and staging in the
build root, **not repository/Git delivery**. Do not stop there when preparing a
console update. The lower-level build behavior is documented below.


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

## Review the prepared repository package

`prepare-package.sh` copies the complete verified package into `deploy/arkos`
after successful build checks. Required delivery files are both ARM64 executables,
launch.sh, config/config.ini.example, the tune, both font files, SHA256SUMS and
the existing artifact identity JSON. The package checker verifies source assets,
ARM64 headers, MIDI negotiation format, checksums, Git visibility and unchanged
bytes through Git filters. It allows new untracked delivery files for review;
they must be included with the tracked binary updates in the eventual Git delivery.

Run these again without rebuilding when reviewing a prepared package:

```sh
bash tools/arkos/check-package.sh
bash tools/arkos/check-install-port.sh
```

The installer validates all package sources and checksums before destination
writes. Its isolated test copies only the delivered package; it never fills
missing files from the source tree. Missing config and corrupt binary cases
must fail preflight. Clean/repeated installations preserve executable-relative user config
(including MIDI settings), user data and launcher backups.

ARM64 package binaries/assets intentionally travel through Git; Windows generated
packages remain untracked. Review current sources and **all** package changes
together before a later commit/transfer. No Git index write occurs in these tools.
Existing executable modes for both binaries and launch.sh are retained.

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
new or legacy `BroTracker.sh` launcher, installs the complete checksummed application package and new launcher, then removes the legacy launcher only after successful
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

For current input use the [controls reference](../../docs/BTX_CONTROLS.md).
Audio, broader controller mapping and EmulationStation handoff checks remain
separate from the confirmed controls/workflows. Local checks:

```sh
bash tools/arkos/check-install-port.sh
bash tools/arkos/check-port-launcher.sh
```

Both use isolated temporary installations/stubs, never the handheld. The
launcher check temporarily uses and restores `/tmp/brotracker-arkos.log`.

During initial startup and runtime recovery, the supervisor polls current sysfs
capture endpoints about once per second during backoff. A new or replaced
matching USB/legacy capture endpoint interrupts the wait and triggers fresh
`--auto` discovery. The bridge still decides identity, viability and ambiguity;
an unchanged busy/unsupported endpoint keeps the capped backoff. Missing or
transient sysfs entries do not prevent timed retries. Appearance interrupts are
logged. CDC IDLE does not guarantee audio readiness: this reduces recovery delay,
but does not guarantee the first START waits for audio, and does not fix missing
USB enumeration after boot. No START is sent or replayed by recovery.

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
2. With audio configured, START using Start. Unplug Teensy while BTX stays open.
   Confirm responsive controls, CDC disconnect and logged runtime audio recovery.
3. Leave it absent for several minutes: attempts must continue at the capped
   interval, with one supervisor and at most one bridge. Reconnect; confirm
   fresh PCM names, handshake and audio configuration without automatic START.
   Press Start explicitly and verify live audio returns without restarting BTX.
4. Repeat unplug/replug, then exit during a recovery backoff using L1+Start or
   window close. Confirm cleanup messages, `pm_finish`, and no remaining bridge
   or supervisor (`ps -ef | grep -E 'BroTracker|audio_supervisor'`).

These are pending physical checks, not evidence of post-reboot USB detection
or subjective audio quality. Audio clicks and USB reset/power fixes are outside
this change.

## BroTracker Diagnostics Ports entry

The installer also installs `tools/arkos/port/BroTracker Diagnostics.sh` as
`/roms/ports/BroTracker Diagnostics.sh` (0755), backing up any existing entry.
It starts the unchanged normal Terminal launcher, controls and audio supervisor.
It never sends START, resets USB, changes audio routing, or requires SSH.
After transferring the reviewed checkout, run on ArkOS:

```sh
bash tools/arkos/install-port.sh
# Prefer selecting BroTracker Diagnostics from the Ports menu:
bash "/roms/ports/BroTracker Diagnostics.sh"
```

Each invocation creates a private, collision-safe directory at
`$HOME/BroTracker/diagnostics/YYYYMMDD-HHMMSS.XXXXXX/`. Previous runs remain
untouched. Do not launch the normal and diagnostic entries simultaneously:
the normal launcher uses one shared `/tmp/brotracker-arkos.log` and audio device.

- `collector.log`: startup date, uptime, kernel, identity, executable SHA-256
  hashes, collection failures, shutdown status and run path.
- `snapshots.log`: USB sysfs identities and interface drivers, ttyACM/ALSA
  ancestry and permissions, detection changes, capture/playback listings and
  read-only per-card mixer contents. Snapshots occur approximately every five
  seconds; collection duration can extend this interval.
- `kernel-N.log`: full available kernel rings, saved initially before BTX
  starts and whenever the polled ring changes. This preserves existing early
  boot USB/audio evidence before EmulationStation launches the entry. It is
  read-only polling, not a privileged kernel follower: inaccessible or already
  overwritten messages cannot be recovered, and rapid ring wrap can lose events.
- `btx-stream.log`: incremental copies approximately once per second, including
  PCM configuration, discontinuities, runtime recovery and shutdown summaries.
  `btx-final.log` saves the final shared log after normal launcher cleanup.
- `exit-status`: present only after orderly finalization. Its absence after a
  reboot/power loss marks an interrupted run; previously written snapshots,
  kernel rings and stream data remain useful.

Collected output is written throughout the run and filesystem-flushed each
collector cycle using available `sync`; nothing waits entirely for shutdown.
This reduces loss but cannot guarantee storage survival during sudden power
failure. Missing tools/read permissions are recorded without preventing BTX;
no `lsusb`, `rg`, Python, sudo or extra packages are required on the handheld.
If the persistent directory cannot be created, the wrapper reports the error
and still launches BTX normally, without persistent diagnostic collection.
HUP is ignored so network/session loss does not end the run. Normal exit or
TERM/INT stops/reaps the launcher and diagnostic worker/retry sleep. The normal
launcher still performs its own bridge cleanup and `pm_finish`.

### Handheld checklist (no SSH or keyboard needed)

Use a separate cold boot/run for each layout below. Connect the devices before
powering on; do not unplug anything before collecting the first snapshot.
Keep power supply, sample, listening level and other conditions constant.

| Run | Wiring at boot |
| --- | --- |
| 1 | T4.1 directly connected through OTG; no hub or Wi-Fi dongle |
| 2 | Only the powered hub and T4.1 on OTG |
| 3 | Powered hub with T4.1 and Wi-Fi dongle |
| 4 | Original chained-hub arrangement; record hub order, ports and supplies |

For **each** run:

1. After EmulationStation loads, immediately select **BroTracker Diagnostics**
   from Ports. Wait 20 seconds without unplugging or pressing START. Record the
   wiring, boot time, UI waiting/ready state, and whether it connects unaided.
   The initial kernel snapshot collects retained boot evidence even though the
   diagnostic entry starts later.
2. If ready, press **Start** once, listen through a full sequence, and record
   clean/distorted/silent audio and whether both samples stay synchronized.
   Note the approximate elapsed time and visible UI state. If still waiting,
   record that instead; do not substitute a reboot before the recovery test.
3. While diagnostics/BTX stays open, unplug **only T4.1** for at least 30 seconds
   (one run should use several minutes), then reconnect it to the same port.
   Wait at least 20 seconds for CDC and fresh audio discovery. Connecting must
   not automatically START. Record time to ready and any USB/audio differences.
4. Press **Start** explicitly after ready, listen again and record whether audio
   recovered and whether distortion changed. Start while playing requests Pause;
   another Start requests current-row Continue. B requests STOP/reset and stays
   in BTX; L1+Start requests clean exit. Do not power off until Ports returns.
5. Record the observed result on paper/phone, with layout/run order and elapsed
   times, to match the unique run directory later. Diagnostic files cannot
   determine subjective distortion. If testing recovery-exit cleanup separately,
   exit while T4.1 is absent and verify Ports returns promptly.

For an unexpected reboot, preserve the interrupted directory and start a new
run after boot; never reuse/delete the earlier directory. Retrieve the whole
`$HOME/BroTracker/diagnostics/` folder later when network access is available.
No diagnostic entry changes USB boot enumeration, hub power or audio buffers.

Local-only checks (Python is needed only by the existing development lifecycle
suite, not by the installed diagnostic entry):

```sh
bash tools/arkos/check-diagnostics.sh
bash tools/arkos/check-install-port.sh
bash tools/arkos/check-port-launcher.sh
```

### USB audio validation status

The original cold-boot distortion is resolved in hardware validation run
`20261006-220244.Ph7ox3`. The [USB audio validation record](../teensy_usb_audio/README.md#hardware-validation-20261006-220244ph7ox3)
contains the trace/WAV results, xrun counters and listening observations.
Intermittent clicks/pauses and ArkOS ALSA xruns remain a separate open issue;
see [TODO tracking](../../docs/TODO_LIST.md#audio-diagnostics). Further collection
can use the existing diagnostic PCM recordings and timeline below.

### Diagnostic PCM recordings and timeline

Diagnostics now opts the existing bridge into [bounded WAV capture recording](../alsa_bridge/README.md#opt-in-diagnostic-capture-recording).
The normal Terminal entry remains recording-disabled. Each bridge after
recovery creates a new unique WAV in the current persistent run directory;
`events.tsv` identifies it by bridge PID/path, selected devices and recorded
frame count. Recording includes initial silence and stops after 180 seconds
of captured PCM (7,938,000 frames), independently of START. Overflow/write
failures are explicit; audio keeps running. No recording or timeline filesystem
writes occur in the audio loop. UI/CDC events are labeled delayed collector
observations; source-timestamped bridge/supervisor events remain distinct.
Finalization drains accepted bytes and joins the writer on normal exit or USB
failure; SIGKILL/power loss cannot guarantee a finalized WAV.

Short hardware collection procedure after separate review/transfer:

1. Cold-boot with Teensy directly attached through OTG; launch **BroTracker
   Diagnostics** promptly. Wait ready, press Start and play the complete sequence.
2. Press B to STOP/reset and stay. Unplug/replug Teensy without closing BTX.
3. Wait for CDC and audio bridge recovery, then explicitly Start and play again.
   This uses a fresh bridge instance and WAV; recovery never sends START.
4. STOP/reset with B and EXIT with L1+Start. Wait for Ports to return, then
   upload the **complete diagnostic run directory**, including all WAVs and
   `events.tsv`. Note which listening interval sounded distorted or clean.

Do not delay START beyond the 180-second capture cap; silence counts. The
manifest and filenames remove the need to remember boot counts. No new
handheld tools, SSH or keyboard are needed during collection.

## MIDI package delivery repair (2026-10-22)

The Oct 20 BTX was still tracked under deploy/arkos after later Eoan builds passed:
those builds had not copied their artifacts into the delivery directory. The
missing config example was not ignored; it had never been delivered/tracked.
The installer test masked this by injecting a source-tree copy into its fixture.
The delivery command and regression checks above close both gaps. Firmware START
requirements and MIDI negotiation are unchanged. Physical console validation is
still the user's task; a local package check is not a handheld retest.

After the reviewed repair has been committed/published by the maintainer, update
from the existing **feature/midi-foundation checkout on the console**. Preserve
an untracked example manually copied as a workaround before pulling the new
tracked example (this is not the runtime user config):

```sh
if [ -f deploy/arkos/config/config.ini.example ] &&
   ! git ls-files --error-unmatch deploy/arkos/config/config.ini.example >/dev/null 2>&1; then
    backup=$(mktemp /tmp/brotracker-config-example.XXXXXX)
    mv deploy/arkos/config/config.ini.example "$backup"
    printf 'Manual example saved as %s\n' "$backup"
fi
git pull --ff-only origin feature/midi-foundation
(cd deploy/arkos && sha256sum -c SHA256SUMS)
bash tools/arkos/install-port.sh
(cd /roms/ports/brotracker && sha256sum -c SHA256SUMS)
bash "/roms/ports/BroTracker Terminal.sh"
```

After exit, inspect `/tmp/brotracker-arkos.log` for the current build and transport
result. With existing `[logging] mode=debug`, the wire log also shows
`BTMIDI1 SET 1 channel` and its matching `BTMIDI1 SELECTED 1 channel` before
START; basic logging can omit these wire/acknowledgment lines. Keep all other
existing runtime config; absent
[midi] uses channel 12. No console compiler or Teensy upload is needed.

The MIDI Connect screen refreshes fresh keeper observations every 0.5 seconds during
the existing 10-second button wait, repainting only changed text. Confirmed missing
devices and errors are red; unknown devices and reduced USB visibility are bright yellow (bold ANSI 33, supported by the handheld Linux console).
Text labels retain their meaning when color is disabled.
Reduced visibility is not an observed USB error. Unknown MIDI health stays hidden;
reported faults remain visible. External MIDI delivery remains unverified.

The screen observes only its 10-second visible window. If Teensy is connected
after that window, reopen MIDI Connect to view the new keeper observation.
The 2026-11-03 follow-up captured a 21:09:17 launch before Teensy device 18
appeared at 21:10:03; the route was verified at 21:10:04. This timing does not
establish a failed refresh while the screen was visible.

MIDI Connect observes Teensy and H4MIDI independently. FOUND requires an exact,
unambiguous directional port and the expected kernel ALSA client, rechecked in
the combined listing. Available USB identity must match the supported profile.
Missing USB mapping alone does not invalidate that ALSA observation: the device
can be FOUND while USB visibility is reduced. A missing or ambiguous port, wrong
client/profile, or failed observation is reported separately for each device;
the keeper JSON log records the reason. Device FOUND does not imply a route or
verified external delivery. Routing still requires both supported devices and
its existing identity rechecks.

Read-only inspection on dArkOS, 2026-11-03 21:34 CET, verified H4MIDI with Wi-Fi
connected and Teensy absent: kernel client H4MIDI-WC, output 20:0, raw MIDI port
H4MIDI-WC MIDI 1, USB 7104:2100 at 1-1.3, device 4. Raw card aliases 1 and 7
resolve through the existing identity routine. There was no observed Wi-Fi
limitation preventing this identification. This boot's identity differs from
the earlier tests; previous device numbers/errors are not evidence for it.
The bottom button-message layout was physically confirmed by the user.
Independent discovery still needs physical screen verification after deployment.
An already-running keeper retains its old code; copying a helper alone does
not upgrade its observations. Older snapshots are displayed conservatively.

## Warning-task asset delivery (2026-11-04)

The normal delivery and installer include native Terminus 6x12/8x14 BDFs and
font notices under `assets/fonts/terminus`, canonical `assets/translations/en_US.ini`,
the runtime warning SVG under `assets/icons`, and `licenses/NanoSVG.txt`.
These paths are included in the existing checksum/package and isolated installer
checks. User `config/config.ini` remains untouched.

For a delivery-only refresh with the existing Eoan root/toolchain, use
`BROTRACKER_ARKOS_DELIVERY_ONLY=1 bash tools/arkos/prepare-package.sh`.
It builds only BroTrackerTerminal/BroTrackerAlsaBridge and skips the full test
suite while retaining the existing ABI, package and isolated installer checks.
The ordinary workflow without that flag is unchanged. See
[warning validation](../../docs/WARNING_VALIDATION.md) for the separate owner-reported
Windows confirmation, SD validation scope, firmware upload instructions and
pending physical handheld validation.

## Standing host delivery workflow

Windows BTX is the development platform; ArkOS/dArkOS are the physical validation
platforms. Relevant host changes must include refreshed Windows **and** ARM64
deliveries, with current runtime assets and existing installer compatibility,
ready for the owner's Git commit/push and handheld pull/install. Preserve user
configuration. Completing only the Windows package does not complete delivery.
Use the established delivery-only ARM64 workflow when broader tests are not
needed; physical validation and Git operations remain owner-controlled.

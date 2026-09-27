# ArkOS Eoan ARM64 build and deployment

The first display test targets the console's Ubuntu 19.10 (Eoan) runtime:
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
  g++ make cmake pkg-config libsdl2-dev file binutils
```

The runner uses a private mount namespace; /workspace is the read-only
checkout, with /proc and /dev available only for the lifetime of the command.
It clears inherited host environment variables. QEMU must be registered with
binfmt_misc so ARM64 subprocesses execute inside chroot.

## Build and check

```sh
bash tools/arkos/run-in-eoan.sh
```

This builds every existing CMake target in /build/brotracker and runs CTest.
The repository's CMake features work with Eoan's 3.13.4; the minimum is 3.13.
No newer target libc or SDL2 is installed to obtain CMake.

The script requires the matching compiler/runtime/development package versions,
selects Eoan's pkg-config SDL2 files, and checks BroTrackerArkOSUI for:

- AArch64 ELF and /lib/ld-linux-aarch64.so.1 interpreter;
- dynamic system SDL2 dependency, expected system libraries, and no RPATH/RUNPATH;
- required GLIBC <= 2.30, GLIBCXX <= 3.4.28, and CXXABI <= 1.3.12;
- dependency and relocation resolution with ldd -r in the Eoan root.

Only after those checks pass does it stage the executable, launch.sh, font
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
```

The package includes the current working-tree sources, including any uncommitted
edits. Review the source and package together before a later commit and
repository transfer. Committing or pushing is not part of the build scripts.
When later adding the package to Git from Windows, record executable modes for
BroTrackerArkOSUI and launch.sh with git update-index --chmod=+x after git add.
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

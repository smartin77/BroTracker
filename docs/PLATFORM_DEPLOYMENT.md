# Platform Deployment

This document collects current deployment findings, experiments and design
directions for running BroTracker on Linux-based handheld platforms.

It is intentionally a working document. Some approaches described here may
change or be discarded as platform bring-up continues.

## Current Platform Targets

The current handheld bring-up focuses on R36S-class devices running Linux.

Platforms tested so far include:

- ArkOS
- dArkOS

The purpose of these tests is not only to make BroTracker run on these
distributions, but also to identify which parts of deployment and platform
integration can remain common between different Linux environments.

Teensy 4.1 remains the primary realtime hardware target. Linux handheld
deployment concerns the BroTracker UI and future host-side functionality.

## Native ARM64 Bring-up

BroTracker can be configured and compiled natively on the tested ARM64
handheld environment.

The current ArkOS bring-up has successfully built:

- the shared BroTracker code;
- the UI renderer;
- BroTracker Terminal for ArkOS (`BroTrackerTerminal`);
- the desktop-style UI preview target where applicable;
- the host-side tests.

The host-side test suite has also run successfully on the device.

Native compilation on the handheld is useful during development and platform
bring-up, but it is not intended to define the final distribution model.

A future user installation should not require a compiler, CMake or a source
checkout on the handheld.

Prebuilt platform packages are the preferred long-term direction.

## Linux Distribution Differences

ArkOS and dArkOS provide different userspace environments even when running
on similar handheld hardware.

Observed differences include:

- distribution and package versions;
- compiler versions;
- CMake versions;
- SDL2 versions;
- available development packages;
- filesystem and launcher integration.

BroTracker should therefore avoid depending unnecessarily on one specific
distribution version.

Platform requirements should eventually describe required capabilities and
interfaces rather than a single fixed Linux distribution.

## SDL Platform Layer

SDL2 is currently used for Linux handheld display and input integration.

Shared BroTracker UI rendering remains independent of SDL. The shared
renderer produces the BroTracker framebuffer, while the platform-specific
SDL backend is responsible for presenting that framebuffer on the device.

Input should follow the same separation.

Where available, platform launchers may provide SDL GameController mappings.
BroTracker should consume semantic controller input rather than embedding
device-specific physical button numbers in shared application logic.

Exact controller and launcher integration remains under active bring-up.

## Application Location

ArkOS/dArkOS and Windows are UI hosts. Their process working directory is not
BroTracker's project workspace: the **Teensy SD card is always the project/data
workspace**, separate from the host application's files.

Windows delivery is portable: BroTrackerTerminal.exe may run from any suitable
disk location without installation, with its required runtime files/assets beside
it. `deploy/windows` is the repository's delivery location, not a required runtime
path or project-storage root.

On both hosts runtime configuration is
`<executable-directory>/config/config.ini`, resolved independently of the shell
working directory. Current ArkOS/dArkOS remains
`/roms/ports/brotracker/config/config.ini`. Missing config uses built-in defaults;
the example is template-only. Existing host log and diagnostic paths remain
unchanged; see [Logging](LOGGING.md) and the
[listening-evidence location](../tools/alsa_bridge/README.md#manual-listening-correlation-normal-ports-launch).

A future Linux distribution may expose application files through `/opt/BroTracker`;
that is an optional deployment mapping, not a current canonical path requirement.
Application logic must resolve its own resources without treating the process
working directory or a host application mapping as project storage.

## Persistent Storage

Project data, Tunes and Samples belong to the Teensy SD workspace. Host runtime
config, logs and diagnostic recordings are separate host files, not substitutes
for that workspace. See [File Formats](FILE_FORMATS.md#projectdata-workspace-and-host-paths)
for the storage boundary, explicit working-session operations and planned module contracts.

This decision neither mounts the Teensy SD card as a host drive nor establishes
implemented file browsing or module/tune persistence. Explicit stopped development
file and working-session operations use the Teensy SD foundation as documented in
[File Formats](FILE_FORMATS.md#working-session-separate-from-tunesprojects); no host
filesystem mapping or synchronization mechanism is implied.

## Application Data and User Data

Deployment should distinguish between application files and persistent user
data.

Application files may include:

- BroTracker executables;
- bundled assets;
- built-in resources;
- platform integration files.

Persistent user data may include:

- projects;
- Tunes;
- samples;
- configuration;
- user-installed modules.

For BroTracker, project/data workspace content stays on the Teensy SD card;
host configuration/logs/diagnostics follow their separate host path contracts.
These categories do not imply that project data and application files share a
filesystem or are visible through the same host path.

## Platform Storage Mapping

A future Linux deployment may use `/opt/BroTracker` as an application mapping;
current executable-relative paths do not require it.

This does not require the application files to be physically stored on the
root filesystem.

A platform may map `/opt/BroTracker` to another storage location when
appropriate. This allows platforms with persistent user or application
storage outside the root filesystem to keep BroTracker there while preserving
the same application-visible layout.

A conceptual installation may contain:

    /opt/BroTracker/
        bin/
        assets/
        modules/

The mapping mechanism is a platform deployment responsibility and must remain
transparent to BroTracker application logic.

## Dedicated Boot-Only System

A future dedicated BroTracker system could expose application files through
`/opt/BroTracker`; its project/data workspace still belongs to the Teensy SD card.

Its physical implementation may differ from a conventional Linux
installation. For example, `/opt/BroTracker` could be backed by a dedicated
filesystem or other application storage while persistent user data resides
separately.

The dedicated system therefore does not need to reproduce ArkOS directory
conventions such as `/roms` or `/roms/ports`.

## Recovery and Reinstallation

A useful deployment goal is that replacing or reflashing the operating system
does not require rebuilding the user's Teensy SD project/data workspace.

Where supported by the platform, application/user storage should therefore be
separable from disposable system storage.

This also makes development images and experimental operating-system builds
less risky for user-created content.

## Open Questions

The following areas remain intentionally undecided:

- physical backing location for `/opt/BroTracker` on each supported platform;
- mechanism used to expose persistent platform storage as `/opt/BroTracker`;
- placement of host application files and host-only logs/diagnostics, separate from Teensy SD project data;
- future host-path extensions, preserving the current executable-relative config contract;
- module installation locations;
- handling of read-only application storage;
- boot-only system partition layout;
- package/update mechanism;
- migration between ArkOS, dArkOS and future BroTracker systems.

These questions should be resolved from further implementation and hardware
testing rather than fixed prematurely.

## Current ARM64 Git package delivery

Compilation, Eoan-root staging and repository delivery are separate steps.
Use `bash tools/arkos/prepare-package.sh` from WSL/Linux to complete all three,
including tests/ABI checks, source-to-package identity, required config/assets,
checksums and isolated clean installation. Review deploy/arkos binaries and all
required assets for the eventual Git delivery; generated Windows packages remain
untracked. No SSH or handheld installation is implied. See
[exact build/update/install instructions](../tools/arkos/README.md#build-check-and-prepare-git-delivery).

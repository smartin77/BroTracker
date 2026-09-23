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
- the ArkOS SDL2 UI executable;
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

The current development checkout may live under a user's home directory.
This is appropriate for development but is not the deployment location.

On Linux systems, the canonical BroTracker application root is:

    /opt/BroTracker

BroTracker must not depend directly on platform-specific locations such as:

- `/home/ark/BroTracker`
- `/roms`
- `/roms/ports`
- `/ports`

The physical storage backing `/opt/BroTracker` may differ between platforms.

On a conventional Linux installation, BroTracker may be installed directly
under `/opt/BroTracker`.

On platforms such as ArkOS, the actual BroTracker files may reside on
persistent storage such as the Ports/EASYROMS filesystem. The platform
installation process may then expose that installation through
`/opt/BroTracker`, for example by using an appropriate filesystem mapping or
symbolic link.

This keeps the application-visible path stable while allowing each platform
to choose the physical storage appropriate for its environment.

Application resources should therefore be addressed relative to
`/opt/BroTracker` rather than through ArkOS-specific, distribution-specific
or user-home paths.

## Persistent Storage

Persistent BroTracker data depends on the runtime architecture.

For the primary Teensy-based BroTracker architecture, persistent Tune data,
projects, samples, configuration and other content required by the realtime
system are stored on the SD card attached to the Teensy. The Linux handheld
must not become the authoritative storage location for this data.

Persistent data stored by the Linux host is primarily relevant to BroTracker
configurations that run without a Teensy, such as future standalone desktop
or handheld versions where the complete BroTracker engine runs locally.

Such host-side persistent data may include:

- Tunes and projects;
- samples;
- user configuration;
- downloaded or installed modules;
- other user-created or user-installed content.

On ArkOS-class systems running a host-only BroTracker configuration, persistent
storage may naturally live on the storage area used for Ports and other user
content rather than inside the replaceable Linux root filesystem.

The exact directory layout and synchronization or transfer model between a
Linux UI and Teensy storage are not yet fixed.

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

These groups may live together on some platforms and on separate filesystems
or partitions on others.

BroTracker should not require them to share the same physical location.

## Platform Storage Mapping

The BroTracker application uses `/opt/BroTracker` as its canonical Linux
application root.

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

A dedicated BroTracker system should use the same `/opt/BroTracker`
application root exposed to BroTracker.

Its physical implementation may differ from a conventional Linux
installation. For example, `/opt/BroTracker` could be backed by a dedicated
filesystem or other application storage while persistent user data resides
separately.

The dedicated system therefore does not need to reproduce ArkOS directory
conventions such as `/roms` or `/roms/ports`.

## Recovery and Reinstallation

A useful deployment goal is that replacing or reflashing the operating system
does not require rebuilding the user's BroTracker workspace.

Where supported by the platform, application/user storage should therefore be
separable from disposable system storage.

This also makes development images and experimental operating-system builds
less risky for user-created content.

## Open Questions

The following areas remain intentionally undecided:

- physical backing location for `/opt/BroTracker` on each supported platform;
- mechanism used to expose persistent platform storage as `/opt/BroTracker`;
- whether application binaries and persistent data share the same storage;
- exact configuration directory;
- module installation locations;
- handling of read-only application storage;
- boot-only system partition layout;
- package/update mechanism;
- migration between ArkOS, dArkOS and future BroTracker systems.

These questions should be resolved from further implementation and hardware
testing rather than fixed prematurely.

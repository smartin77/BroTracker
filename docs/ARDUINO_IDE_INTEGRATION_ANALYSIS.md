# Arduino IDE Integration Analysis

**Date:** 2026-09-04  
**Status:** Current architecture and integration analysis  
**Scope:** Arduino IDE compatibility of the BroTracker reusable library architecture

---

## Purpose

This document records the Arduino IDE integration analysis for the current BroTracker repository structure.

The project uses standard reusable library directories under the repository-level `libraries/` directory. PlatformIO is the current primary build system, while the library layout is intentionally kept compatible with Arduino IDE where practical.

The goal is to maintain a single source of truth for reusable components without hard links, symlinks, bridge files, duplicated implementations, or relocation of reusable code solely for one build environment.

---

## Current Reusable Library Architecture

Reusable components are stored under the repository-level `libraries/` directory.

```text
libraries/
├── scheduler/
│   ├── library.properties
│   └── src/
│       ├── scheduler.h
│       └── scheduler.cpp
│
└── teensy/
    └── diagnostics/
        ├── library.properties
        └── src/
            ├── diagnostics.h
            └── diagnostics.cpp
```

### Platform-independent reusable code

`libraries/scheduler/` contains the platform-independent Scheduler implementation.

It is intended to be reusable by BroTracker runtime code and by other supported build environments.

### Teensy-specific reusable infrastructure

`libraries/teensy/diagnostics/` contains Teensy-specific diagnostics infrastructure such as LED signalling and SD-card logging support.

It is reusable by Teensy firmware and diagnostic tools.

This separation is intentional:

- platform-independent code remains independent of Teensy;
- Teensy-specific infrastructure is grouped under the Teensy library area;
- both are maintained as reusable libraries;
- each implementation has one source-of-truth location.

---

## Why the Repository Uses `libraries/`

The repository-level `libraries/` directory provides a natural boundary for reusable components.

This avoids placing reusable code directly inside an individual diagnostic sketch and avoids maintaining sketch-specific copies.

The architecture therefore follows:

```text
Repository
│
├── libraries/                 ← reusable components
│   ├── scheduler/             ← platform-independent
│   └── teensy/diagnostics/    ← Teensy-specific
│
├── src/                       ← BroTracker application/core source
├── firmware/                  ← firmware application code
└── tools/                     ← development and diagnostic tools
```

The important architectural rule is that reusable implementation code has one canonical location.

---

## PlatformIO Integration

PlatformIO is the current primary build system for the repository.

The root `platformio.ini` defines the `audio_timing_benchmark` environment and uses the repository-level `libraries/` directory for reusable libraries.

The benchmark is located at:

```text
tools/teensy_diagnostics/audio_timing_benchmark/
```

The benchmark consumes the reusable components through normal library includes:

```cpp
#include <scheduler.h>
#include <diagnostics.h>
```

PlatformIO successfully discovers both libraries:

```text
Dependency Graph
|-- BroTrackerDiagnostics @ 1.0.0
|-- BroTrackerScheduler @ 1.0.0
```

The `audio_timing_benchmark` environment has been successfully compiled and linked, producing:

```text
.pio/build/audio_timing_benchmark/firmware.hex
```

This confirms that the repository-level library structure works with the current PlatformIO configuration.

---

## Arduino IDE Compatibility

The reusable libraries use the standard Arduino library structure:

```text
<library>/
├── library.properties
└── src/
    ├── *.h
    └── *.cpp
```

This is intentional because it keeps the libraries recognizable as Arduino-compatible libraries in addition to their use by PlatformIO.

The benchmark therefore does not need:

- repository-relative include paths;
- bridge headers;
- bridge source files;
- hard links;
- symlinks;
- duplicated implementations.

The desired sketch-level interface remains:

```cpp
#include <scheduler.h>
#include <diagnostics.h>
```

### Important limitation

Arduino IDE library discovery is based on its configured library locations. It does not recursively treat arbitrary repository directories as libraries merely because they contain `library.properties`.

Therefore, Arduino IDE compatibility depends on the libraries being exposed through an Arduino-supported library location/configuration.

This document does not claim that a particular Arduino IDE configuration has been physically verified on hardware.

---

## Single Source of Truth

The current architecture intentionally avoids the previous hard-link approach.

There must be exactly one tracked implementation of each reusable component:

```text
Scheduler
→ libraries/scheduler/src/scheduler.*

Diagnostics
→ libraries/teensy/diagnostics/src/diagnostics.*
```

The following approaches are not part of the current repository architecture:

- hard-linked copies;
- symbolic links;
- bridge source files;
- bridge headers containing repository-relative includes;
- copied implementations inside diagnostic sketches.

This makes the repository portable across fresh Git clones.

---

## Relationship Between PlatformIO and Arduino IDE

PlatformIO and Arduino IDE are treated as build/integration environments, not as separate sources of reusable code.

The intended relationship is:

```text
                    ┌──────────────────────┐
                    │   BroTracker repo     │
                    │                      │
                    │     libraries/       │
                    │        │             │
                    └────────┼─────────────┘
                             │
                 ┌───────────┴───────────┐
                 │                       │
          PlatformIO                 Arduino IDE
          primary build              compatible path
```

Both environments should consume the same library source files.

No build environment should receive a separately maintained implementation.

---

## Verification Status

### Verified

- Repository-level `libraries/` structure exists.
- Scheduler is provided as a reusable platform-independent library.
- Diagnostics are provided as reusable Teensy-specific infrastructure.
- `library.properties` files are present.
- Hard-linked sketch-local libraries have been removed.
- No duplicate implementations are maintained.
- PlatformIO discovers both reusable libraries.
- `audio_timing_benchmark` builds successfully with PlatformIO.
- A Teensy 4.1 firmware HEX is generated.

### Not claimed as verified

The following require separate manual hardware/IDE verification:

- Arduino IDE compilation using the current repository configuration;
- Arduino IDE upload;
- execution on physical Teensy 4.1;
- LED diagnostics;
- serial output;
- SD-card logging;
- benchmark measurements on physical hardware.

---

## Architectural Conclusion

The current library architecture is the preferred repository structure for reusable components.

Platform-independent reusable modules belong in the repository-level `libraries/` hierarchy.

Teensy-specific reusable infrastructure belongs in the Teensy-specific part of that hierarchy.

PlatformIO is the current primary build system and successfully consumes this structure.

Arduino IDE compatibility is preserved by using standard Arduino library packaging and conventional library includes, without introducing duplicate source code or filesystem links.

The source of truth remains the Git repository itself.

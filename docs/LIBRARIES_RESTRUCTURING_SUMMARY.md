# BroTracker Reusable Libraries Restructuring - Summary

**Date:** 2026-09-04  
**Status:** Implementation Complete — PlatformIO Build Verified  
**Scope:** Restructured reusable BroTracker components into the repository-level `libraries/` hierarchy

---

## Changes Made

### 1. Repository-Level Library Structure

Reusable components are now organized as libraries:

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

This structure separates reusable platform-independent code from Teensy-specific reusable infrastructure while keeping both under one repository-level library hierarchy.

---

### 2. Scheduler Library

The Scheduler is the platform-independent reusable component.

Current location:

```text
libraries/scheduler/
├── library.properties
└── src/
    ├── scheduler.h
    └── scheduler.cpp
```

The previous source location under `src/runtime/` is no longer the source-of-truth location for the Scheduler implementation.

Consumers use the normal library include:

```cpp
#include <scheduler.h>
```

---

### 3. Teensy Diagnostics Library

The diagnostics infrastructure is reusable Teensy-specific code.

Current location:

```text
libraries/teensy/diagnostics/
├── library.properties
└── src/
    ├── diagnostics.h
    └── diagnostics.cpp
```

The previous location under `firmware/teensy/BroTracker/` is no longer the source-of-truth location for the diagnostics implementation.

Consumers use:

```cpp
#include <diagnostics.h>
```

---

### 4. Removal of Hard-Linked Sketch Libraries

The previous sketch-local hard-linked library structure was removed.

The following structure is no longer used:

```text
tools/teensy_diagnostics/audio_timing_benchmark/libraries/
```

No hard links or symbolic links are required by the repository.

This is important because Git does not preserve hard-link relationships across fresh clones.

The current architecture therefore has a single tracked implementation for each reusable component.

---

## Build System

### PlatformIO

PlatformIO is now the primary build system for the repository.

The root `platformio.ini` defines the `audio_timing_benchmark` environment.

The benchmark remains located at:

```text
tools/teensy_diagnostics/audio_timing_benchmark/
```

The reusable libraries are discovered through the repository-level `libraries/` hierarchy.

PlatformIO reports:

```text
Dependency Graph
|-- BroTrackerDiagnostics @ 1.0.0
|-- BroTrackerScheduler @ 1.0.0
```

The benchmark has been successfully compiled and linked for Teensy 4.1.

A firmware HEX file was generated at:

```text
.pio/build/audio_timing_benchmark/firmware.hex
```

---

## Arduino IDE Compatibility

The libraries use standard Arduino library packaging:

```text
<library>/
├── library.properties
└── src/
    ├── *.h
    └── *.cpp
```

The benchmark therefore uses normal library includes:

```cpp
#include <scheduler.h>
#include <diagnostics.h>
```

No repository-relative include paths, bridge files, hard links, or duplicate implementations are required.

Arduino IDE compatibility is an architectural goal of the library structure, but physical Arduino IDE compilation and hardware execution are separate verification steps.

---

## Source of Truth

The restructuring maintains one source of truth for each reusable component.

```text
Scheduler
→ libraries/scheduler/src/

Diagnostics
→ libraries/teensy/diagnostics/src/
```

There are no maintained copies of these implementations inside the benchmark sketch.

This makes the repository portable across fresh Git clones and avoids filesystem-specific dependency mechanisms.

---

## Verification

### Verified

- [x] Repository-level `libraries/` hierarchy created
- [x] Scheduler organized as platform-independent reusable library
- [x] Diagnostics organized as Teensy-specific reusable library
- [x] `library.properties` files present
- [x] Old hard-linked sketch libraries removed
- [x] Duplicate implementations removed
- [x] PlatformIO discovers both reusable libraries
- [x] `audio_timing_benchmark` compiles successfully
- [x] Firmware links successfully
- [x] Teensy 4.1 HEX generated

### Still Requires Physical/Manual Verification

- [ ] Arduino IDE compilation using the current repository configuration
- [ ] Teensy 4.1 upload
- [ ] LED initialization signalling
- [ ] Serial output
- [ ] SD-card logging
- [ ] All benchmark configurations
- [ ] Timing measurements on physical hardware
- [ ] Completion LED signalling

---

## Architecture Compliance

### D0037 — Reusable Platform Infrastructure

The restructuring supports D0037 by:

- maintaining reusable components under `libraries/`;
- separating platform-independent and platform-specific reusable infrastructure;
- maintaining a single source of truth;
- avoiding duplicate implementations;
- avoiding filesystem links as part of the repository.

### D0038 — Arduino IDE Integration

The library structure supports D0038 by:

- using standard Arduino library packaging;
- keeping reusable code in repository-level libraries;
- using normal library includes;
- avoiding repository-relative includes in sketches;
- avoiding hard links and symlinks;
- avoiding duplicated source files.

PlatformIO is now the primary build environment; Arduino IDE remains a compatible alternative integration path.

---

## Current Repository Model

The resulting separation of responsibilities is:

```text
BroTracker/
│
├── libraries/                         ← reusable libraries
│   ├── scheduler/                     ← platform-independent
│   └── teensy/
│       └── diagnostics/               ← Teensy-specific
│
├── src/                               ← core/application source
├── firmware/                          ← firmware application code
├── tools/                             ← tools and diagnostic applications
│   └── teensy_diagnostics/
│       └── audio_timing_benchmark/
│
└── platformio.ini                     ← PlatformIO project configuration
```

This is the current implemented structure for the reusable-library architecture.

---

## Conclusion

The reusable-library restructuring is complete.

The repository now has a clean, Git-portable library architecture with:

- one source of truth per reusable component;
- clear platform-independent versus Teensy-specific separation;
- standard Arduino library packaging;
- successful PlatformIO integration;
- no hard links, symlinks, bridge files, or duplicated implementations.

The remaining work is verification on the developer's hardware and, separately, confirmation of the Arduino IDE compatibility path.

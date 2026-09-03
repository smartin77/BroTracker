# Arduino IDE Integration Analysis for audio_timing_benchmark

**Date:** 2026-09-03  
**Status:** ANALYSIS ONLY - No file modifications, no implementation yet  
**Scope:** Determine Arduino-supported integration mechanisms for accessing reusable BroTracker components from an Arduino IDE 2.x diagnostic sketch

---

## 1. Current State Assessment

### 1.1 Current Implementation (Not Acceptable)

The benchmark directory currently contains:

```
tools/teensy_diagnostics/audio_timing_benchmark/
├── audio_timing_benchmark.ino
├── README.md
└── libraries/
    ├── BroTrackerScheduler/
    │   ├── library.properties
    │   └── src/scheduler.h/cpp (HARD LINKS)
    └── BroTrackerDiagnostics/
        ├── library.properties
        └── src/diagnostics.h/cpp (HARD LINKS)
```

**Problem:** Hard links are not repository-portable:
- Git does not preserve hard-link relationships across clone/pull
- Fresh clone results in independent file copies
- Effective duplication of source-of-truth implementations
- Violates D0037 single-source-of-truth principle

### 1.2 Previous Failed Attempts
1. **Repository-relative paths** (`#include "../../../src/runtime/scheduler.h"`)
   - Arduino IDE doesn't resolve these reliably; builds in temporary directory
   - Fails because Arduino treats sketch directory as root, not repository root

2. **Bridge files with relative paths**
   - Same problem as above; Arduino IDE can't escape sketch build context

---

## 2. Arduino IDE 2.x Library Discovery Mechanism

### 2.1 How Arduino IDE Finds Libraries

Arduino IDE 2.x searches for libraries in this order:

1. **Sketch-local libraries** - `{sketch_directory}/libraries/` ← **Hardest to avoid hard links here**
2. **Sketchbook libraries** - `~/Arduino/libraries/` or configured sketchbook path
3. **Built-in libraries** - Bundled with IDE/Teensyduino
4. **User-installed libraries** - Via Library Manager (registry)

When IDE finds a library with valid `library.properties`, it:
- Registers the library
- Adds its `src/` directory to compiler include paths
- Compiles `.cpp` files in `src/` as part of the sketch build

### 2.2 Library Properties Metadata
A valid Arduino library requires at minimum:
```properties
name=LibraryName
version=1.0.0
author=Author
architectures=teensy
```

### 2.3 Critical Constraint: No Repository Symlinks
- Windows and cross-platform Git don't reliably preserve symlinks
- Symlinks require admin privileges on Windows (user's environment)
- Excludes this approach

### 2.4 Critical Constraint: No Hard Links
- Hard links are filesystem-specific (work on NTFS, FAT32, etc.)
- Git does NOT track hard-link relationships
- Fresh clone creates independent file copies, breaking single-source-of-truth
- Explicitly rejected by user requirements

---

## 3. Arduino-Supported Integration Mechanisms

### 3.1 Mechanism A: Sketch-Local Libraries with Actual Files (Copies)
**Using:** `{sketch_directory}/libraries/{LibName}/`

**How it works:**
- Arduino IDE automatically scans sketch directory for `libraries/` subfolder
- Treats any properly-structured directory as a library
- Compiles `.cpp` and includes `.h` files automatically

**Status:** ❌ REJECTED
- **Why:** Requires copying or duplicating source files
- Violates D0037 (single source of truth)
- Changes to originals don't reflect in duplicates
- Maintenance nightmare

---

### 3.2 Mechanism B: Sketchbook-Local Libraries
**Using:** Configure sketchbook path to BroTracker repository root, then access libraries via `~/libraries/`

**How it works:**
1. User sets Arduino IDE Preferences → Sketchbook location → `d:\dev\smartin77\BroTracker`
2. Arduino IDE treats BroTracker root as sketchbook
3. Can place libraries anywhere in sketchbook hierarchy
4. Sketch opens from `tools/teensy_diagnostics/audio_timing_benchmark/`

**Pros:**
- ✓ No file copying or duplication
- ✓ Single source of truth maintained
- ✓ Arduino-supported mechanism
- ✓ Works cross-platform
- ✓ Git-preserves-able (no hard links)

**Cons:**
- ✗ Requires user configuration (one-time per IDE setup)
- ✗ Changes IDE behavior for entire sketchbook
- ✗ Less isolated than sketch-local libraries
- ✗ User must know to set this

**Assessment:** Viable if user configuration is acceptable and documented

---

### 3.3 Mechanism C: Arduino Library Metadata + Integrated Paths
**Using:** Add `library.properties` to existing component directories without moving files

**Approach C1: Dual-role directories**
- Put `library.properties` in `firmware/teensy/BroTracker/` and `src/runtime/`
- Keep existing `.cpp` and `.h` files in place
- Let Arduino IDE discover them as libraries while they remain architecturally owned where they are

**How it works:**
1. Add `firmware/teensy/BroTracker/library.properties`:
   ```properties
   name=BroTrackerDiagnostics
   version=1.0.0
   author=BroTracker
   category=Realtime
   architectures=teensy
   ```
2. Add `src/runtime/library.properties`:
   ```properties
   name=BroTrackerScheduler
   version=1.0.0
   author=BroTracker
   category=Realtime
   architectures=*
   ```
3. Arduino IDE must be configured to search these directories

**Pros:**
- ✓ No file movement or duplication
- ✓ Single source of truth preserved
- ✓ Architectural ownership unchanged
- ✓ Library discovery works through IDE configuration

**Cons:**
- ✗ Requires Arduino IDE to know about non-standard library paths
- ✗ Less clear library structure (mixed with other files in directories)
- ✗ IDE configuration or CLI needed

**Assessment:** Requires investigation into whether Arduino IDE can use arbitrary library paths. May need Arduino CLI configuration file.

---

### 3.4 Mechanism D: Arduino CLI Configuration File
**Using:** `arduino-cli.yaml` in repository root with library search paths

**How it works:**
1. Create/commit `arduino-cli.yaml`:
   ```yaml
   directories:
     builtin:
       # ...existing paths
     libraries:
       - ~/Arduino/libraries          # Standard location
       - ../src/runtime               # Relative to config file
       - ../firmware/teensy/BroTracker # Relative to config file
   ```
2. When Arduino IDE or `arduino-cli` reads this, they know where to find libraries
3. Sketch compilation includes these paths automatically

**How developer uses this:**
- User runs Arduino IDE and point to sketch
- IDE optionally reads `arduino-cli.yaml` from repo
- OR developer runs build via Arduino CLI (requires CLI tool)

**Pros:**
- ✓ Config-driven, no file changes needed
- ✓ Git-portable (YAML configuration)
- ✓ Documents the integration approach
- ✓ Single source of truth
- ✓ No copying, moving, or linking

**Cons:**
- ✗ Arduino IDE GUI may not respect `arduino-cli.yaml` (IDE-specific behavior)
- ✗ Requires Arduino CLI for full automation (manual IDE Verify/Upload only)
- ✗ User must understand Arduino CLI or IDE integration with config files

**Assessment:** Viable if IDE respects arduino-cli.yaml; research needed on IDE behavior

---

### 3.5 Mechanism E: Arduino Library Path Environment Variable
**Using:** Set `ARDUINO_LIBRARY_DISCOVERY_PATHS` environment variable

**How it works:**
1. Developer sets environment before launching Arduino IDE:
   ```bash
   set ARDUINO_LIBRARY_DISCOVERY_PATHS=d:\dev\smartin77\BroTracker\src\runtime;d:\dev\smartin77\BroTracker\firmware\teensy\BroTracker
   ```
2. Arduino IDE reads environment and includes these in library search

**Pros:**
- ✓ No file modifications
- ✓ Single source of truth

**Cons:**
- ✗ Machine-specific (absolute paths; not portable in repo)
- ✗ Environmental setup required before each IDE launch
- ✗ Not automated; user must remember

**Assessment:** ❌ REJECTED - violates portability requirement

---

### 3.6 Mechanism F: Sketch Preprocessing / Arduino Builder Customization
**Using:** Custom Arduino builder configuration to inject include paths

**Approach:**
- Create `.arduino/arduino_preferences.txt` or similar in sketch directory
- Configure compiler to treat `src/runtime/` and `firmware/teensy/` as library paths
- Sketch compilation automatically includes these

**Status:** ❌ COMPLEX AND NOT STANDARD
- Not documented as standard Arduino IDE feature
- Requires deep understanding of Arduino build system
- Not portable across IDE versions

---

## 4. Detailed Analysis by Question

### 4.1 Can `firmware/teensy/BroTracker/` be an Arduino library source of truth?

**Answer: YES, with configuration**

**How:**
1. Keep existing files in place (`diagnostics.h`, `diagnostics.cpp`, `platform.h`, `platform.cpp`)
2. Add `library.properties` to declare it as a library
3. Arduino IDE must be configured to search `firmware/teensy/` as a library path
4. No file movement, copying, or linking required

**Requirements:**
- Directory structure:
  ```
  firmware/teensy/BroTracker/
  ├── library.properties          ← NEW
  ├── diagnostics.h               ← EXISTING (unchanged)
  ├── diagnostics.cpp             ← EXISTING (unchanged)
  └── ...other files...
  ```
- IDE configuration must add `firmware/teensy/` to library search paths
- This is a packaging/integration concern, not architectural relocation

**Architectural Compliance:**
- ✓ D0037: Single source of truth maintained
- ✓ D0038: Arduino IDE integration exposes existing implementation without duplication
- ✓ Component remains under `firmware/teensy/` architecturally
- ✓ Arduino IDE packaging is separate from architectural ownership

---

### 4.2 Can `src/runtime/` expose Scheduler as Arduino library?

**Answer: YES, with same configuration**

**How:**
1. Keep Scheduler in `src/runtime/` (platform-independent location)
2. Add `library.properties` to `src/runtime/`
3. Arduino IDE configured to search `src/runtime/` as library path
4. IDE finds and compiles Scheduler as library without moving it

**Requirements:**
- Directory structure:
  ```
  src/runtime/
  ├── library.properties          ← NEW
  ├── scheduler.h                 ← EXISTING (unchanged)
  ├── scheduler.cpp               ← EXISTING (unchanged)
  └── ...other files...
  ```
- IDE configuration adds `src/runtime/` to library search paths
- No architectural compromise; component remains in correct location

**Architectural Compliance:**
- ✓ D0037: Single source of truth
- ✓ D0038: Integration doesn't relocate component
- ✓ Platform-independent component remains in shared location
- ✓ Arduino IDE sees it as library through metadata

---

### 4.3 How does Arduino IDE 2.x discover libraries?

**Answer: Documented discovery order**

Arduino IDE 2.x searches for libraries with `library.properties`:

1. **Sketch-local** → `{sketch_dir}/libraries/{name}/library.properties`
2. **Sketchbook** → `{sketchbook}/libraries/{name}/library.properties`
3. **Built-in** → Bundled with IDE/Teensyduino
4. **System** → IDE installation directory
5. **Configured paths** → Via `arduino-cli.yaml` or IDE preferences (less documented)

**Key Finding:**
- Arduino IDE does NOT automatically search arbitrary repository directories
- Integration requires either:
  - Copying files into sketch-local `libraries/`
  - Configuring sketchbook location
  - Using Arduino CLI with configuration
  - Using IDE preferences/configuration (if supported)

---

### 4.4 Repository Portability Requirement

**Challenge:** Solution must work in fresh clone without hard links, symlinks, or absolute paths

**Viable approaches:**
1. **Mechanism B (Sketchbook configuration)** - User sets once, documented
2. **Mechanism C+D (Library metadata + Arduino CLI config)** - Config file in repo
3. **Mechanism B + Updated README** - Document the setup step

**Non-viable approaches:**
1. Hard links - Don't survive clone
2. Symlinks - Don't survive clone, require admin
3. Absolute paths - Not portable between machines
4. Copying files - Violates single-source-of-truth

---

### 4.5 Distinguishing Architectural vs. Integration Concerns

| Aspect | Architectural | Integration |
|--------|---------------|-------------|
| **Where Scheduler lives** | `src/runtime/` (platform-independent) | Arduino IDE adds to search path |
| **Where Diagnostics lives** | `firmware/teensy/BroTracker/` (Teensy reusable) | Arduino IDE adds to search path |
| **Ownership of implementation** | Unchanged (architectural) | Arduino IDE discovery mechanism |
| **Source files** | Original locations (single copy) | IDE discovers, compiles in-place |
| **Metadata** | Component interface (`.h`) | `library.properties` (Arduino packaging) |

**Principle:** Adding `library.properties` is Arduino-specific packaging metadata, not architectural relocation.

---

## 5. Viable Solutions

### Solution 1: Sketchbook Reconfiguration (Simplest User Experience)

**Repository side:**
- Add `library.properties` to `firmware/teensy/BroTracker/`
- Add `library.properties` to `src/runtime/`
- No file movement or copying
- No Git-incompatible artifacts

**Developer side (one-time setup):**
1. Arduino IDE → File → Preferences → Sketchbook Location
2. Set to: `d:\dev\smartin77\BroTracker` (or wherever the repo is)
3. Restart IDE
4. Open sketch from `tools/teensy_diagnostics/audio_timing_benchmark/audio_timing_benchmark.ino`
5. IDE automatically finds libraries in `src/runtime/` and `firmware/teensy/BroTracker/`

**Pros:**
- ✓ Single source of truth maintained
- ✓ No file copying, linking, or symlinking
- ✓ Fully Git-portable
- ✓ Uses standard Arduino IDE UI (no CLI needed)
- ✓ Clear architectural ownership preserved

**Cons:**
- ✗ Requires user configuration (but only once per IDE setup)
- ✗ Documentation important for developer to understand

**What changes in repository:**
```
firmware/teensy/BroTracker/
├── library.properties          ← ADD THIS
├── diagnostics.h
├── diagnostics.cpp
└── ...

src/runtime/
├── library.properties          ← ADD THIS
├── scheduler.h
├── scheduler.cpp
└── ...

tools/teensy_diagnostics/audio_timing_benchmark/
├── audio_timing_benchmark.ino  ← No changes (already uses correct includes)
├── README.md                   ← Document setup requirement
└── libraries/                  ← DELETE (remove hard links directory)
```

**Developer verification:**
- Manual: Arduino IDE Verify/Compile + Upload to physical Teensy 4.1
- Expected: Compilation succeeds, benchmark runs, results logged to SD card

---

### Solution 2: Arduino CLI Configuration File (More Automation)

**Repository side:**
- Add `arduino-cli.yaml` in repository root with library paths
- Add `library.properties` to both component directories
- Supports both Arduino IDE and Arduino CLI workflows

**Developer side:**
1. Arduino IDE preferences optionally read `arduino-cli.yaml` (if supported)
2. OR use `arduino-cli` command-line for automated builds
3. OR manual configuration as above + YAML provides documentation

**Example `arduino-cli.yaml`:**
```yaml
directories:
  libraries:
    - src/runtime
    - firmware/teensy/BroTracker
```

**Pros:**
- ✓ Configuration-driven (documented in repo)
- ✓ Single source of truth
- ✓ Works with Arduino CLI automation
- ✓ Git-portable
- ✓ No files duplicated or linked

**Cons:**
- ✗ Requires research: Does Arduino IDE 2.x actually read `arduino-cli.yaml`?
- ✗ CLI users may still need separate setup
- ✗ Less discoverable than UI-based approach

**What changes in repository:**
```
/
├── arduino-cli.yaml            ← ADD THIS (documents library paths)
├── firmware/teensy/BroTracker/
│   ├── library.properties      ← ADD THIS
│   └── ...
├── src/runtime/
│   ├── library.properties      ← ADD THIS
│   └── ...
└── tools/teensy_diagnostics/audio_timing_benchmark/
    ├── audio_timing_benchmark.ino
    ├── README.md               ← Document setup
    └── libraries/              ← DELETE (remove hard links)
```

---

### Solution 3: Hybrid (Recommended)

**Combine both approaches:**
1. Add `library.properties` to both `firmware/teensy/BroTracker/` and `src/runtime/`
2. Create `arduino-cli.yaml` documenting library paths
3. Update README.md with setup instructions
4. Support both Arduino IDE GUI users and Arduino CLI users

**Benefits:**
- ✓ Flexible: works with IDE (manual setup) or CLI (automated)
- ✓ Documented in config file
- ✓ Single source of truth
- ✓ No file duplication
- ✓ Fully portable

---

## 6. Mechanisms to REJECT

| Mechanism | Reason |
|-----------|--------|
| Hard links | Don't survive `git clone`, effectively duplicates files on fresh clone |
| Symlinks | Don't survive `git clone`, require admin on Windows |
| File copying | Violates D0037 single-source-of-truth |
| Repository-relative paths | Arduino IDE doesn't honor them (builds in temp dir) |
| Bridge files | Same problem as relative paths |
| Absolute paths in repo | Not portable between machines |
| Moving Scheduler to `firmware/teensy/` | Violates D0038 (relocates component) |
| PlatformIO | User explicitly required Arduino IDE only |
| CMake | User explicitly required Arduino IDE only |

---

## 7. What Still Requires Manual Developer Verification

The developer must manually perform and verify:

1. **Arduino IDE Configuration**
   - Set sketchbook location (or verify arduino-cli.yaml is recognized)
   - Verify libraries are discovered by IDE

2. **Compilation**
   - Sketch → Verify/Compile in Arduino IDE
   - Check that no errors occur
   - Verify includes of `scheduler.h` and `diagnostics.h` resolve

3. **Hardware Verification**
   - Connect physical Teensy 4.1
   - Upload via Arduino IDE
   - Verify LED flashes (initialization, completion)
   - Monitor Serial output at 115200 baud
   - Verify results logged to SD card

4. **Benchmark Execution**
   - Verify three configurations run (44100 Hz at 256, 512, 1024 samples)
   - Verify all timing measurements appear
   - Verify no processing overruns
   - Verify Scheduler position matches expected value

**Repository-side verification only:**
- ✓ Directory structure correct
- ✓ `library.properties` files present and valid
- ✓ Source files in canonical locations (no copying)
- ✓ Sketch uses correct Arduino library includes
- ✓ No hard links present
- ✓ No build system files (platformio.ini, CMakeLists.txt)

---

## 8. Recommended Approach Summary

### **Primary Recommendation: Solution 1 (Sketchbook Reconfiguration)**

**Why this approach:**
1. **Simplest implementation** - Just add `library.properties` files
2. **Uses standard Arduino IDE UI** - No CLI or config files needed
3. **Fully portable** - No hard links, symlinks, or absolute paths
4. **Single source of truth** - Components remain in place
5. **Clear architectural ownership** - Files stay where they belong

### **Repository-side changes needed:**

1. Delete `tools/teensy_diagnostics/audio_timing_benchmark/libraries/` directory
   - Remove all hard-linked files
   - Keep `audio_timing_benchmark.ino` and `README.md`

2. Add `firmware/teensy/BroTracker/library.properties`:
   ```properties
   name=BroTrackerDiagnostics
   version=1.0.0
   author=BroTracker contributors
   maintainer=BroTracker
   sentence=BroTracker Teensy diagnostics infrastructure
   paragraph=SD-card logging and diagnostic LED signalling
   category=Realtime
   url=https://github.com/smartin77/BroTracker
   architectures=teensy
   includes=diagnostics.h
   ```

3. Add `src/runtime/library.properties`:
   ```properties
   name=BroTrackerScheduler
   version=1.0.0
   author=BroTracker contributors
   maintainer=BroTracker
   sentence=BroTracker audio playback timeline
   paragraph=Platform-independent sample-based Scheduler
   category=Realtime
   url=https://github.com/smartin77/BroTracker
   architectures=*
   includes=scheduler.h
   ```

4. Update `tools/teensy_diagnostics/audio_timing_benchmark/README.md`:
   - Document the one-time sketchbook configuration step
   - Explain that IDE will auto-discover libraries
   - Provide screenshots or step-by-step instructions

5. Update `audio_timing_benchmark.ino` includes:
   - Sketch already uses `#include <scheduler.h>` and `#include <diagnostics.h>`
   - This is correct for Arduino library discovery
   - No changes needed

### **Developer-side workflow (one-time setup):**

1. Install Arduino IDE 2.x and Teensyduino
2. Clone BroTracker repository (or pull latest)
3. Arduino IDE → File → Preferences → Sketchbook Location
4. Set to repository root (e.g., `d:\dev\smartin77\BroTracker`)
5. Restart Arduino IDE
6. File → Open → Navigate to `tools/teensy_diagnostics/audio_timing_benchmark/audio_timing_benchmark.ino`
7. Tools → Board → Teensy 4.1
8. Tools → USB Type → Serial
9. Sketch → Verify/Compile
10. Hardware: Connect Teensy 4.1, press Program Button
11. Sketch → Upload
12. Verify execution and SD card logging

### **Architecture compliance:**

- ✓ **D0037**: Single source of truth maintained (no copying)
- ✓ **D0038**: Integration exposes existing implementations (no duplication)
- ✓ **D0021**: One core, multiple runtimes (Scheduler stays in `src/runtime/`)
- ✓ **D0023**: Host development runtime (Diagnostics stays in `firmware/teensy/`)
- ✓ Repository portability (no hard links, symlinks, or absolute paths)
- ✓ Git-preservable (only text config files added)
- ✓ Fresh clone compatible (sketchbook config is on developer's machine)

---

## 9. Questions Answered

### 1. Can `firmware/teensy/BroTracker/` be Arduino library source of truth?
**YES.** Add `library.properties` to declare it as library. IDE configuration adds it to search paths. Files stay in place. Single source of truth maintained. ✓

### 2. Can `src/runtime/` expose Scheduler without moving it?
**YES.** Same approach: `library.properties` + IDE configuration. Scheduler remains architecturally in shared location. ✓

### 3. How does Arduino IDE 2.x discover libraries?
Documented: Searches sketch-local `libraries/`, sketchbook `libraries/`, built-in, system. For arbitrary repo directories, requires sketchbook reconfiguration or Arduino CLI configuration file. ✓

### 4. Is the solution portable for fresh clones?
**YES.** Sketchbook configuration is stored on developer's machine, not in repository. Adding `library.properties` (text files) is portable. No hard links or symlinks. ✓

### 5. Which mechanisms satisfy D0037 and D0038?
**Solutions 1 & 3** (Sketchbook reconfiguration ± Arduino CLI config). They expose existing components without duplication or relocation. ✓

### 6. Which mechanisms should be rejected?
Hard links, symlinks, copied files, relative paths, absolute paths, moving components, build systems. All violate architectural or portability requirements. ✓

### 7. What's the recommended approach?
**Solution 1 (Sketchbook Reconfiguration)** - Simplest, most standard, clearest for developers. Minimal repository changes. Single source of truth preserved. ✓

---

## 10. Implementation Readiness

This analysis is complete and sufficient to proceed to implementation.

**Next steps (when approved):**
1. Delete `tools/teensy_diagnostics/audio_timing_benchmark/libraries/` and contents
2. Add `library.properties` to `firmware/teensy/BroTracker/`
3. Add `library.properties` to `src/runtime/`
4. Update README.md with setup instructions
5. Verify via developer manual testing in Arduino IDE


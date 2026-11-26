# BTT Teensy 4.1 admission diagnostics

Compile/link diagnostics only, run from the repository root. This tool does not
upload, open a serial port, run firmware, or add BTT to a production runtime.
It follows the other developer tools under `tools/teensy_diagnostics/`; generated
files go to ignored `build/btt-teensy-admission/`.

```powershell
pio run -e teensy41_dev
python tools/teensy_diagnostics/btt_admission/analyze.py --prepare-config
pio run -c build/btt-teensy-admission/map-platformio.ini -e teensy41_dev
python tools/teensy_diagnostics/btt_admission/analyze.py --measure
python tools/teensy_diagnostics/btt_admission/budgets.py
```

Capture command output to that build directory when collecting a report.
PlatformIO must have write access to its installed package locks/cache. The
baseline environment's `.pio/build/teensy41_dev/idedata.json` must match the current
configuration; regenerate it using `pio run -e teensy41_dev -t idedata` if needed.
The runner records the compiler/defines/include paths it actually uses.

The copied PlatformIO configuration inherits all production flags and USB core
patching, changing only the build directory, map reporting and stack reporting.
The script requires baseline and mapped ELF section sizes/addresses/alignment to
match before deriving budgets. `map_flags.py` captures the actual linker command
for the subsequent separate reservation ELF. No production ini/source is edited.

`layout.cpp` defines size-valued byte symbols, read with the target `nm`; it is
never linked into firmware. It includes production SD/ownership types but no
host FILE or test dependencies. The backend shapes have no implementations:
their `sizeof` results are proposed handle-owner costs, not working SD adapters.
The internal libstdc++ `make_shared` control type is deliberately toolchain
specific; a changed library may require reviewing this diagnostic.

The unchanged host foundation currently fails ARM compilation because four
`std::min` calls mix unsigned int and uint32_t (unsigned long on this ABI).
`foundation-original-compile.log` and `foundation-original-result.txt` retain
that result. The runner generates **only in build/** a copy with explicit
`std::min<uint32_t>` arguments at those four locations. Stack/code evidence from
`foundation-abi.*` applies to that copy, not the unchanged reviewed source.
No FILE/test code is introduced. Original and adapted compiler commands are saved.

`placement.cpp` reserves a 64 KiB aligned `.dmabuffers` arena and 128 bytes of
DTCM owner state. The separate `reservation.elf` roots all foundation entry
points and these reservations against the same production objects/libraries.
It neither constructs BTT types nor calls BTT operations. Its map measures
code/bank effects and proposed static placement, not runtime admission. The
NOLOAD arena needs explicit initialization/lifetime management in any future
adapter. Do not upload this diagnostic ELF as a product.

`budgets.py` generates `memory.json`, `lifecycle.csv`, and `stack-frames.csv`.
It keeps the full production baseline resident; only incremental proposed
objects are in phase totals. A third compact pattern is counted during stopped
initial-load staging to preserve both old buffers; playback has two. Candidate
paths/definitions/bindings belong only to initial-load projection, not every
phase. The initial-load path-pool shape is a reservation, not support for a
second context in today's singleton `ResourcePaths()` implementation.

The phase counts do not add together. Their maximum plus a 2 KiB alignment and
lifetime guard must fit the proposed 64 KiB arena. The separate 32 KiB DTCM stack
reserve and 64 KiB RAM2 runtime-heap reserve are **unproven policy allowances**.
Compiled stack reports include functions that may be removed by linker GC;
use the saved linked symbol list before claiming a function is resident. Frame
sizes are not complete call-chain/IRQ bounds and exclude prebuilt library frames.

Consult `docs/FILE_FORMATS.md` for the assessed lifecycle, limitations and next
implementation step. These tools establish no SD latency, playback deadline,
power-loss recovery, physical verification, or reliable final device admission.

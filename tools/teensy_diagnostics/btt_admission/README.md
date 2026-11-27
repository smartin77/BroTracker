# BTT Teensy 4.1 admission diagnostics

These compile/link probes remain distinct from the new opt-in executable
`teensy41_btt_diagnostic`. See [the owner guide](../../btt/DEVICE_DIAGNOSTIC.md)
for its explicit serial RUN, private retained artifacts and pending physical
checks. The standalone image does not prove full production memory admission.

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

The four mixed unsigned/uint32_t `std::min` calls are fixed in the actual
foundation. It is compiled directly as `foundation.o`, with no generated source
replacement shim. `foundation-result.txt` and the exact command record success.
No FILE/test code is introduced into the ARM inputs.

For the fixed-owner/arena prototype use `--out build/btt-backing-prototype` on
runner commands, `--prototype --measure` on analyze.py and `--prototype` on
budgets.py. Build that output directory's map-platformio.ini first. This also
compiles and roots the real FsFile adapter via `backing_probe.cpp`; fixed-pool
and arena-metadata costs replace hypothetical shared-control costs. See
`tools/btt/README.md` for commands and the write-once policy.

`--resources` implies prototype mode and adds the actual candidate admission
implementation and its arena lifecycle probe. Pass it to budgets.py too: measured
Candidate state replaces hypothetical second-path-pool/definitions/bindings/maps.
Only the existing singleton path pool is used. `--mapped-baseline <directory>`
can reuse an unchanged production-equivalent map build; ELF section equivalence
is still checked, and callers must verify production source provenance. See
`tools/btt/RESOURCE_ADMISSION.md`. The real `sd_source_service.cpp` is now compiled
and rooted with a placement-construction probe and one-turn lifecycle probe.
The old blocking probe loop is removed. Actual `SdSourceService` sizeof replaces
the former 1,024-byte metadata allowance. Producer job metadata is already in the
production baseline and must not be counted twice. The isolated reservation can
cross an ITCM bank boundary even when the production firmware does not; inspect
both entries in memory.json before assessing RAM1 headroom.

Cold BTT placement is selected per operation with `BTT_FLASHMEM(name)` in
`tools/btt/code_placement.h`. It is empty on hosts and uses `.flashmem.btt.*` plus
noinline on Teensy 4. Never infer helper/clone placement from an entry point:
inspect objdump symbol sections, maps and disassembly. Shared read/materialization
helpers and asynchronous admission/source-adapter operations remain in ITCM.
The before/after experiment under `build/btt-code-placement/` compares all retained
roots, identical production link inputs and both arena reservations; it returns
the isolated link to eight banks. No runtime cache/SD timing claim follows.

`placement.cpp` reserves a 64 KiB aligned `.dmabuffers` arena and 128 bytes of
DTCM owner state. The separate `reservation.elf` roots all foundation entry
points and these reservations against the same production objects/libraries.
Without `--prototype`, it neither constructs BTT types nor calls BTT operations.
With it, a separate rooted diagnostic entry point contains explicit object
initialization/destruction, never called by production. Its map measures
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

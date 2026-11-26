"""Compile-only target evidence. Run at repository root; never uploads hardware.

--prepare-config creates a production-equivalent diagnostic config with only
map/stack reporting and an isolated build directory. --measure compiles layout
and foundation objects with production compiler/ABI flags, then reads symbols.
"""
import argparse
import configparser
import csv
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/btt-teensy-admission"
HERE = Path(__file__).resolve().parent


def run(args, log):
    result = subprocess.run([str(a) for a in args], cwd=ROOT, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (OUT / log).write_text(result.stdout, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(f"{log}: exit {result.returncode}\n{result.stdout[-4000:]}")
    return result.stdout


def prepare():
    config = configparser.ConfigParser(interpolation=None)
    config.read(ROOT / "platformio.ini")
    config["platformio"]["build_dir"] = str(OUT / "mapped")
    config["env:teensy41_dev"]["extra_scripts"] = (
        "${env:teensy41.extra_scripts}\npost:tools/teensy_diagnostics/btt_admission/map_flags.py")
    config["env:teensy41_dev"]["build_flags"] += "\n-fstack-usage"
    with (OUT / "map-platformio.ini").open("w", encoding="utf-8") as stream:
        config.write(stream)


def measure(prototype=False):
    original = ROOT / ".pio/build/teensy41_dev"
    mapped = OUT / "mapped/teensy41_dev"
    info = json.loads((original / "idedata.json").read_text())
    (OUT / "compiler-environment.json").write_text(json.dumps(info, indent=2))
    compiler = Path(info["cxx_path"])
    bindir = compiler.parent
    suffix = compiler.suffix
    nm = bindir / ("arm-none-eabi-nm" + suffix)
    objdump = bindir / ("arm-none-eabi-objdump" + suffix)
    size = bindir / ("arm-none-eabi-size" + suffix)
    flags = info["cxx_flags"] + ["-D" + d for d in info["defines"]]
    for folder in info["includes"]["build"] + [str(ROOT), str(ROOT / "tools/btt")]:
        flags += ["-I", folder]
    commands = []
    inputs = [("layout", HERE / "layout.cpp"), ("placement", HERE / "placement.cpp")]
    if prototype:
        inputs.append(("backing-probe", HERE / "backing_probe.cpp"))
    for name, source in inputs:
        args = [compiler, *flags, "-fstack-usage", "-c", source, "-o", OUT / (name + ".o")]
        commands.append([str(a) for a in args])
        run(args, name + "-compile.log")
    source = ROOT / "tools/btt/foundation.cpp"
    args = [compiler, *flags, "-fstack-usage", "-c", source, "-o", OUT / "foundation.o"]
    commands.append([str(a) for a in args])
    run(args, "foundation-compile.log")
    (OUT / "foundation-result.txt").write_text("PASS: real tools/btt/foundation.cpp, exit=0; no generated source shim.\n")
    run([size, "-A", OUT / "foundation.o"], "foundation-object-sections.txt")
    run([nm, "-S", "-C", OUT / "foundation.o"], "foundation-object-symbols.txt")
    (OUT / "probe-commands.json").write_text(json.dumps(commands, indent=2))
    run([compiler, "--version"], "compiler-version.txt")
    symbols = run([nm, "-S", "--size-sort", OUT / "layout.o"], "layout-symbols.txt")
    measurements = {}
    for line in symbols.splitlines():
        fields = line.split()
        if len(fields) == 4 and fields[3].startswith(("size_", "value_")):
            measurements[fields[3]] = int(fields[1], 16)
    (OUT / "layouts.json").write_text(json.dumps(measurements, indent=2))
    with (OUT / "layouts.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["compiler_symbol", "bytes_or_value"])
        writer.writerows(sorted(measurements.items()))
    sections = []
    for name, folder in [("production", original), ("mapped", mapped)]:
        elf = folder / "firmware.elf"
        run([size, "-A", elf], name + "-sections.txt")
        sections.append(run([objdump, "-h", elf], name + "-objdump.txt"))
        run([nm, "-n", "-S", "-C", elf], name + "-symbols.txt")
        shutil.copyfile(elf, OUT / (name + ".elf"))
    # Compare section sizes/addresses, not timestamps or absolute filenames.
    def section_rows(text):
        return [line.split()[1:7] for line in text.splitlines()
                if len(line.split()) >= 7 and line.split()[0].isdigit()]
    if section_rows(sections[0]) != section_rows(sections[1]):
        raise RuntimeError("Diagnostic build differs in section layout from baseline; inspect before budgeting")
    (OUT / "baseline-equivalence.txt").write_text(
        "PASS: production and map/stack diagnostic ELF section sizes, VMAs, LMAs and alignment match.\n")
    shutil.copyfile(mapped / "firmware.map", OUT / "firmware.map")
    stack = OUT / "production-stack"
    stack.mkdir(exist_ok=True)
    for source in mapped.rglob("*.su"):
        dest = stack / source.relative_to(mapped)
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, dest)
    # Code + proposed arena placement only. No adapter or runtime BTT call is
    # added to production; undefined roots prevent GC of the measured functions.
    link_path = mapped / "link-command.json"
    if not link_path.exists():
        raise RuntimeError("Missing diagnostic linker command; rebuild copied configuration")
    link = json.loads(link_path.read_text())
    link[link.index("-o") + 1] = str(OUT / "reservation.elf")
    link = ["-Wl,-Map," + str(OUT / "reservation.map") if a.startswith("-Wl,-Map,") else a for a in link]
    idx = link.index("-Wl,--start-group")
    link[idx:idx] = [str(OUT / "foundation.o"), str(OUT / "placement.o")]
    if prototype:
        link[idx:idx] = [str(OUT / "backing-probe.o")]
    raw = run([nm, "--defined-only", "-g", OUT / "foundation.o"], "foundation-roots.txt")
    for line in raw.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[1] == "T" and fields[2].startswith("_ZN3btt"):
            link += ["-Wl,--undefined=" + fields[2]]
    if prototype:
        raw = run([nm, "--defined-only", "-g", OUT / "backing-probe.o"], "backing-probe-roots.txt")
        for line in raw.splitlines():
            fields = line.split()
            if len(fields) == 3 and fields[1] in ("T", "W") and (fields[2].startswith("_ZN3btt") or fields[2] == "btt_backing_probe"):
                link += ["-Wl,--undefined=" + fields[2]]
    link += ["-Wl,--undefined=btt_admission_arena", "-Wl,--undefined=btt_admission_owner"]
    (OUT / "reservation-link-command.json").write_text(json.dumps(link, indent=2))
    run(link, "reservation-link.log")
    run([size, "-A", OUT / "reservation.elf"], "reservation-sections.txt")
    run([nm, "-n", "-S", "-C", OUT / "reservation.elf"], "reservation-symbols.txt")
    print(json.dumps(measurements, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare-config", action="store_true")
    parser.add_argument("--measure", action="store_true")
    parser.add_argument("--out", type=Path, help="Generated artifact directory relative to repository root")
    parser.add_argument("--prototype", action="store_true", help="Compile/root the real fixed-owner FsFile adapter")
    options = parser.parse_args()
    if options.out:
        OUT = (ROOT / options.out).resolve()
    OUT.mkdir(parents=True, exist_ok=True)
    if options.prepare_config:
        prepare()
    if options.measure:
        measure(options.prototype)

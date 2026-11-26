"""Derive proposed lifecycle reservations from saved compiler/linker evidence.

These are capacity scenarios, not admission guarantees. Run after analyze.py.
"""
import csv
import json
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--out", type=Path, default=Path("build/btt-teensy-admission"))
parser.add_argument("--prototype", action="store_true")
parser.add_argument("--resources", action="store_true")
options = parser.parse_args()
options.prototype = options.prototype or options.resources
OUT = Path(__file__).resolve().parents[3] / options.out
layout = json.loads((OUT / "layouts.json").read_text())


def size(name):
    return layout["size_" + name]


def linked(name):
    symbols = {}
    for line in (OUT / (name + "-symbols.txt")).read_text().splitlines():
        fields = line.split()
        if len(fields) >= 3:
            symbols[fields[-1]] = int(fields[0], 16)
    sections = {}
    for line in (OUT / (name + "-sections.txt")).read_text().splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[0].startswith("."):
            sections[fields[0]] = {"size": int(fields[1]), "address": int(fields[2])}
    text = sections[".text.itcm"]["size"] + sections[".ARM.exidx"]["size"]
    bank_bytes = symbols["_itcm_block_count"] * 32768
    actual_end = sections[".ARM.exidx"]["address"] + sections[".ARM.exidx"]["size"]
    return {
        "sections": sections,
        "itcm_banks": symbols["_itcm_block_count"],
        "itcm_bank_bytes": bank_bytes,
        "itcm_code_reported": text,
        "itcm_padding_reported": bank_bytes - text,
        "itcm_physical_end": actual_end,
        "itcm_physical_padding": bank_bytes - actual_end,
        "ram1_static": sections[".data"]["size"] + sections[".bss"]["size"],
        "ram1_static_end": symbols["_ebss"],
        "stack_top": symbols["_estack"],
        "ram1_link_gap": symbols["_estack"] - symbols["_ebss"],
        "ram2_static": sections[".bss.dma"]["size"],
        "heap_start": symbols["_heap_start"],
        "heap_end": symbols["_heap_end"],
        "ram2_link_gap": symbols["_heap_end"] - symbols["_heap_start"],
        "external_static": sections[".bss.extram"]["size"],
        "flash_image": symbols["_flashimagelen"],
        "flash_remaining": 7936 * 1024 - symbols["_flashimagelen"],
    }


measured = {name: linked(name) for name in ["production", "reservation"]}
assert measured["production"]["external_static"] == 0
assert size("TwoPatterns") == 15368
assert measured["reservation"]["ram2_static"] - measured["production"]["ram2_static"] == 65536
assert measured["reservation"]["ram1_static"] - measured["production"]["ram1_static"] == 128

# Existing production globals remain entirely in the baseline. Incremental
# candidate projections below are genuinely concurrent, not replacements for
# the active session/definitions/live-visit snapshots.
persistent = {
    "active_document": size("Document"),
    "current_next_patterns": size("TwoPatterns"),
    "active_id_map_reserved": 99 * 8,
    "active_backing_control_shape": 0 if options.prototype else size("SharedBackingControl"),
}
if options.prototype:
    # All three controls and FsFile handles are embedded in the measured pool,
    # not separate heap allocations per backing. Metadata also lives in RAM2.
    persistent.update(fixed_pool=size("FsBackings"), arena_metadata=size("Arena"), backend=size("FsBackend"))
validation = {
    **persistent,
    "candidate_document": size("Document"),
    "workspace": size("Workspace"),
    "candidate_backing_control_shape": 0 if options.prototype else size("SharedBackingControl"),
    "sd_staging_reserved": 2 * 512,
    "store_owner_reserved": 64,
}
phases = {
    "stopped_validation": validation,
    "stopped_initial_load": {
        **validation,
        "candidate_initial_pattern": size("Pattern"),
        "candidate_realtime_projection": size("RealtimePattern"),
        "candidate_definitions": size("InstrumentDefinitions"),
        "candidate_bindings": size("SelectedPatternBindings"),
        "candidate_path_pool_shape": size("ResourcePathPool"),
        "candidate_id_map_reserved": 99 * 8,
    },
    "playback_current_next": {
        **persistent,
        "workspace": size("Workspace"),
        "sd_staging_reserved": 2 * 512,
        "scheduler_owner_reserved": 64,
    },
    "stopped_save_verify": {**validation, "writer_shape": size("FsOutput") if options.prototype else size("WriterShape")},
}
if options.resources:
    # Real singleton path storage is already in production. Each Candidate
    # includes stable-ID metadata/effective bindings/path refs/source leases;
    # no hypothetical second path pool or duplicate definitions/bindings/map.
    persistent.pop("active_id_map_reserved")
    persistent.update(active_resource_candidate=size("ResourceCandidate"),
                      source_service_metadata_reserved=1024)
    validation.update(persistent)
    validation.pop("active_id_map_reserved")
    phases = {
        "stopped_validation": validation,
        "stopped_initial_load": {
            **validation,
            "candidate_initial_pattern": size("Pattern"),
            "candidate_realtime_projection_reserved": size("RealtimePattern"),
            "resource_candidate": size("ResourceCandidate"),
            "admission_scratch": size("AdmissionScratch"),
        },
        "playback_current_next": {**persistent,"workspace":size("Workspace"),
                                  "sd_staging_reserved":1024,"scheduler_owner_reserved":64},
        "stopped_save_verify": {**validation,"writer":size("FsOutput")},
    }
with (OUT / "lifecycle.csv").open("w", newline="") as stream:
    writer = csv.writer(stream)
    writer.writerow(["phase", "component", "bytes", "evidence"])
    for phase, components in phases.items():
        for name, value in components.items():
            evidence = "proposed allowance" if name.endswith("reserved") else "target sizeof; proposed concurrent instance"
            writer.writerow([phase, name, value, evidence])
        subtotal = sum(components.values())
        writer.writerow([phase, "subtotal", subtotal, "derived; excludes existing baseline and CPU stack"])
        writer.writerow([phase, "alignment_lifetime_guard", 2048, "proposed allowance"])
        writer.writerow([phase, "arena_total", subtotal + 2048, "derived"])
        writer.writerow([phase, "arena_headroom", 65536 - subtotal - 2048, "derived within proposed 64 KiB reservation"])
measured["proposal"] = {
    "ram2_arena": 65536,
    "ram1_owner": 128,
    "ram1_stack_reserve_unproven": 32768,
    "ram2_heap_reserve_unproven": 65536,
    "ram1_after_stack_reserve": measured["reservation"]["ram1_link_gap"] - 32768,
    "ram1_after_stack_and_one_extra_itcm_bank": measured["reservation"]["ram1_link_gap"] - 32768 - 32768,
    "ram2_after_heap_reserve": measured["reservation"]["ram2_link_gap"] - 65536,
    "lifecycle": {phase: {"subtotal": sum(values.values()), "with_guard": sum(values.values()) + 2048}
                  for phase, values in phases.items()},
}
(OUT / "memory.json").write_text(json.dumps(measured, indent=2))

frames = []
for source in (OUT / "production-stack").rglob("*.su"):
    for line in source.read_text(errors="replace").splitlines():
        fields = line.split("\t")
        if len(fields) == 3:
            frames.append((int(fields[1]), fields[2], fields[0], str(source.relative_to(OUT))))
with (OUT / "stack-frames.csv").open("w", newline="") as stream:
    writer = csv.writer(stream)
    writer.writerow(["bytes", "classification", "compiled_function_may_be_gc_removed", "source_report"])
    writer.writerows(sorted(frames, reverse=True))
print(json.dumps(measured["proposal"], indent=2))

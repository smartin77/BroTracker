"""Version-locked one-expression patch; never writes the installed framework."""
import hashlib
import json
from pathlib import Path

VERSION = "1.162.0"
SOURCE_SHA256 = "32cce87877e82d69739169390dddf9c6847efb18b3d9c99dfc5b1f2fa2971033"
ORIGINAL = b"memset(usb_audio_transmit_buffer + len, 0, num * 4);"
PATCHED = b"memset((uint32_t *)usb_audio_transmit_buffer + len, 0, num * 4);"

def patch_bytes(source, version):
    if version != VERSION:
        raise RuntimeError(f"USB audio patch requires framework-arduinoteensy {VERSION}; found {version}")
    digest = hashlib.sha256(source).hexdigest()
    if digest != SOURCE_SHA256:
        raise RuntimeError(f"Incompatible Teensy4 usb_audio.cpp SHA-256: {digest}; expected {SOURCE_SHA256}. Review source before updating patch.")
    if source.count(ORIGINAL) != 1:
        raise RuntimeError("Expected exactly one USB transmit zero-fill expression")
    return source.replace(ORIGINAL, PATCHED, 1)

def prepare(framework, build, trace=False):
    framework, build = Path(framework), Path(build)
    package = json.loads((framework / "package.json").read_text())
    if package.get("name") != "framework-arduinoteensy":
        raise RuntimeError("Unexpected framework package")
    source = framework / "cores/teensy4/usb_audio.cpp"
    original = source.read_bytes()
    patched = patch_bytes(original, package.get("version"))
    if trace:
        from trace_core import trace_bytes
        patched=trace_bytes(patched)
    destination = build / "core-patches/usb_audio.cpp"
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists() or destination.read_bytes() != patched:
        destination.write_bytes(patched)
    if trace:
        header=Path(__file__).resolve().parents[2]/"firmware/teensy/BroTracker/usb_tx_trace.h"
        (destination.parent/"usb_tx_trace.h").write_bytes(header.read_bytes())
    receipt = {"usb_tx_trace": trace,"framework_version": VERSION, "original_source": str(source),
               "original_sha256": SOURCE_SHA256,
               "patched_sha256": hashlib.sha256(patched).hexdigest(),
               "original_expression": ORIGINAL.decode(), "patched_expression": PATCHED.decode()}
    (destination.parent / "receipt.json").write_text(json.dumps(receipt, indent=2)+"\n")
    return source.resolve(), destination.resolve()

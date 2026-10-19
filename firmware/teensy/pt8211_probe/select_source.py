Import("env")
import hashlib
from pathlib import Path

source = Path(env.subst("$PROJECT_DIR")) / "firmware/teensy/pt8211_probe"
env.Replace(PROJECT_SRC_DIR=str(source))
identity = hashlib.sha256(b"".join((source / name).read_bytes() for name in ("main.cpp", "record_transport.h", "select_source.py"))).hexdigest()
env.Append(CPPDEFINES=[("PROBE_SOURCE_SHA", '\\"' + identity + '\\"')])

"""Explicit opt-in only: standalone USB serial, actual BTT components, map/identity."""
from pathlib import Path
import hashlib
import json
import subprocess
import os
import re
Import("env")

root=Path(env.subst("$PROJECT_DIR"))
source=root/"firmware/teensy/btt_device_diagnostic"
env.Replace(PROJECT_SRC_DIR=str(source))
run_id=os.environ.get("BTT_DIAG_RUN_ID","iofix-01")
if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,23}",run_id):
    raise RuntimeError("BTT_DIAG_RUN_ID must be 1..24 ASCII letters/digits/_/- and start with a letter/digit")
directory="/BroTracker/.btt-prototype/"+run_id
env.Append(CPPDEFINES=[("BTT_BACKING_DIRECTORY",'\\"'+directory+'\\"')])
components=["foundation.cpp","resource_admission.cpp","sd_source_service.cpp"]
# The three tiny source shims compile the real components as application TUs,
# after PlatformIO has applied all board/framework/LDF flags, with no source copy.
inputs=sorted(set(list(source.glob("*.cpp"))+list(source.glob("*.py"))+
    list((root/"tools/btt").glob("*.h"))+[root/"tools/btt"/name for name in components]+
    list((root/"firmware/teensy/BroTracker").glob("sd_*.h"))+
    list((root/"src").rglob("*.h"))+
    list((root/"libraries/sd_access/src").glob("*.*"))+
    list((root/"libraries/scheduler/src").glob("*.*"))+[root/"platformio.ini"]))
digest=hashlib.sha256()
digest.update(b"BTT_BACKING_DIRECTORY\0"+directory.encode()+b"\0")
for path in inputs: digest.update(str(path.relative_to(root)).encode()+b"\0"+path.read_bytes())
head=subprocess.check_output(["git","rev-parse","HEAD"],cwd=root,text=True).strip()
for key,value in [("BTT_DIAG_SOURCE_SHA",digest.hexdigest()),("BTT_DIAG_GIT_HEAD",head)]:
    env.Append(CPPDEFINES=[(key,'\\"'+value+'\\"')])
build=Path(env.subst("$BUILD_DIR"));build.mkdir(parents=True,exist_ok=True)
(build/"diagnostic-identity.json").write_text(json.dumps({"git_head":head,"source_sha256":digest.hexdigest(),
    "diagnostic_directory":directory,
    "files":{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
    "physical_results":"pending"},indent=2))
env.Append(LINKFLAGS=["-Wl,-Map,"+str(build/"firmware.map")])

def capture(target,source,env):
    args=env.subst_list("$LINK -o $TARGET $LINKFLAGS $__RPATH $SOURCES $_LIBDIRFLAGS $_LIBFLAGS",target=target,source=source)[0]
    (build/"link-command.json").write_text(json.dumps([str(a) for a in args],indent=2))
env.AddPreAction("$BUILD_DIR/${PROGNAME}.elf",capture)

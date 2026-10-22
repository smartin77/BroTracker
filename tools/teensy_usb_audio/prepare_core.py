# PlatformIO pre-script: replace only the known core translation unit.
from pathlib import Path
import sys
Import("env")
sys.path.insert(0, str(Path(env.subst("$PROJECT_DIR")) / "tools/teensy_usb_audio"))
from patch_core import prepare
if env.BoardConfig().get("build.core") != "teensy4":
    raise RuntimeError("BroTracker USB audio patch supports only the Teensy4 core")
framework = env.PioPlatform().get_package_dir("framework-arduinoteensy")
defines=env.ParseFlags(env.GetProjectOption("build_flags")).get("CPPDEFINES",[])
trace=any((d[0] if isinstance(d,(tuple,list)) else d)=="BROTRACKER_USB_TX_TRACE" for d in defines)
original, patched = prepare(framework, env.subst("$BUILD_DIR"), trace=trace)
from midi_core import prepare_midi, prepare_lifecycle
midi_original,midi_patched=prepare_midi(framework,env.subst("$BUILD_DIR"))

usb_original,usb_patched=prepare_lifecycle(framework,env.subst("$BUILD_DIR"),trace)

def replace_core(node):
    if Path(node.srcnode().get_abspath()).resolve() == usb_original:
        return env.File(str(usb_patched))
    if Path(node.srcnode().get_abspath()).resolve() == midi_original:
        return env.File(str(midi_patched))
    if Path(node.srcnode().get_abspath()).resolve() == original:
        print("BroTracker USB audio: compiling isolated patched core:", patched)
        return env.File(str(patched))
    return node
env.AddBuildMiddleware(replace_core)

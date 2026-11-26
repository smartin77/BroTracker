# Diagnostic-only SCons script, injected by analyze.py into a copied config.
# No production configuration or source file is changed.
from pathlib import Path
import json

Import("env")
env.Append(CCFLAGS=["-fstack-usage"])
env.Append(LINKFLAGS=["-Wl,-Map," + str(Path(env.subst("$BUILD_DIR")) / "firmware.map")])


def capture_link(target, source, env):
    args = env.subst_list("$LINK -o $TARGET $LINKFLAGS $__RPATH $SOURCES $_LIBDIRFLAGS $_LIBFLAGS",
                          target=target, source=source)[0]
    args = [str(arg) for arg in args]
    args[0] = env.WhereIs(args[0])
    (Path(env.subst("$BUILD_DIR")) / "link-command.json").write_text(json.dumps(args, indent=2))


env.AddPreAction("$BUILD_DIR/${PROGNAME}.elf", capture_link)

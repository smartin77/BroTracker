Import("env")
env.Append(LINKFLAGS=["-Wl,-Map," + env.subst("$BUILD_DIR") + "/firmware.map"])

# Environment-local isolation; production sources and USB preparation are unchanged.
Import("env")

env.Replace(PROJECT_SRC_DIR=env.subst("$PROJECT_DIR/firmware/teensy/audio_shield_test"))

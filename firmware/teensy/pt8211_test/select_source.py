# Environment-local isolation; leave the production source directory unchanged.
Import("env")
env.Replace(PROJECT_SRC_DIR=env.subst("$PROJECT_DIR/firmware/teensy/pt8211_test"))

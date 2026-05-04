# PlatformIO extra script for the native build environment.
# Adds native/*.cpp source files to the build alongside the
# filtered subset of src/ files.
#
# When running tests (pio test), main_native.cpp is excluded since
# the test files provide their own main().
#
# Loaded as: extra_scripts = pre:native/pio_extra.py

Import("env")
import os

native_dir = os.path.join(env["PROJECT_DIR"], "native")
is_test = "test" in env.GetBuildType() or "test" in COMMAND_LINE_TARGETS

if is_test:
    src_filter = ["+<*.cpp>", "-<main_native.cpp>"]
else:
    src_filter = ["+<*.cpp>"]

env.BuildSources(
    os.path.join("$BUILD_DIR", "native_src"),
    native_dir,
    src_filter=src_filter,
)

"""
PlatformIO pre-build script: Force-clean stale CMake cache.

Previous fix attempts set ARDUINO_USB_MODE=0 via CMake cache variables
and Kconfig defaults. Those entries persist in .pio/build/*/CMakeCache.txt
even after the source config files are changed, causing the Arduino core
to compile with TinyUSB code paths that can't find TinyUSB headers.

This script deletes the CMakeCache.txt and sdkconfig files so CMake
regenerates them from the current (clean) config on every build.
"""
Import("env")
import os
import glob

build_dir = env.get("PROJECT_BUILD_DIR", "")
if not build_dir:
    # Fallback: construct from project dir and env name
    project_dir = env.get("PROJECT_DIR", "")
    env_name = env.get("PIOENV", "")
    if project_dir and env_name:
        build_dir = os.path.join(project_dir, ".pio", "build", env_name)

if build_dir and os.path.isdir(build_dir):
    # Delete CMake cache files that may contain stale ARDUINO_USB_MODE
    for pattern in ["CMakeCache.txt", "sdkconfig", "sdkconfig.old"]:
        for f in glob.glob(os.path.join(build_dir, pattern)):
            try:
                os.remove(f)
                print(f"[cache_clean] Removed stale {f}")
            except OSError:
                pass
    # Also delete CMakeFiles directory (contains cached compiler checks)
    cmake_files_dir = os.path.join(build_dir, "CMakeFiles")
    if os.path.isdir(cmake_files_dir):
        import shutil
        try:
            shutil.rmtree(cmake_files_dir)
            print(f"[cache_clean] Removed stale CMakeFiles/")
        except OSError:
            pass

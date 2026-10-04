"""
PlatformIO pre-build script: remove stale CMake cache + generated sdkconfig
from the build dir so every build regenerates them from sdkconfig.defaults.
(Same idea as the force_clean_cache.py in your other two projects.)

This matters here because the rollback setting in sdkconfig.defaults has to
reach BOTH the bootloader and the app.

NOTE: PlatformIO also keeps a project-root file named
sdkconfig.edgehax-launcher after the first build. If you ever edit
sdkconfig.defaults, delete that file so the new defaults are picked up.
"""
Import("env")
import os
import glob
import shutil

build_dir = env.get("PROJECT_BUILD_DIR", "")
if not build_dir:
    project_dir = env.get("PROJECT_DIR", "")
    env_name = env.get("PIOENV", "")
    if project_dir and env_name:
        build_dir = os.path.join(project_dir, ".pio", "build", env_name)

if build_dir and os.path.isdir(build_dir):
    for pattern in ["CMakeCache.txt", "sdkconfig", "sdkconfig.old"]:
        for f in glob.glob(os.path.join(build_dir, pattern)):
            try:
                os.remove(f)
                print("[cache_clean] Removed stale %s" % f)
            except OSError:
                pass
    cmake_files_dir = os.path.join(build_dir, "CMakeFiles")
    if os.path.isdir(cmake_files_dir):
        try:
            shutil.rmtree(cmake_files_dir)
            print("[cache_clean] Removed stale CMakeFiles/")
        except OSError:
            pass

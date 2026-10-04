#!/usr/bin/env python3
"""
build_all.py -- build and flash Chimera-DualBoot (Launcher + SecureVault + ShadowTune).

  python build_all.py                            build all three
  python build_all.py --only ShadowTune          build just one
  python build_all.py --flash COM5               build all three, then flash everything
  python build_all.py --flash COM5 --no-build    flash what is already built
  python build_all.py --only ShadowTune --flash COM5
                                                 rebuild and flash just that piece
  python build_all.py --merge chimera_all.bin    build, then make ONE image (flash at 0x0)
  python build_all.py --dry-run                  show the commands only

Needs PlatformIO (`pip install platformio`). Flashing is done by Launcher/tools/flash_all.py;
this script finds the two app binaries (.pio/build/*/firmware.bin) and passes them in.

NOTE: never run `pio run -t upload` inside SecureVault/ or ShadowTune/ -- it would overwrite
the shared bootloader + partition table and break the dual boot.
"""
import argparse, glob, os, shutil, subprocess, sys

PROJECTS = ["Launcher", "SecureVault", "ShadowTune"]


def pio():
    exe = shutil.which("pio") or shutil.which("platformio")
    return [exe] if exe else [sys.executable, "-m", "platformio"]


def run(cmd, dry):
    print("\n> " + " ".join(cmd))
    if not dry:
        subprocess.check_call(cmd)


def newest_bin(here, folder):
    found = glob.glob(os.path.join(here, folder, ".pio", "build", "*", "firmware.bin"))
    return max(found, key=os.path.getmtime) if found else None


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="Build / flash Chimera-DualBoot")
    ap.add_argument("--only", choices=PROJECTS, help="build (and flash) only this project")
    ap.add_argument("--no-build", action="store_true", help="skip building")
    ap.add_argument("--flash", metavar="PORT", help="flash to this serial port")
    ap.add_argument("--merge", metavar="OUT.bin", help="make one merged image instead of flashing")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    names = [a.only] if a.only else PROJECTS

    if not a.no_build:
        for name in names:
            d = os.path.join(here, name)
            if not os.path.isfile(os.path.join(d, "platformio.ini")):
                sys.exit("ERROR: %s/platformio.ini not found" % name)
            run(pio() + ["run", "-d", d], a.dry_run)

    if not (a.flash or a.merge):
        print("\nBuild done. Flash with:  python build_all.py --flash <PORT> --no-build")
        return

    tool = os.path.join(here, "Launcher", "tools", "flash_all.py")
    if not os.path.isfile(tool):
        sys.exit("ERROR: Launcher/tools/flash_all.py not found")
    cmd = [sys.executable, tool]
    cmd += ["--port", a.flash] if a.flash else ["--merge", a.merge]
    if a.only:
        cmd += ["--only", a.only.lower()]

    # Pass the app binaries explicitly (only the ones that will be written).
    for folder, flag in (("SecureVault", "--securevault"), ("ShadowTune", "--shadowtune")):
        if a.only and a.only != folder:
            continue
        path = newest_bin(here, folder)
        if not path and not a.dry_run:
            sys.exit("ERROR: no firmware.bin found for %s. Build it first." % folder)
        cmd += [flag, path or "<%s firmware.bin>" % folder]
    if a.dry_run:
        cmd.append("--dry-run")
    run(cmd, False)


if __name__ == "__main__":
    main()

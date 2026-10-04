#!/usr/bin/env python3
"""
flash_all.py -- install the launcher + both apps on the EdgeHax S3 Pro.

Why this script exists: running `pio run -t upload` inside SecureVault or
ShadowTune would overwrite the bootloader + partition table with THEIR own
single-app layout and break dual boot. So the two apps must be written into
ota_0 / ota_1 as plain .bin files, which this script does with esptool.

Usage (from the launcher project folder, after `pio run` of all three):

  python tools/flash_all.py --port COM5 ^
      --securevault ../SecureVault/.pio/build/edgehax-s3-pro/firmware.bin ^
      --shadowtune  ../ShadowTune/.pio/build/esp32-s3-devkitc-1/firmware.bin

  --only launcher | securevault | shadowtune   flash just one piece
  --merge out.bin                              write ONE combined image instead
                                               of flashing (flash it at 0x0)
  --dry-run                                    print the esptool command only

Layout (must match partitions.csv):
  0x00000 bootloader   0x08000 partition table   0x20000 launcher (factory)
  0x160000 SecureVault (ota_0)                   0x560000 ShadowTune (ota_1)
  otadata (0x10000, 8 KB) is erased so the launcher always starts first.
"""
import argparse, os, shutil, subprocess, sys

OFF_BOOTLOADER = 0x0
OFF_PARTITIONS = 0x8000
OFF_OTADATA    = 0x10000
SZ_OTADATA     = 0x2000
OFF_LAUNCHER   = 0x20000
OFF_SECUREVAULT = 0x160000     # ota_0, 4 MB slot
OFF_SHADOWTUNE  = 0x560000     # ota_1, 2 MB slot
SZ_SECUREVAULT  = 0x400000
SZ_SHADOWTUNE   = 0x200000
SZ_LAUNCHER     = 0x140000


def esptool_cmd():
    # Prefer the esptool that ships with PlatformIO, else fall back to PATH.
    home = os.path.expanduser("~")
    for cand in (os.path.join(home, ".platformio", "packages", "tool-esptoolpy", "esptool.py"),):
        if os.path.isfile(cand):
            return [sys.executable, cand]
    for exe in ("esptool.py", "esptool"):
        if shutil.which(exe):
            return [shutil.which(exe)]
    return [sys.executable, "-m", "esptool"]


def need(path, label, max_size=None):
    if not path or not os.path.isfile(path):
        sys.exit("ERROR: %s not found: %s" % (label, path))
    size = os.path.getsize(path)
    if max_size and size > max_size:
        sys.exit("ERROR: %s is %d bytes but its slot is only %d bytes." % (label, size, max_size))
    return path


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    build = os.path.join(root, ".pio", "build", "edgehax-launcher")

    ap = argparse.ArgumentParser(description="Flash EdgeHax launcher + apps")
    ap.add_argument("--port", help="serial port, e.g. COM5 or /dev/ttyACM0")
    ap.add_argument("--baud", default="921600")
    ap.add_argument("--securevault", help="SecureVault firmware.bin")
    ap.add_argument("--shadowtune", help="ShadowTune firmware.bin")
    ap.add_argument("--only", choices=["launcher", "securevault", "shadowtune"])
    ap.add_argument("--merge", metavar="OUT.bin")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    items = []   # (offset, path)
    want = lambda k: a.only in (None, k)

    if want("launcher"):
        items.append((OFF_BOOTLOADER, need(os.path.join(build, "bootloader.bin"), "launcher bootloader.bin")))
        items.append((OFF_PARTITIONS, need(os.path.join(build, "partitions.bin"), "launcher partitions.bin")))
        items.append((OFF_LAUNCHER,   need(os.path.join(build, "firmware.bin"), "launcher firmware.bin", SZ_LAUNCHER)))
    if want("securevault"):
        items.append((OFF_SECUREVAULT, need(a.securevault, "SecureVault firmware.bin", SZ_SECUREVAULT)))
    if want("shadowtune"):
        items.append((OFF_SHADOWTUNE, need(a.shadowtune, "ShadowTune firmware.bin", SZ_SHADOWTUNE)))

    tool = esptool_cmd()

    if a.merge:
        cmd = tool + ["--chip", "esp32s3", "merge_bin", "-o", a.merge,
                      "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "16MB"]
        for off, path in items:
            cmd += [hex(off), path]
        print(" ".join(cmd))
        if not a.dry_run:
            subprocess.check_call(cmd)
            print("\nMerged image written. Flash it with:\n  esptool.py --chip esp32s3 -p <PORT> write_flash 0x0 %s" % a.merge)
        return

    if not a.port:
        sys.exit("ERROR: --port is required (or use --merge)")

    base = tool + ["--chip", "esp32s3", "--port", a.port, "--baud", a.baud]
    steps = []
    # Always clear otadata first so the factory (launcher) boots next.
    steps.append(base + ["erase_region", hex(OFF_OTADATA), hex(SZ_OTADATA)])
    flash = base + ["write_flash", "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep"]
    for off, path in items:
        flash += [hex(off), path]
    steps.append(flash)

    for cmd in steps:
        print("\n> " + " ".join(cmd))
        if not a.dry_run:
            subprocess.check_call(cmd)
    print("\nDone. Reset the board - the launcher menu should appear.")


if __name__ == "__main__":
    main()

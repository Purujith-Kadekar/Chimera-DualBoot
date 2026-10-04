# Partition table

Defined in `Launcher/partitions.csv`. 16 MB flash, built into the Launcher image and flashed at 0x8000.

| Name | Type / subtype | Offset | End | Size | Used by |
|---|---|---|---|---|---|
| (bootloader) | | 0x0 | 0x8000 | 32 KB | Launcher's bootloader (rollback enabled) |
| (partition table) | | 0x8000 | 0x9000 | 4 KB | |
| nvs | data / nvs | 0x9000 | 0xF000 | 24 KB | Shared settings |
| phy_init | data / phy | 0xF000 | 0x10000 | 4 KB | RF calibration |
| otadata | data / ota | 0x10000 | 0x12000 | 8 KB | Boot selection + rollback state |
| gpio_cfg | data / 0x40 | 0x12000 | 0x13000 | 4 KB | ShadowTune GPIO override block |
| **factory** | app / factory | 0x20000 | 0x160000 | 1.25 MB | **Launcher** |
| **ota_0** | app / ota_0 | 0x160000 | 0x560000 | 4 MB | **SecureVault** |
| **ota_1** | app / ota_1 | 0x560000 | 0x760000 | 2 MB | **ShadowTune** |
| littlefs | data / 0x83 | 0x760000 | 0xFF0000 | ~8.5 MB | Shared by both apps |
| coredump | data / coredump | 0xFF0000 | 0x1000000 | 64 KB | |

## Rules

- **App partitions start on a 64 KB (0x10000) boundary.** Data partitions on 4 KB.
- Subtypes `0x83` (littlefs) and `0x40` (gpio_cfg) are written numerically because the plain
  ESP-IDF partition tool does not know those names. The apps look them up by subtype/label.
- `nvs` keeps the same offset and size both apps used before (0x9000, 24 KB).
- The slot sizes equal the apps' former partition sizes, so images that fit before still fit.
- `flash_all.py` checks each image against its slot size and refuses to write an oversized one.

## Resizing a slot (or moving anything)

1. Edit `Launcher/partitions.csv`. Keep alignment rules above, no overlaps, end at or below 0x1000000.
2. Update the matching constants at the top of `Launcher/tools/flash_all.py`
   (`OFF_LAUNCHER`, `OFF_SECUREVAULT`, `OFF_SHADOWTUNE`, `SZ_*`). **They must match the CSV.**
3. Update the table in this document and in the root README.
4. Rebuild and flash everything (`python build_all.py --flash COM5`). Changing the layout moves
   data partitions, so apps will re-format LittleFS on their next start.

Quick validation of a CSV: every row's offset must be >= the previous row's end, and each app row
offset must be a multiple of 0x10000.

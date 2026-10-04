# Troubleshooting

## Boot menu problems

| Symptom | Cause | Fix |
|---|---|---|
| An app starts directly, no menu | The bootloader is keeping that app as the boot target | Erase otadata and reset: `python -m esptool --chip esp32s3 -p COM5 erase_region 0x10000 0x2000` |
| An app reboots into itself every time | The app confirms itself (rollback enabled or `esp_ota_mark_app_valid_cancel_rollback()` called) | Make sure `CONFIG_APP_ROLLBACK_ENABLE` is off in that app's sdkconfig; rebuild and reflash that app |
| Menu never appears; blank screen | Launcher not flashed, or display wiring/pins differ | Flash with `--only launcher` and read the serial log |
| Card says NOT INSTALLED | Slot is empty or holds an invalid image | Build and flash that app: `python build_all.py --only SecureVault --flash COM5` (or ShadowTune) |
| LAUNCH FAILED on a card | Image failed validation | Reflash that app |
| Launching an app returns straight to the menu | The app crashes at start | Reset, read the serial log, check app logs |

## Flashing problems

| Symptom | Fix |
|---|---|
| Cannot connect to the board | Hold BOOT, tap RESET, release BOOT, retry. Try another cable or port. |
| `firmware.bin not found` | Build that project first: `python build_all.py` |
| `... is N bytes but its slot is only M bytes` | The image is larger than its slot. Shrink the app or enlarge the slot ([PARTITIONS.md](PARTITIONS.md)) |
| Flashed an app with `pio run -t upload` and nothing boots | You overwrote the shared bootloader/partition table. Run a full `python build_all.py --flash COM5` |
| Python or esptool not found | `pip install platformio esptool` and use the same Python for both |

## Build problems

| Symptom | Fix |
|---|---|
| `littlefs/lfs.h: No such file or directory` | Include-path workaround for vendored components; see the comments in that project's `platformio.ini` |
| Config changes in `sdkconfig.defaults` ignored | Delete the generated `sdkconfig.<env>` file in that project and rebuild |
| Stale or odd build state | `pio run -d <Project> -t clean`, then rebuild |
| Library download fails | Check internet access; PlatformIO fetches libraries on first build |

## Power and input

| Symptom | Fix |
|---|---|
| Board wakes by itself right after power-off | Wake pin is reading LOW: check the joystick OK switch / ladder |
| Triple-tap does not power off | Taps too slow (must land within 1.5 s) or touch pad not wired to GPIO 40 |
| OK press registers as another button | Ladder thresholds ([HARDWARE.md](HARDWARE.md)); the Launcher uses OK <= 157 |
| No buzzer sound in one app | Buzzer pin differs: GPIO 38 in SecureVault, GPIO 22 in ShadowTune |
| Touch lands in the wrong place | The Launcher uses landscape (rotation 1) mapping like the apps; check the display rotation |

## Data

| Symptom | Explanation |
|---|---|
| ShadowTune settings reset after a SecureVault duress wipe | The duress wipe erases the shared NVS |
| App formats LittleFS on first start | Expected after the partition layout moved |
| RTC time wrong after flashing | Flashing does not touch the RTC module; check the coin cell |

## Getting help

Open an issue with: the step in [TESTING.md](TESTING.md) that failed, the serial log (115200
baud), your commit hash, and the output of `python -m esptool --chip esp32s3 -p COM5 flash_id`.

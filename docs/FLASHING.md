# Building and flashing

## Requirements

- [PlatformIO Core](https://docs.platformio.org/) (`pip install platformio`)
- Python 3
- The board connected over USB-C
- Internet on the first build (PlatformIO downloads the toolchain and libraries)

## Golden rule

> Never run `pio run -t upload` inside `SecureVault/` or `ShadowTune/`.

Uploading from an app project overwrites the bootloader and partition table with that app's own
single-app layout. The Launcher and the other app would no longer boot. Always flash with
`build_all.py` or `Launcher/tools/flash_all.py`.

## Full build and flash

```bash
python build_all.py --flash COM5          # Linux/macOS: /dev/ttyACM0
```

This builds Launcher, SecureVault and ShadowTune, then writes:

| Address | Content | File |
|---|---|---|
| 0x0 | Bootloader | `Launcher/.pio/build/edgehax-launcher/bootloader.bin` |
| 0x8000 | Partition table | `.../partitions.bin` |
| 0x10000 | otadata (erased) | |
| 0x20000 | Launcher | `.../firmware.bin` |
| 0x160000 | SecureVault | `SecureVault/.pio/build/*/firmware.bin` |
| 0x560000 | ShadowTune | `ShadowTune/.pio/build/*/firmware.bin` |

Erasing otadata guarantees the Launcher is what boots next.

## Other forms

```bash
python build_all.py                                  # build only
python build_all.py --flash COM5 --no-build          # flash what is already built
python build_all.py --only ShadowTune --flash COM5   # rebuild and flash one piece
python build_all.py --only Launcher --flash COM5     # Launcher only
python build_all.py --merge chimera_all.bin          # one merged image
python build_all.py --dry-run --flash COM5           # show commands, change nothing
```

`--only` accepts `Launcher`, `SecureVault`, `ShadowTune`.

### Flashing by hand

```bash
python Launcher/tools/flash_all.py --port COM5 \
  --securevault SecureVault/.pio/build/edgehax-s3-pro/firmware.bin \
  --shadowtune  ShadowTune/.pio/build/esp32-s3-devkitc-1/firmware.bin
```

Options: `--only launcher|securevault|shadowtune`, `--merge OUT.bin`, `--baud N`, `--dry-run`.

### Flashing a merged image

```bash
python -m esptool --chip esp32s3 -p COM5 write_flash 0x0 chimera_all.bin
```

The merged file starts at 0x0 and runs to the end of ShadowTune's slot, so it also overwrites the
`nvs` area. LittleFS (0x760000 and up) is not touched.

## Entering download mode

If the upload cannot connect: hold **BOOT**, tap **RESET**, release **BOOT**, then retry.

## What survives a flash

| Data | Full flash | Merged image | `--only` one app |
|---|---|---|---|
| RTC module / EEPROM (I2C) | kept | kept | kept |
| SD card | kept | kept | kept |
| ESP32 `nvs` | kept unless the table changed | wiped | kept |
| `littlefs` | kept (offset unchanged) | kept | kept |

Changing `partitions.csv` moves partitions, so apps re-format LittleFS once on their next start.

## Full erase (last resort)

```bash
python -m esptool --chip esp32s3 -p COM5 erase_flash
```

This wipes **all** flash (NVS, LittleFS, every app). It does not touch the RTC module or SD card.
Flash everything again afterwards.

## Build notes

- All three projects use the pioarduino platform in **dual-framework mode** (`arduino, espidf`),
  so each project has its own `sdkconfig.defaults`. After editing it, delete the generated
  `sdkconfig.<env>` file so the new defaults are applied.
- `force_clean_cache.py` and `disable_component_manager.py` run before each build; do not remove them.
- The Launcher must keep `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` in its `sdkconfig.defaults`.

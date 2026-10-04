# Chimera-DualBoot

One board, two firmwares, one boot menu.

Chimera-DualBoot runs two complete firmwares on a single **EdgeHax S3 Pro** (ESP32-S3 N16R8).
A small boot **Launcher** starts on every power-up and lets you choose which one to enter:

| | |
|---|---|
| **SecureVault** | Hardware password manager: PIN-protected encrypted vault on the SD card, Dashboard Mode over USB with an authenticated, AES-256-GCM encrypted session, BLE keyboard, hotspot mode. |
| **ShadowTune** | MP3 player: CS4344 I2S DAC, Helix MP3 decoder, playlists from `/MP3/` on the SD card, Now Playing screen, hotspot web upload. |

You pick with the joystick or by tapping the screen. Nothing in the two apps had to be
rewritten to share the chip: each one runs from its own flash slot.

---

## Repository layout

```
Chimera-DualBoot/
├── Launcher/        Dual-boot menu (factory partition) + shared partition table + flash tool
├── SecureVault/     Password manager firmware (PlatformIO project)
├── ShadowTune/      MP3 player firmware (PlatformIO project)
├── docs/            Architecture, hardware, flashing, testing and more
├── build_all.py     Builds all three (and can flash them)
├── CHANGELOG.md
├── SECURITY.md
├── THIRD_PARTY_NOTICES.md
└── LICENSE
```

Each folder is an independent PlatformIO project with its own `platformio.ini`.

## Hardware

- **Board:** EdgeHax S3 Pro, ESP32-S3 with 16 MB flash and 8 MB octal PSRAM
- **Display:** ILI9341 320x240 with XPT2046 touch (landscape)
- **Input:** 5-way joystick on a resistor ladder (GPIO 6), TTP223 capacitive touch pad (GPIO 40)
- **Storage:** microSD (SPI)
- **Audio (ShadowTune):** CS4344 DAC over I2S
- **I2C:** RTC, MPU, INA219 and EEPROM share one bus (GPIO 1 / GPIO 2)

Pin maps live in each project's `include/board_config.h`.

## Using it

### Launcher menu

| Action | Result |
|---|---|
| Joystick any direction | Move between the two cards |
| Joystick **OK** | Launch the selected app |
| Tap a card on the screen | Launch that app |
| **Triple-tap** the touch pad | Power off (screen goes dark, deep sleep) |
| Click joystick **OK** while off | Power on, menu appears directly |

The menu appears on **every** boot, including after a crash or a power-off
wake-up. A card shows NOT INSTALLED if its slot has no valid firmware.

### Inside the apps

| App | Power off | Wake |
|---|---|---|
| ShadowTune | Hold the touch pad for 5 s (any screen) | Joystick OK, then pick it in the menu |
| SecureVault | Hold the touch pad for 5 s (any screen) | Joystick OK, then pick it in the menu |

ShadowTune shows its RESUMING screen when it starts after a power-off.

## How it works

Flash layout (16 MB), defined in `Launcher/partitions.csv`:

| Partition | Offset | Size | Holds |
|---|---|---|---|
| nvs | 0x9000 | 24 KB | Shared settings |
| otadata | 0x10000 | 8 KB | Which app boots next |
| gpio_cfg | 0x12000 | 4 KB | ShadowTune GPIO override block |
| **factory** | 0x20000 | 1.25 MB | **Launcher** |
| **ota_0** | 0x160000 | 4 MB | **SecureVault** |
| **ota_1** | 0x560000 | 2 MB | **ShadowTune** |
| littlefs | 0x760000 | ~8.5 MB | Shared by both apps |
| coredump | 0xFF0000 | 64 KB | |

**Menu on every boot, without touching the apps.** The Launcher's bootloader is built with
app-rollback support. Launching an app marks it "next boot, unverified". The apps never
confirm themselves, so on the *next* reset of any kind (power cycle, wake from deep sleep,
crash, restart) the bootloader falls back to the factory app, which is the Launcher.

Both apps find `nvs`, `littlefs` and `gpio_cfg` by type and label at runtime, so they run
from the shared layout. Their NVS namespaces and LittleFS file names do not collide.

## Build and flash

**Requirements:** [PlatformIO](https://platformio.org/) (`pip install platformio`), Python 3.

```bash
# Build all three firmwares, then flash bootloader + partition table + all three apps
python build_all.py --flash COM5          # Linux/macOS: /dev/ttyACM0
```

`build_all.py` finds the two app binaries by itself. If the board does not connect, hold
**BOOT**, tap **RESET**, release **BOOT**, then retry.

Other forms:

```bash
python build_all.py                              # build only
python build_all.py --flash COM5 --no-build     # flash what is already built
python build_all.py --only ShadowTune --flash COM5   # rebuild + flash just one piece
python build_all.py --merge chimera_all.bin     # one merged image, flash it at 0x0
```

`--only` accepts `Launcher`, `SecureVault` or `ShadowTune`.

<details>
<summary>Flashing by hand (without build_all.py)</summary>

```bash
python Launcher/tools/flash_all.py --port COM5 \
  --securevault SecureVault/.pio/build/edgehax-s3-pro/firmware.bin \
  --shadowtune  ShadowTune/.pio/build/esp32-s3-devkitc-1/firmware.bin
```

Add `--only launcher|securevault|shadowtune` to write a single piece, or `--merge out.bin`
for a combined image (then `python -m esptool --chip esp32s3 -p COM5 write_flash 0x0 out.bin`).
</details>

> **Do not run `pio run -t upload` inside `SecureVault/` or `ShadowTune/`.**
> It would overwrite the shared bootloader and partition table with that project's
> single-app layout and break the dual boot. Always flash through `build_all.py` or `flash_all.py`.

## Good to know

- **Shared storage.** Both apps use the same NVS and LittleFS partitions. SecureVault's
  duress wipe erases NVS, which also clears ShadowTune's saved settings (last track, volume).
- **RTC / EEPROM data is separate.** The Launcher never touches the I2C bus, and flashing only
  writes the ESP32's own flash, so data stored on the RTC module is not reset.
- **Slot sizes.** SecureVault image up to 4 MB, ShadowTune up to 2 MB, Launcher up to 1.25 MB.
  `flash_all.py` refuses images that do not fit.
- **After changing `sdkconfig.defaults`** in a project, delete its generated
  `sdkconfig.<env>` file so the new defaults are applied.

## Troubleshooting

| Problem | Fix |
|---|---|
| An app starts directly, no menu | `python -m esptool --chip esp32s3 -p COM5 erase_region 0x10000 0x2000`, then reset |
| Card says NOT INSTALLED | That slot is empty: build and flash it (`python build_all.py --only SecureVault --flash COM5`, same for ShadowTune) |
| Build error about `littlefs/lfs.h` | Vendored component include paths: see the comments in `platformio.ini` of the failing project |
| firmware.bin not found | Build that project first (`python build_all.py`) |

Serial log: USB-C, 115200 baud. The Launcher prints `[Launcher] slot ...` lines at startup.

## Documentation

Full documentation is in [`docs/`](docs/README.md):
[Architecture](docs/ARCHITECTURE.md) |
[Boot flow](docs/BOOT_FLOW.md) |
[Partitions](docs/PARTITIONS.md) |
[Hardware](docs/HARDWARE.md) |
[Controls](docs/CONTROLS.md) |
[Boot screens](docs/BOOT_SCREENS.md) |
[Flashing](docs/FLASHING.md) |
[Development](docs/DEVELOPMENT.md) |
[Testing](docs/TESTING.md) |
[Troubleshooting](docs/TROUBLESHOOTING.md)

## Contributing

Issues and pull requests are welcome. Please keep each project building on its own, and
describe which board and which project folder your change touches.

## License

Original code in this repository is released under the [MIT License](LICENSE).
Bundled third-party components keep their own licenses, including the Helix MP3 decoder
(RealNetworks RPSL/RCSL). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

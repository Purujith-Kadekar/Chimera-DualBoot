# Development guide

## Repository layout

```
Chimera-DualBoot/
├── Launcher/          PlatformIO project: boot menu, shared partition table, flash tool
│   ├── src/ include/  main, display, input, boot_manager
│   ├── partitions.csv Shared 16 MB layout
│   ├── sdkconfig.defaults   (rollback enabled here)
│   └── tools/flash_all.py
├── SecureVault/       PlatformIO project: password manager
├── ShadowTune/        PlatformIO project: MP3 player
├── docs/
└── build_all.py
```

Each project is an independent PlatformIO project (`platformio.ini`). Build one with
`python build_all.py --only <Name>` or `pio run -d <Name>`.

## Rules for an app that runs behind the Launcher

An app (SecureVault, ShadowTune, or any future one) works in a slot only if it follows these:

| Rule | Why |
|---|---|
| **Do not enable app rollback.** Keep `CONFIG_APP_ROLLBACK_ENABLE` and `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` off in the app's sdkconfig, and never call `esp_ota_mark_app_valid_cancel_rollback()` | If the app confirms itself, the bootloader keeps booting it and the menu stops appearing |
| Find partitions by **type/label** (`nvs`, `littlefs`, `gpio_cfg`), never by offset | The shared table puts them at different addresses than the app's own table |
| Do not call `esp_ota_set_boot_partition()` or write the partition table | Would interfere with the Launcher's boot selection |
| Use a unique NVS namespace and unique LittleFS file names | NVS and LittleFS are shared between apps |
| Fit the slot: SecureVault 4 MB, ShadowTune 2 MB | `flash_all.py` refuses larger images |
| Power off through deep sleep with `ext0` wake on GPIO 6 LOW | Consistent wake behaviour; the Launcher menu then appears |
| Keep the app's own `partitions.csv` for standalone use only | The Launcher's table is the one that gets flashed |

Behind the Launcher an app **cannot rely on the hardware wake cause** (the Launcher's restart
erases it). If it needs to know it slept, leave a flag in NVS, as ShadowTune does (`st_power`).

## Working on the Launcher

- **UI:** `Launcher/src/main.cpp`. Two cards, colours and layout constants at the top. Drawing is
  Adafruit GFX primitives.
- **Input:** `Launcher/src/input.cpp`. Joystick thresholds are in `include/board_pins.h` and must
  match the hardware ladder. Touch-pad triple-tap constants (`TRIPLE_TAP_MS`, `PAD_DEBOUNCE_MS`)
  are at the top of `input.cpp`.
- **Booting an app:** `Launcher/src/boot_manager.cpp`. The `AppSlot` table in `main.cpp` maps a card
  to an OTA subtype (`ota_0` / `ota_1`).
- **Bootloader behaviour** comes from `sdkconfig.defaults`. After editing it, delete the generated
  `Launcher/sdkconfig.edgehax-launcher` and rebuild.

## Changing slot sizes or offsets

See [PARTITIONS.md](PARTITIONS.md). Edit `partitions.csv` **and** the constants in
`tools/flash_all.py` together.

## Adding a third app

1. Add an `ota_2` row to `Launcher/partitions.csv` (64 KB aligned) and shrink `littlefs` to make room.
2. Update `flash_all.py` constants and `build_all.py` (`PROJECTS`) for the new project.
3. Extend the `AppSlot` list and the card layout in `Launcher/src/main.cpp`
   (currently two cards: layout constants `CARD_X`, `CARD_W`; selection wraps modulo 2).
4. Make sure the new app follows the rules above.
5. Update the partition table in `PARTITIONS.md`, the README and CHANGELOG.

## Changing boot animations

See [BOOT_SCREENS.md](BOOT_SCREENS.md). Colours are two constants per animation.

## Conventions

- Keep each project buildable on its own.
- Do not commit build output or generated `sdkconfig.<env>` files (see `.gitignore`).
- Never commit real vault data, keys or credentials.
- Update `CHANGELOG.md` for user-visible changes, and the relevant doc when behaviour changes.
- Hardware pin changes belong in each project's `board_config.h` **and** in `docs/HARDWARE.md`.

## Useful commands

```bash
python build_all.py                          # build everything
python build_all.py --only Launcher          # one project
pio run -d ShadowTune -t clean               # clean one project
python -m esptool --chip esp32s3 -p COM5 read_flash 0x10000 0x2000 otadata.bin   # inspect boot state
pio device monitor -p COM5 -b 115200         # serial log (USB-C)
```

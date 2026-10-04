# Changelog

Chimera-DualBoot: all notable changes to this repository. Format based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Each project folder also keeps its own detailed notes (for example `SecureVault/CHANGELOG.md`).

## [1.0.0] - 2026-10-04

### Added
- **Launcher**: dual-boot menu that starts on every boot, with joystick and touch control.
  Triple-tap on the touch pad powers off; the joystick OK button powers on.
- Shared 16 MB partition table: Launcher in `factory`, SecureVault in `ota_0`, ShadowTune in `ota_1`.
- `Launcher/tools/flash_all.py`: flash everything in one command (or build one merged image).
- `build_all.py`: build and flash all three projects with one command.
- `docs/`: architecture, boot flow, partitions, hardware, controls, boot screens, flashing,
  development, testing and troubleshooting guides.
- **ShadowTune**: hold the touch pad for 5 s to power off, wake with the joystick OK button.
- **ShadowTune**: RESUMING screen now also shows when started through the Launcher after a power-off.
- **ShadowTune**: new animated boot screen (equalizer bars, red-orange to amber).
- **SecureVault**: new animated boot screen (vault ring, closing padlock, emerald green).

### Changed
- Both apps now live in OTA slots of the shared partition table. LittleFS moved to a new offset
  and is formatted once on the first boot of each app.
- Use `flash_all.py` to flash; `pio run -t upload` inside an app folder would overwrite the shared
  bootloader and partition table.

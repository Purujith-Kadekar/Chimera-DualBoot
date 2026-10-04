# Third-party notices

The original code of Chimera-DualBoot is MIT licensed (see `LICENSE`). The projects bundle or
download the following third-party software, each under its own license. Keep these notices
when you redistribute the source or firmware binaries.

Licenses below are listed to the best of my knowledge; please check each upstream repository
for the authoritative, current terms before you redistribute.

## Bundled in the repository (`components/`)

| Component | Used by | License | Where |
|---|---|---|---|
| TinyUSB (hathach) | SecureVault, ShadowTune | MIT | `*/components/tinyusb/LICENSE` |
| esp_tinyusb (Espressif) | SecureVault, ShadowTune | Apache-2.0 | `*/components/esp_tinyusb/LICENSE` |
| esp_littlefs (Brian Pugh) | SecureVault, ShadowTune | MIT | `*/components/esp_littlefs/LICENSE` |
| littlefs (Arm) | via esp_littlefs | BSD-3-Clause | `*/components/esp_littlefs/src/littlefs/LICENSE.md` |
| **Helix MP3 decoder (RealNetworks)** | ShadowTune | **RPSL 1.0 / RCSL 1.0** | `ShadowTune/components/helix_mp3/src/` (`LICENSE.txt`, `RPSL.txt`, `RCSL.txt`) |

### Helix MP3 decoder: please read

The Helix decoder is **not** MIT licensed. Its files are distributed under the RealNetworks Public
Source License (RPSL 1.0), or the RealNetworks Community Source License (RCSL 1.0) if you hold one.
Keep the license headers and `LICENSE.txt` / `RPSL.txt` / `RCSL.txt` in place, and read the RPSL
terms before distributing modified source or compiled firmware that contains it. MP3 decoding may
also involve patent or licensing considerations that depend on your country and use. This file is
not legal advice.

## Downloaded at build time (PlatformIO `lib_deps` and toolchain)

| Library | Used by | License (upstream) |
|---|---|---|
| Adafruit GFX Library | all | BSD |
| Adafruit ILI9341 | all | BSD |
| ArduinoJson (Benoit Blanchon) | SecureVault, ShadowTune | MIT |
| QRCode (ricmoo) | SecureVault, ShadowTune | MIT |
| NimBLE-Arduino (h2zero) | SecureVault | Apache-2.0 |
| ESPAsyncWebServer (ESP32Async) | SecureVault | LGPL-3.0 |
| AsyncTCP (ESP32Async) | SecureVault | LGPL-3.0 |
| Arduino-ESP32 core (pioarduino platform) | all | LGPL-2.1 |
| ESP-IDF (Espressif) | all | Apache-2.0 |

These are fetched by PlatformIO when you build and are not stored in this repository.

# Vendored components

All components here are vendored because PlatformIO's ESP-IDF component
manager cannot resolve online dependencies under `framework-espidf` (see
`disable_component_manager.py` for the full explanation). With the
component manager disabled, PlatformIO auto-discovers anything under
`components/` directly -- no manifest resolution or internet access
needed.

## esp_littlefs/

LittleFS wrapper for ESP-IDF. Used for the internal LittleFS partition
that stores touch calibration and other persistent settings.

## helix_mp3/

Helix MP3 decoder. Provides `mp3_decoder.h` used by `mp3_player.cpp`
for real-time audio decoding via I2S.

## esp_tinyusb/

Espressif's TinyUSB wrapper component (v2.2.1), providing CDC, MSC, and
other USB class drivers on top of the TinyUSB stack.

- Source: ESP Component Registry `espressif/esp_tinyusb` v2.2.1
- `test_apps/` stripped (not needed for build)
- `CMakeLists.txt` is **unmodified upstream** -- it already handles the
  component-manager-disabled case:
  ```cmake
  idf_build_get_property(idf_component_manager IDF_COMPONENT_MANAGER)
  if(NOT idf_component_manager)
      list(APPEND req tinyusb)
  endif()
  ```
  When the manager is off, it looks for `tinyusb` as a sibling local
  component (which we provide -- see below).

- **Kconfig** options (set in `sdkconfig.defaults`):
  - `CONFIG_TINYUSB_CDC_ENABLED=y` — enables the CDC (serial) class.
    Required by Arduino-ESP32's `USB.h` / `USBCDC` class.
  - `CONFIG_TINYUSB_MSC_ENABLED=y` — enables the MSC (mass storage)
    class. Required by Arduino-ESP32's `USBMSC.h` / `USBMSC` class.
    Without this, `USBMSC` compiles out and you get
    `"USBMSC does not name a type"` errors.

## tinyusb/

Base TinyUSB stack (v0.21.0~1 from the ESP Component Registry).

- Source: ESP Component Registry `espressif/tinyusb` v0.21.0~1
- This is Espressif's fork of `hathach/tinyusb` with proper
  `idf_component_register()` integration.
- Unneeded portable drivers (nRF, STM32, NXP, etc.) have been stripped
  -- only the Synopsys DWC2 driver used by ESP32-S3's USB-OTG
  peripheral remains under `src/portable/synopsys/`.
- CI/CD files (`.circleci/`, etc.) and examples have been stripped.
- `CMakeLists.txt` is **unmodified upstream** -- it compiles all device
  class sources (CDC, MSC, HID, MIDI, etc.) which TinyUSB's
  `tusb_config.h` then conditionally enables at compile time.

## No component manager needed

Just run `pio run` -- everything is already here. No cloning, no
internet access, no component manager.

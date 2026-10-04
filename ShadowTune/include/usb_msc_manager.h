#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  usb_msc_manager.h — USB Mass Storage (drag-and-drop music management)
// ═══════════════════════════════════════════════════════════════════════════════
//  Exposes ONLY the MP3 partition of the on-board microSD card as a USB
//  flash drive when the device is plugged into a PC over the USB OTG port.
//  The Vault partition (partition 1) and internal LittleFS are never
//  touched by USB MSC — the read/write callbacks operate on the exact same
//  sector range as the on-device FatFs mount (MP3_PARTITION_OFFSET_SECTORS /
//  MP3_PARTITION_SECTOR_COUNT, defined once in sd_manager.h/.cpp).
//
//  Lifecycle (mirrors HotspotManager):
//    1. begin() — called once at boot. Registers the MSC callbacks and
//       brings up the composite USB device (CDC + MSC), but with
//       mediaPresent(false) — the host sees an empty card-reader with no
//       disk inserted until the user explicitly connects.
//    2. User selects "USB DRIVE" in the mode menu.
//    3. start() — stops playback's access to the card and calls
//       SdManager::suspendForUsb() to cleanly unmount the local FatFs
//       mount, then flips mediaPresent(true). The host now sees the MP3
//       partition and can add/delete/rename files directly.
//    4. UI shows a simple "connected — do not unplug" screen.
//    5. User taps BACK (or the host ejects the drive) → stop() flips
//       mediaPresent(false) and calls SdManager::resumeAfterUsb() to
//       remount locally, then the MP3 library is rescanned.
//
//  IMPORTANT: local SD access (playback, browsing, Hotspot uploads) and
//  USB MSC must never be active at the same time — both ultimately share
//  the one SPI bus / SD object, and only one FAT "owner" may exist at a
//  once. start()/stop() enforce that hand-off.
// ═══════════════════════════════════════════════════════════════════════════════
#include <Arduino.h>

class UsbMscManager {
public:
  static UsbMscManager& getInstance();

  // Registers the MSC callbacks and brings up the USB device. Safe to call
  // even if the SD card isn't ready yet — the drive just stays hidden
  // (mediaPresent=false) until start() succeeds.
  bool begin();

  // Enters USB drive mode: unmounts the local MP3 partition mount and
  // makes it visible to the USB host. Returns false (and stays inactive)
  // if the SD card isn't available.
  bool start();

  // Leaves USB drive mode: hides the drive from the host and remounts
  // the MP3 partition locally so playback/browsing can resume.
  void stop();

  bool isActive() const { return _active; }

  // Call every loop() while active. Currently just services a
  // host-initiated eject request so the UI can leave the screen on its
  // own, the same way Hotspot mode exits on idle timeout.
  void tick();

private:
  UsbMscManager() = default;
  UsbMscManager(const UsbMscManager&) = delete;
  UsbMscManager& operator=(const UsbMscManager&) = delete;

  bool _initialized = false;
  bool _active = false;
  volatile bool _hostEjectRequested = false;

  // ── TinyUSB MSC callbacks (must be static — C function pointers) ────
  static int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize);
  static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize);
  static bool onStartStop(uint8_t power_condition, bool start, bool load_eject);
};

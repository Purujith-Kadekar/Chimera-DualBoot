#include "usb_msc_manager.h"
#include "sd_manager.h"
#include <SD.h>
#include <USB.h>
#include <USBMSC.h>

// USBMSC is Arduino-ESP32's own wrapper class: its tud_msc_* TinyUSB
// callbacks (defined in Arduino core's USBMSC.cpp) are what dispatch to
// onRead/onWrite/onStartStop below, and its composite USB descriptor is
// assembled and brought up by Arduino's own USB.begin() (ESPUSB, in
// USB.cpp) -- NOT by calling the vendored esp_tinyusb component's
// tinyusb_driver_install() directly. That component's tinyusb.c/
// tinyusb_msc.c implement a second, independent driver-install +
// tud_msc_* callback set with the exact same symbol names as Arduino's,
// so calling into it here was linking two competing TinyUSB stacks into
// one firmware.elf ("multiple definition of tud_msc_get_maxlun_cb" /
// "multiple definition of tinyusb_driver_install"). USB.begin() is the
// correct/only call needed to bring the composite device up; it's also
// safe to call multiple times.
//
// Requires ARDUINO_USB_MODE=0 (USB-OTG/TinyUSB mode) -- see
// platformio.ini's build_flags. Arduino's own USBMSC.ino example
// explicitly #warnings that ARDUINO_USB_MODE==1 (CDC/JTAG hardware mode)
// cannot support MSC at all, since that mode doesn't route through
// TinyUSB/the OTG peripheral in the first place.

// TinyUSB MSC device object. USBMSC currently only supports a single LUN
// per device on arduino-esp32, which is exactly what we want here — one
// drive, the MP3 partition, nothing else.
static USBMSC s_msc;

UsbMscManager& UsbMscManager::getInstance() {
  static UsbMscManager instance;
  return instance;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  begin() — called once at boot from the init orchestrator
// ═══════════════════════════════════════════════════════════════════════════════
bool UsbMscManager::begin() {
  if (_initialized) return true;

  s_msc.vendorID("EDGEHAX");       // max 8 chars
  s_msc.productID("ShadowTune");    // max 16 chars
  s_msc.productRevision("1.0");    // max 4 chars
  s_msc.onStartStop(onStartStop);
  s_msc.onRead(onRead);
  s_msc.onWrite(onWrite);
  s_msc.isWritable(true);

  // Hidden ("no disk inserted") until the user explicitly enters USB Drive
  // mode from the mode menu — see usb_msc_manager.h for why local SD
  // access and USB MSC must never be active on the card at the same time.
  s_msc.mediaPresent(false);

  if (!s_msc.begin(MP3_PARTITION_SECTOR_COUNT, MP3_PARTITION_SECTOR_SIZE)) {
    Serial.println("[UsbMsc] MSC.begin() failed.");
    return false;
  }

  // Bring up the composite USB device now that the MSC interface is
  // registered with it. Safe to call more than once if another manager
  // (e.g. a future USBCDC use) also calls USB.begin().
  if (!USB.begin()) {
    Serial.println("[UsbMsc] USB.begin() failed.");
    return false;
  }

  _initialized = true;
  Serial.println("[UsbMsc] USB MSC ready (hidden until USB Drive mode is entered).");
  return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  start() / stop() — enter/leave USB Drive mode (called from the UI)
// ═══════════════════════════════════════════════════════════════════════════════
bool UsbMscManager::start() {
  if (!_initialized && !begin()) return false;
  if (_active) return true;

  // Hand the card over to USB MSC. This unmounts the local FatFs mount
  // first so the two never touch the card at the same time.
  sd.suspendForUsb();

  _hostEjectRequested = false;
  s_msc.mediaPresent(true);
  _active = true;
  Serial.println("[UsbMsc] USB Drive mode ON — MP3 partition exposed to host.");
  return true;
}

void UsbMscManager::stop() {
  if (!_active) return;

  // Hide the drive first so the host stops issuing reads/writes...
  s_msc.mediaPresent(false);
  _active = false;
  delay(50);  // let the host's I/O in flight settle before we touch the card

  // ...then take the card back for on-device playback/browsing.
  if (!sd.resumeAfterUsb()) {
    Serial.println("[UsbMsc] WARNING: failed to remount MP3 partition after USB Drive mode.");
  }
  Serial.println("[UsbMsc] USB Drive mode OFF — MP3 partition remounted locally.");
}

void UsbMscManager::tick() {
  // The onStartStop callback runs on the TinyUSB task and only sets a
  // flag — the actual unmount/remount (which touches the SD/SPI bus)
  // always happens here, back on the main loop, to avoid the two tasks
  // racing on the same hardware.
  if (_hostEjectRequested) {
    _hostEjectRequested = false;
    stop();
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  TinyUSB MSC callbacks
//  bufsize is always a whole multiple of the 512-byte sector size; offset
//  is an intra-sector byte offset that arduino-esp32 always calls us with
//  as 0 for full-sector block transfers, so it's unused here (matches the
//  pattern used by sd_manager.cpp's own FatFs disk-I/O wrapper).
// ═══════════════════════════════════════════════════════════════════════════════
int32_t UsbMscManager::onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
  UsbMscManager& self = getInstance();
  if (!self._active) return -1;
  if (bufsize == 0 || (bufsize % MP3_PARTITION_SECTOR_SIZE) != 0) return -1;

  uint32_t count = bufsize / MP3_PARTITION_SECTOR_SIZE;
  if ((uint64_t)lba + count > MP3_PARTITION_SECTOR_COUNT) return -1;

  uint8_t* buf = (uint8_t*)buffer;
  for (uint32_t i = 0; i < count; i++) {
    if (!SD.readRAW(buf + i * MP3_PARTITION_SECTOR_SIZE, MP3_PARTITION_OFFSET_SECTORS + lba + i))
      return -1;
  }
  return (int32_t)bufsize;
}

int32_t UsbMscManager::onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
  UsbMscManager& self = getInstance();
  if (!self._active) return -1;
  if (bufsize == 0 || (bufsize % MP3_PARTITION_SECTOR_SIZE) != 0) return -1;

  uint32_t count = bufsize / MP3_PARTITION_SECTOR_SIZE;
  if ((uint64_t)lba + count > MP3_PARTITION_SECTOR_COUNT) return -1;

  for (uint32_t i = 0; i < count; i++) {
    if (!SD.writeRAW(buffer + i * MP3_PARTITION_SECTOR_SIZE, MP3_PARTITION_OFFSET_SECTORS + lba + i))
      return -1;
  }
  return (int32_t)bufsize;
}

bool UsbMscManager::onStartStop(uint8_t power_condition, bool start, bool load_eject) {
  // The host asked to eject/stop the volume (e.g. "Safely Remove" on the
  // PC side). Just set a flag — see tick() for why the real work happens
  // there instead of here.
  if (!start && load_eject) {
    getInstance()._hostEjectRequested = true;
  }
  return true;
}

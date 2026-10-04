#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  sd_manager.h — on-board microSD, its own dedicated SPI bus
// ═══════════════════════════════════════════════════════════════════════════════
// The microSD slot has four pins entirely separate from the display/touch
// bus (GPIO 10-13), so card I/O never contends with screen redraws.
#include <Arduino.h>
#include <SPI.h>

// Switch FATFS default drive to MP3 partition (drive 1)
void sd_switch_to_mp3();
// Switch FATFS default drive back to Vault partition (drive 0)
void sd_switch_to_vault();

// ── MP3 partition geometry — single source of truth ─────────────────────
// Both the on-device FatFs disk-I/O wrapper (sd_manager.cpp) and the USB
// MSC block device (usb_msc_manager.cpp) must read/write the exact same
// sector range on the physical SD card. Defining these once here and
// pulling them into both .cpp files avoids the two ever drifting apart,
// which would otherwise silently corrupt the filesystem.
extern const uint32_t MP3_PARTITION_OFFSET_SECTORS;  // first sector of the MP3 partition
extern const uint32_t MP3_PARTITION_SECTOR_COUNT;    // size of the MP3 partition, in sectors
extern const uint16_t MP3_PARTITION_SECTOR_SIZE;     // bytes/sector (512)

class SdManager {
public:
  bool begin();
  bool isOK() const { return _ok; }

  // ── USB Mass Storage hand-off ───────────────────────────────────────
  // The on-device FatFs mount and USB MSC's raw sector access must never
  // touch the card at the same time — cleanly unmount before letting the
  // host take over, and remount afterwards.
  //
  // Cleanly unmounts the MP3-partition FATFS (f_mount(..., 0)) so USB MSC
  // can safely take raw, sector-level ownership of the card. isOK()
  // returns false after this call.
  void suspendForUsb();

  // Re-initializes the card and remounts the MP3 partition for on-device
  // playback/browsing (equivalent to calling begin() again). Call after
  // the USB host has ejected/disconnected. Returns true on success.
  bool resumeAfterUsb();

private:
  SPIClass _spi{HSPI};
  bool _ok = false;
};

// ── Global SD manager instance (defined in main.cpp) ─────────────────────
// UsbMscManager needs this to suspend/resume the local FatFs mount around
// USB Drive mode — same extern pattern as mp3Mgr in mp3_manager.h.
extern SdManager sd;

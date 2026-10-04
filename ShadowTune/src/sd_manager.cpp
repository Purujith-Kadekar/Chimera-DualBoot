#include "sd_manager.h"
#include "gpio_config_manager.h"
#include <SD.h>
#include <ff.h>
#include <diskio_impl.h>
#include <esp_vfs_fat.h>

// MP3 partition: offset=20608 sectors (10551296 bytes), size=7538688 sectors (3770 MB)
// Declared in sd_manager.h and shared with usb_msc_manager.cpp — see the
// header comment for why these must not be redefined anywhere else.
const uint32_t MP3_PARTITION_OFFSET_SECTORS = 20608;
const uint32_t MP3_PARTITION_SECTOR_COUNT   = 7538688;
const uint16_t MP3_PARTITION_SECTOR_SIZE    = 512;

#define MP3_PART_OFFSET  MP3_PARTITION_OFFSET_SECTORS
#define SECTOR_SIZE      MP3_PARTITION_SECTOR_SIZE

static FATFS s_mp3_fs;

// ── Custom disk I/O for partition 2 (wraps SD.readRAW with sector offset) ──
static DSTATUS mp3_init(BYTE pdrv) { return 0; }
static DSTATUS mp3_status(BYTE pdrv) { return 0; }

static DRESULT mp3_read(BYTE pdrv, BYTE* buff, DWORD sector, UINT count) {
  for (UINT i = 0; i < count; i++) {
    if (!SD.readRAW(buff + i * SECTOR_SIZE, sector + MP3_PART_OFFSET + i))
      return RES_ERROR;
  }
  return RES_OK;
}

static DRESULT mp3_write(BYTE pdrv, const BYTE* buff, DWORD sector, UINT count) {
  for (UINT i = 0; i < count; i++) {
    if (!SD.writeRAW((uint8_t*)buff + i * SECTOR_SIZE, sector + MP3_PART_OFFSET + i))
      return RES_ERROR;
  }
  return RES_OK;
}

static DRESULT mp3_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
  switch (cmd) {
    case CTRL_SYNC: return RES_OK;
    case GET_SECTOR_COUNT: *(DWORD*)buff = MP3_PARTITION_SECTOR_COUNT; return RES_OK;
    case GET_SECTOR_SIZE:  *(WORD*)buff  = SECTOR_SIZE; return RES_OK;
    case GET_BLOCK_SIZE:   *(DWORD*)buff = 1; return RES_OK;
    default: return RES_PARERR;
  }
}

void sd_switch_to_mp3() {}
void sd_switch_to_vault() {}

bool SdManager::begin() {
  if (_ok) { SD.end(); _ok = false; }
  _spi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);

  // 1. Initialize SD card hardware via Arduino SD library.
  //    SD.begin() initializes SPI bus, probes card, mounts partition 1,
  //    and registers VFS at "/" with a FATFS object at drive 0.
  static const uint32_t speeds[] = {20000000, 16000000, 10000000, 4000000};
  bool hwOK = false;
  for (uint32_t hz : speeds) {
    hwOK = SD.begin(PIN_SD_CS, _spi, hz);
    Serial.printf("[SdManager] SD.begin at %u Hz: %s\n", (unsigned)hz,
                  hwOK ? "OK" : "failed");
    if (hwOK) break;
    SD.end();
  }
  if (!hwOK) return false;

  // 2. Replace the disk I/O at drive 0 with our partition 2 wrapper.
  //    This makes ALL reads/writes go to partition 2 (MP3 partition).
  ff_diskio_impl_t mp3_impl = {
    .init = mp3_init,
    .status = mp3_status,
    .read = mp3_read,
    .write = mp3_write,
    .ioctl = mp3_ioctl
  };
  ff_diskio_register(0, &mp3_impl);

  // 3. Re-register VFS at "/" with OUR FATFS object (not SD.begin()'s).
  //    This is CRITICAL — without this, the VFS still points to SD.begin()'s
  //    FATFS which has cached partition 1 data. Every read would hit stale
  //    cache → wrong data → retries → 1-2 second lag.
  FATFS* p_fs = &s_mp3_fs;
  esp_err_t err = esp_vfs_fat_register("/", "sd0", 5, &p_fs);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    Serial.printf("[SdManager] esp_vfs_fat_register failed: %s\n", esp_err_to_name(err));
  }

  // 4. Mount partition 2's FAT filesystem at drive 0 using our FATFS object.
  //    f_mount with mount_now=1 reads partition 2's boot sector and
  //    initializes the FATFS (cluster size, FAT location, root dir, etc.)
  memset(&s_mp3_fs, 0, sizeof(s_mp3_fs));
  FRESULT fr = f_mount(&s_mp3_fs, "0:", 1);
  if (fr != FR_OK) {
    Serial.printf("[SdManager] f_mount partition 2 failed: %d\n", fr);
    // Fallback: re-mount partition 1 (so at least something works)
    SD.begin(PIN_SD_CS, _spi, 20000000);
    Serial.println("[SdManager] WARNING: partition 2 mount failed, using partition 1");
    return true;
  }

  _ok = true;
  Serial.println("[SdManager] MP3 partition (p2) mounted at drive 0 + VFS /");
  Serial.printf("[SdManager]   offset: %u sectors (%u bytes)\n",
                (unsigned)MP3_PART_OFFSET, (unsigned)(MP3_PART_OFFSET * SECTOR_SIZE));
  return true;
}

// ── USB Mass Storage hand-off ────────────────────────────────────────────
void SdManager::suspendForUsb() {
  if (!_ok) return;
  // Cleanly unmount the FATFS object at drive 0. This flushes any cached
  // directory/FAT state and releases our claim on the card so raw sector
  // reads/writes from USB MSC (going through the same SD.readRAW/writeRAW
  // path) don't race against an in-memory FatFs cache that's gone stale
  // the moment the host modifies the card.
  f_mount(nullptr, "0:", 0);
  _ok = false;
  Serial.println("[SdManager] Suspended — MP3 partition released for USB MSC.");
}

bool SdManager::resumeAfterUsb() {
  Serial.println("[SdManager] Resuming after USB MSC — remounting MP3 partition...");
  return begin();
}

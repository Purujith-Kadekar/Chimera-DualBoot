#include "sd_manager.h"
#include "board_config.h"
#include <SD.h>
#include <ff.h>
#include <diskio_impl.h>
#include <esp_vfs_fat.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

// ── Vault partition geometry ─────────────────────────────────────────────
// Partition 1 on the SD card, as reported by the partition table:
//   Type   : 0x0E (FAT16, LBA)
//   Offset : 65536 bytes  = sector 128   (65536 / 512)
//   Size   : 10 MB        = 20480 sectors (10 * 1024 * 1024 / 512)
//
// This sits immediately before ShadowTune's MP3 partition (which starts at
// sector 20608 -- see shadowtune/src/sd_manager.cpp), i.e. 128 + 20480 =
// 20608 exactly. The two partitions are back-to-back with no gap, which is
// strong confirmation this geometry is correct for this card layout.
//
// v11.1 fix: plain SD.begin() delegates to FatFs's default single-partition
// mode, which mounts whichever MBR table entry it finds FIRST -- not
// necessarily this one. After a disk-image restore, MBR entry order can
// change, silently pointing SD.begin() at the wrong partition (or one with
// no vault.db). Mirroring ShadowTune's approach: register a custom disk
// I/O driver that always reads/writes at this fixed sector offset,
// regardless of MBR table order. This makes vault mounting deterministic.
static const uint32_t VAULT_PARTITION_OFFSET_SECTORS = 128;
static const uint32_t VAULT_PARTITION_SECTOR_COUNT   = 20480;
static const uint16_t VAULT_PARTITION_SECTOR_SIZE    = 512;

#define VAULT_PART_OFFSET VAULT_PARTITION_OFFSET_SECTORS
#define SECTOR_SIZE       VAULT_PARTITION_SECTOR_SIZE

static FATFS s_vault_fs;

// ── Custom disk I/O for the vault partition (wraps SD.readRAW/writeRAW
//    with a fixed sector offset, exactly like ShadowTune does for its
//    MP3 partition) ─────────────────────────────────────────────────────
static DSTATUS vault_disk_init(BYTE pdrv) { return 0; }
static DSTATUS vault_disk_status(BYTE pdrv) { return 0; }

static DRESULT vault_disk_read(BYTE pdrv, BYTE* buff, DWORD sector, UINT count) {
  for (UINT i = 0; i < count; i++) {
    if (!SD.readRAW(buff + i * SECTOR_SIZE, sector + VAULT_PART_OFFSET + i))
      return RES_ERROR;
  }
  return RES_OK;
}

static DRESULT vault_disk_write(BYTE pdrv, const BYTE* buff, DWORD sector, UINT count) {
  for (UINT i = 0; i < count; i++) {
    if (!SD.writeRAW((uint8_t*)buff + i * SECTOR_SIZE, sector + VAULT_PART_OFFSET + i))
      return RES_ERROR;
  }
  return RES_OK;
}

static DRESULT vault_disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
  switch (cmd) {
    case CTRL_SYNC:        return RES_OK;
    case GET_SECTOR_COUNT: *(DWORD*)buff = VAULT_PARTITION_SECTOR_COUNT; return RES_OK;
    case GET_SECTOR_SIZE:  *(WORD*)buff  = SECTOR_SIZE; return RES_OK;
    case GET_BLOCK_SIZE:   *(DWORD*)buff = 1; return RES_OK;
    default: return RES_PARERR;
  }
}

// ── Diagnostics (set to 0 once the card is behaving) ─────────────────────
#define SD_DIAG 1
#if SD_DIAG
// Raw MBR dump: tells you where the card's partitions REALLY start.
static void sd_diag_mbr() {
  uint8_t s[SECTOR_SIZE];
  if (!SD.readRAW(s, 0)) { Serial.println("[SdDiag] cannot read physical sector 0"); return; }
  Serial.printf("[SdDiag] sector0: first byte=%02X sig=%02X%02X (MBR sig should be 55AA; EB/E9 first byte = FAT boot sector, i.e. no MBR)\n",
                s[0], s[510], s[511]);
  for (int i = 0; i < 4; i++) {
    const uint8_t* e = s + 446 + i * 16;
    uint32_t lba = e[8] | (e[9] << 8) | (e[10] << 16) | ((uint32_t)e[11] << 24);
    uint32_t cnt = e[12] | (e[13] << 8) | (e[14] << 16) | ((uint32_t)e[15] << 24);
    Serial.printf("[SdDiag] MBR entry %d: boot=%02X type=%02X start=%u sectors=%u (%u MB)%s\n",
                  i + 1, e[0], e[4], (unsigned)lba, (unsigned)cnt, (unsigned)(cnt / 2048),
                  (lba == VAULT_PARTITION_OFFSET_SECTORS) ? "   <-- matches firmware offset" : "");
  }
}

// FAT boot sector (BPB) of whatever is at the given physical sector.
static void sd_diag_bpb(uint32_t lba) {
  uint8_t s[SECTOR_SIZE];
  if (!SD.readRAW(s, lba)) { Serial.printf("[SdDiag] cannot read sector %u\n", (unsigned)lba); return; }
  char oem[9], fs16[9], label[12];
  memcpy(oem, s + 3, 8);    oem[8] = 0;
  memcpy(label, s + 43, 11); label[11] = 0;
  memcpy(fs16, s + 54, 8);  fs16[8] = 0;
  Serial.printf("[SdDiag] sector %u: jump=%02X oem='%s' bytes/sec=%u sec/clus=%u fs='%s' label='%s' sig=%02X%02X\n",
                (unsigned)lba, s[0], oem, (unsigned)(s[11] | (s[12] << 8)), (unsigned)s[13],
                fs16, label, s[510], s[511]);
}

// What FatFs sees directly (bypasses the VFS layer) vs what POSIX/VFS sees.
static void sd_diag_listing() {
  FILINFO fi;
  FRESULT fr = f_stat("0:/vault.db", &fi);
  Serial.printf("[SdDiag] FatFs f_stat(0:/vault.db) = %d  (0 OK, 4 NO_FILE, 5 NO_PATH)\n", (int)fr);

  FF_DIR dir;
  fr = f_opendir(&dir, "0:/");
  if (fr != FR_OK) {
    Serial.printf("[SdDiag] FatFs f_opendir(0:/) failed: %d\n", (int)fr);
  } else {
    Serial.println("[SdDiag] FatFs root listing:");
    int n = 0;
    while (f_readdir(&dir, &fi) == FR_OK && fi.fname[0] && n < 40) {
      Serial.printf("[SdDiag]    %s%s  %lu bytes\n", fi.fname, (fi.fattrib & AM_DIR) ? "/" : "",
                    (unsigned long)fi.fsize);
      n++;
    }
    if (n == 0) Serial.println("[SdDiag]    (root directory is EMPTY)");
    f_closedir(&dir);
  }

  struct stat st;
  int r = stat("/sd/vault.db", &st);
  Serial.printf("[SdDiag] VFS stat(/sd/vault.db) = %d errno=%d\n", r, r ? errno : 0);
  DIR* d = opendir("/sd");
  if (!d) {
    Serial.printf("[SdDiag] VFS opendir(/sd) failed errno=%d\n", errno);
  } else {
    int n = 0;
    while (struct dirent* e = readdir(d)) { Serial.printf("[SdDiag]    VFS: %s\n", e->d_name); if (++n >= 40) break; }
    if (n == 0) Serial.println("[SdDiag]    VFS: (empty)");
    closedir(d);
  }
}
#endif

bool SdManager::begin() {
  // Safe to call again after a prior success (e.g. a save-time remount
  // attempt) -- end() the old mount first so SD.begin() doesn't just
  // return the stale cached state.
  if (_ok) {
    SD.end();
    _ok = false;
  }
  _spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  // 1. Initialize SD card hardware via Arduino SD library. This step only
  //    needs to succeed at the SPI/card-init level -- WHICH partition it
  //    auto-mounts here doesn't matter, since step 2-4 override it.
  static const uint32_t speeds[] = {20000000, 16000000, 10000000, 4000000};
  bool hwOK = false;
  for (uint32_t hz : speeds) {
    hwOK = SD.begin(SD_CS, _spi, hz);
    Serial.printf("[SdManager] SD.begin (hw init) at %u Hz: %s\n", (unsigned)hz,
                  hwOK ? "OK" : "failed");
    if (hwOK) break;
    SD.end();
  }
  if (!hwOK) {
    Serial.println("[SdManager] FAILED: SD card hardware init failed (check wiring/card insertion)");
    return false;
  }

  // 2. Replace the disk I/O at drive 0 with our fixed-offset vault wrapper.
  //    This makes ALL reads/writes go to the vault partition, regardless
  //    of MBR table order.
  ff_diskio_impl_t vault_impl = {
    .init   = vault_disk_init,
    .status = vault_disk_status,
    .read   = vault_disk_read,
    .write  = vault_disk_write,
    .ioctl  = vault_disk_ioctl
  };
  ff_diskio_register(0, &vault_impl);

  // 3. Re-register VFS at "/sd" with OUR FATFS object (not SD.begin()'s).
  //    Without this, the VFS still points to SD.begin()'s FATFS, which has
  //    cached whatever partition it auto-mounted in step 1 -- every read
  //    would hit that stale cache instead of our vault partition.
  FATFS* p_fs = &s_vault_fs;
  // NOTE: the 2nd argument is the FatFs *drive path* and must be "0:" (it is
  // prepended to every VFS path). It was "sd0", which would make every
  // lookup resolve to "sd0/vault.db" if this registration ever succeeded.
  esp_err_t err = esp_vfs_fat_register("/sd", "0:", 5, &p_fs);
  Serial.printf("[SdManager] esp_vfs_fat_register(/sd) -> %s (INVALID_STATE = already registered by SD.begin, expected)\n",
                esp_err_to_name(err));

  // 4. Mount the vault partition's FAT filesystem at drive 0 using our
  //    FATFS object. f_mount with mount_now=1 reads the vault partition's
  //    own boot sector (at our fixed offset) and initializes the FATFS.
  memset(&s_vault_fs, 0, sizeof(s_vault_fs));
  FRESULT fr = f_mount(&s_vault_fs, "0:", 1);
  if (fr != FR_OK) {
    Serial.printf("[SdManager] f_mount vault partition failed: %d\n", fr);
    Serial.println("[SdManager] FAILED: vault partition not readable at expected offset (sector 128)");
    _ok = false;
    return false;
  }

  _ok = true;
  Serial.println("[SdManager] Vault partition (p1) mounted at drive 0 + VFS /sd");
  Serial.printf("[SdManager]   offset: %u sectors (%u bytes), size: %u sectors (%u MB)\n",
                (unsigned)VAULT_PART_OFFSET, (unsigned)(VAULT_PART_OFFSET * SECTOR_SIZE),
                (unsigned)VAULT_PARTITION_SECTOR_COUNT,
                (unsigned)(VAULT_PARTITION_SECTOR_COUNT * SECTOR_SIZE / (1024 * 1024)));

#if SD_DIAG
  sd_diag_mbr();
  sd_diag_bpb(VAULT_PART_OFFSET);
  sd_diag_listing();
#endif

  if (SD.exists("/vault.db")) {
    Serial.println("[SdManager] vault.db found on vault partition — OK");
  } else {
    Serial.println("[SdManager] NOTE: vault.db not found yet (first boot, or needs setup)");
  }

  return true;
}


#pragma once
// ============================================================================
//  boot_manager.h -- finds the two app partitions and selects the next boot.
//
//  How the "menu on every boot" works (no changes to the apps needed):
//    1. This launcher lives in the factory partition.
//    2. launch() calls esp_ota_set_boot_partition(ota_x). With bootloader
//       rollback enabled, that entry is stored in state NEW.
//    3. Bootloader boots ota_x once and flips it to PENDING_VERIFY.
//    4. The app never confirms itself, so on the NEXT reset (power cycle,
//       deep-sleep wake, crash, restart) the bootloader marks it ABORTED and
//       falls back to the factory app = this launcher.
// ============================================================================
#include <Arduino.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>

struct AppSlot {
  const char*             name;       // shown on the card
  const char*             subtitle;
  esp_partition_subtype_t subtype;    // ota_0 / ota_1
  uint16_t                color;      // card accent (RGB565)
  // filled by inspect():
  const esp_partition_t*  part = nullptr;
  bool                    valid = false;   // partition holds a real app image
  char                    built[20] = {0}; // "Sep 26 2026" from the app descriptor
};

namespace BootManager {
  // Look up the partition and check it contains an ESP32 app image.
  void inspect(AppSlot& slot);

  // Verify the image and make it the next boot target. ESP_OK on success.
  esp_err_t selectNextBoot(const AppSlot& slot);
}

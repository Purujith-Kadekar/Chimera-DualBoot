#include "boot_manager.h"
#include <string.h>

namespace BootManager {

void inspect(AppSlot& slot) {
  slot.part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, slot.subtype, nullptr);
  slot.valid = false;
  slot.built[0] = '\0';
  if (!slot.part) return;

  // First byte of every ESP32 app image is the magic 0xE9; erased flash is 0xFF.
  uint8_t magic = 0;
  if (esp_partition_read(slot.part, 0, &magic, 1) != ESP_OK || magic != 0xE9) return;

  esp_app_desc_t desc;
  if (esp_ota_get_partition_description(slot.part, &desc) != ESP_OK) return;

  slot.valid = true;
  strlcpy(slot.built, desc.date, sizeof(slot.built));
}

esp_err_t selectNextBoot(const AppSlot& slot) {
  if (!slot.part || !slot.valid) return ESP_ERR_NOT_FOUND;
  // Validates the whole image (checksum/hash) before writing otadata.
  return esp_ota_set_boot_partition(slot.part);
}

}  // namespace BootManager

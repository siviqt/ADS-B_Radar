#include "esp_partition.h"
#include "nvs_guard.h"

// IDF's default guard asks esp_ota_get_running_partition(), which aborts for
// AppFS-mapped code outside an APP partition. Keep the guard enabled, replacing
// its OTA-specific check with a stricter allowlist for this application's only
// flash-write destination. The SDK separately enforces readonly partitions.
// No app/OTA/AppFS/FAT/bootloader/partition-table write is allowed here.
bool __wrap_esp_partition_main_flash_region_safe(size_t address, size_t length) {
    const esp_partition_t *nvs = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
    return nvs && !nvs->readonly &&
        adsb_nvs_write_in_bounds(address, length, nvs->address, nvs->size);
}

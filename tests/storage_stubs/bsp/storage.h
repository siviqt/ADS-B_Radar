#pragma once
#include <stdbool.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
typedef enum { BSP_STORAGE_TYPE_INTERNAL, BSP_STORAGE_TYPE_SDCARD } bsp_storage_type_t;
esp_err_t bsp_storage_mount_advanced(bsp_storage_type_t, const char*, int, bool);
esp_err_t bsp_storage_unmount(bsp_storage_type_t);

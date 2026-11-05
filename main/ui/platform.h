#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    size_t internal_free, internal_min, largest_block, psram_free;
    unsigned tasks, stack_free, flash_bytes;
    int rssi, battery_percent, battery_mv;
    bool connected, charging;
    char ip[20];
} adsb_health_t;
void adsb_health_read(adsb_health_t *out);
#ifdef __cplusplus
}
#endif

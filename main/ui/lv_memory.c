#include "lvgl.h"
#include "esp_heap_caps.h"
#include <string.h>
// All LVGL widgets and transient draw tasks use PSRAM, preserving radio/DMA memory.
void lv_mem_init(void) {}
void lv_mem_deinit(void) {}
lv_mem_pool_t lv_mem_add_pool(void *memory,size_t bytes) {(void)memory;(void)bytes;return NULL;}
void lv_mem_remove_pool(lv_mem_pool_t pool) {(void)pool;}
void *lv_malloc_core(size_t size) {return heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
void *lv_realloc_core(void *p,size_t size) {return heap_caps_realloc(p,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
void lv_free_core(void *p) {heap_caps_free(p);}
void lv_mem_monitor_core(lv_mem_monitor_t *monitor) {
    memset(monitor,0,sizeof(*monitor));monitor->total_size=heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    monitor->free_size=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);monitor->free_biggest_size=heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
}
lv_result_t lv_mem_test_core(void) {return heap_caps_check_integrity(MALLOC_CAP_SPIRAM,false)?LV_RESULT_OK:LV_RESULT_INVALID;}

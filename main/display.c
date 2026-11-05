#include "display.h"
#include "rotate.h"
#include <stdlib.h>
#include "bsp/device.h"
#include "bsp/display.h"
#include "dsi_panel_nicolaielectronics_st7701.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "lvgl.h"

#define PIXELS (800 * 24)
static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t done;
static uint16_t *transfer;
static uint64_t window_start, frame_start, rendered_pixels, frame_pixels, render_us, max_render_us;
static unsigned rendered_frames;

static void refresh_event(lv_event_t *event) {
    uint64_t now = esp_timer_get_time();
    if(lv_event_get_code(event) == LV_EVENT_REFR_START) { frame_start=now;frame_pixels=0;return; }
    if(frame_pixels) {
        uint64_t elapsed=now-frame_start;
        render_us+=elapsed;if(elapsed>max_render_us)max_render_us=elapsed;
        rendered_frames++;rendered_pixels+=frame_pixels;
    }
    if(!window_start)window_start=now;
    if(now-window_start>=5000000 && rendered_frames) {
        ESP_LOGI("display", "Refresh: %.1f fps, %.1f ms avg / %.1f ms max, %llu pixels/frame",
                 rendered_frames*1000000.0/(now-window_start),render_us/1000.0/rendered_frames,
                 max_render_us/1000.0,(unsigned long long)(rendered_pixels/rendered_frames));
        window_start=now;rendered_frames=0;rendered_pixels=0;render_us=0;max_render_us=0;
    }
}

static uint32_t ticks(void) { return esp_timer_get_time() / 1000; }

static bool IRAM_ATTR completed(esp_lcd_panel_handle_t p, esp_lcd_dpi_panel_event_data_t *e, void *ctx) {
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(done, &wake);
    return wake == pdTRUE;
}

static void flush(lv_display_t *display, const lv_area_t *a, uint8_t *pixels) {
    const uint16_t *src = (const uint16_t *)pixels;
    int w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
    frame_pixels+=(uint64_t)w*h;
    // BSP rotation 270 maps to PAX's clockwise orientation: (479-y, x).
    // Rotate each packed dirty rectangle, keeping the LVGL buffer independent of DMA.
    adsb_rotate565(transfer, src, w, h);
    esp_err_t err = esp_lcd_panel_draw_bitmap(panel, 479 - a->y2, a->x1,
                                             480 - a->y1, a->x2 + 1, transfer);
    if (err != ESP_OK || xSemaphoreTake(done, pdMS_TO_TICKS(1000)) != pdTRUE) {
        // Completion is unknown: never reuse a potentially DMA-owned buffer.
        ESP_LOGE("display", "Panel transfer failed/timed out: %s", esp_err_to_name(err));
        bsp_device_restart_to_launcher();
        abort();
    }
    lv_display_flush_ready(display);
}

esp_err_t adsb_display_init(void) {
    size_t w, h;
    bsp_display_color_format_t fmt;
    bsp_display_endianness_t endian;
    ESP_ERROR_CHECK(bsp_display_get_parameters(&w, &h, &fmt, &endian));
    if (w != 480 || h != 800 || fmt != BSP_DISPLAY_COLOR_FORMAT_16_565RGB ||
        endian != BSP_DISPLAY_ENDIAN_LITTLE || bsp_display_get_default_rotation() != BSP_DISPLAY_ROTATION_270)
        return ESP_ERR_NOT_SUPPORTED;
    ESP_ERROR_CHECK(bsp_display_get_panel(&panel));
    uint8_t id[3];
    esp_err_t id_result = st7701_get_id(id);
    if (id_result == ESP_OK) {
        ESP_LOGI("display", "Panel ID %02X %02X %02X; variant %u; bounded DSI reads enabled",
                 id[0], id[1], id[2], st7701_is_display_variant_2() ? 2u : 1u);
    } else {
        ESP_LOGW("display", "Panel ID unavailable: %s; using the driver's default variant",
                 esp_err_to_name(id_result));
    }
    done = xSemaphoreCreateBinary();
    transfer = heap_caps_malloc(PIXELS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *draw = heap_caps_malloc(PIXELS * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!done || !transfer || !draw) return ESP_ERR_NO_MEM;
    // Sole submission/completion owner after BSP initialization; never call bsp_display_blit().
    esp_lcd_dpi_panel_event_callbacks_t callbacks = {.on_color_trans_done = completed};
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(panel, &callbacks, NULL));
    lv_init();
    lv_tick_set_cb(ticks);
    lv_display_t *display = lv_display_create(800, 480);
    if (!display) return ESP_ERR_NO_MEM;
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, draw, NULL, PIXELS * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    lv_display_add_event_cb(display,refresh_event,LV_EVENT_REFR_START,NULL);
    lv_display_add_event_cb(display,refresh_event,LV_EVENT_REFR_READY,NULL);
    return ESP_OK;
}

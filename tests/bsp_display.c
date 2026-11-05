#include "platform.h"
#include <assert.h>
#include <stdio.h>

static int panel_result, panel_calls, callback_calls, te_calls, otp_calls, nvs_calls, power_calls;
static st7701_configuration_t last_config;
static int handle;

esp_err_t esp_ldo_acquire_channel(const esp_ldo_channel_config_t *config, esp_ldo_channel_handle_t *out) {
    assert(config->chan_id == BSP_DSI_LDO_CHAN);
    *out = &handle; power_calls++; return ESP_OK;
}
esp_err_t st7701_initialize(const st7701_configuration_t *config) {
    last_config = *config; panel_calls++; return panel_result;
}
esp_lcd_panel_handle_t st7701_get_panel(void) { return panel_result == ESP_OK ? &handle : NULL; }
esp_lcd_panel_io_handle_t st7701_get_panel_io(void) { return &handle; }
esp_err_t st7701_get_parameters(size_t *w, size_t *h, lcd_color_format_t *fmt) {
    if(w) *w=480;
    if(h) *h=800;
    *fmt=LCD_COLOR_FMT_RGB565;
    return ESP_OK;
}
esp_err_t st7701_get_vcom(uint8_t *out) { *out=0x2D; return ESP_OK; }
esp_err_t st7701_get_vcom_otp(uint8_t *out) { otp_calls++; *out=0x40; return ESP_OK; }
esp_err_t st7701_set_vcom(uint8_t value) { (void)value; return ESP_OK; }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return &handle; }
int xSemaphoreGive(SemaphoreHandle_t sem) { assert(sem); return 1; }
int xSemaphoreGiveFromISR(SemaphoreHandle_t sem, void *wake) { (void)wake; assert(sem); return 1; }
int xSemaphoreTake(SemaphoreHandle_t sem, int ticks) { (void)ticks; assert(sem); return 1; }
esp_err_t esp_lcd_dpi_panel_register_event_callbacks(esp_lcd_panel_handle_t panel,
    const esp_lcd_dpi_panel_event_callbacks_t *callbacks, void *ctx) {
    (void)ctx; assert(panel && callbacks->on_color_trans_done); callback_calls++; return ESP_OK;
}
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io, int cmd, const void *data, size_t size) {
    (void)io; (void)cmd; (void)data; (void)size; return ESP_OK;
}
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x1, int y1, int x2, int y2, const void *data) {
    (void)panel; (void)x1; (void)y1; (void)x2; (void)y2; (void)data; return ESP_OK;
}
esp_err_t gpio_config(const gpio_config_t *config) { assert(config->pin_bit_mask); return ESP_OK; }
esp_err_t gpio_isr_handler_add(int pin, void (*callback)(void *), void *ctx) {
    (void)ctx; assert(pin == BSP_LCD_TE_PIN && callback); te_calls++; return ESP_OK;
}
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *out) {
    (void)name; (void)out; assert(mode == NVS_READONLY); nvs_calls++; return ESP_ERR_NOT_FOUND;
}
esp_err_t nvs_get_u8(nvs_handle_t nvs, const char *key, uint8_t *out) {
    (void)nvs; (void)key; (void)out; assert(false); return ESP_FAIL;
}
void nvs_close(nvs_handle_t nvs) { (void)nvs; }
esp_err_t bsp_tanmatsu_coprocessor_get_handle(tanmatsu_coprocessor_handle_t *out) { *out=&handle; return ESP_OK; }
esp_err_t tanmatsu_coprocessor_get_display_backlight(tanmatsu_coprocessor_handle_t coprocessor, uint8_t *out) {
    (void)coprocessor; *out=255; return ESP_OK;
}
esp_err_t tanmatsu_coprocessor_set_display_backlight(tanmatsu_coprocessor_handle_t coprocessor, uint8_t value) {
    (void)coprocessor; (void)value; return ESP_OK;
}

int main(void) {
    const bsp_display_configuration_t config={BSP_DISPLAY_COLOR_FORMAT_24_888RGB, 2};
    const int failures[]={ESP_ERR_NOT_FOUND, ESP_ERR_NO_MEM, ESP_ERR_TIMEOUT, ESP_FAIL};
    for(size_t i=0;i<sizeof(failures)/sizeof(failures[0]);i++) {
        panel_result=failures[i];
        assert(bsp_display_initialize(&config) == panel_result);
        assert(last_config.reset_pin == BSP_LCD_RESET_PIN && last_config.use_24_bit_color && last_config.num_fbs == 2);
        assert(callback_calls == 0 && te_calls == 0 && otp_calls == 0 && nvs_calls == 0);
        assert(bsp_display_get_parameters(NULL,NULL,NULL,NULL) == ESP_FAIL);
    }
    panel_result=ESP_ERR_NOT_FOUND;
    assert(bsp_display_initialize(NULL) == ESP_ERR_NOT_FOUND);
    assert(!last_config.use_24_bit_color && last_config.num_fbs == 1);
    panel_result=ESP_OK;
    assert(bsp_display_initialize(NULL) == ESP_OK);
    assert(callback_calls == 1 && te_calls == 1 && otp_calls == 1 && nvs_calls == 1 && power_calls == 1);
    int calls=panel_calls;
    assert(bsp_display_initialize(NULL) == ESP_OK && panel_calls == calls);
    size_t w=0,h=0; bsp_display_color_format_t fmt; bsp_display_endianness_t endian;
    assert(bsp_display_get_parameters(&w,&h,&fmt,&endian) == ESP_OK && w == 480 && h == 800);
    puts("BSP panel errors propagate before settings/callback initialization; successful initialization remains usable");
}

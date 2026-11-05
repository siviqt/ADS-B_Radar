#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_TIMEOUT 0x107
#define IRAM_ATTR
#define BIT64(n) (UINT64_C(1) << (n))
#define pdMS_TO_TICKS(n) (n)
#define ESP_RETURN_ON_ERROR(call, ...) do { esp_err_t result_ = (call); if (result_ != ESP_OK) return result_; } while (0)
#define ESP_RETURN_ON_FALSE(test, err, ...) do { if (!(test)) return (err); } while (0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)

typedef void *SemaphoreHandle_t;
typedef void *esp_lcd_panel_handle_t;
typedef void *esp_lcd_panel_io_handle_t;
typedef void *esp_ldo_channel_handle_t;
typedef void *tanmatsu_coprocessor_handle_t;
typedef int nvs_handle_t;
typedef int esp_lcd_dpi_panel_event_data_t;
typedef enum { LCD_COLOR_FMT_RGB565, LCD_COLOR_FMT_RGB888 } lcd_color_format_t;
typedef enum { BSP_DISPLAY_COLOR_FORMAT_16_565RGB, BSP_DISPLAY_COLOR_FORMAT_24_888RGB } bsp_display_color_format_t;
typedef enum { BSP_DISPLAY_ENDIAN_LITTLE } bsp_display_endianness_t;
typedef enum { BSP_DISPLAY_ROTATION_270 } bsp_display_rotation_t;
typedef enum { BSP_DISPLAY_TE_DISABLED, BSP_DISPLAY_TE_V_AND_H_BLANKING } bsp_display_te_mode_t;
typedef struct { bsp_display_color_format_t requested_color_format; int num_fbs; } bsp_display_configuration_t;
typedef struct { int reset_pin; bool use_24_bit_color; int num_fbs; } st7701_configuration_t;
typedef struct { int chan_id; int voltage_mv; } esp_ldo_channel_config_t;
typedef struct { uint64_t pin_bit_mask; int mode; bool pull_up_en; bool pull_down_en; int intr_type; } gpio_config_t;
typedef struct { bool (*on_color_trans_done)(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t *, void *); } esp_lcd_dpi_panel_event_callbacks_t;

#define BSP_DSI_LDO_CHAN 3
#define BSP_DSI_LDO_VOLTAGE_MV 2500
#define BSP_LCD_RESET_PIN 27
#define BSP_LCD_TE_PIN 28
#define GPIO_MODE_INPUT 0
#define GPIO_INTR_POSEDGE 1
#define NVS_READONLY 0
#define LCD_CMD_TEOFF 0x34
#define LCD_CMD_TEON 0x35

esp_err_t esp_ldo_acquire_channel(const esp_ldo_channel_config_t *, esp_ldo_channel_handle_t *);
esp_err_t st7701_initialize(const st7701_configuration_t *);
esp_lcd_panel_handle_t st7701_get_panel(void);
esp_lcd_panel_io_handle_t st7701_get_panel_io(void);
esp_err_t st7701_get_parameters(size_t *, size_t *, lcd_color_format_t *);
esp_err_t st7701_get_vcom(uint8_t *);
esp_err_t st7701_get_vcom_otp(uint8_t *);
esp_err_t st7701_set_vcom(uint8_t);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
int xSemaphoreGive(SemaphoreHandle_t);
int xSemaphoreGiveFromISR(SemaphoreHandle_t, void *);
int xSemaphoreTake(SemaphoreHandle_t, int);
esp_err_t esp_lcd_dpi_panel_register_event_callbacks(esp_lcd_panel_handle_t, const esp_lcd_dpi_panel_event_callbacks_t *, void *);
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t, int, const void *, size_t);
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t, int, int, int, int, const void *);
esp_err_t gpio_config(const gpio_config_t *);
esp_err_t gpio_isr_handler_add(int, void (*)(void *), void *);
esp_err_t nvs_open(const char *, int, nvs_handle_t *);
esp_err_t nvs_get_u8(nvs_handle_t, const char *, uint8_t *);
void nvs_close(nvs_handle_t);
esp_err_t bsp_tanmatsu_coprocessor_get_handle(tanmatsu_coprocessor_handle_t *);
esp_err_t tanmatsu_coprocessor_get_display_backlight(tanmatsu_coprocessor_handle_t, uint8_t *);
esp_err_t tanmatsu_coprocessor_set_display_backlight(tanmatsu_coprocessor_handle_t, uint8_t);

esp_err_t bsp_display_initialize(const bsp_display_configuration_t *);
esp_err_t bsp_display_get_parameters(size_t *, size_t *, bsp_display_color_format_t *, bsp_display_endianness_t *);
esp_err_t bsp_display_get_vcom_default(uint8_t *);

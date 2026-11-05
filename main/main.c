#include <stdio.h>
#include "data/services.h"
#include "bsp/device.h"
#include "bsp/input.h"
#include "driver/gpio.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "wifi_connection.h"
#include "wifi_settings.h"
#include "wifi_remote.h"
#include "display.h"
#include "ui/shell.h"

void adsb_maps_start(void);
bool adsb_maps_stop(void);
void adsb_maps_radio_initialized(void);
bool adsb_maps_diagnostic_mode(void);
bool adsb_maps_diagnostic_finished(void);
void adsb_maps_network_ready(bool connected);
void adsb_maps_sd_event(bool present);
static TaskHandle_t network_task;
static QueueHandle_t status_queue;
typedef struct { char text[160]; } status_t;

static void status(const char *text, esp_err_t err) {
    status_t s;
    snprintf(s.text, sizeof(s.text), "Wi-Fi: %s (%s)", text, esp_err_to_name(err));
    xQueueOverwrite(status_queue, &s);
}

static void network(void *arg) {
    status("initializing radio", ESP_OK);
    esp_err_t err = wifi_remote_initialize();
    if (err != ESP_OK) {
        status("radio unavailable; F1 opens exit choice", err);
        for (;;) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    err = wifi_connection_init_stack();
    if (err != ESP_OK) {
        status("stack unavailable; F1 opens exit choice", err);
        for (;;) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    // The shared SDMMC controller is now initialized even without a usable AP.
    adsb_maps_radio_initialized();
    if (adsb_maps_diagnostic_mode()) {
        status("offline SD diagnostic; Wi-Fi association disabled", ESP_OK);
        for (;;) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    // Avoid manager 0.3.0's unbounded await and stale connected bit.
    // All profile/connection operations belong to this worker; UI never waits for it.
    for (;;) {
        bool connected = false;
        for (int i = 0; i < WIFI_SETTINGS_MAX; i++) {
            wifi_settings_t profile;
            if (wifi_settings_get(i, &profile) != ESP_OK) continue;
            status("connecting to saved profile", ESP_OK);
            err = wifi_connection_connect(i, 1);
            if (err != ESP_OK) continue;
            for (int n = 0; n < 100; n++) {
                esp_netif_ip_info_t ip;
                wifi_ap_record_t ap;
                if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK &&
                    esp_netif_get_ip_info(wifi_get_netif(), &ip) == ESP_OK && ip.ip.addr) {
                    connected = true;
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
                if (ulTaskNotifyTake(pdTRUE, 0)) break;
            }
            if (connected) break;
        }
        adsb_maps_network_ready(connected);
        if (connected) {
            status("connected; reconnect in F6 Menu", ESP_OK);
            while (!ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000))) {
                wifi_ap_record_t ap;
                if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) break;
            }
        } else {
            status("offline/no usable profile; retry in F6 Menu", ESP_FAIL);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000));
        }
        adsb_maps_network_ready(false);
        wifi_connection_disconnect();
    }
}

static void application_main(void) {
    esp_err_t nvs = nvs_flash_init(); // Never erase shared settings on recovery errors.
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { bsp_device_restart_to_launcher(); return; }
    const bsp_configuration_t cfg = {.display = {
        .requested_color_format = BSP_DISPLAY_COLOR_FORMAT_16_565RGB, .num_fbs = 1}};
    if (bsp_device_initialize(&cfg) != ESP_OK || adsb_display_init() != ESP_OK) {
        bsp_device_restart_to_launcher(); return;
    }
    QueueHandle_t input;
    ESP_ERROR_CHECK(bsp_input_get_queue(&input));
    status_queue = xQueueCreate(1, sizeof(status_t));
    ESP_ERROR_CHECK(status_queue ? ESP_OK : ESP_ERR_NO_MEM);
    bool ready = nvs == ESP_OK && adsb_services_start();
    adsb_maps_start();
    adsb_ui_init(ready); // Settings loaded before the first shared view is constructed.
    if (!ready) {
        char error[160];
        snprintf(error, sizeof(error), "Services unavailable: %s; shared settings preserved", nvs==ESP_OK?"data service initialization failed":esp_err_to_name(nvs));
        adsb_ui_network(error);
    }
    if (nvs == ESP_OK && xTaskCreate(network, "adsb_wifi", 6144, NULL, 4, &network_task) != pdPASS)
        adsb_ui_network("Wi-Fi worker allocation failed");
    uint64_t timing_due=0,max_ui_us=0;
    ESP_LOGI("adsb_ui", "UI task: core %d, priority %u",xPortGetCoreID(),(unsigned)uxTaskPriorityGet(NULL));
    for (;;) {
        uint64_t ui_started=esp_timer_get_time();
        bsp_input_event_t event;
        // One consumer and bounded batches; continuous input cannot starve rendering.
        for (int n = 0; n < 32 && xQueueReceive(input, &event, 0) == pdTRUE; n++) {
            if (event.type == INPUT_EVENT_TYPE_ACTION && event.args_action.type == BSP_INPUT_ACTION_TYPE_SD_CARD)
                adsb_maps_sd_event(event.args_action.state);
            adsb_ui_input(&event);
        }
        status_t s;
        if (xQueueReceive(status_queue, &s, 0)) adsb_ui_network(s.text);
        if (adsb_ui_take_reconnect() && network_task) xTaskNotifyGive(network_task);
        if (adsb_ui_take_exit() || adsb_maps_diagnostic_finished()) {
            adsb_services_stop();
            if(!adsb_maps_stop())ESP_LOGW("adsb_atlas","SD shutdown incomplete after bounded wait; restarting to launcher");
            bsp_device_restart_to_launcher();
            return;
        }
        adsb_ui_tick();
        uint64_t ui_elapsed=esp_timer_get_time()-ui_started;
        if(ui_elapsed>max_ui_us)max_ui_us=ui_elapsed;
        if(ui_started>=timing_due) {
            timing_due=ui_started+5000000;
            ESP_LOGI("adsb_ui", "UI update: %.1f ms max",max_ui_us/1000.0);max_ui_us=0;
        }
        lv_timer_handler(); // Sole LVGL owner.
        // At 100 Hz a one-tick sleep may end at the very next tick. Reserve
        // a complete idle tick even while a full-map redraw is continuously due.
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// Keep the sole LVGL owner above HTTP/JSON workers and away from the radio's core.
// The IDF main task runs at priority 1 on core 0 and could be preempted throughout a feed parse.
static void application(void *arg) {
    (void)arg;application_main();vTaskDelete(NULL);
}
void app_main(void) {
    if(xTaskCreatePinnedToCore(application,"adsb_ui",12288,NULL,5,NULL,1)!=pdPASS)
        bsp_device_restart_to_launcher();
}

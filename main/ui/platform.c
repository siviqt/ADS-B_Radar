#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "wifi_connection.h"
#include "esp_flash.h"
#include "bsp/power.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
void adsb_health_read(adsb_health_t *out) {
    memset(out,0,sizeof(*out));out->battery_percent=-1;
    out->internal_free=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    out->internal_min=heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    out->largest_block=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    out->psram_free=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    out->tasks=uxTaskGetNumberOfTasks();out->stack_free=uxTaskGetStackHighWaterMark(NULL);
    uint32_t flash_size=0;if(esp_flash_get_size(NULL,&flash_size)==ESP_OK)out->flash_bytes=(unsigned)flash_size;
    wifi_ap_record_t ap;
    if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK) {out->connected=true;out->rssi=ap.rssi;}
    esp_netif_t *net=wifi_get_netif();esp_netif_ip_info_t ip;
    if(net && esp_netif_get_ip_info(net,&ip)==ESP_OK) snprintf(out->ip,sizeof(out->ip),IPSTR,IP2STR(&ip.ip));
    bsp_power_battery_information_t battery;
    if(bsp_power_get_battery_information(&battery)==ESP_OK && battery.battery_available) {
        out->battery_percent=(int)battery.remaining_percentage;out->battery_mv=battery.voltage;out->charging=battery.battery_charging;
    }
}

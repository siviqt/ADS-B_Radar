# ESP-IDF 6 enables this accelerator by default, but P4 ECO <3 has no ECDSA peripheral.
# Use software signature verification; the full CA bundle and hostname checks stay enabled.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    if(CONFIG_ESP32P4_SELECTS_REV_LESS_V3 AND CONFIG_MBEDTLS_HARDWARE_ECDSA_VERIFY)
        message(FATAL_ERROR "Pre-revision-3 P4 requires CONFIG_MBEDTLS_HARDWARE_ECDSA_VERIFY=n")
    endif()

    if(NOT CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY)
        message(FATAL_ERROR "HTTPS feeds require CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y")
    endif()

    if(NOT CONFIG_BOOTLOADER_RESERVE_RTC_MEM)
        message(FATAL_ERROR "AppFS launcher return requires reserved RTC memory")
    endif()

    if(NOT CONFIG_MIPI_DSI_ABSTRACTION_USE_VENDORED_ESP_HAL_LCD)
        message(FATAL_ERROR "Panel identification requires CONFIG_MIPI_DSI_ABSTRACTION_USE_VENDORED_ESP_HAL_LCD=y")
    endif()

endif()

idf_component_register(SRCS "main.c" "display.c" "nvs_guard.c" "data/services.cpp" "ui/shell.cpp" "ui/platform.c" "ui/lv_memory.c" "ui/maps.cpp"
    INCLUDE_DIRS "."
    REQUIRES badge-bsp mipi_dsi_abstraction lvgl esp_timer esp_driver_gpio esp_lcd nvs_flash wifi-manager tanmatsu-wifi esp_partition spi_flash esp_http_client mbedtls esp_netif lwip arduinojson)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    include("${CMAKE_CURRENT_LIST_DIR}/../compat/bsp_display.cmake")
endif()

# AppFS has no ordinary running APP partition; retain a strict NVS-only guard.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_partition_main_flash_region_safe"
    "-Wl,--undefined=lv_malloc_core")

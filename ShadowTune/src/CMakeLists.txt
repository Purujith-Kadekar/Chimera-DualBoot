# src/CMakeLists.txt — MP3 Player firmware component registration
#
# NOTE: mbedtls is used for dashboard PIN hashing (SHA-256).
# esp_tinyusb + tinyusb are used for USB Mass Storage (expose SD card
# as a USB flash drive when connected via USB OTG). They are vendored
# in components/ — no component manager needed.
# No BLE — the MP3 player doesn't type credentials.

file(GLOB APP_SOURCES ${CMAKE_CURRENT_LIST_DIR}/*.cpp ${CMAKE_CURRENT_LIST_DIR}/*.c)

idf_component_register(
    SRCS ${APP_SOURCES}
    INCLUDE_DIRS "../include"
    REQUIRES
        driver
        esp_driver_i2s
        nvs_flash
        esp_event
        esp_littlefs
        mbedtls
        esp_hw_support
        esp_wifi
        esp_netif
        esp_timer
        fatfs
        esp_driver_sdmmc
        helix_mp3
        esp_tinyusb
        tinyusb
)

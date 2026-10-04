# Component registration for src/ (PlatformIO dual-framework mode).
# app_update      -> esp_ota_ops.h (select next boot partition)
# esp_partition   -> find ota_0 / ota_1
# nvs_flash       -> Preferences (remember last selection)
file(GLOB APP_SOURCES ${CMAKE_CURRENT_LIST_DIR}/*.cpp)

idf_component_register(
    SRCS ${APP_SOURCES}
    INCLUDE_DIRS "../include"
    REQUIRES
        driver
        nvs_flash
        app_update
        esp_partition
        esp_system
        esp_timer
)

cmake_minimum_required(VERSION 3.16.0)

# MP3 Player firmware for EdgeHax S3-PRO (ESP32-S3 N16R8)
# Forked from SecureVault Password Manager

# Excludes esp_lcd — same workaround as SecureVault for GCC toolchain bug
set(EXCLUDE_COMPONENTS esp_lcd)

# Tell ESP-IDF to also look in the project's components/ directory
if(PROJECT_DIR)
    list(APPEND EXTRA_COMPONENT_DIRS "${PROJECT_DIR}/components")
    message(STATUS "EXTRA_COMPONENT_DIRS: using PROJECT_DIR = ${PROJECT_DIR}")
else()
    list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")
    message(STATUS "EXTRA_COMPONENT_DIRS: using CMAKE_CURRENT_LIST_DIR = ${CMAKE_CURRENT_LIST_DIR}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(MP3Player)

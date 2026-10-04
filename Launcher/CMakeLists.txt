cmake_minimum_required(VERSION 3.16.0)

# EdgeHax Launcher (dual-boot menu). Same esp_lcd exclusion as the other two
# firmwares (unused here, and avoids the Windows GCC crash they hit).
set(EXCLUDE_COMPONENTS esp_lcd)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(EdgeHaxLauncher)

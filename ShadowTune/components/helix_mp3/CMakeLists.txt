# helix_mp3 CMakeLists.txt — ESP-IDF component registration for the Helix MP3 decoder
# This component provides real MP3 decoding (not a stub). The implementation
# is the RealNetworks/Helix fixed-point MP3 decoder (RPSL/RCSL 1.0 licensed),
# the same decoder used by ESP8266Audio / ESP-ADF, vendored into src/.
# See src/LICENSE.txt, src/RPSL.txt, src/RCSL.txt for licensing terms.

file(GLOB HELIX_SOURCES ${CMAKE_CURRENT_LIST_DIR}/src/*.c ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp)

idf_component_register(
    SRCS ${HELIX_SOURCES}
    INCLUDE_DIRS "include"
)

# The vendored decoder selects its platform-specific fast-math/PROGMEM code
# path via `#if defined(ARDUINO)`. PlatformIO's dual-framework (arduino,
# espidf) build normally defines ARDUINO globally, but this component is
# compiled as a plain ESP-IDF component — force the define here so the
# correct (portable, ESP32-safe) branch in assembly.h is always selected
# regardless of global build-flag ordering.
target_compile_definitions(${COMPONENT_LIB} PRIVATE ARDUINO=10812)

# priv_include/pgmspace.h is a local shim so the vendored sources' plain
# `#include <pgmspace.h>` resolves when this component is built in ESP-IDF
# isolation (see priv_include/pgmspace.h for why it's needed at all).
#
# CRITICAL: this is added PRIVATE, not via INCLUDE_DIRS above. INCLUDE_DIRS
# in idf_component_register() is PUBLIC by default -- it's exported to every
# component/library that depends on helix_mp3, transitively. Putting the
# shim there previously leaked it into the Arduino-framework build (e.g.
# WebServer.cpp), where it shadowed the REAL Arduino-ESP32 pgmspace.h and
# broke compilation elsewhere (missing PGM_VOID_P/memccpy_P, which our
# minimal shim doesn't define -- it was only ever meant for this component's
# own PROGMEM-free ESP32 math tables). PRIVATE scopes it to just this
# component's own .c files, so it can never shadow anything else's headers.
target_include_directories(${COMPONENT_LIB} PRIVATE "priv_include")

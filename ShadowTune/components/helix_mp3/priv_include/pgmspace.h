#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  pgmspace.h — local shim for the vendored Helix MP3 decoder
// ═══════════════════════════════════════════════════════════════════════════════
//  The vendored decoder sources (dct32.c, dqchan.c, huffman.c, hufftabs.c,
//  imdct.c, mp3tabs.c, trigtabs.c) come from ESP8266Audio's libhelix-mp3,
//  which was written to also target AVR-based Arduino boards. On AVR,
//  PROGMEM/pgm_read_*() are required because flash and RAM are separate
//  address spaces. On ESP32 (Xtensa/RISC-V) there is no such split — flash
//  is memory-mapped and directly addressable — so these all collapse to
//  no-ops / plain dereferences.
//
//  Normally the Arduino-ESP32 core supplies this exact header, but this
//  component is compiled as a plain ESP-IDF component (idf_component_register
//  in components/helix_mp3/CMakeLists.txt), which does NOT have the Arduino
//  core's include path attached — hence "pgmspace.h: No such file or
//  directory". This local copy (found via this component's own "include"
//  dir, already on its include path) fixes that without depending on
//  build-order / global Arduino include paths at all.
// ═══════════════════════════════════════════════════════════════════════════════

#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef PGM_P
#define PGM_P const char*
#endif
#ifndef PSTR
#define PSTR(s) (s)
#endif

#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const unsigned char*)(const void*)(addr))
#endif
#ifndef pgm_read_word
#define pgm_read_word(addr) (*(const unsigned short*)(const void*)(addr))
#endif
#ifndef pgm_read_dword
#define pgm_read_dword(addr) (*(const unsigned long*)(const void*)(addr))
#endif
#ifndef pgm_read_float
#define pgm_read_float(addr) (*(const float*)(const void*)(addr))
#endif
#ifndef pgm_read_ptr
#define pgm_read_ptr(addr) (*(void* const*)(const void*)(addr))
#endif

// Signed variants some libraries expect
#ifndef pgm_read_byte_near
#define pgm_read_byte_near(addr) pgm_read_byte(addr)
#endif
#ifndef pgm_read_word_near
#define pgm_read_word_near(addr) pgm_read_word(addr)
#endif
#ifndef pgm_read_dword_near
#define pgm_read_dword_near(addr) pgm_read_dword(addr)
#endif

#ifndef memcpy_P
#include <string.h>
#define memcpy_P(dest, src, n) memcpy((dest), (src), (n))
#endif
#ifndef strcpy_P
#include <string.h>
#define strcpy_P(dest, src) strcpy((dest), (src))
#endif
#ifndef strlen_P
#include <string.h>
#define strlen_P(s) strlen(s)
#endif

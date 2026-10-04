#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  ui_theme.h — named color palette for the MP3 Player firmware
//  Forked from SecureVault — same 4 themes, C_TOTP_CHIP renamed to C_CHIP
// ═══════════════════════════════════════════════════════════════════════════════
#include <Arduino.h>

// ── ThemeDefaults — compile-time defaults (Air-Gapped theme) ────────────────
struct ThemeDefaults {
  static constexpr uint16_t C_BG        = 0x0000;
  static constexpr uint16_t C_PANEL     = 0x1082;
  static constexpr uint16_t C_HEADER    = 0x000B;
  static constexpr uint16_t C_ACCENT    = 0x07FF;
  static constexpr uint16_t C_GREEN     = 0x07E0;
  static constexpr uint16_t C_RED       = 0xF800;
  static constexpr uint16_t C_ORANGE    = 0xFB40;
  static constexpr uint16_t C_YELLOW    = 0xFFE0;
  static constexpr uint16_t C_WHITE     = 0xFFFF;
  static constexpr uint16_t C_GREY      = 0x7BEF;
  static constexpr uint16_t C_DARKGREY  = 0x39E7;
  static constexpr uint16_t C_BTN       = 0x2965;
  static constexpr uint16_t C_BTN_DARK  = 0x0320;
  static constexpr uint16_t C_CHIP      = 0x1800;   // small badge background (was C_TOTP_CHIP)
};

struct ThemeColors {
  uint16_t bg, panel, header, accent;
  uint16_t green, red, orange, yellow, white, grey, darkgrey, btn, btnDark, chip;
};

// ── Theme 1: Cyan-on-black "Air-Gapped" (the original) ────────────────
static const ThemeColors THEME_AIR_GAPPED = {
  ThemeDefaults::C_BG, ThemeDefaults::C_PANEL, ThemeDefaults::C_HEADER, ThemeDefaults::C_ACCENT,
  ThemeDefaults::C_GREEN, ThemeDefaults::C_RED, ThemeDefaults::C_ORANGE, ThemeDefaults::C_YELLOW,
  ThemeDefaults::C_WHITE, ThemeDefaults::C_GREY, ThemeDefaults::C_DARKGREY,
  ThemeDefaults::C_BTN, ThemeDefaults::C_BTN_DARK, ThemeDefaults::C_CHIP
};

// ── Theme 2: Pure Black & White (monochrome) ──────────────────────────
static const ThemeColors THEME_MONOCHROME = {
  0x0000,  // bg
  0x2104,  // panel
  0x1082,  // header
  0xFFFF,  // accent
  0xFFFF,  // green
  0x7BEF,  // red
  0x7BEF,  // orange
  0xFFFF,  // yellow
  0xFFFF,  // white
  0xC618,  // grey
  0x7BEF,  // darkgrey
  0x4208,  // btn
  0x7BEF,  // btnDark
  0x1082   // chip
};

// ── Theme 3: Emerald (green-on-black) ─────────────────────────────────
static const ThemeColors THEME_EMERALD = {
  0x0000,  // bg
  0x0120,  // panel
  0x00C0,  // header
  0x07E0,  // accent
  0x07E0,  // green
  0xF800,  // red
  0xFD20,  // orange
  0xFFE0,  // yellow
  0xFFFF,  // white
  0xB7E0,  // grey
  0x4B32,  // darkgrey
  0x0240,  // btn
  0x0120,  // btnDark
  0x0240   // chip
};

// ── Theme 4: Sunlight (white background, dark text) ───────────────────
static const ThemeColors THEME_SUNLIGHT = {
  0xFFFF,  // bg
  0xF7BE,  // panel
  0xB5B6,  // header
  0x021F,  // accent
  0x0380,  // green
  0xD000,  // red
  0xFC00,  // orange
  0xCC00,  // yellow
  0x0000,  // white (semantic: primary text = black in Sunlight)
  0x630C,  // grey
  0x9492,  // darkgrey
  0xB5B6,  // btn
  0x8410,  // btnDark
  0x8410   // chip
};

// ═══════════════════════════════════════════════════════════════════════════════
//  RUNTIME THEME COLOR VARIABLES
// ═══════════════════════════════════════════════════════════════════════════════
#ifdef THEME_COLORS_IMPLEMENTED
uint16_t C_BG        = ThemeDefaults::C_BG;
uint16_t C_PANEL     = ThemeDefaults::C_PANEL;
uint16_t C_HEADER    = ThemeDefaults::C_HEADER;
uint16_t C_ACCENT    = ThemeDefaults::C_ACCENT;
uint16_t C_GREEN     = ThemeDefaults::C_GREEN;
uint16_t C_RED       = ThemeDefaults::C_RED;
uint16_t C_ORANGE    = ThemeDefaults::C_ORANGE;
uint16_t C_YELLOW    = ThemeDefaults::C_YELLOW;
uint16_t C_WHITE     = ThemeDefaults::C_WHITE;
uint16_t C_GREY      = ThemeDefaults::C_GREY;
uint16_t C_DARKGREY  = ThemeDefaults::C_DARKGREY;
uint16_t C_BTN       = ThemeDefaults::C_BTN;
uint16_t C_BTN_DARK  = ThemeDefaults::C_BTN_DARK;
uint16_t C_CHIP      = ThemeDefaults::C_CHIP;
#else
extern uint16_t C_BG;
extern uint16_t C_PANEL;
extern uint16_t C_HEADER;
extern uint16_t C_ACCENT;
extern uint16_t C_GREEN;
extern uint16_t C_RED;
extern uint16_t C_ORANGE;
extern uint16_t C_YELLOW;
extern uint16_t C_WHITE;
extern uint16_t C_GREY;
extern uint16_t C_DARKGREY;
extern uint16_t C_BTN;
extern uint16_t C_BTN_DARK;
extern uint16_t C_CHIP;
#endif

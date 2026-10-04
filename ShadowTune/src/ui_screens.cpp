// ═══════════════════════════════════════════════════════════════════════════════
//  RUNTIME THEME COLORS — defined HERE, extern'd in ui_theme.h
//
//  CRITICAL: MUST define THEME_COLORS_IMPLEMENTED BEFORE the first #include
//  that transitively pulls in ui_theme.h. ui_screens.h → display_manager.h →
//  ui_theme.h is the first include chain, and ui_theme.h uses #pragma once.
//  If THEME_COLORS_IMPLEMENTED isn't set before that first include, the
//  #pragma once guard skips the second include (line 20), and all C_* vars
//  become extern declarations instead of definitions — causing linker errors.
// ═══════════════════════════════════════════════════════════════════════════════
#define THEME_COLORS_IMPLEMENTED
#include "ui_screens.h"
#include "qr_display.h"  // WiFi QR code on Hotspot info screen
#include <string.h>
#include <strings.h>      // strcasecmp
#include <stdlib.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <driver/gpio.h>
#include <Preferences.h>
#include "gpio_config_manager.h"   // PIN_LADDER_PIN
// Hotspot mode needs WiFi.softAPgetStationNum() on the info screen.
#include <WiFi.h>

static const char* NUM_LABELS[12] = { "1","2","3","4","5","6","7","8","9","CLR","0","OK" };
static const char* MODE_MENU_LABELS[4] = { "HOTSPOT", "USB DRIVE", "SETTINGS", "CANCEL" };

// ── Helper: check if filename has a music extension ────────────────────
static bool isMusicExt(const char* filename) {
  if (!filename) return false;
  const char* dot = strrchr(filename, '.');
  if (!dot) return false;
  return strcasecmp(dot, ".mp3")  == 0 || strcasecmp(dot, ".wav")  == 0 ||
         strcasecmp(dot, ".aac")  == 0 || strcasecmp(dot, ".ogg")  == 0 ||
         strcasecmp(dot, ".flac") == 0 || strcasecmp(dot, ".wma")  == 0 ||
         strcasecmp(dot, ".m4a")  == 0 || strcasecmp(dot, ".opus") == 0 ||
         strcasecmp(dot, ".mid")  == 0 || strcasecmp(dot, ".midi") == 0;
}

// ── Helper: strip music extension in-place for display ────────────────
// Strips the trailing extension (e.g. ".mp3", ".wav", ".flac", etc.)
// from a mutable char buffer. If no recognized music extension, does nothing.
static void stripMusicExt(char* buf) {
  if (!buf) return;
  int len = strlen(buf);
  if (len < 5) return;  // shortest possible: "a.mp3"
  const char* dot = strrchr(buf, '.');
  if (!dot) return;
  int extLen = len - (dot - buf);
  // Only strip known music extensions (1-5 chars after the dot)
  if (extLen >= 2 && extLen <= 5 && isMusicExt(buf)) {
    buf[dot - buf] = '\0';
  }
}

// ── Helper: word-prefix match ──────────────────────────────────────────
// Returns true if any WORD in 'haystack' starts with 'needle' (case-insensitive).
// Words are delimited by spaces, hyphens, underscores, or parentheses.
// e.g. needle="pin" matches "Pin", "pink floyd", "Pinball Wizard"
//      but NOT "pumpkin" or "spine" (pin is mid-word, not at word start)
static bool wordPrefixMatch(const char* haystack, const char* needle) {
  if (!haystack || !needle || needle[0] == '\0') return false;
  int nLen = strlen(needle);
  const char* p = haystack;
  // Check start of string (first word)
  if (strncasecmp(p, needle, nLen) == 0) return true;
  // Walk through string, check after each word boundary
  while (*p) {
    // Word boundary: space, hyphen, underscore, open paren, dot, bracket
    if (*p == ' ' || *p == '-' || *p == '_' || *p == '(' || *p == '.' || *p == '[') {
      p++;
      if (*p && strncasecmp(p, needle, nLen) == 0) return true;
    } else {
      p++;
    }
  }
  return false;
}

// ── Helper: print text with word-wrap within a bounded width ────────────
// Prints text character by character, wrapping to the next line when the
// current line is full. Returns the total height used (pixels).
// maxLines=0 means unlimited. If text exceeds maxLines, last line gets "…".
static int printWrapped(Adafruit_ILI9341& tft, const char* text,
                        int x, int y, int maxWidth, int lineH,
                        int maxLines = 0) {
  if (!text || text[0] == '\0') return 0;
  int charW = 6;  // textSize(1) = 6px per char
  int maxCharsPerLine = maxWidth / charW;
  if (maxCharsPerLine < 1) maxCharsPerLine = 1;

  int curX = x, curY = y, lineNum = 0;
  int textLen = strlen(text);
  int pos = 0;

  while (pos < textLen) {
    // Check line limit
    if (maxLines > 0 && lineNum >= maxLines - 1) {
      // Last allowed line — fill remaining and add "…" if more text
      int remaining = textLen - pos;
      int avail = maxCharsPerLine;
      if (remaining > avail) {
        // Truncate and add ellipsis
        int show = avail - 1;  // leave room for "…"
        if (show < 1) show = 1;
        for (int i = 0; i < show && pos < textLen; i++, pos++) {
          tft.setCursor(curX, curY); tft.print(text[pos]); curX += charW;
        }
        tft.setCursor(curX, curY); tft.print('~');  // ~ as ellipsis (font-safe)
      } else {
        // Fits on last line
        while (pos < textLen) {
          tft.setCursor(curX, curY); tft.print(text[pos]); curX += charW; pos++;
        }
      }
      return (lineNum + 1) * lineH;
    }

    // Find next word boundary (space) or end of string
    int charsThisLine = 0;
    while (pos < textLen && charsThisLine < maxCharsPerLine) {
      tft.setCursor(curX, curY); tft.print(text[pos]);
      curX += charW; pos++; charsThisLine++;
    }

    // If we stopped because of width limit and there's more text, wrap
    if (pos < textLen && charsThisLine >= maxCharsPerLine) {
      curX = x; curY += lineH; lineNum++;
    }
  }
  return (lineNum + 1) * lineH;
}

// ── Mode menu layout constants (same pattern as SecureVault) ────────────
static const int MENU_OPT_H = 30;
static const int MENU_GAP   = 4;
// ── Search keyboard key layout (adapted from SecureVault) ────────────
struct SearchKey {
  int x, y, w, h;
  const char* label;
  char ch;  // 0 = special key
};

static const SearchKey SEARCH_KEYS_LETTERS[] = {
  // Row 1: Q W E R T Y U I O P (10 keys, 30px each)
  {1,116,30,28,"Q",'q'},{33,116,30,28,"W",'w'},{65,116,30,28,"E",'e'},{97,116,30,28,"R",'r'},
  {129,116,30,28,"T",'t'},{161,116,30,28,"Y",'y'},{193,116,30,28,"U",'u'},{225,116,30,28,"I",'i'},
  {257,116,30,28,"O",'o'},{289,116,30,28,"P",'p'},
  // Row 2: A S D F G H J K L (9 keys, centered)
  {17,146,30,28,"A",'a'},{49,146,30,28,"S",'s'},{81,146,30,28,"D",'d'},{113,146,30,28,"F",'f'},
  {145,146,30,28,"G",'g'},{177,146,30,28,"H",'h'},{209,146,30,28,"J",'j'},{241,146,30,28,"K",'k'},
  {273,146,30,28,"L",'l'},
  // Row 3: Z X C V B N M ⌫(wide)
  {17,176,30,28,"Z",'z'},{49,176,30,28,"X",'x'},{81,176,30,28,"C",'c'},{113,176,30,28,"V",'v'},
  {145,176,30,28,"B",'b'},{177,176,30,28,"N",'n'},{209,176,30,28,"M",'m'},
  {241,176,78,28,"<x",0},
  // Row 4: 123(70) SEARCH(174) BACK(70)
  {1,206,70,28,"123",0},{73,206,174,28,"SEARCH",0},{249,206,70,28,"BACK",0}
};
static const int SEARCH_KEYS_LETTERS_COUNT = sizeof(SEARCH_KEYS_LETTERS)/sizeof(SearchKey);

static const SearchKey SEARCH_KEYS_NUMBERS[] = {
  // Row 1: 1 2 3 4 5 6 7 8 9 0
  {1,116,30,28,"1",'1'},{33,116,30,28,"2",'2'},{65,116,30,28,"3",'3'},{97,116,30,28,"4",'4'},
  {129,116,30,28,"5",'5'},{161,116,30,28,"6",'6'},{193,116,30,28,"7",'7'},{225,116,30,28,"8",'8'},
  {257,116,30,28,"9",'9'},{289,116,30,28,"0",'0'},
  // Row 2: ! @ # $ % ^ & * - _
  {1,146,30,28,"!",'!'},{33,146,30,28,"@",'@'},{65,146,30,28,"#",'#'},{97,146,30,28,"$",'$'},
  {129,146,30,28,"%",'%'}, {161,146,30,28,"^",'^'},{193,146,30,28,"&",'&'},{225,146,30,28,"*",'*'},
  {257,146,30,28,"-",'-'},{289,146,30,28,"_",'_'},
  // Row 3: . , / + = ? ' ⌫(wide)
  {17,176,30,28,".",'.'},{49,176,30,28,",",','},{81,176,30,28,"/",'/'},{113,176,30,28,"+",'+'},
  {145,176,30,28,"=",'='},{177,176,30,28,"?",'?'},{209,176,30,28,"'",'\''},
  {241,176,78,28,"<x",0},
  // Row 4: ABC(70) SEARCH(174) BACK(70)
  {1,206,70,28,"ABC",0},{73,206,174,28,"SEARCH",0},{249,206,70,28,"BACK",0}
};
static const int SEARCH_KEYS_NUMBERS_COUNT = sizeof(SEARCH_KEYS_NUMBERS)/sizeof(SearchKey);


UiController::UiController(DisplayManager& disp, Mp3Manager& mp3Mgr, Mp3Player& player,
                            RtcManager& rtc, MpuManager& mpu, ButtonManager& btn,
                            HotspotManager& hotspot, Ina219Manager& ina219,
                            UsbMscManager& usbMsc)
  : _disp(disp), _mp3Mgr(mp3Mgr), _player(player), _rtc(rtc), _mpu(mpu), _btn(btn),
    _hotspot(hotspot), _ina219(ina219), _usbMsc(usbMsc) {}

// ═══════════════════════════════════════════════════════════════════════════════
//  LIFECYCLE
// ═══════════════════════════════════════════════════════════════════════════════
void UiController::begin() {
  _lastActivity = millis();
  loadTheme();  // load saved theme from NVS before first screen draw

  // Check if this is the device's first boot (no PIN has ever been set).
  _isFirstBoot = _mp3Mgr.isFirstBoot();

  bool timeSane = _rtc.isOK() && _rtc.unixEpoch() >= SANE_EPOCH;
  if (_isFirstBoot) {
    // First boot: force PIN setup screen (skip playlist entirely).
    transitionTo(timeSane ? Screen::FIRST_BOOT_PIN : Screen::SETTIME);
  } else {
    // Normal boot: go directly to the playlist, then restore last state.
    transitionTo(timeSane ? Screen::PLAYLIST : Screen::SETTIME);
    // After the playlist screen is drawn, restore the last-opened folder
    // and last-played song so the device feels like it never turned off.
    if (timeSane) {
      restoreBootState();
      // Redraw the playlist with the restored state (folder + selected song)
      drawPlaylistScreen();
    }
  }
}

void UiController::transitionTo(Screen s) {
  bool animate = (_prevScreen != Screen::NONE);

  // Reset the inactivity timer on every screen transition.
  _lastActivity = millis();

  _prevScreen = _screen;
  _screen = s;
  _modeMenuOpen = false;
  _disp.cancelFlash();

  // Reset Now Playing incremental-draw state on any transition
  _npFirstDraw = true;

  if (animate) _disp.wipeTransition();

  switch (s) {
    case Screen::PLAYLIST:        drawPlaylistScreen();        break;
    case Screen::NOW_PLAYING:     drawNowPlayingScreen();      break;
    case Screen::HOTSPOT_INFO:    drawHotspotInfoScreen();     break;
    case Screen::HOTSPOT_CHANGE:  drawHotspotChangeScreen();   break;
    case Screen::HOTSPOT_INPUT:   drawHotspotInputScreen();    break;
    case Screen::USB_INFO:        drawUsbInfoScreen();         break;
    case Screen::CLOCK:           drawClockScreen();           break;
    case Screen::SEARCH:          drawSearchScreen();          break;
    case Screen::SETTINGS:        drawSettingsScreen();        break;
    case Screen::ABOUT:           drawAboutScreen();           break;
    case Screen::SETTIME:         drawSetTimeScreen();         break;
    case Screen::FIRST_BOOT_PIN:  drawFirstBootPinScreen();    break;
    default: break;
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  STATUS BAR
//  Same layout as SecureVault but with a music note icon instead of padlock.
//  Mode badge shows: "NORM" for NORMAL, "HOT" for HOTSPOT, "DASH" for DASHBOARD
// ═══════════════════════════════════════════════════════════════════════════════
void UiController::drawStatusBar() {
  auto& tft = _disp.tft();
  uint32_t epoch = _rtc.unixEpoch();

  // Time (left)
  tft.fillRect(4, 4, 48, 12, C_HEADER);
  if (epoch > 0) {
    uint32_t secOfDay = epoch % 86400UL;
    byte h = secOfDay / 3600, m = (secOfDay % 3600) / 60;
    byte im = (m + 30) % 60;
    byte ih = (h + 5 + (m + 30) / 60) % 24;
    char ts[9]; sprintf(ts, "%02d:%02d", ih, im);
    tft.setTextColor(C_ACCENT, C_HEADER); tft.setTextSize(1);
    tft.setCursor(4, 6); tft.print(ts);
  } else {
    tft.setTextColor(C_DARKGREY, C_HEADER); tft.setTextSize(1);
    tft.setCursor(4, 6); tft.print("--:--");
  }

  // Music note icon (center — replaces SecureVault's padlock)
  int noteCx = SCREEN_W / 2;
  int noteCy = 10;
  // Draw a simple music note: vertical stem + note head + flag
  tft.drawFastVLine(noteCx, noteCy - 5, 10, C_ACCENT);
  tft.fillCircle(noteCx - 2, noteCy + 5, 3, C_ACCENT);
  tft.drawFastHLine(noteCx, noteCy - 5, 5, C_ACCENT);
  tft.drawFastVLine(noteCx + 5, noteCy - 5, 4, C_ACCENT);

  // Mode badge (center-right)
  tft.fillRect(MODE_BADGE_X, MODE_BADGE_Y + 4, MODE_BADGE_W, 12, C_HEADER);
  tft.setTextColor(C_ACCENT, C_HEADER); tft.setTextSize(1);
  tft.setCursor(MODE_BADGE_X + 2, MODE_BADGE_Y + 6);
  switch (_mode) {
    case PlayerMode::NORMAL:    tft.print("NORM"); break;
    case PlayerMode::HOTSPOT:   tft.print("HOT");  break;
    case PlayerMode::USB_MSC:   tft.print("USB");  break;
  }

  // Battery icon + percentage
  drawBatteryIcon(BATT_ICON_X, BATT_ICON_Y, _batteryPercent);
  tft.setTextColor(C_WHITE, C_BG); tft.setTextSize(1);
  tft.setCursor(BATT_TEXT_X, BATT_TEXT_Y);
  char bbuf[5];
  sprintf(bbuf, "%d%%", _batteryPercent);
  tft.print(bbuf);

  // Time warning
  bool needsWarn = (epoch < SANE_EPOCH);
  if (needsWarn) {
    tft.fillRect(58, 4, 44, 12, C_RED);
    tft.setTextColor(C_WHITE, C_RED); tft.setTextSize(1);
    tft.setCursor(60, 6); tft.print("! TIME");
  } else {
    tft.fillRect(58, 4, 44, 12, C_BG);
  }
}

// ── Smartphone-style battery icon (same as SecureVault) ─────────────────
void UiController::drawBatteryIcon(int x, int y, uint8_t pct) {
  auto& tft = _disp.tft();
  int w = BATT_ICON_W;
  int h = BATT_ICON_H;
  int nibW = BATT_NIB_W;
  int nibH = BATT_NIB_H;

  tft.fillRect(x, y - 1, w + nibW + 1, h + 2, C_BG);

  int nibY = y + (h - nibH) / 2;
  tft.fillRect(x + w, nibY, nibW, nibH, C_WHITE);

  tft.drawRoundRect(x, y, w, h, 2, C_WHITE);

  int innerW = w - 2;
  int fillW = (pct * innerW) / 100;
  uint16_t fillColor = (pct <= 15) ? C_RED : ((pct <= 30) ? C_ORANGE : C_GREEN);

  if (fillW > 0) {
    tft.fillRect(x + 1, y + 1, fillW, h - 2, fillColor);
  }

  if (pct > 90 && fillW > 1) {
    tft.fillRect(x + 1 + fillW - 1, y + 1, 1, h - 2, C_ACCENT);
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  PIN DOT / NUMPAD / SHAKE / ERROR (same as SecureVault)
// ═══════════════════════════════════════════════════════════════════════════════
void UiController::drawPinDot(int idx, bool filled) {
  auto& tft = _disp.tft();
  uint16_t col = filled ? C_ACCENT : C_DARKGREY;
  tft.fillCircle(20 + idx * 36, 53, 6, col);
  tft.drawCircle(20 + idx * 36, 53, 6, C_GREY);
}

void UiController::drawNumpadBtn(int idx, bool flash) {
  auto& tft = _disp.tft();
  int r = idx / NUM_COLS, c = idx % NUM_COLS;
  int bx = NUM_X0 + c * (NUM_BW + NUM_GAP);
  int by = NUM_Y0 + r * (NUM_BH + NUM_GAP);
  uint16_t col = flash ? C_ACCENT : ((idx == 9) ? C_RED : (idx == 11) ? C_BTN_DARK : C_BTN);
  tft.fillRoundRect(bx, by, NUM_BW, NUM_BH, 4, col);
  if (!flash) {
    tft.drawRoundRect(bx, by, NUM_BW, NUM_BH, 4, C_ACCENT);
    tft.setTextColor(C_WHITE); tft.setTextSize(2);
    int lw = strlen(NUM_LABELS[idx]) * 12;
    tft.setCursor(bx + (NUM_BW - lw) / 2, by + (NUM_BH - 16) / 2);
    tft.print(NUM_LABELS[idx]);
  }
}

void UiController::drawFirstBootPinDot(int idx, bool filled) {
  auto& tft = _disp.tft();
  int dotSpacing = 24;
  int startX = SCREEN_W / 2 - (MAX_PIN_LEN * dotSpacing) / 2;
  uint16_t col = filled ? C_ACCENT : C_DARKGREY;
  tft.fillCircle(startX + idx * dotSpacing, 56, 5, col);
  tft.drawCircle(startX + idx * dotSpacing, 56, 5, C_GREY);
}

void UiController::triggerShake() {
  _shaking = true;
  _shakeStart = millis();
}

void UiController::drawPinDotsShaking() {
  unsigned long elapsed = millis() - _shakeStart;
  if (elapsed > 400) {
    _shaking = false;
    for (int i = 0; i < MAX_PIN_LEN; i++) drawPinDot(i, i < _pinLen);
    return;
  }
  int offset = (int)(8.0f * sin(elapsed * 0.05f));
  auto& tft = _disp.tft();
  for (int i = 0; i < MAX_PIN_LEN; i++) {
    bool filled = i < _pinLen;
    uint16_t col = filled ? C_ACCENT : C_DARKGREY;
    tft.fillCircle(20 + i * 36 + offset, 53, 6, col);
    tft.drawCircle(20 + i * 36 + offset, 53, 6, C_GREY);
  }
}

void UiController::showLockError(const char* msg) {
  auto& tft = _disp.tft();
  tft.fillRect(0, 230, SCREEN_W, 10, C_BG);
  tft.setTextColor(C_RED, C_BG); tft.setTextSize(1);
  tft.setCursor(SCREEN_W / 2 - strlen(msg) * 3, 232);
  tft.print(msg);
  _lockErrorTime = millis();
}

void UiController::clearLockError() {
  if (_lockErrorTime && millis() - _lockErrorTime > 2000) {
    _disp.tft().fillRect(0, 230, SCREEN_W, 10, C_BG);
    _lockErrorTime = 0;
  }
}

// ── Color blending + glow helpers (ported from SecureVault) ──────────────
uint16_t UiController::lerp565(uint16_t c1, uint16_t c2, float t) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  uint8_t r1 = (c1 >> 11) & 0x1F;
  uint8_t g1 = (c1 >> 5) & 0x3F;
  uint8_t b1 = c1 & 0x1F;
  uint8_t r2 = (c2 >> 11) & 0x1F;
  uint8_t g2 = (c2 >> 5) & 0x3F;
  uint8_t b2 = c2 & 0x1F;
  uint8_t r = r1 + (uint8_t)((r2 - r1) * t);
  uint8_t g = g1 + (uint8_t)((g2 - g1) * t);
  uint8_t b = b1 + (uint8_t)((b2 - b1) * t);
  return (r << 11) | (g << 5) | b;
}

void UiController::drawBreathingHalo(int cx, int cy, int maxR, uint16_t color) {
  auto& tft = _disp.tft();
  for (int i = 8; i >= 1; i--) {
    float t = (float)i / 8.0f;
    uint16_t c = lerp565(C_BG, color, t * t * 0.6f);
    int r = (int)(maxR * t);
    tft.fillCircle(cx, cy, r, c);
  }
}

// ── Padlock glyph (kept for status bar compatibility, but not used
//    as the primary icon — the music note replaces it) ────────────────────
void UiController::drawPadlockGlyph(int x, int y, int size, uint16_t color) {
  auto& tft = _disp.tft();
  int bodyW = size;
  int bodyH = size * 3 / 4;
  int bodyY = y + size / 3;
  int shackleR = bodyW / 3;
  int shackleCx = x + bodyW / 2;
  int shackleCy = bodyY;
  tft.drawCircle(shackleCx, shackleCy, shackleR, color);
  tft.drawCircleHelper(shackleCx, shackleCy, shackleR, 0b0001, color);
  tft.fillRect(shackleCx - shackleR, shackleCy, shackleR * 2, shackleR, C_BG);
  tft.fillRoundRect(x, bodyY, bodyW, bodyH, 3, color);
  tft.fillCircle(shackleCx, bodyY + bodyH / 3, 2, C_BG);
  tft.fillRect(shackleCx - 1, bodyY + bodyH / 3, 2, bodyH / 3, C_BG);
}

void UiController::drawStatusBarPadlock(int cx, int cy, bool closed) {
  auto& tft = _disp.tft();
  tft.fillRoundRect(cx - 4, cy - 1, 8, 6, 1, C_ACCENT);
  if (closed) {
    tft.drawCircle(cx, cy - 1, 3, C_ACCENT);
    tft.fillRect(cx - 3, cy - 1, 6, 3, C_BG);
    tft.fillRoundRect(cx - 4, cy - 1, 8, 6, 1, C_ACCENT);
  } else {
    tft.drawCircle(cx + 1, cy - 2, 3, C_ACCENT);
    tft.fillRect(cx - 2, cy - 2, 4, 3, C_BG);
    tft.fillRoundRect(cx - 4, cy - 1, 8, 6, 1, C_ACCENT);
  }
  tft.fillCircle(cx, cy + 2, 1, C_BG);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  VOLUME PERSISTENCE
//  setVolumePersisted() applies the volume AND marks it dirty so the next
//  tick() will flush it to NVS (after an 800ms debounce). This avoids
//  hammering NVS during volume-hold (1% per 200ms) while still persisting
//  within ~1s of the user releasing the button.
// ═══════════════════════════════════════════════════════════════════════════════
void UiController::setVolumePersisted(uint8_t vol) {
  _player.setVolume(vol);
  _lastVolumeChange = millis();
  _volumeDirty = true;
}

void UiController::checkVolumeSave() {
  if (_volumeDirty && (millis() - _lastVolumeChange) > 800) {
    _mp3Mgr.saveVolume(_player.getVolume());
    _volumeDirty = false;
    Serial.printf("[Vol] Saved to NVS: %u%%\n", _player.getVolume());
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  CLOCK / LOCK SCREEN — compact layout with playback controls
//  Layout (when music active):
//    y=0-20:    Status bar
//    y=24-64:   Large HH:MM (textSize 5)
//    y=68-78:   Track name (textSize 1)
//    y=82-88:   Progress bar (6px, scrubbable)
//    y=90-100:  Time: current (left) — total (right)
//    y=105-131: [<<PREV] [PLAY/PAUSE] [NEXT>>] (compact, 26px tall)
//    y=136-158: [-] [VOL: 70%] [+] (compact, 22px tall)
//  Any tap NOT on a control returns to NOW_PLAYING (or PLAYLIST if idle).
// ═══════════════════════════════════════════════════════════════════════════════

// Compact clock-screen control geometry
static const int CLK_BTN_Y    = 130;
static const int CLK_BTN_H    = 26;
static const int CLK_BTN_W    = 80;
static const int CLK_VOL_Y    = 162;
static const int CLK_VOL_H    = 22;
static const int CLK_BAR_Y    = 104;
static const int CLK_BAR_H    = 6;
static const int CLK_BAR_X    = 16;
static const int CLK_BAR_W    = SCREEN_W - 32;

void UiController::drawClockScreen() {
  auto& tft = _disp.tft();
  tft.fillRect(0, 0, SCREEN_W, SCREEN_H, C_BG);
  drawStatusBar();

  // ── Large HH:MM time at TOP (textSize 6 = 36px/char) ──────────────
  uint32_t epoch = _rtc.unixEpoch();
  if (epoch >= SANE_EPOCH) {
    uint32_t secOfDay = epoch % 86400UL;
    byte h = secOfDay / 3600, m = (secOfDay % 3600) / 60;
    byte im = (m + 30) % 60;
    byte ih = (h + 5 + (m + 30) / 60) % 24;
    char ts[6];
    snprintf(ts, sizeof(ts), "%02d:%02d", ih, im);
    tft.setTextColor(C_WHITE); tft.setTextSize(6);  // 6x = 36x48 per char
    int tw = strlen(ts) * 36;
    tft.setCursor((SCREEN_W - tw) / 2, 30);
    tft.print(ts);
  } else {
    tft.setTextColor(C_DARKGREY); tft.setTextSize(2);
    tft.setCursor(SCREEN_W / 2 - 60, 50);
    tft.print("Clock not set");
  }

  // ── Now-playing overlay at BOTTOM (only if actively playing/paused) ──
  PlaybackState st = _player.state();
  if (st == PlaybackState::PLAYING || st == PlaybackState::PAUSED) {
    // Track name (small, left-aligned) — use _playingName so it works
    // even when the song was played from search (where _selectedEntry
    // may still point to a folder in the playlist).
    if (_playingName[0] != '\0') {
      char displayName[MAX_FILENAME_LEN];
      strncpy(displayName, _playingName, MAX_FILENAME_LEN - 1);
      displayName[MAX_FILENAME_LEN - 1] = 0;
      stripMusicExt(displayName);
      tft.setTextColor(C_ACCENT); tft.setTextSize(1);
      tft.setCursor(8, 90);
      char clip[54] = {0};
      strncpy(clip, displayName, 53);
      tft.print(clip);
    }

    // Progress bar (compact, scrubbable)
    int barY = 104, barH = 6, barX = 16, barW = SCREEN_W - 32;
    tft.fillRect(barX, barY, barW, barH, C_DARKGREY);
    tft.drawRect(barX, barY, barW, barH, C_GREY);
    uint8_t pct = _player.progressPercent();
    int fillW = (pct * barW) / 100;
    if (fillW > 0) tft.fillRect(barX, barY, fillW, barH, C_ACCENT);

    // Time: current (left) — total (right)
    uint32_t samps = _player.samplesDecoded();
    uint32_t totSamps = _player.totalSamples();
    int sr = _player.audioInfo().sampleRate; if (sr <= 0) sr = 44100;
    int ch = _player.audioInfo().channels; if (ch <= 0) ch = 2;
    uint32_t curSec = samps / (uint32_t)(sr * ch);
    uint32_t totSec = totSamps / (uint32_t)(sr * ch);
    char curStr[8], totStr[8];
    snprintf(curStr, sizeof(curStr), "%lu:%02lu", (unsigned long)(curSec/60), (unsigned long)(curSec%60));
    snprintf(totStr, sizeof(totStr), "%lu:%02lu", (unsigned long)(totSec/60), (unsigned long)(totSec%60));
    tft.setTextColor(C_WHITE); tft.setTextSize(1);
    tft.setCursor(barX, barY + barH + 2); tft.print(curStr);
    int totW = strlen(totStr) * 6;
    tft.setCursor(barX + barW - totW, barY + barH + 2); tft.print(totStr);

    // Compact playback controls at bottom
    int btnY = 130, btnH = 26, btnW = 80;
    _disp.drawBtn(8, btnY, btnW, btnH, "<<PREV", C_BTN, 1, C_WHITE);
    const char* playLabel = (st == PlaybackState::PLAYING) ? "PAUSE" : "PLAY";
    _disp.drawBtn(SCREEN_W/2-btnW/2, btnY, btnW, btnH, playLabel, C_ACCENT, 1, C_BG);
    _disp.drawBtn(SCREEN_W-btnW-8, btnY, btnW, btnH, "NEXT>>", C_BTN, 1, C_WHITE);

    // Compact volume at very bottom
    int volY = 162, volH = 22;
    _disp.drawBtn(8, volY, 40, volH, "-", C_BTN, 1, C_WHITE);
    tft.setTextColor(C_WHITE); tft.setTextSize(1);
    char volStr[12];
    snprintf(volStr, sizeof(volStr), "VOL: %d%%", _player.getVolume());
    int volW = strlen(volStr) * 6;
    tft.setCursor((SCREEN_W - volW) / 2, volY + 5); tft.print(volStr);
    _disp.drawBtn(SCREEN_W - 48, volY, 40, volH, "+", C_BTN, 1, C_WHITE);

    _lastClockProgress = pct;
  } else {
    // No track playing — show "tap to wake" hint at center-bottom
    tft.setTextColor(C_DARKGREY); tft.setTextSize(1);
    const char* hint = "Tap to wake";
    int hw = strlen(hint) * 6;
    tft.setCursor((SCREEN_W - hw) / 2, 160);
    tft.print(hint);
  }
}

void UiController::updateClockScreen() {
  auto& tft = _disp.tft();
  uint32_t epoch = _rtc.unixEpoch();

  // Only update the TIME digits (partial — no full screen redraw)
  static unsigned long lastSecDraw = 0;
  static char lastTs[6] = {0};
  unsigned long nowSec = epoch;
  if (nowSec != lastSecDraw) {
    lastSecDraw = nowSec;
    if (epoch >= SANE_EPOCH) {
      uint32_t secOfDay = epoch % 86400UL;
      byte h = secOfDay / 3600, m = (secOfDay % 3600) / 60;
      byte im = (m + 30) % 60;
      byte ih = (h + 5 + (m + 30) / 60) % 24;
      char ts[6];
      snprintf(ts, sizeof(ts), "%02d:%02d", ih, im);
      if (strcmp(ts, lastTs) != 0) {
        // Only redraw time if it changed (saves flicker)
        strcpy(lastTs, ts);
        // Clear just the time area (y=24 to y=72, full width)
        tft.fillRect(0, 24, SCREEN_W, 48, C_BG);
        tft.setTextColor(C_WHITE); tft.setTextSize(6);
        int tw = strlen(ts) * 36;
        tft.setCursor((SCREEN_W - tw) / 2, 30);
        tft.print(ts);
      }
    }
  }

  // Update progress bar + time text only if playing (partial update)
  if (_player.state() == PlaybackState::PLAYING) {
    uint8_t pct = _player.progressPercent();
    if (pct != _lastClockProgress) {
      _lastClockProgress = pct;
      // Redraw just the progress bar + time (y=90 to y=120)
      int barY = 104, barH = 6, barX = 16, barW = SCREEN_W - 32;
      tft.fillRect(barX, barY, barW, barH, C_DARKGREY);
      tft.drawRect(barX, barY, barW, barH, C_GREY);
      int fillW = (pct * barW) / 100;
      if (fillW > 0) tft.fillRect(barX, barY, fillW, barH, C_ACCENT);
      // Redraw time text
      uint32_t samps = _player.samplesDecoded();
      uint32_t totSamps = _player.totalSamples();
      int sr = _player.audioInfo().sampleRate; if (sr <= 0) sr = 44100;
      int ch = _player.audioInfo().channels; if (ch <= 0) ch = 2;
      uint32_t curSec = samps / (uint32_t)(sr * ch);
      uint32_t totSec = totSamps / (uint32_t)(sr * ch);
      char curStr[8], totStr[8];
      snprintf(curStr, sizeof(curStr), "%lu:%02lu", (unsigned long)(curSec/60), (unsigned long)(curSec%60));
      snprintf(totStr, sizeof(totStr), "%lu:%02lu", (unsigned long)(totSec/60), (unsigned long)(totSec%60));
      tft.fillRect(0, barY + barH + 2, SCREEN_W, 12, C_BG);
      tft.setTextColor(C_WHITE); tft.setTextSize(1);
      tft.setCursor(barX, barY + barH + 2); tft.print(curStr);
      int totW = strlen(totStr) * 6;
      tft.setCursor(barX + barW - totW, barY + barH + 2); tft.print(totStr);
    }
  }
}

void UiController::handleClockTouch(int tx, int ty) {
  _lastActivity = millis();
  PlaybackState st = _player.state();
  bool musicActive = (st == PlaybackState::PLAYING || st == PlaybackState::PAUSED);

  // If music is active, check for compact control button taps first
  if (musicActive) {
    // Progress bar scrubbing
    if (ty >= CLK_BAR_Y - 4 && ty <= CLK_BAR_Y + CLK_BAR_H + 4 &&
        tx >= CLK_BAR_X && tx <= CLK_BAR_X + CLK_BAR_W) {
      int pct = ((tx - CLK_BAR_X) * 100) / CLK_BAR_W;
      if (pct < 0) pct = 0; if (pct > 100) pct = 100;
      _player.seek((uint32_t)((uint64_t)pct * _player.totalBytes() / 100));
      drawClockScreen();
      return;
    }
    // [<<PREV]
    if (hitTest(tx, ty, 8, CLK_BTN_Y, CLK_BTN_W, CLK_BTN_H)) {
      int prev = findAdjacentPlayable(_selectedEntry, -1);
      if (prev >= 0) {
        _selectedEntry = prev; _player.stop();
        const Mp3Entry* pe = _mp3Mgr.entryAt(_sortedIndex[_selectedEntry]);
        if (pe) { strncpy(_playingName, pe->name, MAX_FILENAME_LEN-1); _playingName[MAX_FILENAME_LEN-1]='\0'; _playingSize=pe->fileSize; }
        char path[MAX_FILENAME_LEN + 6];
        _mp3Mgr.getFullPath(_selectedEntry, path, sizeof(path));
        _player.play(path); saveBootState();
      }
      drawClockScreen();
      return;
    }
    // [PLAY/PAUSE]
    if (hitTest(tx, ty, SCREEN_W/2-CLK_BTN_W/2, CLK_BTN_Y, CLK_BTN_W, CLK_BTN_H)) {
      if (st == PlaybackState::PLAYING) _player.pause();
      else if (st == PlaybackState::PAUSED) _player.resume();
      drawClockScreen();
      return;
    }
    // [NEXT>>]
    if (hitTest(tx, ty, SCREEN_W-CLK_BTN_W-8, CLK_BTN_Y, CLK_BTN_W, CLK_BTN_H)) {
      int next = findAdjacentPlayable(_selectedEntry, +1);
      if (next >= 0) {
        _selectedEntry = next; _player.stop();
        const Mp3Entry* pe = _mp3Mgr.entryAt(_sortedIndex[_selectedEntry]);
        if (pe) { strncpy(_playingName, pe->name, MAX_FILENAME_LEN-1); _playingName[MAX_FILENAME_LEN-1]='\0'; _playingSize=pe->fileSize; }
        char path[MAX_FILENAME_LEN + 6];
        _mp3Mgr.getFullPath(_selectedEntry, path, sizeof(path));
        _player.play(path); saveBootState();
      }
      drawClockScreen();
      return;
    }
    // [-] volume
    if (hitTest(tx, ty, 8, CLK_VOL_Y, 40, CLK_VOL_H)) {
      uint8_t vol = _player.getVolume();
      if (vol > 0) setVolumePersisted(vol - 1);
      drawClockScreen();
      return;
    }
    // [+] volume
    if (hitTest(tx, ty, SCREEN_W - 48, CLK_VOL_Y, 40, CLK_VOL_H)) {
      uint8_t vol = _player.getVolume();
      if (vol < 100) setVolumePersisted(vol + 1);
      drawClockScreen();
      return;
    }
  }

  // Tap anywhere else → go to NOW_PLAYING (if music active) or PLAYLIST (if stopped)
  if (musicActive) {
    transitionTo(Screen::NOW_PLAYING);
  } else {
    transitionTo(Screen::PLAYLIST);
  }
}

void UiController::handleClockButtons() {
  if (!_btn.pressed()) return;
  BtnEvent e = _btn.state();
  _lastActivity = millis();
  PlaybackState st = _player.state();
  bool musicActive = (st == PlaybackState::PLAYING || st == PlaybackState::PAUSED);

  if (e == BtnEvent::UP) {
    // UP = next track (if music active)
    if (musicActive) {
      int next = findAdjacentPlayable(_selectedEntry, +1);
      if (next >= 0) {
        _selectedEntry = next; _player.stop();
        const Mp3Entry* pe = _mp3Mgr.entryAt(_sortedIndex[_selectedEntry]);
        if (pe) { strncpy(_playingName, pe->name, MAX_FILENAME_LEN-1); _playingName[MAX_FILENAME_LEN-1]='\0'; _playingSize=pe->fileSize; }
        char path[MAX_FILENAME_LEN + 6];
        _mp3Mgr.getFullPath(_selectedEntry, path, sizeof(path));
        _player.play(path); saveBootState();
        drawClockScreen();
      }
    }
  } else if (e == BtnEvent::DOWN) {
    // DOWN = previous track (if music active)
    if (musicActive) {
      int prev = findAdjacentPlayable(_selectedEntry, -1);
      if (prev >= 0) {
        _selectedEntry = prev; _player.stop();
        const Mp3Entry* pe = _mp3Mgr.entryAt(_sortedIndex[_selectedEntry]);
        if (pe) { strncpy(_playingName, pe->name, MAX_FILENAME_LEN-1); _playingName[MAX_FILENAME_LEN-1]='\0'; _playingSize=pe->fileSize; }
        char path[MAX_FILENAME_LEN + 6];
        _mp3Mgr.getFullPath(_selectedEntry, path, sizeof(path));
        _player.play(path); saveBootState();
        drawClockScreen();
      }
    }
  } else if (e == BtnEvent::LEFT) {
    // LEFT = wake to playlist
    transitionTo(Screen::PLAYLIST);
  } else if (e == BtnEvent::RIGHT || e == BtnEvent::OK) {
    // RIGHT/TOUCH = play/pause toggle (if music) or wake (if idle)
    if (musicActive) {
      if (st == PlaybackState::PLAYING) _player.pause();
      else if (st == PlaybackState::PAUSED) _player.resume();
      drawClockScreen();
    } else {
      transitionTo(Screen::PLAYLIST);
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  PLAYLIST SCREEN (replaces VAULT)
//  Same layout as vault list: status bar, header with file count + UP/DOWN,
//  scrollable list of MP3 files. Each row shows filename (.mp3 stripped)
//  and file size.
// ═══════════════════════════════════════════════════════════════════════════════
void UiController::buildSortedIndex() {
  // Mp3Manager already sorts its own listing (folders first, then
  // alphabetical/case-insensitive) every time it scans a folder -- see
  // Mp3Manager::buildSortedIndex(), called from begin()/rescan()/
  // enterFolder()/goUp(). So _mp3Mgr.entryAt(i) / getFullPath(i) already
  // return entries in the correct display order for i in [0, count).
  //
  // This function used to ALSO independently re-sort the same data ("kept
  // for historical reasons"), and its comparator called _mp3Mgr.entryAt()
  // on RAW pre-sort indices while the sort was still in progress -- which
  // Mp3Manager::entryAt() itself remaps through ITS OWN sorted-index table
  // internally. Every call site elsewhere in this file that then did
  // `_mp3Mgr.entryAt(_sortedIndex[_selectedEntry])` or
  // `_mp3Mgr.getFullPath(_sortedIndex[_selectedEntry], ...)` was applying
  // that same remap a SECOND time on top of an already-resolved index --
  // landing on an essentially unrelated entry whenever a folder's sort
  // order wasn't already the identity permutation (i.e. almost always).
  // That's what was behind tracks/names not matching what's shown/played.
  //
  // Fix: _sortedIndex is now just the identity mapping. Kept (rather than
  // removed, and rather than rewriting every one of those call sites) to
  // minimize the size of this change -- every existing `_sortedIndex[i]`
  // expression elsewhere in this file now safely evaluates to `i`.
  _sortedCount = 0;
  for (int i = 0; i < _mp3Mgr.count() && _sortedCount < MAX_DISPLAY; i++) {
    _sortedIndex[_sortedCount] = _sortedCount;
    _sortedCount++;
  }
}

// findAdjacentPlayable() — used by every NEXT/PREV (and UP/DOWN-as-track-skip)
// handler so they never land on a folder. Without this, "next track" simply
// did _selectedEntry++ with no regard for what was actually at that index --
// if the next entry in sorted order was a folder, it got treated exactly
// like a track: stop(), then play(<folder path>). SD.open() on a directory
// "succeeds" (it opens a valid File handle), but reading audio data from it
// returns nothing, so playback state flips to PLAYING with no real audio --
// that's the "folders shown as playing" symptom.
int UiController::findAdjacentPlayable(int fromIndex, int direction) const {
  int i = fromIndex + direction;
  while (i >= 0 && i < _sortedCount) {
    const Mp3Entry* e = _mp3Mgr.entryAt(i);
    if (e && !e->isFolder) return i;
    i += direction;
  }
  return -1;  // no playable track further in that direction
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Play All / Shuffle — play sequence management
// ═══════════════════════════════════════════════════════════════════════════════

void UiController::buildPlaySequence() {
  // Populate _playSeq[] with indices of all playable (music) entries
  // in the current folder, skipping folders and non-music files.
  _playSeqCount = 0;
  for (int i = 0; i < _sortedCount; i++) {
    const Mp3Entry* e = _mp3Mgr.entryAt(i);
    if (e && !e->isFolder && isMusicExt(e->name)) {
      _playSeq[_playSeqCount++] = i;
    }
  }
  // Record which folder this sequence was built for
  strncpy(_playSeqFolder, _mp3Mgr.currentRelativePath(),
          sizeof(_playSeqFolder) - 1);
  _playSeqFolder[sizeof(_playSeqFolder) - 1] = '\0';
  // Apply shuffle if enabled
  if (_shuffleOn) shufflePlaySequence();
}

void UiController::shufflePlaySequence() {
  // Fisher-Yates (Knuth) shuffle using ESP32 hardware RNG
  for (int i = _playSeqCount - 1; i > 0; i--) {
    int j = esp_random() % (i + 1);
    int tmp = _playSeq[i];
    _playSeq[i] = _playSeq[j];
    _playSeq[j] = tmp;
  }
}

void UiController::loadFolderShuffleState() {
  // Read per-folder shuffle toggle from NVS
  _shuffleOn = _mp3Mgr.getShuffleForFolder(_mp3Mgr.currentRelativePath());
  // Rebuild the play sequence for this folder
  buildPlaySequence();
}

void UiController::playAllFromCurrent() {
  // Ensure the play sequence is up-to-date for the current folder
  buildPlaySequence();
  if (_playSeqCount == 0) return;  // no music files
  _playAllActive = true;
  _playSeqPos = 0;
  // Play the first song in the sequence
  int entryIdx = _playSeq[0];
  _selectedEntry = entryIdx;
  const Mp3Entry* e = _mp3Mgr.entryAt(entryIdx);
  if (e) {
    strncpy(_playingName, e->name, MAX_FILENAME_LEN - 1);
    _playingName[MAX_FILENAME_LEN - 1] = '\0';
    _playingSize = e->fileSize;
  }
  _player.stop();
  char path[MAX_FILENAME_LEN + 6];
  _mp3Mgr.getFullPath(entryIdx, path, sizeof(path));
  _player.play(path);
  saveBootState();
  transitionTo(Screen::NOW_PLAYING);
}

void UiController::advanceToNextTrack() {
  if (!_playAllActive) return;
  _playSeqPos++;
  if (_playSeqPos >= _playSeqCount) {
    // End of sequence — stop play-all
    _playAllActive = false;
    _playSeqPos = -1;
    return;
  }
  int entryIdx = _playSeq[_playSeqPos];
  _selectedEntry = entryIdx;
  const Mp3Entry* e = _mp3Mgr.entryAt(entryIdx);
  if (e) {
    strncpy(_playingName, e->name, MAX_FILENAME_LEN - 1);
    _playingName[MAX_FILENAME_LEN - 1] = '\0';
    _playingSize = e->fileSize;
  }
  char path[MAX_FILENAME_LEN + 6];
  _mp3Mgr.getFullPath(entryIdx, path, sizeof(path));
  _player.play(path);
  saveBootState();
  // Refresh NOW_PLAYING screen if we're on it
  if (_screen == Screen::NOW_PLAYING) {
    _npFirstDraw = true;
    drawNowPlayingScreen();
  }
}

void UiController::skipTrack(int direction) {
  // Prev/Next that respects play-all sequence
  if (_playAllActive && _playSeqCount > 0 && _playSeqPos >= 0) {
    int newPos = _playSeqPos + direction;
    if (newPos < 0 || newPos >= _playSeqCount) return;  // at boundary
    _playSeqPos = newPos;
    int entryIdx = _playSeq[_playSeqPos];
    _selectedEntry = entryIdx;
    const Mp3Entry* e = _mp3Mgr.entryAt(entryIdx);
    if (e) {
      strncpy(_playingName, e->name, MAX_FILENAME_LEN - 1);
      _playingName[MAX_FILENAME_LEN - 1] = '\0';
      _playingSize = e->fileSize;
    }
    _player.stop();
    char path[MAX_FILENAME_LEN + 6];
    _mp3Mgr.getFullPath(entryIdx, path, sizeof(path));
    _player.play(path);
    saveBootState();
    if (_screen == Screen::NOW_PLAYING) {
      _npFirstDraw = true;
      drawNowPlayingScreen();
    }
  } else {
    // Standard behavior: find next/prev playable in folder
    int adj = findAdjacentPlayable(_selectedEntry, direction);
    if (adj < 0) return;
    _selectedEntry = adj;
    const Mp3Entry* pe = _mp3Mgr.entryAt(_sortedIndex[_selectedEntry]);
    if (pe) { strncpy(_playingName, pe->name, MAX_FILENAME_LEN-1); _playingName[MAX_FILENAME_LEN-1]='\0'; _playingSize=pe->fileSize; }
    _player.stop();
    char path[MAX_FILENAME_LEN + 6];
    _mp3Mgr.getFullPath(_selectedEntry, path, sizeof(path));
    _player.play(path);
    saveBootState();
    if (_screen == Screen::NOW_PLAYING) {
      _npFirstDraw = true;
      drawNowPlayingScreen();
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Boot state persistence — save/restore last folder, song, and play-all
// ═══════════════════════════════════════════════════════════════════════════════
void UiController::saveBootState() {
  // Persist the current folder path, currently-playing song name, and
  // play-all state to NVS so they can be restored on next boot.
  _mp3Mgr.saveLastFolder(_mp3Mgr.currentRelativePath());
  if (_playingName[0] != '\0') {
    _mp3Mgr.saveLastSong(_playingName);
  }
  _mp3Mgr.saveLastPlayAll(_playAllActive);
}

void UiController::restoreBootState() {
  // Read the last-played state from NVS and restore it:
  // 1. Navigate to the last-opened folder
  // 2. Find and select the last-played song
  // 3. Restore play-all state if it was active
  char lastFolder[MAX_FILENAME_LEN * MAX_FOLDER_DEPTH] = {0};
  char lastSong[MAX_FILENAME_LEN] = {0};

  _mp3Mgr.loadLastFolder(lastFolder, sizeof(lastFolder));
  _mp3Mgr.loadLastSong(lastSong, sizeof(lastSong));

  // Step 1: Navigate to the saved folder
  if (lastFolder[0] != '\0') {
    if (!_mp3Mgr.navigateToFolder(lastFolder)) {
      // Folder no longer exists (SD card changed?) — stay at root
      Serial.println("[UiController] restoreBootState: folder not found, staying at root");
    }
  }

  // Rebuild sorted index for the (possibly navigated) folder
  buildSortedIndex();

  // Step 2: Find and select the last-played song in the current folder
  if (lastSong[0] != '\0') {
    for (int i = 0; i < _sortedCount; i++) {
      const Mp3Entry* e = _mp3Mgr.entryAt(i);
      if (e && !e->isFolder && strcmp(e->name, lastSong) == 0) {
        _selectedEntry = i;
        // Set scroll so the selected entry is visible
        if (_selectedEntry < _listScroll) _listScroll = _selectedEntry;
        if (_selectedEntry >= _listScroll + LIST_VISIBLE)
          _listScroll = _selectedEntry - LIST_VISIBLE + 1;
        // Also set _playingName/_playingSize so NOW_PLAYING shows it
        strncpy(_playingName, e->name, MAX_FILENAME_LEN - 1);
        _playingName[MAX_FILENAME_LEN - 1] = '\0';
        _playingSize = e->fileSize;
        break;
      }
    }
  }

  // Step 3: Load shuffle state and build play sequence for this folder
  loadFolderShuffleState();

  // Step 4: Restore play-all state
  if (_mp3Mgr.loadLastPlayAll() && _playSeqCount > 0 && _playingName[0] != '\0') {
    _playAllActive = true;
    // Find current position in the play sequence
    for (int i = 0; i < _playSeqCount; i++) {
      const Mp3Entry* e = _mp3Mgr.entryAt(_playSeq[i]);
      if (e && strcmp(e->name, _playingName) == 0) {
        _playSeqPos = i;
        break;
      }
    }
  }

  Serial.printf("[UiController] restoreBootState: folder='%s' song='%s' playAll=%d\n",
                lastFolder, lastSong, _playAllActive);
}

void UiController::drawListRow(int r) {
  auto& tft = _disp.tft();
  int pos = _listScroll + r;
  int y = LIST_Y0 + r * LIST_ITEM_H;
  if (pos >= _sortedCount) {
    tft.fillRect(0, y, SCREEN_W, LIST_ITEM_H, C_BG);
    return;
  }
  int idx = _sortedIndex[pos];
  bool sel = (pos == _selectedEntry);
  uint16_t rc = sel ? C_BTN : (r % 2 ? C_PANEL : C_BG);
  const Mp3Entry* e = _mp3Mgr.entryAt(idx);
  if (!e) { tft.fillRect(0, y, SCREEN_W, LIST_ITEM_H, C_BG); return; }
  tft.fillRect(0, y, SCREEN_W, LIST_ITEM_H - 2, rc);
  if (sel) tft.drawRect(0, y, SCREEN_W, LIST_ITEM_H - 2, C_ACCENT);
  int avX = 14, avY = y + 7, avR = 11;
  if (e->isFolder) {
    tft.fillRect(avX - 8, avY + 2, 20, 14, C_ACCENT);
    tft.fillRect(avX - 8, avY, 10, 4, C_ACCENT);
    tft.setTextColor(C_BG, C_ACCENT); tft.setTextSize(1);
    tft.setCursor(avX - 4, avY + 5); tft.print("DIR");
  } else {
    static const uint16_t AV_COLS[6] = { 0x4D9F, 0x4B32, 0xB380, 0xFB40, 0x37C8, 0xF800 };
    uint16_t avCol = (_themeId == 1) ? C_DARKGREY : AV_COLS[((toupper(e->name[0]) - 'A') % 6 + 6) % 6];
    tft.fillCircle(avX, avY + avR, avR, avCol);
    tft.setTextColor(C_WHITE, avCol); tft.setTextSize(1);
    tft.setCursor(avX - 3, avY + avR - 4); tft.print(toupper(e->name[0]));
  }
  char displayName[MAX_FILENAME_LEN];
  strncpy(displayName, e->name, MAX_FILENAME_LEN - 1);
  displayName[MAX_FILENAME_LEN - 1] = 0;
  if (!e->isFolder) {
    stripMusicExt(displayName);
  } else {
    int len = strlen(displayName);
    if (len < MAX_FILENAME_LEN - 2) { displayName[len] = '/'; displayName[len + 1] = 0; }
  }

  // ── Size/folder label (compute FIRST to know name width) ──────────────
  char sizeStr[16] = {0};
  const char* rightLabel = nullptr;
  if (e->isFolder) {
    rightLabel = "<folder>";
  } else {
    if (e->fileSize >= 1048576) snprintf(sizeStr, sizeof(sizeStr), "%.1f MB", (float)e->fileSize / 1048576.0f);
    else snprintf(sizeStr, sizeof(sizeStr), "%lu KB", (unsigned long)(e->fileSize / 1024));
    rightLabel = sizeStr;
  }
  int rightLabelW = strlen(rightLabel) * 6;  // textSize(1) = 6px/char
  // Reserve: 8px right margin + label width + 4px gap before name ends
  int nameMaxW = SCREEN_W - 8 - rightLabelW - 4 - 34;  // 34 = left start x

  // ── Print name with word-wrap (up to 2 lines, 8px line height) ───────
  tft.setTextColor(C_WHITE, rc); tft.setTextSize(1);
  int nameH = printWrapped(tft, displayName, 34, y + 6, nameMaxW, 8, 2);

  // ── Print size/folder label on right, vertically centered ─────────────
  tft.setTextColor(C_GREY, rc); tft.setTextSize(1);
  // Vertically center the size label within the item
  int sizeY = y + (LIST_ITEM_H - 8) / 2;  // 8px = text height
  tft.setCursor(SCREEN_W - 8 - rightLabelW, sizeY); tft.print(rightLabel);

  // ── Bottom row: Track N or Tap to open (only if name used 1 line) ────
  if (nameH <= 8) {
    tft.setTextColor(C_DARKGREY, rc); tft.setCursor(34, y + 20);
    if (e->isFolder) tft.print("Tap to open");
    else { tft.print("Track "); tft.print(pos + 1); }
  }
}

void UiController::redrawList() {
  for (int r = 0; r < LIST_VISIBLE; r++) drawListRow(r);
}

// Helper: total visible rows including the ".." up-row if present.
static int totalListRows(const Mp3Manager& m) {
  return m.count();
}

void UiController::refreshListing() {
  _mp3Mgr.rescan();
  _selectedEntry = 0;
  _listScroll = 0;
  buildSortedIndex();
  redrawList();
}

void UiController::drawPlaylistScreen() {
  buildSortedIndex();
  // Load shuffle state for this folder if not already loaded
  if (strcmp(_playSeqFolder, _mp3Mgr.currentRelativePath()) != 0) {
    loadFolderShuffleState();
  }
  auto& tft = _disp.tft();
  tft.fillRect(0, 0, SCREEN_W, SCREEN_H, C_BG);
  drawStatusBar();
  tft.fillRect(0, SBAR_H, SCREEN_W, 24, C_HEADER);
  // BACK button always visible: at root → CLOCK screen, in folder → go up
  _disp.drawBtn(4, SBAR_H + 1, 60, 22, "BACK", C_BTN_DARK, 1, C_WHITE);
  _disp.drawBtn(68, SBAR_H + 1, 54, 22, "SEARCH", C_BTN);  // search button
  // PLAY ALL button — accent color when active
  {
    uint16_t col = _playAllActive ? C_ACCENT : C_BTN;
    uint16_t tcol = _playAllActive ? C_BG : C_WHITE;
    _disp.drawBtn(PLAYALL_BTN_X, PLAYALL_BTN_Y, PLAYALL_BTN_W, PLAYALL_BTN_H, "PLAY>", col, 1, tcol);
  }
  // SHUFFLE button — accent when on, shows checkmark
  {
    uint16_t col = _shuffleOn ? C_ACCENT : C_BTN;
    uint16_t tcol = _shuffleOn ? C_BG : C_WHITE;
    const char* label = _shuffleOn ? "SHUF*" : "SHUF";
    _disp.drawBtn(SHUFFLE_BTN_X, SHUFFLE_BTN_Y, SHUFFLE_BTN_W, SHUFFLE_BTN_H, label, col, 1, tcol);
  }
  _disp.drawBtn(228, SBAR_H + 1, 42, 22, " UP ", C_BTN);
  _disp.drawBtn(274, SBAR_H + 1, 42, 22, "DOWN", C_BTN);
  int total = totalListRows(_mp3Mgr);
  if (total == 0) {
    tft.setTextColor(C_GREY); tft.setTextSize(1);
    tft.setCursor(SCREEN_W / 2 - 48, 100);
    tft.print("This folder is empty");
    tft.setCursor(SCREEN_W / 2 - 72, 120);
    if (_mp3Mgr.atRoot()) tft.print("Use Hotspot or Dashboard to upload");
    else tft.print("Press BACK to go back");
    return;
  }
  redrawList();
}

void UiController::handlePlaylistTouch(int tx, int ty) {
  // BACK button: at root → CLOCK screen; in folder → go up one level
  if (hitTest(tx, ty, 4, SBAR_H, 60, 24)) {
    if (_mp3Mgr.atRoot()) {
      transitionTo(Screen::CLOCK);
    } else {
      _mp3Mgr.goUp(); _selectedEntry = 0; _listScroll = 0;
      _playAllActive = false; _playSeqPos = -1;  // cancel play-all on folder change
      buildSortedIndex(); loadFolderShuffleState(); drawPlaylistScreen();
      saveBootState();
    }
    return;
  }
  if (hitTest(tx, ty, 68, SBAR_H, 54, 24)) {
    _searchQueryLen = 0; _searchQuery[0] = '\0'; _searchResultCount = 0;
    _searchSelIdx = 0; _searchKeyboardMode = false; _searchKeyFlash = -1;
    // At root → search ALL folders; inside a folder → search current folder only
    _searchFromRoot = _mp3Mgr.atRoot();
    transitionTo(Screen::SEARCH); return;
  }
  // PLAY ALL button
  if (hitTest(tx, ty, PLAYALL_BTN_X, PLAYALL_BTN_Y, PLAYALL_BTN_W, PLAYALL_BTN_H)) {
    playAllFromCurrent(); return;
  }
  // SHUFFLE toggle
  if (hitTest(tx, ty, SHUFFLE_BTN_X, SHUFFLE_BTN_Y, SHUFFLE_BTN_W, SHUFFLE_BTN_H)) {
    _shuffleOn = !_shuffleOn;
    _mp3Mgr.setShuffleForFolder(_mp3Mgr.currentRelativePath(), _shuffleOn);
    // Rebuild play sequence with new shuffle state
    buildPlaySequence();
    // Redraw just the header buttons
    drawPlaylistScreen();
    saveBootState();
    return;
  }
  if (hitTest(tx, ty, 228, SBAR_H, 42, 24)) {
    if (_listScroll > 0) { _listScroll--; redrawList(); } return;
  }
  if (hitTest(tx, ty, 274, SBAR_H, 42, 24)) {
    int total = totalListRows(_mp3Mgr);
    if (_listScroll + LIST_VISIBLE < total) { _listScroll++; redrawList(); } return;
  }
  if (ty >= LIST_Y0) {
    int row = (ty - LIST_Y0) / LIST_ITEM_H;
    if (row < 0 || row >= LIST_VISIBLE) return;
    int pos = _listScroll + row;
    if (pos < 0 || pos >= _sortedCount) return;
    const Mp3Entry* e = _mp3Mgr.entryAt(pos);
    if (!e) return;
    if (e->isFolder) {
      _mp3Mgr.enterFolder(pos); _selectedEntry = 0; _listScroll = 0;
      _playAllActive = false; _playSeqPos = -1;  // cancel play-all on folder change
      buildSortedIndex(); loadFolderShuffleState(); drawPlaylistScreen();
      saveBootState(); return;
    }
    // Only play music files — never folders, documents, photos, etc.
    if (!isMusicExt(e->name)) return;
    // Tap on a music file → stop + play immediately (single tap, no double-tap needed)
    // Single-tap overrides play-all mode
    _playAllActive = false; _playSeqPos = -1;
    _selectedEntry = pos;
    // Store the actual song name+size for the NOW_PLAYING screen
    strncpy(_playingName, e->name, MAX_FILENAME_LEN - 1);
    _playingName[MAX_FILENAME_LEN - 1] = '\0';
    _playingSize = e->fileSize;
    _player.stop();
    char path[MAX_FILENAME_LEN + 6];
    _mp3Mgr.getFullPath(_sortedIndex[_selectedEntry], path, sizeof(path));
    _player.play(path);
    saveBootState();
    transitionTo(Screen::NOW_PLAYING);
  }
}

void UiController::handlePlaylistButtons() {
  if (!_btn.pressed()) return;
  BtnEvent e = _btn.state();
  if (e == BtnEvent::UP) {
    if (_selectedEntry > 0) {
      _selectedEntry--;
      if (_selectedEntry < _listScroll) _listScroll = _selectedEntry;
      redrawList();
    }
  } else if (e == BtnEvent::DOWN) {
    if (_selectedEntry < _sortedCount - 1) {
      _selectedEntry++;
      if (_selectedEntry >= _listScroll + LIST_VISIBLE)
        _listScroll = _selectedEntry - LIST_VISIBLE + 1;
      redrawList();
    }
  } else if (e == BtnEvent::LEFT) {
    if (_mp3Mgr.atRoot()) {
      transitionTo(Screen::CLOCK);
    } else {
      _mp3Mgr.goUp(); _selectedEntry = 0; _listScroll = 0;
      _playAllActive = false; _playSeqPos = -1;
      buildSortedIndex(); loadFolderShuffleState(); drawPlaylistScreen();
      saveBootState();
    }
  } else if (e == BtnEvent::RIGHT || e == BtnEvent::OK) {
    if (_selectedEntry >= 0 && _selectedEntry < _sortedCount) {
      const Mp3Entry* sel = _mp3Mgr.entryAt(_selectedEntry);
      if (sel) {
        if (sel->isFolder) {
          // OK on a folder → enter it
          _mp3Mgr.enterFolder(_selectedEntry);
          _selectedEntry = 0; _listScroll = 0;
          _playAllActive = false; _playSeqPos = -1;
          buildSortedIndex(); loadFolderShuffleState(); drawPlaylistScreen();
          saveBootState();
        } else if (isMusicExt(sel->name)) {
          // OK on a music file → stop + play from beginning
          // Only music files can be played — never folders, docs, photos, etc.
          // Store the actual song name+size for the NOW_PLAYING screen
          strncpy(_playingName, sel->name, MAX_FILENAME_LEN - 1);
          _playingName[MAX_FILENAME_LEN - 1] = '\0';
          _playingSize = sel->fileSize;
          _player.stop();
          char path[MAX_FILENAME_LEN + 6];
          _mp3Mgr.getFullPath(_sortedIndex[_selectedEntry], path, sizeof(path));
          _player.play(path);
          saveBootState();
          transitionTo(Screen::NOW_PLAYING);
        }
      }
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  NOW PLAYING SCREEN
//  Layout:
//    y=0-20:   Status bar
//    y=24-40:  Track name (centered, .mp3 stripped)
//    y=42-52:  File size
//    y=58-72:  Playback state indicator
//    y=80-88:  Progress bar background
//    y=NP_PROGRESS_Y: Progress bar (8px tall)
//    y=140:    Time display: current / total
//    y=NP_BTN_Y: Playback controls: [<<PREV] [PLAY/PAUSE] [NEXT>>]
//    y=NP_VOL_Y: Volume controls: [-] volume% [+]
// ═══════════════════════════════════════════════════════════════════════════════
void UiController::drawNowPlayingScreen() {
  auto& tft = _disp.tft();
  tft.fillRect(0, 0, SCREEN_W, SCREEN_H, C_BG);
  drawStatusBar();
  // BACK button on its OWN row (y=21-43)
  _disp.drawBtn(4, SBAR_H + 1, 60, 22, "BACK", C_BTN_DARK, 1, C_WHITE);
  // Use _playingName (set whenever _player.play() is called) instead of
  // looking up from the playlist via _selectedEntry. This ensures the
  // correct song name is shown even when the song was played from search,
  // where _selectedEntry may still point to a folder in the playlist.
  if (_playingName[0] == '\0') {
    tft.setTextColor(C_GREY); tft.setTextSize(1);
    tft.setCursor(SCREEN_W / 2 - 36, 100); tft.print("No track selected"); return;
  }
  // Track name on the NEXT row (y=48), no overlap with BACK button
  char displayName[MAX_FILENAME_LEN];
  strncpy(displayName, _playingName, MAX_FILENAME_LEN - 1);
  displayName[MAX_FILENAME_LEN - 1] = 0;
  stripMusicExt(displayName);
  // Track name — textSize 2 (12px/char), clip to fit screen
  tft.setTextColor(C_ACCENT); tft.setTextSize(2);
  tft.setCursor(8, 48);
  char clipName[27] = {0};
  strncpy(clipName, displayName, 26);
  tft.print(clipName);
  // File size (below track name)
  tft.setTextColor(C_GREY); tft.setTextSize(1);
  char sizeStr[32];
  if (_playingSize >= 1048576) snprintf(sizeStr, sizeof(sizeStr), "%.1f MB", (float)_playingSize / 1048576.0f);
  else snprintf(sizeStr, sizeof(sizeStr), "%lu KB", (unsigned long)(_playingSize / 1024));
  tft.setCursor(8, 72); tft.print(sizeStr);
  // Progress bar
  int barX = 16, barW = SCREEN_W - 32;
  tft.fillRect(barX, NP_PROGRESS_Y, barW, NP_PROGRESS_H, C_DARKGREY);
  tft.drawRect(barX, NP_PROGRESS_Y, barW, NP_PROGRESS_H, C_GREY);
  uint8_t pct = _player.progressPercent();
  int fillW = (pct * barW) / 100;
  if (fillW > 0) tft.fillRect(barX, NP_PROGRESS_Y, fillW, NP_PROGRESS_H, C_ACCENT);
  // Time: current (left) — total (right)
  uint32_t samps = _player.samplesDecoded();
  uint32_t totSamps = _player.totalSamples();
  int sr = _player.audioInfo().sampleRate; if (sr <= 0) sr = 44100;
  int ch = _player.audioInfo().channels; if (ch <= 0) ch = 2;
  uint32_t curSec = samps / (uint32_t)(sr * ch);
  uint32_t totSec = totSamps / (uint32_t)(sr * ch);
  char curStr[16], totStr[16];
  snprintf(curStr, sizeof(curStr), "%lu:%02lu", (unsigned long)(curSec/60), (unsigned long)(curSec%60));
  snprintf(totStr, sizeof(totStr), "%lu:%02lu", (unsigned long)(totSec/60), (unsigned long)(totSec%60));
  tft.setTextColor(C_WHITE); tft.setTextSize(1);
  tft.setCursor(barX, NP_PROGRESS_Y + NP_PROGRESS_H + 4); tft.print(curStr);
  int totW = strlen(totStr) * 6;
  tft.setCursor(barX + barW - totW, NP_PROGRESS_Y + NP_PROGRESS_H + 4); tft.print(totStr);
  // Playback controls — PLAY/PAUSE text uses C_BG for contrast (monochrome fix)
  int btnY = NP_BTN_Y, btnH = NP_BTN_H, btnW = NP_BTN_W;
  _disp.drawBtn(8, btnY, btnW, btnH, "<<PREV", C_BTN, 1, C_WHITE);
  const char* playLabel = (_player.state() == PlaybackState::PLAYING) ? "PAUSE" : "PLAY";
  _disp.drawBtn(SCREEN_W / 2 - btnW / 2, btnY, btnW, btnH, playLabel, C_ACCENT, 1, C_BG);
  _disp.drawBtn(SCREEN_W - btnW - 8, btnY, btnW, btnH, "NEXT>>", C_BTN, 1, C_WHITE);
  // Volume
  int volY = NP_VOL_Y;
  _disp.drawBtn(8, volY, 50, 30, "-", C_BTN, 2, C_WHITE);
  tft.setTextColor(C_WHITE); tft.setTextSize(2);
  char volStr[8]; snprintf(volStr, sizeof(volStr), "%d%%", _player.getVolume());
  int volW = strlen(volStr) * 12;
  tft.setCursor((SCREEN_W - volW) / 2, volY + 5); tft.print(volStr);
  _disp.drawBtn(SCREEN_W - 58, volY, 50, 30, "+", C_BTN, 2, C_WHITE);
  _lastProgress = pct; _lastState = _player.state(); _lastVolume = _player.getVolume(); _npFirstDraw = false;
}

void UiController::updateNowPlayingScreen() {
  if (_npFirstDraw) {
    drawNowPlayingScreen();
    return;
  }

  auto& tft = _disp.tft();
  uint8_t curProgress = _player.progressPercent();
  uint8_t curVolume = _player.getVolume();
  PlaybackState curState = _player.state();

  // ── Update progress bar ────────────────────────────────────────────
  if (curProgress != _lastProgress) {
    int barX = 16;
    int barW = SCREEN_W - 32;
    tft.fillRect(barX, NP_PROGRESS_Y, barW, NP_PROGRESS_H, C_DARKGREY);
    tft.drawRect(barX, NP_PROGRESS_Y, barW, NP_PROGRESS_H, C_GREY);
    int fillW = (curProgress * barW) / 100;
    if (fillW > 0) {
      tft.fillRect(barX, NP_PROGRESS_Y, fillW, NP_PROGRESS_H, C_ACCENT);
    }
    _lastProgress = curProgress;

    // Update time display
    uint32_t curBytes = _player.currentBytePos();
    uint32_t totBytes = _player.totalBytes();
    if (totBytes == 0 && _playingSize > 0) totBytes = _playingSize;
    if (totBytes == 0) totBytes = 1;
    uint32_t curSec = curBytes / 16000;
    uint32_t totSec = totBytes / 16000;
    char timeStr[32];
    snprintf(timeStr, sizeof(timeStr), "%lu:%02lu / %lu:%02lu",
             (unsigned long)(curSec / 60), (unsigned long)(curSec % 60),
             (unsigned long)(totSec / 60), (unsigned long)(totSec % 60));
    tft.fillRect(0, NP_PROGRESS_Y + NP_PROGRESS_H + 2, SCREEN_W, 12, C_BG);
    tft.setTextColor(C_WHITE); tft.setTextSize(1);
    int timeW = strlen(timeStr) * 6;
    tft.setCursor((SCREEN_W - timeW) / 2, NP_PROGRESS_Y + NP_PROGRESS_H + 4);
    tft.print(timeStr);
  }

  // ── Update PLAY/PAUSE button label only (no duplicate state text) ──
  if (curState != _lastState) {
    int btnW = NP_BTN_W;
    const char* playLabel = (curState == PlaybackState::PLAYING) ? "PAUSE" : "PLAY";
    _disp.drawBtn(SCREEN_W / 2 - btnW / 2, NP_BTN_Y, btnW, NP_BTN_H, playLabel, C_ACCENT, 1, C_BG);
    _lastState = curState;
  }

  // ── Update volume display ──────────────────────────────────────────
  if (curVolume != _lastVolume) {
    // Redraw volume percentage
    tft.fillRect(66, NP_VOL_Y, SCREEN_W - 132, 30, C_BG);
    tft.setTextColor(C_WHITE); tft.setTextSize(2);
    char volStr[8];
    snprintf(volStr, sizeof(volStr), "%d%%", curVolume);
    int volW = strlen(volStr) * 12;
    tft.setCursor((SCREEN_W - volW) / 2, NP_VOL_Y + 5);
    tft.print(volStr);
    _lastVolume = curVolume;
  }
}

// ── Now Playing touch handler ──────────────────────────────────────────
void UiController::handleNowPlayingTouch(int tx, int ty) {
  int btnY = NP_BTN_Y, btnH = NP_BTN_H, btnW = NP_BTN_W, volY = NP_VOL_Y;
  if (hitTest(tx, ty, 4, SBAR_H, 60, 24)) { transitionTo(Screen::PLAYLIST); return; }
  int barX = 16, barW = SCREEN_W - 32;
  if (ty >= NP_PROGRESS_Y - 4 && ty <= NP_PROGRESS_Y + NP_PROGRESS_H + 4 && tx >= barX && tx <= barX + barW) {
    int pct = ((tx - barX) * 100) / barW; if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    _player.seek((uint32_t)((uint64_t)pct * _player.totalBytes() / 100));
    _npFirstDraw = true; drawNowPlayingScreen(); return;
  }
  if (hitTest(tx, ty, 8, btnY, btnW, btnH)) {
    skipTrack(-1); return;
  }
  if (hitTest(tx, ty, SCREEN_W / 2 - btnW / 2, btnY, btnW, btnH)) {
    if (_player.state() == PlaybackState::STOPPED) {
      if (_selectedEntry >= 0 && _selectedEntry < _sortedCount) {
        const Mp3Entry* pe = _mp3Mgr.entryAt(_sortedIndex[_selectedEntry]);
        if (pe) { strncpy(_playingName, pe->name, MAX_FILENAME_LEN-1); _playingName[MAX_FILENAME_LEN-1]='\0'; _playingSize=pe->fileSize; }
        char path[MAX_FILENAME_LEN + 6];
        _mp3Mgr.getFullPath(_selectedEntry, path, sizeof(path));
        _player.play(path);
        saveBootState();
      }
    } else if (_player.state() == PlaybackState::PLAYING) _player.pause();
    else if (_player.state() == PlaybackState::PAUSED) _player.resume();
    return;
  }
  if (hitTest(tx, ty, SCREEN_W - btnW - 8, btnY, btnW, btnH)) {
    skipTrack(+1); return;
  }
  if (hitTest(tx, ty, 8, volY, 50, 30)) {
    uint8_t vol = _player.getVolume(); if (vol >= 5) setVolumePersisted(vol - 5); else setVolumePersisted(0);
    return;
  }
  if (hitTest(tx, ty, SCREEN_W - 58, volY, 50, 30)) {
    uint8_t vol = _player.getVolume(); if (vol <= 95) setVolumePersisted(vol + 5); else setVolumePersisted(100);
    return;
  }
}

void UiController::handleNowPlayingButtons() {
  if (!_btn.pressed()) return;
  BtnEvent e = _btn.state();
  if (e == BtnEvent::UP) {
    skipTrack(+1);
  } else if (e == BtnEvent::DOWN) {
    skipTrack(-1);
  } else if (e == BtnEvent::LEFT) {
    transitionTo(Screen::PLAYLIST);
  } else if (e == BtnEvent::RIGHT || e == BtnEvent::OK) {
    if (_player.state() == PlaybackState::STOPPED) {
      if (_selectedEntry >= 0 && _selectedEntry < _sortedCount) {
        const Mp3Entry* pe = _mp3Mgr.entryAt(_sortedIndex[_selectedEntry]);
        if (pe) { strncpy(_playingName, pe->name, MAX_FILENAME_LEN-1); _playingName[MAX_FILENAME_LEN-1]='\0'; _playingSize=pe->fileSize; }
        char path[MAX_FILENAME_LEN + 6];
        _mp3Mgr.getFullPath(_selectedEntry, path, sizeof(path));
        _player.play(path);
        saveBootState();
      }
    } else if (_player.state() == PlaybackState::PLAYING) _player.pause();
    else if (_player.state() == PlaybackState::PAUSED) _player.resume();
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  HOTSPOT INFO SCREEN
//  Layout (v1.6 — proper visual hierarchy, spread across 2.4" LCD):
//
//  Left column (x=8..178)          Right column (QR at cx=250)
//  ─────────────────────           ─────────────────────────
//  y=0-20:  Status bar
//  y=26:    "HOTSPOT" textSize(3)  — BIGGEST element, accent color
//  y=54:    ─── separator line ───
//  y=64:    "SSID" label ts(1)     QR code (~99px + quiet zone)
//  y=76:    SSID value ts(2)       white bg: x≈189..312
//  y=98:    "PASSWORD" label ts(1)
//  y=110:   Password value ts(2)   "Scan to join" below QR
//  Visual hierarchy: title(3) > values(2) > labels/status(1)
//  Full-screen spread on 320×240 display:
//  y=0..18:  Status bar
//  y=28:     "HOTSPOT" title ts(3) — BIGGEST, visually dominant
//  y=56:     ─── separator ───
//  y=68:     SSID label ts(1)
//  y=82:     SSID value ts(2) — clearly smaller than title
//  y=108:    PASSWORD label ts(1)
//  y=122:    Password value ts(2)
//  y=150:    ─── separator ───
//  y=162:    CONNECTED/Waiting ts(2)
//  y=186:    Timeout ts(1)
//  y=210:    [BACK] button
//
//  Text column x=0..178, QR white bg starts at x≈189.
//  fillRect clears limited to width=170 — never overlaps QR.
// ═══════════════════════════════════════════════════════════════════════════════
void UiController::drawHotspotInfoScreen() {
  auto& tft = _disp.tft();
  tft.fillRect(0, 0, SCREEN_W, SCREEN_H, C_BG);
  drawStatusBar();

  // ── Title "HOTSPOT" — textSize(3), the dominant header ───────────
  tft.setTextColor(C_ACCENT); tft.setTextSize(3);
  tft.setCursor(8, 28); tft.print("HOTSPOT");

  // Pulse indicator (right side of title area)
  tft.fillCircle(SCREEN_W - 14, 40, 5, C_GREEN);

  // ── Separator line below title ───────────────────────────────────
  tft.drawFastHLine(8, 56, 170, C_DARKGREY);

  // ── SSID — small grey label ABOVE, white value below ────────────
  tft.setTextColor(C_GREY); tft.setTextSize(1);
  tft.setCursor(8, 68); tft.print("SSID");
  tft.setTextColor(C_WHITE); tft.setTextSize(2);
  tft.setCursor(8, 82);
  tft.print(_hotspot.ssid());

  // ── Password — small label, bigger value ─────────────────────────
  tft.setTextColor(C_GREY); tft.setTextSize(1);
  tft.setCursor(8, 108); tft.print("PASSWORD");
  tft.setTextColor(C_WHITE); tft.setTextSize(2);
  tft.setCursor(8, 122);
  tft.print(_hotspot.password());

  // ── Separator line above status ──────────────────────────────────
  tft.drawFastHLine(8, 150, 170, C_DARKGREY);

  // ── Connection status — textSize(2) ──────────────────────────────
  tft.setTextSize(2);
  tft.setCursor(8, 162);
  int clients = _hotspot.connectedClients();
  if (clients > 0) {
    tft.setTextColor(C_GREEN);
    tft.print("CONNECTED");
    char clientStr[16];
    snprintf(clientStr, sizeof(clientStr), " (%d)", clients);
    tft.print(clientStr);
  } else {
    tft.setTextColor(C_YELLOW);
    tft.print("Waiting...");
  }

  // ── Idle timeout countdown — textSize(1), compact ────────────────
  tft.setTextColor(C_GREY); tft.setTextSize(1);
  tft.setCursor(8, 186);
  unsigned long lastAct = _hotspot.lastActivity();
  unsigned long elapsed = (millis() - lastAct) / 1000;
  unsigned long remaining = (AP_IDLE_TIMEOUT_MS / 1000);
  if (lastAct > 0) {
    if (elapsed < remaining) remaining = remaining - elapsed;
    else remaining = 0;
  }
  char timeoutStr[32];
  snprintf(timeoutStr, sizeof(timeoutStr), "Timeout: %lum %lus",
           (unsigned long)(remaining / 60), (unsigned long)(remaining % 60));
  tft.print(timeoutStr);

  // ── QR code (right column) ───────────────────────────────────────
  // cx=250, cy=128, maxPx=115 → QR ~99px, white bg x≈189..312
  // Left text column ends at x≈178 — no overlap.
  drawWiFiQR(tft, 250, 128, 115, _hotspot.ssid(), _hotspot.password());

  // "Scan to join" label below QR
  tft.setTextColor(C_GREY); tft.setTextSize(1);
  const char* scanLabel = "Scan to join";
  int labelW = strlen(scanLabel) * 6;
  tft.setCursor(250 - labelW / 2, 196);
  tft.print(scanLabel);

  // ── BACK button ──────────────────────────────────────────────────
  _disp.drawBtn(8, 210, 90, 28, "BACK", C_BTN_DARK, 1, C_WHITE);
}

void UiController::updateHotspotInfoScreen() {
  auto& tft = _disp.tft();

  // Pulse the status indicator (matches title area y=40)
  bool on = (millis() / 500) % 2 == 0;
  if (on) {
    tft.fillCircle(SCREEN_W - 14, 40, 5, C_GREEN);
  } else {
    tft.fillCircle(SCREEN_W - 14, 40, 5, C_BG);
    tft.drawCircle(SCREEN_W - 14, 40, 5, C_GREEN);
  }

  // Update connection status — textSize(2), y=162
  // Clear area limited to width=170 (x=8..178) — QR white bg starts at x≈189
  int clients = _hotspot.connectedClients();
  tft.fillRect(8, 162, 170, 16, C_BG);
  tft.setTextSize(2);
  tft.setCursor(8, 162);
  if (clients > 0) {
    tft.setTextColor(C_GREEN);
    tft.print("CONNECTED");
    char clientStr[16];
    snprintf(clientStr, sizeof(clientStr), " (%d)", clients);
    tft.print(clientStr);
  } else {
    tft.setTextColor(C_YELLOW);
    tft.print("Waiting...");
  }

  // Update idle timeout countdown — textSize(1), y=186
  // Clear area limited to width=170 (x=8..178) — no QR overlap
  tft.fillRect(8, 186, 170, 10, C_BG);
  tft.setTextColor(C_GREY); tft.setTextSize(1);
  tft.setCursor(8, 186);
  unsigned long lastAct = _hotspot.lastActivity();
  unsigned long elapsed = (millis() - lastAct) / 1000;
  unsigned long remaining = (AP_IDLE_TIMEOUT_MS / 1000);
  if (lastAct > 0) {
    if (elapsed < remaining) remaining = remaining - elapsed;
    else remaining = 0;
  }
  char timeoutStr[32];
  snprintf(timeoutStr, sizeof(timeoutStr), "Timeout: %lum %lus",
           (unsigned long)(remaining / 60), (unsigned long)(remaining % 60));
  tft.print(timeoutStr);

  // If hotspot was stopped (e.g. by idle timeout), exit the screen
  if (!_hotspot.isActive()) {
    _mode = PlayerMode::NORMAL;
    _lastActivity = millis();
    transitionTo(Screen::PLAYLIST);
  }
}

void UiController::handleHotspotInfoTouch(int tx, int ty) {
  // BACK button
  if (hitTest(tx, ty, 8, 208, 90, 28)) {
    _hotspot.stop();
    _mode = PlayerMode::NORMAL;
    _lastActivity = millis();
    transitionTo(Screen::PLAYLIST);
    return;
  }

  // Mode badge (top-right) — open mode menu
  if (hitTest(tx, ty, MODE_BADGE_X, MODE_BADGE_Y, MODE_BADGE_W, MODE_BADGE_H)) {
    _modeMenuOpen = true;
    _modeMenuHost = Screen::HOTSPOT_INFO;
    drawModeMenu();
    return;
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  USB DRIVE INFO SCREEN
//  Layout:
//    y=0-20:    Status bar
//    y=24-40:   "USB DRIVE" title + pulse indicator
//    y=50-...:  Explanation text
//    y=200-228: BACK button (also exits if the host ejects the drive)
// ═══════════════════════════════════════════════════════════════════════════════
void UiController::drawUsbInfoScreen() {
  auto& tft = _disp.tft();
  tft.fillRect(0, 0, SCREEN_W, SCREEN_H, C_BG);
  drawStatusBar();

  // Title + pulse indicator
  tft.setTextColor(C_ACCENT); tft.setTextSize(2);
  tft.setCursor(8, 24); tft.print("USB DRIVE");
  tft.fillCircle(SCREEN_W - 12, 31, 4, C_GREEN);

  tft.setTextColor(C_WHITE); tft.setTextSize(1);
  tft.setCursor(8, 56);  tft.print("Connected as a USB flash drive.");
  tft.setCursor(8, 70);  tft.print("Add, delete, or rename files in");
  tft.setCursor(8, 82);  tft.print("the MP3 folder from your computer.");

  tft.setTextColor(C_YELLOW);
  tft.setCursor(8, 104); tft.print("Do not unplug while transferring.");

  tft.setTextColor(C_GREY);
  tft.setCursor(8, 126); tft.print("Playback is paused during transfer.");
  tft.setCursor(8, 138); tft.print("Eject the drive on your computer,");
  tft.setCursor(8, 150); tft.print("or tap BACK, when you're done.");

  // ── BACK button ────────────────────────────────────────────────────
  _disp.drawBtn(8, 200, 90, 28, "BACK", C_BTN_DARK, 1, C_WHITE);
}

void UiController::updateUsbInfoScreen() {
  auto& tft = _disp.tft();

  // Pulse the status indicator
  bool on = (millis() / 500) % 2 == 0;
  if (on) {
    tft.fillCircle(SCREEN_W - 12, 33, 4, C_GREEN);
  } else {
    tft.fillCircle(SCREEN_W - 12, 33, 4, C_BG);
    tft.drawCircle(SCREEN_W - 12, 33, 4, C_GREEN);
  }

  // If USB Drive mode ended on its own (host ejected the volume), rescan
  // the library — files may have been added/removed/renamed — and leave
  // this screen.
  if (!_usbMsc.isActive()) {
    _mode = PlayerMode::NORMAL;
    _lastActivity = millis();
    _mp3Mgr.rescan();
    transitionTo(Screen::PLAYLIST);
  }
}

void UiController::handleUsbInfoTouch(int tx, int ty) {
  // BACK button
  if (hitTest(tx, ty, 8, 200, 90, 28)) {
    _usbMsc.stop();
    _mode = PlayerMode::NORMAL;
    _lastActivity = millis();
    _mp3Mgr.rescan();
    transitionTo(Screen::PLAYLIST);
    return;
  }

  // Mode badge (top-right) — open mode menu
  if (hitTest(tx, ty, MODE_BADGE_X, MODE_BADGE_Y, MODE_BADGE_W, MODE_BADGE_H)) {
    _modeMenuOpen = true;
    _modeMenuHost = Screen::USB_INFO;
    drawModeMenu();
    return;
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  DASHBOARD PIN SCREEN (same as SecureVault's LOCK screen)
// ═══════════════════════════════════════════════════════════════════════════════






// (the SecureVault-style out-of-band PSK) that the user must enter into
// the Electron webapp. The Electron app then performs the ECDH P-256
// handshake with this device over USB CDC and derives an AES-256-GCM










// ═══════════════════════════════════════════════════════════════════════════════
//  FIRST-BOOT PIN SETUP SCREEN (same as SecureVault's F12)
//  Two-step flow: enter PIN → confirm PIN
// ═══════════════════════════════════════════════════════════════════════════════
void UiController::drawFirstBootPinScreen() {
  auto& tft = _disp.tft();
  tft.fillRect(0, 0, SCREEN_W, SCREEN_H, C_BG);
  drawStatusBar();

  const char* titles[] = { "SET UP YOUR PIN", "CONFIRM YOUR PIN" };
  const char* subtitles[] = { "Choose a PIN (4-8 digits)", "Re-enter your PIN" };

  tft.setTextColor(C_ACCENT); tft.setTextSize(1);
  int titleLen = strlen(titles[_firstBootPinStep]);
  tft.setCursor(SCREEN_W / 2 - titleLen * 3, 22);
  tft.print(titles[_firstBootPinStep]);

  tft.setTextColor(C_GREY); tft.setTextSize(1);
  int subLen = strlen(subtitles[_firstBootPinStep]);
  tft.setCursor(SCREEN_W / 2 - subLen * 3, 33);
  tft.print(subtitles[_firstBootPinStep]);

  int n = (_firstBootPinStep == 0) ? _firstBootPinLen : _firstBootPinConfirmLen;
  for (int i = 0; i < MAX_PIN_LEN; i++) {
    drawFirstBootPinDot(i, i < n);
  }

  if (_firstBootError[0] != '\0') {
    tft.setTextColor(C_RED, C_BG); tft.setTextSize(1);
    tft.setCursor(SCREEN_W / 2 - strlen(_firstBootError) * 3, 68);
    tft.print(_firstBootError);
  }

  // Numpad
  for (int r = 0; r < NUM_ROWS; r++)
    for (int c = 0; c < NUM_COLS; c++)
      drawNumpadBtn(r * NUM_COLS + c, false);
}

void UiController::handleFirstBootPinTouch(int tx, int ty) {
  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) {
      int idx = r * NUM_COLS + c;
      int bx = NUM_X0 + c * (NUM_BW + NUM_GAP);
      int by = NUM_Y0 + r * (NUM_BH + NUM_GAP);
      if (!hitTest(tx, ty, bx, by, NUM_BW, NUM_BH)) continue;

      _disp.triggerFlash(bx, by, NUM_BW, NUM_BH,
        (idx == 9) ? C_RED : (idx == 11) ? C_BTN_DARK : C_BTN, NUM_LABELS[idx], 2);

      _firstBootError[0] = '\0';

      if (idx == 9) {  // CLR
        if (_firstBootPinStep == 0 && _firstBootPinLen > 0) {
          _firstBootPinLen--; _firstBootPinBuf[_firstBootPinLen] = 0;
          drawFirstBootPinScreen();
        } else if (_firstBootPinStep == 1 && _firstBootPinConfirmLen > 0) {
          _firstBootPinConfirmLen--; _firstBootPinConfirmBuf[_firstBootPinConfirmLen] = 0;
          drawFirstBootPinScreen();
        }
      } else if (idx == 11) {  // OK
        submitFirstBootPin();
      } else {  // digit
        int digit = (idx < 9) ? (idx + 1) : 0;
        if (_firstBootPinStep == 0 && _firstBootPinLen < MAX_PIN_LEN) {
          _firstBootPinBuf[_firstBootPinLen++] = '0' + digit;
          _firstBootPinBuf[_firstBootPinLen] = 0;
          drawFirstBootPinScreen();
        } else if (_firstBootPinStep == 1 && _firstBootPinConfirmLen < MAX_PIN_LEN) {
          _firstBootPinConfirmBuf[_firstBootPinConfirmLen++] = '0' + digit;
          _firstBootPinConfirmBuf[_firstBootPinConfirmLen] = 0;
          drawFirstBootPinScreen();
        }
      }
      return;
    }
  }
}

void UiController::handleFirstBootPinButtons() {
  if (!_btn.pressed()) return;
  BtnEvent e = _btn.state();
  if (e == BtnEvent::LEFT) {
    if (_firstBootPinStep == 0 && _firstBootPinLen > 0) {
      _firstBootPinLen--; _firstBootPinBuf[_firstBootPinLen] = 0;
      drawFirstBootPinScreen();
    } else if (_firstBootPinStep == 1 && _firstBootPinConfirmLen > 0) {
      _firstBootPinConfirmLen--; _firstBootPinConfirmBuf[_firstBootPinConfirmLen] = 0;
      drawFirstBootPinScreen();
    }
  } else if (e == BtnEvent::OK) {
    _firstBootError[0] = '\0';
    submitFirstBootPin();
  }
}

void UiController::submitFirstBootPin() {
  if (_firstBootPinStep == 0) {
    if (_firstBootPinLen < 4) {
      triggerShake();
      snprintf(_firstBootError, sizeof(_firstBootError), "PIN must be at least 4 digits");
      drawFirstBootPinScreen();
      return;
    }
    _firstBootPinStep = 1;
    _firstBootPinConfirmLen = 0;
    memset(_firstBootPinConfirmBuf, 0, sizeof(_firstBootPinConfirmBuf));
    _firstBootError[0] = '\0';
    drawFirstBootPinScreen();
  } else if (_firstBootPinStep == 1) {
    if (_firstBootPinConfirmLen < 4) {
      triggerShake();
      snprintf(_firstBootError, sizeof(_firstBootError), "PIN must be at least 4 digits");
      drawFirstBootPinScreen();
      return;
    }
    if (strcmp(_firstBootPinBuf, _firstBootPinConfirmBuf) == 0) {
      // PINs match — write to NVS
      _mp3Mgr.completeFirstBoot(_firstBootPinBuf);
      _mp3Mgr.setPin(_firstBootPinBuf);
      memset(_firstBootPinBuf, 0, sizeof(_firstBootPinBuf));
      memset(_firstBootPinConfirmBuf, 0, sizeof(_firstBootPinConfirmBuf));
      _firstBootPinLen = 0;
      _firstBootPinConfirmLen = 0;
      _firstBootPinStep = 0;
      _isFirstBoot = false;
      // Success — go to playlist
      transitionTo(Screen::PLAYLIST);
    } else {
      // PINs don't match — error, start over
      triggerShake();
      snprintf(_firstBootError, sizeof(_firstBootError), "PINs don't match — try again");
      _firstBootPinStep = 0;
      _firstBootPinLen = 0;
      _firstBootPinConfirmLen = 0;
      memset(_firstBootPinBuf, 0, sizeof(_firstBootPinBuf));
      memset(_firstBootPinConfirmBuf, 0, sizeof(_firstBootPinConfirmBuf));
      drawFirstBootPinScreen();
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SETTINGS SCREEN
//  6 rows: Theme, Auto-Timeout, Volume, Hotspot, About, Back
//  Hotspot row opens a sub-menu (HOTSPOT_CHANGE) where the user can
//  change SSID or password.
// ═══════════════════════════════════════════════════════════════════════════════
void UiController::drawSettingsScreen() {
  auto& tft = _disp.tft();
  tft.fillRect(0, 0, SCREEN_W, SCREEN_H, C_BG);
  drawStatusBar();
  tft.fillRect(0, SBAR_H, SCREEN_W, 24, C_HEADER);
  tft.setTextColor(C_ACCENT); tft.setTextSize(1);
  tft.setCursor(6, 27); tft.print("Settings");

  const int rowH = 30;
  const int startY = SBAR_H + 24;
  const char* labels[SETTINGS_COUNT] = {
    "Theme",
    "Auto-Timeout",
    "Volume",
    "Hotspot",
    "About",
    "Back"
  };

  for (int i = 0; i < SETTINGS_COUNT; i++) {
    int y = startY + i * rowH;
    tft.fillRoundRect(4, y, SCREEN_W - 8, rowH - 4, 4, C_PANEL);
    tft.drawRoundRect(4, y, SCREEN_W - 8, rowH - 4, 4, C_ACCENT);
    tft.setTextColor(C_WHITE); tft.setTextSize(1);
    tft.setCursor(14, y + 7); tft.print(labels[i]);

    // Row 0: Theme
    if (i == (int)SettingsRow::THEME) {
      const char* themeNames[] = {"Air-Gapped", "Monochrome", "Emerald", "Sunlight"};
      const char* val = themeNames[_themeId % 4];
      tft.setTextColor(C_GREY);
      tft.setCursor(SCREEN_W - 14 - strlen(val) * 6, y + 7);
      tft.print(val);
    }
    // Row 1: Auto-Timeout
    else if (i == (int)SettingsRow::AUTO_TIMEOUT) {
      uint32_t ms = _mp3Mgr.getAutoTimeoutMs();
      char val[16];
      if (ms == 0) snprintf(val, sizeof(val), "Never");
      else if (ms < 60000) snprintf(val, sizeof(val), "%lus", ms / 1000);
      else snprintf(val, sizeof(val), "%lum", ms / 60000);
      tft.setTextColor(C_GREY);
      tft.setCursor(SCREEN_W - 14 - strlen(val) * 6, y + 7);
      tft.print(val);
    }
    // Row 2: Volume
    else if (i == (int)SettingsRow::VOLUME) {
      char val[8];
      snprintf(val, sizeof(val), "%d%%", _player.getVolume());
      tft.setTextColor(C_GREY);
      tft.setCursor(SCREEN_W - 14 - strlen(val) * 6, y + 7);
      tft.print(val);
    }
    // Row 3: Hotspot — show ">" indicator (tap to open sub-menu)
    else if (i == (int)SettingsRow::HOTSPOT) {
      tft.setTextColor(C_ACCENT);
      tft.setCursor(SCREEN_W - 14, y + 7);
      tft.print(">");
    }
  }
}

void UiController::handleSettingsTouch(int tx, int ty) {
  const int rowH = 30;
  const int startY = SBAR_H + 24;
  for (int i = 0; i < SETTINGS_COUNT; i++) {
    int y = startY + i * rowH;
    if (hitTest(tx, ty, 4, y, SCREEN_W - 8, rowH - 4)) {
      switch ((SettingsRow)i) {
        case SettingsRow::THEME: // Theme — cycle
          cycleTheme();
          drawSettingsScreen();
          break;
        case SettingsRow::AUTO_TIMEOUT: { // Auto-Timeout — cycle
          uint32_t current = _mp3Mgr.getAutoTimeoutMs();
          uint32_t values[] = {15000, 30000, 60000, 120000, 300000, 0};
          int n = sizeof(values) / sizeof(values[0]);
          int idx = 0;
          for (int j = 0; j < n; j++) if (values[j] == current) idx = j;
          idx = (idx + 1) % n;
          _mp3Mgr.setAutoTimeoutMs(values[idx]);
          drawSettingsScreen();
          break;
        }
        case SettingsRow::VOLUME: { // Volume — cycle
          uint8_t vol = _player.getVolume();
          uint8_t volSteps[] = {0, 25, 50, 70, 85, 100};
          int n = sizeof(volSteps) / sizeof(volSteps[0]);
          int idx = 0;
          for (int j = 0; j < n; j++) if (volSteps[j] == vol) { idx = j; break; }
          idx = (idx + 1) % n;
          setVolumePersisted(volSteps[idx]);
          drawSettingsScreen();
          break;
        }
        case SettingsRow::HOTSPOT: // Hotspot — open sub-menu
          transitionTo(Screen::HOTSPOT_CHANGE);
          break;
        case SettingsRow::ABOUT: // About
          transitionTo(Screen::ABOUT);
          break;
        case SettingsRow::BACK: // Back
          transitionTo(Screen::PLAYLIST);
          break;
        case SettingsRow::COUNT: break;
      }
      return;
    }
  }
}

void UiController::handleSettingsButtons() {
  if (!_btn.pressed()) return;
  BtnEvent e = _btn.state();
  if (e == BtnEvent::LEFT) transitionTo(Screen::PLAYLIST);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  HOTSPOT CHANGE SUB-MENU
//  Two rows: "Change SSID" and "Change Password". Each opens the
//  HOTSPOT_INPUT screen with the appropriate mode.
// ═══════════════════════════════════════════════════════════════════════════════
void UiController::drawHotspotChangeScreen() {
  auto& tft = _disp.tft();
  tft.fillRect(0, 0, SCREEN_W, SCREEN_H, C_BG);
  drawStatusBar();
  tft.fillRect(0, SBAR_H, SCREEN_W, 24, C_HEADER);
  tft.setTextColor(C_ACCENT); tft.setTextSize(1);
  tft.setCursor(6, 27); tft.print("Hotspot Settings");

  // Show current SSID + password (masked)
  char ssidBuf[40], passBuf[64];
  _mp3Mgr.getHotspotSSID(ssidBuf, sizeof(ssidBuf));
  _mp3Mgr.getHotspotPassword(passBuf, sizeof(passBuf));

  tft.setTextColor(C_GREY); tft.setTextSize(1);
  tft.setCursor(8, 52); tft.print("Current SSID:");
  tft.setTextColor(C_WHITE);
  tft.setCursor(8, 64); tft.print(ssidBuf);

  tft.setTextColor(C_GREY);
  tft.setCursor(8, 80); tft.print("Current Password:");
  tft.setTextColor(C_WHITE);
  tft.setCursor(8, 92);
  // Mask password with asterisks (show length only)
  for (int i = 0; i < (int)strlen(passBuf) && i < 20; i++) tft.print("*");

  // Row 0: Change SSID
  int y0 = 116;
  tft.fillRoundRect(4, y0, SCREEN_W - 8, 36, 4, C_PANEL);
  tft.drawRoundRect(4, y0, SCREEN_W - 8, 36, 4, C_ACCENT);
  tft.setTextColor(C_WHITE); tft.setTextSize(1);
  tft.setCursor(14, y0 + 12); tft.print("Change SSID");
  tft.setTextColor(C_ACCENT);
  tft.setCursor(SCREEN_W - 14, y0 + 12); tft.print(">");

  // Row 1: Change Password
  int y1 = 156;
  tft.fillRoundRect(4, y1, SCREEN_W - 8, 36, 4, C_PANEL);
  tft.drawRoundRect(4, y1, SCREEN_W - 8, 36, 4, C_ACCENT);
  tft.setTextColor(C_WHITE); tft.setTextSize(1);
  tft.setCursor(14, y1 + 12); tft.print("Change Password");
  tft.setTextColor(C_ACCENT);
  tft.setCursor(SCREEN_W - 14, y1 + 12); tft.print(">");

  // BACK button
  _disp.drawBtn(8, 200, 90, 28, "BACK", C_BTN_DARK, 1, C_WHITE);
}

void UiController::handleHotspotChangeTouch(int tx, int ty) {
  // Change SSID row
  if (hitTest(tx, ty, 4, 116, SCREEN_W - 8, 36)) {
    _hsInputMode = HotspotInputMode::SSID;
    _hsTextLen = 0; _hsTextBuf[0] = '\0';
    _hsMultitapKey = -1;
    transitionTo(Screen::HOTSPOT_INPUT);
    return;
  }
  // Change Password row
  if (hitTest(tx, ty, 4, 156, SCREEN_W - 8, 36)) {
    _hsInputMode = HotspotInputMode::PASSWORD;
    _hsTextLen = 0; _hsTextBuf[0] = '\0';
    _hsMultitapKey = -1;
    transitionTo(Screen::HOTSPOT_INPUT);
    return;
  }
  // BACK
  if (hitTest(tx, ty, 8, 200, 90, 28)) {
    transitionTo(Screen::SETTINGS);
    return;
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  HOTSPOT INPUT SCREEN — multi-tap text entry
//
//  Layout (uses the existing numpad geometry):
//    y=0-20:   status bar
//    y=22-32:  title ("Change SSID" / "Change Password")
//    y=34-44:  subtitle (length rules)
//    y=46-66:  text being entered (large, with cursor)
//    y=66+:    numpad (reused) — keys are multi-tap:
//              1 → "." "/" "-" "_" " "  (symbols)
//              2 → a b c 2
//              3 → d e f 3
//              4 → g h i 4
//              5 → j k l 5
//              6 → m n o 6
//              7 → p q r s 7
//              8 → t u v 8
//              9 → w x y z 9
//              0 → " " 0
//              CLR → backspace
//              OK  → save & commit
// ═══════════════════════════════════════════════════════════════════════════════
// Multi-tap letter groups (index = numpad key index 0..11)
// Index 0="1", 1="2", ..., 8="9", 9="CLR", 10="0", 11="OK"
static const char* HS_MULTITAP_GROUPS[12] = {
  "./-_ ",       // 1
  "abc2",        // 2
  "def3",        // 3
  "ghi4",        // 4
  "jkl5",        // 5
  "mno6",        // 6
  "pqrs7",       // 7
  "tuv8",        // 8
  "wxyz9",       // 9
  "",            // CLR — handled specially
  " 0",          // 0
  ""             // OK — handled specially
};

void UiController::drawHotspotInputScreen() {
  auto& tft = _disp.tft();
  tft.fillRect(0, 0, SCREEN_W, SCREEN_H, C_BG);
  drawStatusBar();

  // Title
  const char* title = (_hsInputMode == HotspotInputMode::SSID) ?
                      "Change SSID" : "Change Password";
  tft.setTextColor(C_ACCENT); tft.setTextSize(1);
  int titleW = strlen(title) * 6;
  tft.setCursor((SCREEN_W - titleW) / 2, 24);
  tft.print(title);

  // Subtitle — length rules
  const char* sub = (_hsInputMode == HotspotInputMode::SSID) ?
                    "1-31 chars. A-Z a-z 0-9 .-_/" :
                    "8-63 chars. A-Z a-z 0-9";
  tft.setTextColor(C_GREY);
  int subW = strlen(sub) * 6;
  tft.setCursor((SCREEN_W - subW) / 2, 36);
  tft.print(sub);

  // Text being entered (with cursor)
  tft.fillRect(8, 46, SCREEN_W - 16, 18, C_PANEL);
  tft.drawRoundRect(8, 46, SCREEN_W - 16, 18, 2, C_ACCENT);
  tft.setTextColor(C_WHITE, C_PANEL); tft.setTextSize(1);
  tft.setCursor(12, 51);
  if (_hsTextLen == 0) {
    tft.setTextColor(C_DARKGREY, C_PANEL);
    tft.print("(empty)");
  } else {
    // For password mode, mask with asterisks
    if (_hsInputMode == HotspotInputMode::PASSWORD) {
      for (int i = 0; i < _hsTextLen && i < 36; i++) tft.print("*");
    } else {
      // Show up to 36 chars
      int n = _hsTextLen < 36 ? _hsTextLen : 36;
      tft.print(String(_hsTextBuf).substring(0, n));
    }
  }
  // Cursor (blink)
  if ((millis() / 500) % 2 == 0) {
    int curX = 12 + (_hsTextLen < 36 ? _hsTextLen : 36) * 6;
    if (_hsTextLen == 0) curX = 12 + 7 * 6;  // length of "(empty)"
    tft.drawFastVLine(curX, 51, 10, C_ACCENT);
  }

  // Numpad — labels show the multi-tap groups
  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) {
      int idx = r * NUM_COLS + c;
      int bx = NUM_X0 + c * (NUM_BW + NUM_GAP);
      int by = NUM_Y0 + r * (NUM_BH + NUM_GAP);

      uint16_t col = (idx == 9) ? C_RED : (idx == 11) ? C_BTN_DARK : C_BTN;
      tft.fillRoundRect(bx, by, NUM_BW, NUM_BH, 4, col);
      tft.drawRoundRect(bx, by, NUM_BW, NUM_BH, 4, C_ACCENT);

      tft.setTextColor(C_WHITE); tft.setTextSize(1);
      // Two lines: group label (top), key index hint (bottom)
      if (idx == 9) {
        tft.setCursor(bx + (NUM_BW - 18) / 2, by + (NUM_BH - 8) / 2);
        tft.print("CLR");
      } else if (idx == 11) {
        tft.setCursor(bx + (NUM_BW - 18) / 2, by + (NUM_BH - 8) / 2);
        tft.print("SAVE");
      } else {
        // Show the multi-tap group on top, the digit on bottom
        tft.setCursor(bx + 4, by + 4);
        tft.print(HS_MULTITAP_GROUPS[idx]);
        tft.setCursor(bx + NUM_BW - 8, by + NUM_BH - 12);
        tft.setTextColor(C_GREY);
        tft.print(NUM_LABELS[idx]);
      }
    }
  }
}

void UiController::handleHotspotInputTouch(int tx, int ty) {
  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) {
      int idx = r * NUM_COLS + c;
      int bx = NUM_X0 + c * (NUM_BW + NUM_GAP);
      int by = NUM_Y0 + r * (NUM_BH + NUM_GAP);
      if (!hitTest(tx, ty, bx, by, NUM_BW, NUM_BH)) continue;

      // CLR — backspace
      if (idx == 9) {
        if (_hsTextLen > 0) {
          _hsTextBuf[--_hsTextLen] = '\0';
        }
        _hsMultitapKey = -1;
        drawHotspotInputScreen();
        return;
      }
      // OK / SAVE
      if (idx == 11) {
        commitHotspotInput();
        return;
      }
      // Multi-tap digit
      const char* group = HS_MULTITAP_GROUPS[idx];
      if (!group || group[0] == '\0') return;

      unsigned long now = millis();
      // If user tapped the same key within the timeout, cycle to next letter.
      if (idx == _hsMultitapKey && (now - _hsMultitapLast) < HS_MULTITAP_TIMEOUT) {
        _hsMultitapIdx = (_hsMultitapIdx + 1) % strlen(group);
        // Replace the last entered char
        if (_hsTextLen > 0) {
          _hsTextBuf[_hsTextLen - 1] = group[_hsMultitapIdx];
        }
      } else {
        // Start a new multi-tap sequence — commit any previous letter (already
        // in the buffer) and start fresh with the first letter of this group.
        if (_hsTextLen < (int)sizeof(_hsTextBuf) - 1) {
          _hsTextBuf[_hsTextLen++] = group[0];
          _hsTextBuf[_hsTextLen] = '\0';
        }
        _hsMultitapKey = idx;
        _hsMultitapIdx = 0;
      }
      _hsMultitapLast = now;
      drawHotspotInputScreen();
      return;
    }
  }
}

void UiController::handleHotspotInputButtons() {
  if (!_btn.pressed()) return;
  BtnEvent e = _btn.state();
  if (e == BtnEvent::LEFT) {
    // Back to the hotspot change sub-menu (no save)
    transitionTo(Screen::HOTSPOT_CHANGE);
  } else if (e == BtnEvent::OK) {
    // TOUCH = SAVE
    commitHotspotInput();
  }
}

void UiController::commitHotspotInput() {
  if (_hsTextLen == 0) {
    triggerShake();
    return;
  }
  if (_hsInputMode == HotspotInputMode::SSID) {
    if (_hsTextLen < 1 || _hsTextLen > 31) {
      triggerShake();
      return;
    }
    _mp3Mgr.setHotspotSSID(_hsTextBuf);
    Serial.printf("[UI] Hotspot SSID saved: %s\n", _hsTextBuf);
  } else {
    if (_hsTextLen < 8 || _hsTextLen > 63) {
      triggerShake();
      return;
    }
    _mp3Mgr.setHotspotPassword(_hsTextBuf);
    Serial.printf("[UI] Hotspot password saved (%d chars)\n", _hsTextLen);
  }
  // Clear the buffer (security hygiene)
  memset(_hsTextBuf, 0, sizeof(_hsTextBuf));
  _hsTextLen = 0;
  _hsMultitapKey = -1;
  transitionTo(Screen::HOTSPOT_CHANGE);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  ABOUT SCREEN
// ═══════════════════════════════════════════════════════════════════════════════
void UiController::drawAboutScreen() {
  auto& tft = _disp.tft();
  tft.fillRect(0, 0, SCREEN_W, SCREEN_H, C_BG);
  drawStatusBar();

  // Music note glyph + brand
  // Draw a simple music note
  int cx = SCREEN_W / 2;
  tft.drawFastVLine(cx, 30, 18, C_ACCENT);
  tft.fillCircle(cx - 3, 48, 4, C_ACCENT);
  tft.drawFastHLine(cx, 30, 6, C_ACCENT);
  tft.drawFastVLine(cx + 6, 30, 8, C_ACCENT);

  tft.setTextColor(C_ACCENT); tft.setTextSize(2);
  tft.setCursor(SCREEN_W / 2 - 54, 62); tft.print("ShadowTune");
  tft.setTextColor(C_GREY); tft.setTextSize(1);
  tft.setCursor(SCREEN_W / 2 - 24, 80); tft.print("v1.0.0");

  // Info rows
  int y = 100;
  tft.setTextColor(C_WHITE); tft.setTextSize(1);
  tft.setCursor(20, y); tft.print("Developer: Purujith Kadekar"); y += 14;
  tft.setCursor(20, y); tft.print("Board: ESP32-S3 Pro"); y += 14;
  tft.setCursor(20, y); tft.print("DAC: CS4344"); y += 14;
  tft.setCursor(20, y); tft.print("Display: ILI9341 320x240"); y += 14;
  tft.setCursor(20, y); tft.print("Tracks: "); tft.print(_mp3Mgr.count()); y += 14;
  tft.setCursor(20, y); tft.print("Storage: SD card (/MP3/)"); y += 14;

  // Back button
  _disp.drawBtn(SCREEN_W / 2 - 50, SCREEN_H - 36, 100, 28, "Back", C_BTN);
}

void UiController::handleAboutTouch(int tx, int ty) {
  if (hitTest(tx, ty, SCREEN_W / 2 - 50, SCREEN_H - 36, 100, 28)) {
    transitionTo(Screen::SETTINGS);
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SETTIME SCREEN (same as SecureVault)
// ═══════════════════════════════════════════════════════════════════════════════
void UiController::drawTimeDigits() {
  auto& tft = _disp.tft();
  tft.fillRect(8, 22, 304, 28, C_PANEL);
  tft.setTextColor(C_WHITE); tft.setTextSize(2);
  tft.setCursor(8, 26);
  if (_timeLen > 0) {
    tft.print(_timeBuf);
  } else {
    tft.setTextColor(C_GREY);
    tft.print("Enter epoch seconds");
  }
}

void UiController::drawSetTimeScreen() {
  auto& tft = _disp.tft();
  tft.fillRect(0, 0, SCREEN_W, SCREEN_H, C_BG);
  tft.setTextColor(C_ACCENT); tft.setTextSize(1);
  tft.setCursor(8, 6); tft.print("SET TIME - unix epoch seconds");
  drawTimeDigits();
  for (int r = 0; r < NUM_ROWS; r++)
    for (int c = 0; c < NUM_COLS; c++)
      drawNumpadBtn(r * NUM_COLS + c, false);
}

void UiController::handleSetTimeTouch(int tx, int ty) {
  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) {
      int idx = r * NUM_COLS + c;
      int bx = NUM_X0 + c * (NUM_BW + NUM_GAP), by = NUM_Y0 + r * (NUM_BH + NUM_GAP);
      if (!hitTest(tx, ty, bx, by, NUM_BW, NUM_BH)) continue;

      _disp.triggerFlash(bx, by, NUM_BW, NUM_BH,
        (idx == 9) ? C_RED : (idx == 11) ? C_BTN_DARK : C_BTN, NUM_LABELS[idx], 2);

      if (idx == 9) {  // CLR
        if (_timeLen > 0) { _timeLen--; _timeBuf[_timeLen] = 0; drawTimeDigits(); }
      } else if (idx == 11) {  // OK
        uint32_t epoch = strtoul(_timeBuf, NULL, 10);
        if (_timeLen > 0 && epoch >= SANE_EPOCH) {
          _rtc.writeFromEpoch(epoch);
          _timeLen = 0; _timeBuf[0] = 0;
          // After setting time, go to the appropriate screen
          if (_isFirstBoot) {
            transitionTo(Screen::FIRST_BOOT_PIN);
          } else {
            transitionTo(Screen::PLAYLIST);
          }
        } else {
          _disp.tft().fillRect(8, 40, 304, 18, C_RED);
          delay(150);
          drawTimeDigits();
        }
      } else {  // digit
        int digit = (idx < 9) ? (idx + 1) : 0;
        if (_timeLen < 10) { _timeBuf[_timeLen++] = '0' + digit; _timeBuf[_timeLen] = 0; drawTimeDigits(); }
      }
      return;
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  MODE MENU OVERLAY (simplified: HOTSPOT, DASHBOARD, SETTINGS, CANCEL)
// ═══════════════════════════════════════════════════════════════════════════════
void UiController::drawModeMenu() {
  auto& tft = _disp.tft();
  int n = 4;  // HOTSPOT, USB DRIVE, SETTINGS, CANCEL
  int h = n * MENU_OPT_H + (n + 1) * MENU_GAP;
  int y0 = SCREEN_H - h - 6;
  tft.fillRect(0, y0 - 4, SCREEN_W, h + 8, C_PANEL);
  tft.drawFastHLine(0, y0 - 4, SCREEN_W, C_ACCENT);
  for (int i = 0; i < n; i++) {
    int y = y0 + MENU_GAP + i * (MENU_OPT_H + MENU_GAP);
    bool isCurrent = (i == 0 && _mode == PlayerMode::HOTSPOT) ||
                      (i == 1 && _mode == PlayerMode::USB_MSC);
    // i == 3 is CANCEL (dark button)
    uint16_t col = (i == 3) ? C_BTN_DARK : (isCurrent ? C_ACCENT : C_BTN);
    uint16_t txtCol = isCurrent ? C_BG : C_WHITE;
    _disp.drawBtn(12, y, SCREEN_W - 24, MENU_OPT_H, MODE_MENU_LABELS[i], col, 1, txtCol);
  }
}

void UiController::closeModeMenu() {
  _modeMenuOpen = false;
  switch (_modeMenuHost) {
    case Screen::PLAYLIST:        drawPlaylistScreen();        break;
    case Screen::NOW_PLAYING:     drawNowPlayingScreen();      break;
    case Screen::HOTSPOT_INFO:    drawHotspotInfoScreen();     break;
    case Screen::SETTINGS:        drawSettingsScreen();        break;
    case Screen::HOTSPOT_CHANGE:  drawHotspotChangeScreen();   break;
    case Screen::HOTSPOT_INPUT:   drawHotspotInputScreen();    break;
    case Screen::USB_INFO:        drawUsbInfoScreen();         break;
    case Screen::CLOCK:           drawClockScreen();           break;
    case Screen::SEARCH:          drawSearchScreen();          break;
    default:                      drawPlaylistScreen();        break;
  }
}

void UiController::handleModeMenuTouch(int tx, int ty) {
  int n = 4;
  int h = n * MENU_OPT_H + (n + 1) * MENU_GAP;
  int y0 = SCREEN_H - h - 6;
  for (int i = 0; i < n; i++) {
    int y = y0 + MENU_GAP + i * (MENU_OPT_H + MENU_GAP);
    if (hitTest(tx, ty, 12, y, SCREEN_W - 24, MENU_OPT_H)) {

      // i == 2 is SETTINGS — open the settings screen
      if (i == 2) {
        _modeMenuOpen = false;
        transitionTo(Screen::SETTINGS);
        return;
      }

      // i == 3 is CANCEL
      if (i == 3) {
        closeModeMenu();
        return;
      }

      // i == 0 = HOTSPOT, i == 1 = USB DRIVE
      if (i == 0) {
        _modePendingSwitch = PlayerMode::HOTSPOT;
      } else if (i == 1) {
        _modePendingSwitch = PlayerMode::USB_MSC;
      }

      if (_modePendingSwitch == _mode) {
        closeModeMenu();
        return;
      }

      // Start the deferred mode switch sequence
      _modeMenuOpen = false;
      _modeSwitchState = ModeSwitchState::TEARDOWN_OLD;
      _modeSwitchStartTime = millis();

      // Redraw the host screen so the menu overlay is gone immediately
      switch (_modeMenuHost) {
        case Screen::PLAYLIST:     drawPlaylistScreen();     break;
        case Screen::NOW_PLAYING:  drawNowPlayingScreen();   break;
        case Screen::HOTSPOT_INFO: drawHotspotInfoScreen();  break;
        case Screen::USB_INFO:     drawUsbInfoScreen();      break;
        case Screen::SEARCH:      drawSearchScreen();      break;
        default:                   drawPlaylistScreen();     break;
      }
      return;
    }
  }
  closeModeMenu(); // tap outside the options = cancel
}

// ═══════════════════════════════════════════════════════════════════════════════
//  DEFERRED MODE SWITCH (same pattern as SecureVault)
//  Two-step state machine: TEARDOWN_OLD → INIT_NEW
//  NOTE: DASHBOARD mode does NOT enter through this path — it requires PIN
//  So the DASHBOARD branch here is only reached if the mode was already
//  DASHBOARD and we're tearing it down (in TEARDOWN_OLD) — at which point
//  there's nothing to start in INIT_NEW because the user already left.
// ═══════════════════════════════════════════════════════════════════════════════
void UiController::processModeSwitch() {
  if (_modeSwitchState == ModeSwitchState::TEARDOWN_OLD) {
    // Tear down the old mode
    if (_mode == PlayerMode::HOTSPOT) {
      _hotspot.stop();
    } else if (_mode == PlayerMode::USB_MSC) {
      _usbMsc.stop();
    }

    // Set to NORMAL temporarily
    _mode = PlayerMode::NORMAL;

    // Safety timeout: 5 seconds
    if (millis() - _modeSwitchStartTime > 5000) {
      _modeSwitchState = ModeSwitchState::IDLE;
      _modePendingSwitch = PlayerMode::NORMAL;
      transitionTo(Screen::PLAYLIST);
      return;
    }

    // Advance to INIT_NEW
    _modeSwitchState = ModeSwitchState::INIT_NEW;
    return;
  }

  if (_modeSwitchState == ModeSwitchState::INIT_NEW) {
    // Start the new mode
    if (_modePendingSwitch == PlayerMode::HOTSPOT) {
      if (_hotspot.start()) {
        _mode = PlayerMode::HOTSPOT;
        _modeSwitchState = ModeSwitchState::IDLE;
        transitionTo(Screen::HOTSPOT_INFO);
        return;
      } else {
        // AP start failed — fall back to playlist
        _mode = PlayerMode::NORMAL;
        _modeSwitchState = ModeSwitchState::IDLE;
        transitionTo(Screen::PLAYLIST);
        return;
      }
    }

    if (_modePendingSwitch == PlayerMode::USB_MSC) {
      if (_usbMsc.start()) {
        _mode = PlayerMode::USB_MSC;
        _modeSwitchState = ModeSwitchState::IDLE;
        transitionTo(Screen::USB_INFO);
        return;
      } else {
        // SD not available — fall back to playlist
        _mode = PlayerMode::NORMAL;
        _modeSwitchState = ModeSwitchState::IDLE;
        transitionTo(Screen::PLAYLIST);
        return;
      }
    }

    // Fallback: NORMAL mode
    _mode = PlayerMode::NORMAL;
    _modeSwitchState = ModeSwitchState::IDLE;
    transitionTo(Screen::PLAYLIST);
    return;
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  5-SECOND HOLD-TO-RETURN (TTP223 capacitive pad)
//  In the MP3 player, holding the pad for 5 seconds returns to the playlist
//  from any screen (not lock — there is no lock screen).
// ═══════════════════════════════════════════════════════════════════════════════


// ═══════════════════════════════════════════════════════════════════════════════
//  THEME SYSTEM
// ═══════════════════════════════════════════════════════════════════════════════
void UiController::loadTheme() {
  _themeId = _mp3Mgr.getThemeId();
  applyTheme(_themeId);
}

void UiController::applyTheme(uint8_t id) {
  _themeId = id;
  ThemeColors t;
  switch (id) {
    case 1:  t = THEME_MONOCHROME; break;
    case 2:  t = THEME_EMERALD;   break;
    case 3:  t = THEME_SUNLIGHT;  break;
    default: t = THEME_AIR_GAPPED; break;
  }
  C_BG = t.bg;
  C_PANEL = t.panel;
  C_HEADER = t.header;
  C_ACCENT = t.accent;
  C_GREEN = t.green;
  C_RED = t.red;
  C_ORANGE = t.orange;
  C_YELLOW = t.yellow;
  C_WHITE = t.white;
  C_GREY = t.grey;
  C_DARKGREY = t.darkgrey;
  C_BTN = t.btn;
  C_BTN_DARK = t.btnDark;
  C_CHIP = t.chip;
  _mp3Mgr.setThemeId(id);
  transitionTo(_screen);
}

void UiController::cycleTheme() {
  _themeId = (_themeId + 1) % 4;
  applyTheme(_themeId);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  DEEP SLEEP (5 s touch pad hold) — same as SecureVault
// ═══════════════════════════════════════════════════════════════════════════════
void UiController::enterPowerOff() {
  auto& tft = _disp.tft();
  _screen = Screen::POWER_OFF;

  drawPowerOffScreen(3, "Saving state...");
  delay(900);
  drawPowerOffScreen(2, "Saving state...");
  delay(900);
  drawPowerOffScreen(1, "Shutting down...");
  delay(900);

  // Zero ALL secrets
  _pinLen = 0; memset(_pinBuf, 0, sizeof(_pinBuf));
  memset(_firstBootPinBuf, 0, sizeof(_firstBootPinBuf));
  memset(_firstBootPinConfirmBuf, 0, sizeof(_firstBootPinConfirmBuf));
  _failCount = 0;

  // Save playback state
  if (_selectedEntry >= 0 && _selectedEntry < _sortedCount) {
    _mp3Mgr.saveState(_sortedIndex[_selectedEntry], _player.currentBytePos(), _player.getVolume());
  }
  // Save boot state (folder + song + play-all) so the next boot restores it
  saveBootState();

  // Stop playback
  _player.stop();

  // Tear down hotspot
  if (_mode == PlayerMode::HOTSPOT) {
    _hotspot.stop();
    _mode = PlayerMode::NORMAL;
  }

  // Tear down USB Drive mode — remount the MP3 partition locally so the
  // saved playback state above (and any future boot) sees a consistent
  // filesystem rather than one left suspended for USB MSC.
  if (_mode == PlayerMode::USB_MSC) {
    _usbMsc.stop();
    _mode = PlayerMode::NORMAL;
  }

  delay(100);

  drawPowerOffScreen(0, "Deep sleep");
  delay(400);

  // Display OFF
  tft.sendCommand(0x28);  // DISPOFF
  delay(50);
  tft.sendCommand(0x10);  // SLPIN
  delay(50);

  // Remember that this run ended in a power-off, so the NEXT start of
  // ShadowTune shows the RESUMING screen. (Behind the dual-boot launcher
  // the chip restarts through the launcher, so the hardware wake cause is
  // lost and cannot be used for this.)
  {
    Preferences pwr;
    if (pwr.begin("st_power", false)) {
      pwr.putBool("slept", true);
      pwr.end();
    }
  }

  // Wake source: the OK button on the joystick pulls the ladder pin LOW
  // (same as SecureVault). The ladder idles HIGH, so waking on HIGH would
  // wake the chip immediately after it went to sleep.
  gpio_num_t wakePin = (PIN_LADDER_PIN >= 1 && PIN_LADDER_PIN <= 21)
                         ? (gpio_num_t)PIN_LADDER_PIN : GPIO_NUM_6;
  gpio_reset_pin(wakePin);
  esp_sleep_enable_ext0_wakeup(wakePin, 0);

  Serial.println("[POWER] Deep sleep. Wake: press OK button.");
  Serial.flush();
  delay(100);
  esp_deep_sleep_start();
}

void UiController::drawPowerOffScreen(int countdownSeconds, const char* statusMsg) {
  auto& tft = _disp.tft();
  tft.fillScreen(C_BG);

  int cx = SCREEN_W / 2;
  int cy = 60;

  // Crescent moon + Z
  tft.fillCircle(cx, cy, 22, C_ACCENT);
  tft.fillCircle(cx + 8, cy - 4, 20, C_BG);
  tft.setTextColor(C_ACCENT); tft.setTextSize(2);
  tft.setCursor(cx + 14, cy + 8); tft.print("z");

  // Headline
  tft.setTextColor(C_WHITE); tft.setTextSize(2);
  const char* headline = "POWERING OFF";
  int hw = strlen(headline) * 12;
  tft.setCursor((SCREEN_W - hw) / 2, 110);
  tft.print(headline);

  // Countdown
  if (countdownSeconds > 0) {
    tft.setTextColor(C_YELLOW); tft.setTextSize(4);
    char num[4];
    snprintf(num, sizeof(num), "%d", countdownSeconds);
    int nw = strlen(num) * 24;
    tft.setCursor((SCREEN_W - nw) / 2, 140);
    tft.print(num);
  } else {
    tft.setTextColor(C_GREY); tft.setTextSize(1);
    const char* gone = "going to sleep";
    int gw = strlen(gone) * 6;
    tft.setCursor((SCREEN_W - gw) / 2, 150);
    tft.print(gone);
  }

  // Status message
  if (statusMsg) {
    tft.setTextColor(C_GREY); tft.setTextSize(1);
    int mw = strlen(statusMsg) * 6;
    tft.setCursor((SCREEN_W - mw) / 2, 200);
    tft.print(statusMsg);
  }

  // Spring hint
  tft.setTextColor(C_DARKGREY); tft.setTextSize(1);
  const char* hint = "Press OK to wake";
  int hh = strlen(hint) * 6;
  tft.setCursor((SCREEN_W - hh) / 2, 220);
  tft.print(hint);
}

void UiController::drawResumeScreen(int phase) {
  auto& tft = _disp.tft();
  if (phase == 0) {
    tft.fillScreen(C_BG);

    int cx = SCREEN_W / 2;
    int cy = 60;
    // Horizon line
    tft.drawFastHLine(cx - 35, cy + 12, 70, C_GREY);
    // Sun
    int r = 4;
    if (phase >= 2) r = 8;
    if (phase >= 3) r = 12;
    if (phase >= 4) r = 14;
    tft.fillCircle(cx, cy, r, C_YELLOW);

    // Headline
    tft.setTextColor(C_WHITE); tft.setTextSize(2);
    const char* headline = "RESUMING";
    int hw = strlen(headline) * 12;
    tft.setCursor((SCREEN_W - hw) / 2, 110);
    tft.print(headline);
  }

  // Status message
  tft.fillRect(0, 140, SCREEN_W, 12, C_BG);
  tft.setTextColor(C_GREY); tft.setTextSize(1);
  const char* msg;
  switch (phase) {
    case 0:  msg = "Waking up...";       break;
    case 1:  msg = "Restoring display";  break;
    case 2:  msg = "Loading tracks";     break;
    case 3:  msg = "Starting player";    break;
    case 4:  msg = "Ready";             break;
    default: msg = "";                   break;
  }
  int mw = strlen(msg) * 6;
  tft.setCursor((SCREEN_W - mw) / 2, 140);
  tft.print(msg);

  // Animated dots
  if (phase < 4) {
    int cx = SCREEN_W / 2;
    int dotY = 170;
    tft.fillRect(cx - 20, dotY - 4, 40, 8, C_BG);
    for (int i = 0; i < 3; i++) {
      int dotX = cx - 12 + i * 12;
      uint16_t col = (i == phase % 3) ? C_ACCENT : C_DARKGREY;
      tft.fillCircle(dotX, dotY, 3, col);
    }
  } else {
    int cx = SCREEN_W / 2;
    tft.fillRect(cx - 20, 166, 40, 12, C_BG);
    tft.setTextColor(C_GREEN); tft.setTextSize(2);
    tft.setCursor(cx - 6, 166);
    tft.print("OK");
  }
}

void UiController::showResumeScreen() {
  _screen = Screen::RESUME;
  drawResumeScreen(0);
  delay(500);
  drawResumeScreen(1); delay(500);
  drawResumeScreen(2); delay(500);
  drawResumeScreen(3); delay(500);
  drawResumeScreen(4);
  delay(500);
}

#ifndef TOUCH_LONG_PRESS_MS
#define TOUCH_LONG_PRESS_MS 5000   // hold the touch pad this long to power off
#endif

// Holding the capacitive touch pad (TTP223) for 5 seconds powers the player
// off (deep sleep) immediately, on any screen. Short touches do nothing.
void UiController::checkTouchPowerOff() {
  if (_screen == Screen::POWER_OFF || _screen == Screen::RESUME) return;
  if (_btn.touchActive() && _btn.touchHoldDuration() >= TOUCH_LONG_PRESS_MS) {
    enterPowerOff();   // never returns (deep sleep)
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  TICK — main loop driver
//  Handles: audio playback, 1Hz status bar, battery read, auto-timeout,
//  button dispatch, touch dispatch, mode switch state machine,
//  hotspot tick, now playing screen update.
// ═══════════════════════════════════════════════════════════════════════════════
void UiController::tick() {
  unsigned long t = millis();

  if (!_modeMenuOpen) _disp.restoreFlashedButton();

  // ── Drive audio playback ───────────────────────────────────────────
  _prevPlayerState = _player.state();
  _player.tick();

  // ── Auto-advance on song end ──────────────────────────────────────
  // Detect PLAYING→STOPPED transition (song finished naturally).
  // User-initiated stops set _playAllActive=false before calling stop(),
  // so they won't trigger auto-advance.
  {
    PlaybackState curState = _player.state();
    if (_prevPlayerState == PlaybackState::PLAYING && curState == PlaybackState::STOPPED) {
      if (_playAllActive) {
        advanceToNextTrack();
      }
    }
  }

  // ── Deferred mode switch — MUST be first thing in tick() ───────────
  if (_modeSwitchState != ModeSwitchState::IDLE) {
    processModeSwitch();
    return;
  }

  // ── Hotspot tick ───────────────────────────────────────────────────
  if (_mode == PlayerMode::HOTSPOT) {
    _hotspot.tick();
    if (!_modeMenuOpen && _screen == Screen::HOTSPOT_INFO && t - _lastClockTick >= 500) {
      _lastClockTick = t;
      updateHotspotInfoScreen();
    }
  }

  // ── USB Drive mode tick ──────────────────────────────────────────────
  if (_mode == PlayerMode::USB_MSC) {
    _usbMsc.tick();
    if (!_modeMenuOpen && _screen == Screen::USB_INFO && t - _lastClockTick >= 500) {
      _lastClockTick = t;
      updateUsbInfoScreen();
    }
  }

  // ── Now Playing screen update ──────────────────────────────────────
  if (!_modeMenuOpen && _screen == Screen::NOW_PLAYING && t - _lastClockTick >= 500) {
    _lastClockTick = t;
    updateNowPlayingScreen();
  }

  // Volume: handled on button press only (no continuous hold)

  // ── Auto-timeout (goes to CLOCK screen, NOT back to playlist) ──────
  // When the user is idle for the configured timeout, the device drops
  // to a clock screen that also shows the currently-playing track. This
  // prevents the "blind" transition where music suddenly starts/continues
    // exempt (they have their own timeouts).
  t = millis();
  bool exempt = (_screen == Screen::FIRST_BOOT_PIN || _screen == Screen::SETTIME ||
                 _screen == Screen::POWER_OFF || _screen == Screen::RESUME ||
                 _screen == Screen::CLOCK ||
                 _mode == PlayerMode::HOTSPOT ||
                 _mode == PlayerMode::USB_MSC);
  uint32_t autoTimeoutMs = _mp3Mgr.getAutoTimeoutMs();
  if (!exempt && autoTimeoutMs > 0 && t - _lastActivity > autoTimeoutMs) {
    _pinLen = 0; memset(_pinBuf, 0, sizeof(_pinBuf));

    // Tear down hotspot on timeout
    if (_mode == PlayerMode::HOTSPOT) {
      _hotspot.stop();
      _mode = PlayerMode::NORMAL;
    }

    // Cancel any in-progress deferred mode switch
    if (_modeSwitchState != ModeSwitchState::IDLE) {
      _modeSwitchState = ModeSwitchState::IDLE;
      _modePendingSwitch = PlayerMode::NORMAL;
    }

    // Go to the clock screen (NOT the playlist) so the user can still
    // see what's playing if music is active.
    transitionTo(Screen::CLOCK);
  }

  // ── Debounced volume save (flushes to NVS ~800ms after last change) ─
  checkVolumeSave();

  // 5-second touch pad hold = power off (deep sleep)
  checkTouchPowerOff();

    // ── First-boot PIN screen: shake + error clear ─────────────────────
  if (_screen == Screen::FIRST_BOOT_PIN) {
    if (_shaking) drawPinDotsShaking();
    clearLockError();
  }

  // ── Clock screen refresh (1Hz + on-progress-change) ────────────────
  if (!_modeMenuOpen && _screen == Screen::CLOCK && t - _lastClockTick >= 1000) {
    _lastClockTick = t;
    updateClockScreen();
  }

  // ── Hotspot input screen: cursor blink ─────────────────────────────
  if (!_modeMenuOpen && _screen == Screen::HOTSPOT_INPUT &&
      t - _lastStatusBarTick >= 500) {
    _lastStatusBarTick = t;
    drawHotspotInputScreen();  // cheap redraw — includes blinking cursor
  }

  // ── Search screen cursor blink ─────────────────────────────────────
  if (!_modeMenuOpen && _screen == Screen::SEARCH && t - _lastClockTick >= 500) {
    _lastClockTick = t;
    auto& tft = _disp.tft();
    int cursorX = 8 + _searchQueryLen * 12;
    if (cursorX < SCREEN_W - 60) {
      tft.fillRect(cursorX, 28, 10, 16, C_PANEL);
      if ((millis() / 500) % 2 == 0 && _searchQueryLen < 31) {
        tft.fillRect(cursorX, 28, 10, 16, C_ACCENT);
      }
    }
    if (_searchKeyFlash >= 0 && (millis() - _searchKeyFlashTime >= 100)) {
      _searchKeyFlash = -1;
      drawSearchKeyboard();
    }
  }

  // ── Orientation ────────────────────────────────────────────────────
  if (t - _lastMpuPoll >= 50) {
    _lastMpuPoll = t;
    _mpu.poll();
    byte r = _mpu.detectRotation();
    if (r == 1 || r == 3) {
      if (r == _newRot) _rotStableCount++;
      else { _rotStableCount = 0; _newRot = r; }
      if (_rotStableCount >= 10 && _newRot != _curRot) {
        _curRot = _newRot;
        _disp.setRotation(_curRot);
        transitionTo(_screen);
        _rotStableCount = 0;
      }
    } else {
      _rotStableCount = 0;
    }
  }

  // ── Button ladder ──────────────────────────────────────────────────
  if (t - _lastBtnPoll >= BTN_POLL_MS) {
    _lastBtnPoll = t;
    _btn.poll();

    // ── Continuous scroll on playlist ────────────────────────────────
    if (_screen == Screen::PLAYLIST && !_modeMenuOpen) {
      BtnEvent curState = _btn.state();
      if (curState == BtnEvent::UP || curState == BtnEvent::DOWN) {
        if (_btn.pressed()) {
          _scrollHeld = true;
          _scrollDir = curState;
          _lastScrollTime = t;
          handlePlaylistButtons();
        } else if (_scrollHeld && _scrollDir == curState &&
                   t - _lastScrollTime >= SCROLL_REPEAT_MS) {
          _lastScrollTime = t;
          _lastActivity = t;
          if (curState == BtnEvent::UP) {
            if (_selectedEntry > 0) {
              _selectedEntry--;
              if (_selectedEntry < _listScroll) _listScroll = _selectedEntry;
              redrawList();
            }
          } else {
            if (_selectedEntry < _sortedCount - 1) {
              _selectedEntry++;
              if (_selectedEntry >= _listScroll + LIST_VISIBLE)
                _listScroll = _selectedEntry - LIST_VISIBLE + 1;
              redrawList();
            }
          }
        }
      } else {
        _scrollHeld = false;
      }
    } else {
      _scrollHeld = false;
    }

    // ── Button dispatch (rising edge) ──────────────────────
    if (_btn.pressed()) {
      _lastActivity = t;
      if (_modeMenuOpen) {
        BtnEvent e = _btn.state();
        if (e == BtnEvent::LEFT) closeModeMenu();
      } else {
        bool skipPlaylist = (_screen == Screen::PLAYLIST && _scrollHeld);
        if (!skipPlaylist) {
          switch (_screen) {
            case Screen::PLAYLIST:        handlePlaylistButtons();        break;
            case Screen::NOW_PLAYING:     handleNowPlayingButtons();      break;
            case Screen::SEARCH:          handleSearchButtons();          break;
            case Screen::FIRST_BOOT_PIN:  handleFirstBootPinButtons();    break;
            case Screen::SETTINGS:        handleSettingsButtons();        break;
            case Screen::HOTSPOT_INPUT:   handleHotspotInputButtons();    break;
            case Screen::CLOCK:           handleClockButtons();           break;
              default: break;
          }
        }
      }
    }
  }

  // ── 1Hz status bar refresh ─────────────────────────────────────────
  bool hasStatusBar = (_screen == Screen::PLAYLIST || _screen == Screen::NOW_PLAYING ||
                       _screen == Screen::HOTSPOT_INFO || _screen == Screen::HOTSPOT_CHANGE || _screen == Screen::HOTSPOT_INPUT ||
                       _screen == Screen::USB_INFO ||
                       _screen == Screen::SETTINGS || _screen == Screen::ABOUT ||
                       _screen == Screen::FIRST_BOOT_PIN || _screen == Screen::CLOCK ||
                       _screen == Screen::SEARCH);

  // ── Battery read every 5s ──────────────────────────────────────────
  if (_ina219.isOK() && t - _lastBatteryRead >= BATTERY_READ_MS) {
    _lastBatteryRead = t;
    uint8_t newPct = _ina219.getBatteryPercent();
    bool significantChange = (abs((int)newPct - (int)_batteryPercent) > 3);
    uint8_t prevPct = _batteryPercent;
    if (significantChange || prevPct == 0) {
      _batteryPercent = newPct;
    }
  }

  if (!_modeMenuOpen && hasStatusBar && t - _lastStatusBarTick >= 1000) {
    _lastStatusBarTick = t;
    drawStatusBar();
  }

  // ── Touch ──────────────────────────────────────────────────────────
  static bool wasTouching = false;
  static unsigned long lastTouchT = 0;
  int tx, ty;
  bool touching = _disp.getTouchPoint(tx, ty);

  if (touching && !wasTouching && t - lastTouchT > TOUCH_DEBOUNCE_MS) {
    wasTouching = true; lastTouchT = t; _lastActivity = t;

    if (_modeMenuOpen) {
      handleModeMenuTouch(tx, ty);
    } else if (hasStatusBar &&
               hitTest(tx, ty, MODE_BADGE_X, MODE_BADGE_Y, MODE_BADGE_W, MODE_BADGE_H)) {
      _modeMenuOpen = true;
      _modeMenuHost = _screen;
      drawModeMenu();
    } else if (hasStatusBar && _rtc.unixEpoch() < SANE_EPOCH && hitTest(tx, ty, 58, 4, 44, 12)) {
      transitionTo(Screen::SETTIME);
    } else {
      switch (_screen) {
        case Screen::PLAYLIST:        handlePlaylistTouch(tx, ty);        break;
        case Screen::SEARCH:          handleSearchTouch(tx, ty);          break;
        case Screen::NOW_PLAYING:     handleNowPlayingTouch(tx, ty);      break;
        case Screen::HOTSPOT_INFO:    handleHotspotInfoTouch(tx, ty);     break;
        case Screen::HOTSPOT_CHANGE:  handleHotspotChangeTouch(tx, ty);   break;
        case Screen::HOTSPOT_INPUT:   handleHotspotInputTouch(tx, ty);    break;
        case Screen::USB_INFO:        handleUsbInfoTouch(tx, ty);         break;
        case Screen::CLOCK:           handleClockTouch(tx, ty);           break;
        case Screen::FIRST_BOOT_PIN:  handleFirstBootPinTouch(tx, ty);    break;
        case Screen::SETTIME:         handleSetTimeTouch(tx, ty);         break;
        case Screen::SETTINGS:        handleSettingsTouch(tx, ty);        break;
        case Screen::ABOUT:           handleAboutTouch(tx, ty);           break;
        default: break;
      }
    }
  }
  if (!touching) wasTouching = false;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SEARCH SCREEN — QWERTY keyboard, live-filtered music track search
//  TWO MODES:
//    - Root level: search ALL music files across ALL folders (playlists)
//      Uses Mp3Manager::searchAllMusic() for recursive SD card walk.
//    - In a folder: search only music files in the current folder.
//  ONLY music files can appear in results — never folders, docs, photos, etc.
// ═══════════════════════════════════════════════════════════════════════════════

void UiController::drawSearchScreen() {
  auto& tft = _disp.tft();
  tft.fillRect(0, 0, SCREEN_W, SCREEN_H, C_BG);
  drawStatusBar();

  // Search input field (y=22-46)
  tft.fillRect(4, 22, SCREEN_W - 8, 24, C_PANEL);
  tft.drawRect(4, 22, SCREEN_W - 8, 24, C_DARKGREY);
  tft.setTextColor(C_WHITE); tft.setTextSize(2);
  tft.setCursor(8, 27);
  if (_searchQueryLen > 0) {
    tft.print(_searchQuery);
  } else {
    tft.setTextColor(C_GREY);
    tft.print(_searchFromRoot ? "Search all..." : "Type to search...");
  }
  // Blinking cursor
  if ((millis() / 500) % 2 == 0 && _searchQueryLen < 31) {
    int cursorX = 8 + _searchQueryLen * 12;
    tft.fillRect(cursorX, 28, 10, 16, C_ACCENT);
  }
  // Result count (right side)
  tft.setTextColor(C_GREY); tft.setTextSize(1);
  tft.setCursor(SCREEN_W - 50, 30);
  tft.printf("%d", _searchResultCount);

  // Results list (y=48-114)
  drawSearchResults();

  // Keyboard (y=116-240)
  drawSearchKeyboard();
}

void UiController::drawSearchResults() {
  auto& tft = _disp.tft();
  tft.fillRect(0, 48, SCREEN_W, 66, C_BG);

  if (_searchQueryLen == 0) {
    tft.setTextColor(C_GREY); tft.setTextSize(1);
    tft.setCursor(SCREEN_W / 2 - 48, 70);
    tft.print(_searchFromRoot ? "Search across all folders" : "Search this playlist");
    return;
  }
  if (_searchResultCount == 0) {
    tft.setTextColor(C_GREY); tft.setTextSize(1);
    tft.setCursor(SCREEN_W / 2 - 36, 70);
    tft.print("No matches found");
    return;
  }

  // Draw up to 3 results — ALWAYS from searchAllMusic() results
  int maxShow = _searchResultCount < 3 ? _searchResultCount : 3;
  for (int i = 0; i < maxShow; i++) {
    int y = 48 + i * 22;
    uint16_t bg = (i == _searchSelIdx) ? C_ACCENT : C_PANEL;
    uint16_t fg = (i == _searchSelIdx) ? C_BG : C_WHITE;
    tft.fillRect(4, y, SCREEN_W - 8, 20, bg);
    tft.drawRect(4, y, SCREEN_W - 8, 20, C_DARKGREY);
    tft.setTextColor(fg); tft.setTextSize(1);
    tft.setCursor(8, y + 3);

    char displayName[MAX_FILENAME_LEN];
    uint32_t sz = 0;

    // Always use searchAllMusic results
    const auto* sr = _mp3Mgr.searchResultAt(i);
    if (!sr) continue;

    // sr->name is just the filename (e.g. "song.mp3") — use it directly
    // For root mode, prepend the folder name from fullPath so the user
    // knows which playlist the song belongs to (e.g. "Rock/song")
    // For in-folder mode, just show the song name
    if (_searchFromRoot) {
      // Extract folder name from fullPath: "/Rock/song.mp3" → "Rock"
      const char* fp = sr->fullPath;
      // Find last '/' to get filename start
      const char* lastSlash = strrchr(fp, '/');
      if (lastSlash && lastSlash > fp) {
        // There's a folder above the file
        // Find the second-to-last '/' to get folder name
        const char* prevSlash = nullptr;
        for (const char* p = fp; p < lastSlash; p++) {
          if (*p == '/') prevSlash = p;
        }
        const char* folderStart = prevSlash ? prevSlash + 1 : fp;
        int folderLen = lastSlash - folderStart;
        // Build "Folder/song" display name
        int pos = 0;
        if (folderLen > 0 && folderLen < 40) {
          memcpy(displayName, folderStart, folderLen);
          pos = folderLen;
          displayName[pos++] = '/';
        }
        // Append filename without extension
        const char* songName = sr->name;
        int songLen = strlen(songName);
        const char* extDot = strrchr(songName, '.');
        if (extDot) songLen = extDot - songName;
        if (pos + songLen >= (int)sizeof(displayName)) songLen = sizeof(displayName) - 1 - pos;
        memcpy(displayName + pos, songName, songLen);
        pos += songLen;
        displayName[pos] = '\0';
      } else {
        // No folder — just use the filename
        strncpy(displayName, sr->name, sizeof(displayName) - 1);
        displayName[sizeof(displayName) - 1] = '\0';
        stripMusicExt(displayName);
      }
    } else {
      // In-folder mode: just show the song name
      strncpy(displayName, sr->name, sizeof(displayName) - 1);
      displayName[sizeof(displayName) - 1] = '\0';
      stripMusicExt(displayName);
    }
    sz = sr->fileSize;

    // File size (compute FIRST to know name width)
    char sizeBuf[12];
    if (sz >= 1048576) snprintf(sizeBuf, sizeof(sizeBuf), "%.1fMB", sz / 1048576.0);
    else if (sz >= 1024) snprintf(sizeBuf, sizeof(sizeBuf), "%.0fKB", sz / 1024.0);
    else snprintf(sizeBuf, sizeof(sizeBuf), "%luB", (unsigned long)sz);
    int sizeW = strlen(sizeBuf) * 6;
    int nameMaxW = SCREEN_W - 8 - sizeW - 4 - 8;  // 8 = left start, 4 = gap

    // Print name with word-wrap (up to 2 lines, 8px line height)
    tft.setTextColor(fg, bg); tft.setTextSize(1);
    printWrapped(tft, displayName, 8, y + 3, nameMaxW, 8, 2);

    // File size on right
    tft.setTextColor(i == _searchSelIdx ? C_BG : C_GREY);
    tft.setCursor(SCREEN_W - 8 - sizeW, y + 3);
    tft.print(sizeBuf);
  }
}

void UiController::drawSearchKeyboard() {
  const SearchKey* keys = _searchKeyboardMode ? SEARCH_KEYS_NUMBERS : SEARCH_KEYS_LETTERS;
  int keyCount = _searchKeyboardMode ? SEARCH_KEYS_NUMBERS_COUNT : SEARCH_KEYS_LETTERS_COUNT;

  for (int i = 0; i < keyCount; i++) {
    const SearchKey& k = keys[i];
    uint16_t col = C_BTN;
    uint16_t txtCol = C_WHITE;

    if (k.ch == 0) {
      if (strcmp(k.label, "<x") == 0) {
        col = C_BTN_DARK;
      } else if (strcmp(k.label, "SEARCH") == 0) {
        col = C_ACCENT; txtCol = C_BG;
      } else if (strcmp(k.label, "BACK") == 0) {
        col = C_BTN_DARK;
      }
    }

    if (_searchKeyFlash == i && (millis() - _searchKeyFlashTime < 100)) {
      uint16_t tmp = col; col = txtCol; txtCol = tmp;
    }

    _disp.drawBtn(k.x, k.y, k.w, k.h, k.label, col, 1, txtCol);
  }

  if (_searchKeyFlash >= 0 && (millis() - _searchKeyFlashTime >= 100)) {
    _searchKeyFlash = -1;
  }
}

void UiController::updateSearchResults() {
  _searchResultCount = 0;
  _searchSelIdx = 0;
  if (_searchQueryLen == 0) return;

  // ALWAYS use recursive search via searchAllMusic():
  //   - Root mode (at root): search from /MP3/ → finds ALL music on SD card
  //   - In-folder mode (inside a playlist): search from current folder → finds
  //     music in current folder AND all subfolders within it
  const char* rootPath = nullptr;  // nullptr = default MP3_FOLDER
  if (!_searchFromRoot) {
    // Inside a playlist: restrict search to current folder and its subfolders
    rootPath = _mp3Mgr.currentAbsolutePath();
  }
  _searchResultCount = _mp3Mgr.searchAllMusic(_searchQuery, rootPath);
  // Clamp to display max (3 visible rows, but allow up to 16 stored)
  if (_searchResultCount > 16) _searchResultCount = 16;
}

void UiController::handleSearchKeyPress(char c) {
  if (_searchQueryLen < 31) {
    _searchQuery[_searchQueryLen++] = c;
    _searchQuery[_searchQueryLen] = '\0';
    updateSearchResults();
    auto& tft = _disp.tft();
    tft.fillRect(4, 22, SCREEN_W - 8, 24, C_PANEL);
    tft.drawRect(4, 22, SCREEN_W - 8, 24, C_DARKGREY);
    tft.setTextColor(C_WHITE); tft.setTextSize(2);
    tft.setCursor(8, 27);
    tft.print(_searchQuery);
    tft.setTextColor(C_GREY); tft.setTextSize(1);
    tft.setCursor(SCREEN_W - 50, 30);
    tft.printf("%d", _searchResultCount);
    drawSearchResults();
  }
}

void UiController::handleSearchBackspace() {
  if (_searchQueryLen > 0) {
    _searchQueryLen--;
    _searchQuery[_searchQueryLen] = '\0';
    updateSearchResults();
    auto& tft = _disp.tft();
    tft.fillRect(4, 22, SCREEN_W - 8, 24, C_PANEL);
    tft.drawRect(4, 22, SCREEN_W - 8, 24, C_DARKGREY);
    tft.setTextColor(C_WHITE); tft.setTextSize(2);
    tft.setCursor(8, 27);
    if (_searchQueryLen > 0) {
      tft.print(_searchQuery);
    } else {
      tft.setTextColor(C_GREY);
      tft.print(_searchFromRoot ? "Search all..." : "Search here...");
    }
    tft.setTextColor(C_GREY); tft.setTextSize(1);
    tft.setCursor(SCREEN_W - 50, 30);
    tft.printf("%d", _searchResultCount);
    drawSearchResults();
  }
}

void UiController::handleSearchTouch(int tx, int ty) {
  // Check keyboard keys first
  const SearchKey* keys = _searchKeyboardMode ? SEARCH_KEYS_NUMBERS : SEARCH_KEYS_LETTERS;
  int keyCount = _searchKeyboardMode ? SEARCH_KEYS_NUMBERS_COUNT : SEARCH_KEYS_LETTERS_COUNT;

  for (int i = 0; i < keyCount; i++) {
    const SearchKey& k = keys[i];
    if (hitTest(tx, ty, k.x, k.y, k.w, k.h)) {
      _searchKeyFlash = i;
      _searchKeyFlashTime = millis();
      drawSearchKeyboard();

      if (k.ch != 0) {
        handleSearchKeyPress(k.ch);
      } else {
        if (strcmp(k.label, "<x") == 0) {
          handleSearchBackspace();
        } else if (strcmp(k.label, "123") == 0 || strcmp(k.label, "ABC") == 0) {
          _searchKeyboardMode = !_searchKeyboardMode;
          drawSearchKeyboard();
        } else if (strcmp(k.label, "SEARCH") == 0) {
          // No-op — search is already live
        } else if (strcmp(k.label, "BACK") == 0) {
          transitionTo(Screen::PLAYLIST);
          return;
        }
      }
      return;
    }
  }

  // Check results list (y=48-114) — ALWAYS use searchAllMusic results
  if (ty >= 48 && ty < 114 && _searchResultCount > 0) {
    int resultIdx = (ty - 48) / 22;
    if (resultIdx < _searchResultCount && resultIdx < 3) {
      const auto* sr = _mp3Mgr.searchResultAt(resultIdx);
      if (!sr) return;
      // Only play if it's a music file (defense-in-depth — searchAllMusic
      // should never return folders, but check anyway)
      if (!isMusicExt(sr->name)) return;
      // Store the actual song name+size for the NOW_PLAYING screen
      strncpy(_playingName, sr->name, MAX_FILENAME_LEN - 1);
      _playingName[MAX_FILENAME_LEN - 1] = '\0';
      _playingSize = sr->fileSize;
      _player.stop();
      _player.play(sr->fullPath);  // full absolute path from recursive search
      saveBootState();
      transitionTo(Screen::NOW_PLAYING);
      return;
    }
  }

  // Mode badge
  if (hitTest(tx, ty, MODE_BADGE_X, MODE_BADGE_Y, MODE_BADGE_W, MODE_BADGE_H)) {
    _modeMenuOpen = true;
    _modeMenuHost = Screen::SEARCH;
    drawModeMenu();
    return;
  }
}

// ── Physical button handling for search screen ──────────────────────────
void UiController::handleSearchButtons() {
  if (!_btn.pressed()) return;
  BtnEvent e = _btn.state();
  if (e == BtnEvent::UP) {
    // Scroll up in search results
    if (_searchSelIdx > 0) {
      _searchSelIdx--;
      drawSearchResults();
    }
  } else if (e == BtnEvent::DOWN) {
    // Scroll down in search results
    int maxSel = (_searchResultCount < 3 ? _searchResultCount : 3) - 1;
    if (_searchSelIdx < maxSel) {
      _searchSelIdx++;
      drawSearchResults();
    }
  } else if (e == BtnEvent::LEFT || e == BtnEvent::OK) {
    // LEFT/OK on a selected search result → play it
    // ALWAYS use searchAllMusic results with full paths
    if (_searchResultCount > 0 && _searchSelIdx < _searchResultCount && _searchSelIdx < 3) {
      const auto* sr = _mp3Mgr.searchResultAt(_searchSelIdx);
      if (sr && isMusicExt(sr->name)) {
        // Store the actual song name+size for the NOW_PLAYING screen
        strncpy(_playingName, sr->name, MAX_FILENAME_LEN - 1);
        _playingName[MAX_FILENAME_LEN - 1] = '\0';
        _playingSize = sr->fileSize;
        _player.stop();
        _player.play(sr->fullPath);
        saveBootState();
        transitionTo(Screen::NOW_PLAYING);
      }
    }
  } else if (e == BtnEvent::RIGHT) {
    // RIGHT → backspace (same as <x key)
    handleSearchBackspace();
  }
}

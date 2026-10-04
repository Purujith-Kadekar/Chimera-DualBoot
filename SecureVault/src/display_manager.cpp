#include "display_manager.h"
#include "ui_theme.h"
#include <string.h>
#include <math.h>

// ui_theme.h #undefs the C_* macros and declares the runtime color
// variables as extern. All draw calls in this file (drawBtn,
// wipeTransition, showBootSplash) now use the theme-aware variables.

bool DisplayManager::begin() {
  pinMode(TOUCH_CS, OUTPUT); digitalWrite(TOUCH_CS, HIGH);
  pinMode(TFT_CS, OUTPUT);   digitalWrite(TFT_CS, HIGH);
  pinMode(T_DO, INPUT);

  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, HIGH); delay(50);
  digitalWrite(TFT_RST, LOW);  delay(150);
  digitalWrite(TFT_RST, HIGH); delay(150);

  // Real hardware SPI bus on custom pins — this replaces the previous
  // bit-banged constructor and is the actual fix for slow screen redraws.
  //
  // CAVEAT: readTouchRaw() below bit-bangs TFT_MOSI/TFT_CLK directly with
  // digitalWrite() while these same two pins are also bound to this
  // hardware SPI peripheral. On the ESP32-S3's GPIO matrix this is
  // expected to work -- digitalWrite() overrides the peripheral's signal
  // on that pin for as long as you're driving it manually, and nothing
  // else uses the bus mid-touch-read since touch reads happen between
  // display draw calls, not concurrently with them. This was NOT an
  // issue in the original all-bit-banged version (Proper_code.txt) since
  // no hardware SPI peripheral was ever bound to these pins there. If
  // touch reads come back reliable in isolation but start glitching
  // specifically right after/during a display redraw, this shared-pin
  // interaction is the first place to look.
  _spi.begin(TFT_CLK, TFT_MISO, TFT_MOSI, TFT_CS);
  _tft.begin(TFT_SPI_HZ);

  _tft.sendCommand(ILI9341_SLPOUT); delay(150);
  _tft.sendCommand(ILI9341_DISPON); delay(150);
  // Rotation 1 = landscape, 180° from rotation 3. The boot splash now
  // matches the runtime orientation, so the user no longer sees the screen
  // flip after the MPU auto-rotation kicks in. Touch coordinates are
  // handled separately in getTouchPoint() (the else-branch already
  // produces the correct 180° transform).
  setRotation(1);
  return true;
}

void DisplayManager::setRotation(byte r) {
  _rot = r;
  _tft.setRotation(r);
}

// ============================================================================
//  Boot splash (SecureVault) -- vault-door ring sweeps around, a padlock
//  closes and "clicks" shut, SECUREVAULT fades in, segmented bolt-bar fills.
//  Pure black background, emerald green accent.
//  To change the colour theme, edit SPLASH_ACCENT / SPLASH_ACCENT_DIM below.
// ============================================================================
static inline uint16_t splashRgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Linear blend of two RGB565 colours, t = 0..255
static uint16_t splashMix(uint16_t a, uint16_t b, int t) {
  int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  int r = ar + ((br - ar) * t) / 255;
  int g = ag + ((bg - ag) * t) / 255;
  int bl = ab + ((bb - ab) * t) / 255;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

// One 3 px dot of the vault ring at a given angle (0 deg = top, clockwise)
static void splashRingDot(Adafruit_ILI9341& t, int cx, int cy, int R, int deg, uint16_t col) {
  float a = (deg - 90) * 0.0174533f;
  t.fillCircle(cx + (int)lroundf(cosf(a) * R), cy + (int)lroundf(sinf(a) * R), 1, col);
}

void DisplayManager::showBootSplash() {
  const uint16_t BG     = 0x0000;
  const uint16_t WHITE  = 0xFFFF;
  const uint16_t GREY   = 0x7BEF;
  const uint16_t DKGREY = 0x2945;
  const uint16_t SPLASH_ACCENT     = splashRgb(0, 230, 118);    // emerald green
  const uint16_t SPLASH_ACCENT_DIM = splashRgb(0, 80, 45);      // ring start / empty cells
  const uint16_t FLASH             = splashRgb(190, 255, 220);  // "click" flash
  const uint16_t SHACKLE           = splashRgb(205, 215, 210);  // steel

  const int cx = SCREEN_W / 2, cy = 84, R = 46;                 // vault ring
  const int FRAMES = 44, FRAME_MS = 50;                         // ~2.2 s total
  const int SWEEP_END = 22;                                     // ring fully drawn
  const int LOCK_START = 6, CLOSE_START = 14, CLOSE_END = 26;   // padlock appears / closes
  const int CLICK = 27;                                         // ring flashes
  const int TXT_START = 26, TXT_END = 38;                       // wordmark fade-in
  const int TXT_Y = 156, TXT_X = cx - (11 * 18) / 2;            // "SECUREVAULT" = 11 * 18 px
  const int SEGS = 12, SEG_W = 14, SEG_GAP = 4, SEG_Y = 212, SEG_H = 5;
  const int SEG_X0 = cx - (SEGS * SEG_W + (SEGS - 1) * SEG_GAP) / 2;

  _tft.fillScreen(BG);

  // Empty bolt-bar track
  for (int k = 0; k < SEGS; k++)
    _tft.fillRect(SEG_X0 + k * (SEG_W + SEG_GAP), SEG_Y, SEG_W, SEG_H, DKGREY);

  int drawnDeg = 0, nextTick = 0, litDrawn = 0;

  for (int f = 0; f < FRAMES; f++) {
    // ---- vault ring sweeps clockwise from the top; bolt ticks appear ----
    if (f < SWEEP_END) {
      int curDeg = ((f + 1) * 360) / SWEEP_END;
      for (; drawnDeg < curDeg; drawnDeg += 1)
        splashRingDot(_tft, cx, cy, R, drawnDeg, splashMix(SPLASH_ACCENT_DIM, SPLASH_ACCENT, (drawnDeg * 255) / 360));
      while (nextTick < 12 && nextTick * 30 <= drawnDeg) {
        float a = (nextTick * 30 - 90) * 0.0174533f;
        _tft.drawLine(cx + (int)lroundf(cosf(a) * (R + 6)),  cy + (int)lroundf(sinf(a) * (R + 6)),
                      cx + (int)lroundf(cosf(a) * (R + 12)), cy + (int)lroundf(sinf(a) * (R + 12)),
                      SPLASH_ACCENT_DIM);
        nextTick++;
      }
    }

    // ---- padlock: appears open, shackle drops in and locks ----
    if (f >= LOCK_START && f <= CLICK) {
      int L = 12;                                                   // shackle lift in px
      if (f >= CLOSE_END) {
        L = 0;
      } else if (f >= CLOSE_START) {
        int t = ((f - CLOSE_START) * 100) / (CLOSE_END - CLOSE_START);   // 0..100
        L = 12 - (12 * t * t) / 10000;                              // accelerates downward
      }
      _tft.fillRect(cx - 14, cy - 42, 28, 38, BG);                  // clear shackle area
      _tft.drawRoundRect(cx - 11, cy - 26 - L, 22, 30, 9, SHACKLE);
      _tft.drawRoundRect(cx - 10, cy - 25 - L, 20, 28, 8, SHACKLE);
      _tft.drawRoundRect(cx - 9,  cy - 24 - L, 18, 26, 7, SHACKLE);
      _tft.fillRoundRect(cx - 17, cy - 4, 34, 28, 4, SPLASH_ACCENT);           // body
      _tft.fillCircle(cx, cy + 8, 4, BG);                                      // keyhole
      _tft.fillRect(cx - 2, cy + 8, 4, 9, BG);
    }

    // ---- "click": ring flashes bright for two frames, then settles ----
    if (f == CLICK || f == CLICK + 1 || f == CLICK + 2) {
      uint16_t col = (f == CLICK + 2) ? SPLASH_ACCENT : FLASH;
      for (int d = 0; d < 360; d += 1) splashRingDot(_tft, cx, cy, R, d, col);
    }

    // ---- wordmark + tagline fade in ----
    if (f >= TXT_START && f <= TXT_END) {
      int t = ((f - TXT_START) * 255) / (TXT_END - TXT_START);
      _tft.setTextSize(3);
      _tft.setCursor(TXT_X, TXT_Y);
      _tft.setTextColor(splashMix(DKGREY, WHITE, t), BG);
      _tft.print("SECURE");
      _tft.setTextColor(splashMix(DKGREY, SPLASH_ACCENT, t), BG);
      _tft.print("VAULT");
      _tft.setTextSize(1);
      _tft.setTextColor(splashMix(DKGREY, GREY, t), BG);
      _tft.setCursor(cx - (19 * 6) / 2, 188);
      _tft.print("AIR-GAPPED SECURITY");
    }

    // ---- segmented bolt-bar fills across the whole animation ----
    int lit = ((f + 1) * SEGS) / FRAMES;
    for (; litDrawn < lit; litDrawn++)
      _tft.fillRect(SEG_X0 + litDrawn * (SEG_W + SEG_GAP), SEG_Y, SEG_W, SEG_H,
                    splashMix(SPLASH_ACCENT_DIM, SPLASH_ACCENT, (litDrawn * 255) / (SEGS - 1)));

    delay(FRAME_MS);
  }
  delay(200);
}


// Helper for the boot splash's halo (local to this file)
uint16_t DisplayManager::lerp565_helper(uint16_t c1, uint16_t c2, float t) {
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

void DisplayManager::drawBtn(int x, int y, int w, int h, const char* label, uint16_t col,
                              int textSize, uint16_t textCol) {
  _tft.fillRoundRect(x, y, w, h, 4, col);
  _tft.drawRoundRect(x, y, w, h, 4, C_ACCENT);
  int lw = strlen(label) * 6 * textSize;
  _tft.setTextColor(textCol);
  _tft.setTextSize(textSize);
  _tft.setCursor(x + (w - lw) / 2, y + (h - 8 * textSize) / 2);
  _tft.print(label);
}

void DisplayManager::triggerFlash(int x, int y, int w, int h, uint16_t col, const char* label, int textSize) {
  // v10.6 FIX: Use C_ACCENT as the press-flash fill color instead of
  // C_DARKGREY. In the Monochrome theme, C_DARKGREY == C_RED == 0x7BEF
  // (both are mid-gray), so when the OK button (idx 11) was pressed,
  // its flash fill (C_DARKGREY) became the SAME color as the adjacent
  // CLR button (which uses C_RED) — the two buttons visually merged
  // and OK appeared to "disappear" during the press. The user reported
  // this as "OK becomes invisible when pressed".
  //
  // C_ACCENT is the theme's high-contrast outline/highlight color, and
  // is ALWAYS chosen to be visibly distinct from every button color:
  //   - Air-Gapped: cyan (0x07FF) on dark cyan buttons / black bg
  //   - Monochrome: white (0xFFFF) on light/mid/dark gray buttons / black bg
  //   - Emerald:    green (0x07E0) on dark green buttons / black bg
  //   - Sunlight:   dark blue (0x021F) on gray buttons / white bg
  // Using C_ACCENT for the flash guarantees the press is visible in
  // every theme, against every button color, with no color collisions.
  //
  // The label is intentionally NOT drawn during the flash (the brief
  // disappearance of the text is part of the "pressed" feedback).
  _tft.fillRoundRect(x + 1, y + 1, w - 2, h - 2, 3, C_ACCENT);
  _flash = {true, x, y, w, h, col, label, textSize};
}

void DisplayManager::restoreFlashedButton() {
  if (!_flash.active) return;
  drawBtn(_flash.x, _flash.y, _flash.w, _flash.h, _flash.label, _flash.col, _flash.textSize);
  _flash.active = false;
}

void DisplayManager::wipeTransition() {
  const int bands = 8;
  const int bw = SCREEN_W / bands;
  for (int i = 0; i < bands; i++) {
    _tft.fillRect(i * bw, 0, bw, SCREEN_H, C_ACCENT);
  }
}

uint16_t DisplayManager::readTouchRaw(uint8_t cmd) {
  // Exact bit-bang from the last confirmed-working version (Proper_code).
  // Do NOT replace this with _spi.transfer() -- that reads GPIO39
  // (TFT_MISO), which the touch chip is not wired to on this board.
  uint16_t r = 0;
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(TOUCH_CS, LOW);
  delayMicroseconds(2);
  for (int i = 0; i < 8; i++) {
    digitalWrite(TFT_MOSI, (cmd & 0x80) ? HIGH : LOW);
    cmd <<= 1;
    digitalWrite(TFT_CLK, HIGH); delayMicroseconds(2);
    digitalWrite(TFT_CLK, LOW);  delayMicroseconds(2);
  }
  digitalWrite(TFT_CLK, HIGH); delayMicroseconds(2);
  digitalWrite(TFT_CLK, LOW);  delayMicroseconds(2);
  for (int i = 0; i < 12; i++) {
    digitalWrite(TFT_CLK, HIGH); delayMicroseconds(2);
    r <<= 1;
    if (digitalRead(T_DO)) r |= 1;
    digitalWrite(TFT_CLK, LOW);  delayMicroseconds(2);
  }
  for (int i = 0; i < 3; i++) {
    digitalWrite(TFT_CLK, HIGH); delayMicroseconds(2);
    digitalWrite(TFT_CLK, LOW);  delayMicroseconds(2);
  }
  digitalWrite(TOUCH_CS, HIGH);
  return r;
}

void DisplayManager::_touchSpiRelease() {
  _spi.end();
  pinMode(TFT_MOSI, OUTPUT);
  pinMode(TFT_CLK, OUTPUT);
}

void DisplayManager::_touchSpiAcquire() {
  pinMode(TFT_MOSI, OUTPUT);
  pinMode(TFT_CLK, OUTPUT);
  _spi.begin(TFT_CLK, TFT_MISO, TFT_MOSI, TFT_CS);
}

bool DisplayManager::getTouchPoint(int& px, int& py) {
  _touchSpiRelease();
  uint16_t z = readTouchRaw(0xB0);
  if (z < 100 || z == 4095) { _touchSpiAcquire(); return false; }
  uint16_t rx = readTouchRaw(0xD0);
  uint16_t ry = readTouchRaw(0x90);
  _touchSpiAcquire();
  if (rx == 4095 || ry == 4095) return false;

  // Primary orientation is now rotation 1 (landscape, 180° from rot 3).
  // The else-branch (rot 0/1/2) reverses both axes relative to the rot 3
  // mapping, which is exactly the correct 180° transform.
  if (_rot == 3) {
    px = map(ry, 250, 3850, 0, SCREEN_W);
    py = map(rx, 3850, 250, 0, SCREEN_H);
  } else {
    px = map(ry, 3850, 250, 0, SCREEN_W);
    py = map(rx, 250, 3850, 0, SCREEN_H);
  }
  px = constrain(px, 0, SCREEN_W - 1);
  py = constrain(py, 0, SCREEN_H - 1);
  return true;
}

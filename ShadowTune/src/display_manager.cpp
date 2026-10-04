#include "display_manager.h"
#include "gpio_config_manager.h"  // runtime PIN_* variables (GPIO config manager)
#include "ui_theme.h"
#include "mp3_player.h"  // mp3Player.tick() — see wipeTransition()
#include <string.h>

// ui_theme.h #undefs the C_* macros and declares the runtime color
// variables as extern. All draw calls in this file (drawBtn,
// wipeTransition, showBootSplash) now use the theme-aware variables.

bool DisplayManager::begin() {
  pinMode(PIN_TOUCH_CS, OUTPUT); digitalWrite(PIN_TOUCH_CS, HIGH);
  pinMode(PIN_TFT_CS, OUTPUT);   digitalWrite(PIN_TFT_CS, HIGH);
  pinMode(PIN_T_DO, INPUT);

  pinMode(PIN_TFT_RST, OUTPUT);
  digitalWrite(PIN_TFT_RST, HIGH); delay(50);
  digitalWrite(PIN_TFT_RST, LOW);  delay(150);
  digitalWrite(PIN_TFT_RST, HIGH); delay(150);

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
  _spi.begin(PIN_TFT_CLK, PIN_TFT_MISO, PIN_TFT_MOSI, PIN_TFT_CS);
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

void DisplayManager::showBootSplash() {
  _tft.fillScreen(C_BG);

  // ── Premium boot screen: music note logo + brand + animated dots ──────
  int cx = SCREEN_W / 2;
  int cy = 70;

  // Breathing halo behind the music note
  for (int i = 8; i >= 1; i--) {
    float t = (float)i / 8.0f;
    uint16_t c = lerp565_helper(C_BG, C_ACCENT, t * t * 0.5f);
    _tft.fillCircle(cx, cy, (int)(25 * t), c);
  }

  // Music note body (simplified)
  _tft.fillRoundRect(cx - 14, cy - 4, 28, 22, 3, C_ACCENT);
  // Music note stem
  _tft.fillRect(cx + 10, cy - 20, 3, 18, C_ACCENT);
  // Music note flag
  _tft.fillTriangle(cx + 13, cy - 20, cx + 13, cy - 10, cx + 22, cy - 14, C_ACCENT);

  // Brand text
  _tft.setTextColor(C_ACCENT); _tft.setTextSize(2);
  _tft.setCursor(cx - 54, 110); _tft.print("ShadowTune");
  _tft.setTextColor(C_GREY); _tft.setTextSize(1);
  _tft.setCursor(cx - 36, 128); _tft.print("EdgeHax S3-PRO");

  // Animated 3-dot boot indicator
  for (int cycle = 0; cycle < 12; cycle++) {  // ~2.4s total
    for (int i = 0; i < 3; i++) {
      int dotX = cx - 12 + i * 12;
      int dotY = 150;
      uint16_t col = (i == cycle % 3) ? C_ACCENT : C_DARKGREY;
      _tft.fillCircle(dotX, dotY, 3, col);
    }
    delay(80);
  }

  // Final fade — clear the dots
  _tft.fillRect(cx - 20, 145, 40, 12, C_BG);
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
  // and OK appeared to "disappear" during the press.
  _tft.fillRoundRect(x + 1, y + 1, w - 2, h - 2, 3, C_ACCENT);
  _flash = {true, x, y, w, h, col, label, textSize};
}

void DisplayManager::restoreFlashedButton() {
  if (!_flash.active) return;
  drawBtn(_flash.x, _flash.y, _flash.w, _flash.h, _flash.label, _flash.col, _flash.textSize);
  _flash.active = false;
}

void DisplayManager::wipeTransition() {
  // Interleave an audio tick() between each band instead of doing all 8
  // fillRect() calls back-to-back. Each band is a full-height, 40px-wide
  // SPI transfer; back-to-back with the target screen's own subsequent
  // draw, this loop was one of the bigger blocking-SPI-time stretches in
  // the whole UI -- long enough to occasionally outrun the DMA buffer's
  // margin during playback, heard as a brief glitch right as you navigate
  // away from the Now Playing / Clock screen ("sudden disturbance, then
  // it continues properly"). Calling tick() between bands keeps audio
  // flowing THROUGH the animation instead of only before and after it.
  // Safe to call even when nothing is playing -- tick() itself no-ops
  // immediately if the player isn't in the PLAYING state.
  const int bands = 8;
  const int bw = SCREEN_W / bands;
  for (int i = 0; i < bands; i++) {
    _tft.fillRect(i * bw, 0, bw, SCREEN_H, C_ACCENT);
    mp3Player.tick();
  }
}

uint16_t DisplayManager::readTouchRaw(uint8_t cmd) {
  // Exact bit-bang from the last confirmed-working version (Proper_code).
  // Do NOT replace this with _spi.transfer() -- that reads GPIO39
  // (TFT_MISO), which the touch chip is not wired to on this board.
  uint16_t r = 0;
  digitalWrite(PIN_TFT_CS, HIGH);
  digitalWrite(PIN_TOUCH_CS, LOW);
  delayMicroseconds(2);
  for (int i = 0; i < 8; i++) {
    digitalWrite(PIN_TFT_MOSI, (cmd & 0x80) ? HIGH : LOW);
    cmd <<= 1;
    digitalWrite(PIN_TFT_CLK, HIGH); delayMicroseconds(2);
    digitalWrite(PIN_TFT_CLK, LOW);  delayMicroseconds(2);
  }
  digitalWrite(PIN_TFT_CLK, HIGH); delayMicroseconds(2);
  digitalWrite(PIN_TFT_CLK, LOW);  delayMicroseconds(2);
  for (int i = 0; i < 12; i++) {
    digitalWrite(PIN_TFT_CLK, HIGH); delayMicroseconds(2);
    r <<= 1;
    if (digitalRead(PIN_T_DO)) r |= 1;
    digitalWrite(PIN_TFT_CLK, LOW);  delayMicroseconds(2);
  }
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_TFT_CLK, HIGH); delayMicroseconds(2);
    digitalWrite(PIN_TFT_CLK, LOW);  delayMicroseconds(2);
  }
  digitalWrite(PIN_TOUCH_CS, HIGH);
  return r;
}

void DisplayManager::_touchSpiRelease() {
  _spi.end();
  pinMode(PIN_TFT_MOSI, OUTPUT);
  pinMode(PIN_TFT_CLK, OUTPUT);
}

void DisplayManager::_touchSpiAcquire() {
  pinMode(PIN_TFT_MOSI, OUTPUT);
  pinMode(PIN_TFT_CLK, OUTPUT);
  _spi.begin(PIN_TFT_CLK, PIN_TFT_MISO, PIN_TFT_MOSI, PIN_TFT_CS);
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

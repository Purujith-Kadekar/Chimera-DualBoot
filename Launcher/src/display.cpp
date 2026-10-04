#include "display.h"

bool Display::begin() {
  pinMode(TOUCH_CS, OUTPUT); digitalWrite(TOUCH_CS, HIGH);
  pinMode(TFT_CS, OUTPUT);   digitalWrite(TFT_CS, HIGH);
  pinMode(T_DO, INPUT);

  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, HIGH); delay(20);
  digitalWrite(TFT_RST, LOW);  delay(60);
  digitalWrite(TFT_RST, HIGH); delay(100);

  _spi.begin(TFT_CLK, TFT_MISO, TFT_MOSI, TFT_CS);
  _tft.begin(TFT_SPI_HZ);

  _tft.sendCommand(ILI9341_SLPOUT); delay(120);
  _tft.sendCommand(ILI9341_DISPON); delay(50);

  _tft.setRotation(1);   // landscape, same default as SecureVault/ShadowTune
  _tft.fillScreen(0x0000);
  return true;
}

// ---- XPT2046 read: exact bit-bang timing from the apps -------------------
// The touch chip's DOUT is on T_DO (GPIO41), not on the display's MISO, so it
// cannot go through the SPI peripheral. MOSI/CLK are shared with the display.
uint16_t Display::readTouchRaw(uint8_t cmd) {
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

void Display::touchSpiRelease() {
  _spi.end();
  pinMode(TFT_MOSI, OUTPUT);
  pinMode(TFT_CLK, OUTPUT);
}

void Display::touchSpiAcquire() {
  pinMode(TFT_MOSI, OUTPUT);
  pinMode(TFT_CLK, OUTPUT);
  _spi.begin(TFT_CLK, TFT_MISO, TFT_MOSI, TFT_CS);
}

bool Display::getTouchPoint(int& px, int& py) {
  touchSpiRelease();
  uint16_t z = readTouchRaw(0xB0);                 // pressure
  if (z < 100 || z == 4095) { touchSpiAcquire(); return false; }
  uint16_t rx = readTouchRaw(0xD0);
  uint16_t ry = readTouchRaw(0x90);
  touchSpiAcquire();
  if (rx == 4095 || ry == 4095) return false;

  // Rotation 1 mapping (the "else" branch in the apps' getTouchPoint()).
  px = map(ry, 3850, 250, 0, SCREEN_W);
  py = map(rx, 250, 3850, 0, SCREEN_H);
  px = constrain(px, 0, SCREEN_W - 1);
  py = constrain(py, 0, SCREEN_H - 1);
  return true;
}

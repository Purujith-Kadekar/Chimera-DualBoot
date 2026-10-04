#pragma once
// ============================================================================
//  display.h -- ILI9341 (hardware SPI) + XPT2046 (bit-banged) driver.
//  Same wiring/timing as DisplayManager in the two apps.
// ============================================================================
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "board_pins.h"

class Display {
public:
  bool begin();
  Adafruit_ILI9341& tft() { return _tft; }

  // Returns true while the panel is being touched; px/py are screen pixels
  // (landscape, rotation 1 -- same mapping the apps use).
  bool getTouchPoint(int& px, int& py);

private:
  SPIClass _spi{FSPI};
  Adafruit_ILI9341 _tft{&_spi, TFT_DC, TFT_CS, TFT_RST};

  uint16_t readTouchRaw(uint8_t cmd);
  void touchSpiRelease();
  void touchSpiAcquire();
};

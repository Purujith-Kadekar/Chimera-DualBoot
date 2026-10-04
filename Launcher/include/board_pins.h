#pragma once
// ============================================================================
//  board_pins.h -- EdgeHax S3 Pro pin map + input thresholds for the launcher.
//  Values copied 1:1 from the SecureVault / ShadowTune board_config.h so the
//  launcher drives the exact same display, touch controller and joystick.
// ============================================================================
#include <Arduino.h>

// ---- ILI9341 TFT (hardware SPI, FSPI bus) ----
#define TFT_CS      4
#define TFT_RST     5
#define TFT_DC      7
#define TFT_MOSI    15
#define TFT_CLK     16
#define TFT_MISO    39
#define TFT_SPI_HZ  40000000UL

// ---- XPT2046 touch (shares MOSI/CLK with the display, own CS + DOUT) ----
#define TOUCH_CS    14
#define T_DO        41

// ---- TTP223 capacitive touch pad (HIGH while touched) ----
#define TOUCH_PAD_PIN  40

// ---- 5-way joystick (resistor ladder on one ADC pin) ----
#define LADDER_PIN  6
#define TH_OK_MAX      157
#define TH_RIGHT_MAX   489
#define TH_UP_MAX      935
#define TH_DOWN_MAX    1565
#define TH_LEFT_MAX    3009

// ---- Screen ----
#define SCREEN_W    320
#define SCREEN_H    240

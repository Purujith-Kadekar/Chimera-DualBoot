// ═══════════════════════════════════════════════════════════════════════════════
//  main.cpp — MP3 Player firmware (EdgeHax ESP32-S3 S3-PRO, N16R8)
//  Forked from SecureVault Password Manager v9.20-v5.3.1
//  Developed by Purujith Kadekar
// ═══════════════════════════════════════════════════════════════════════════════
//  Every feature is a standalone manager class in include/ + src/ — this file
//  only wires them together and runs the loop. See SecureVault's main.cpp for
//  the original wiring pattern.
//
//  Removed from SecureVault: vault_manager, crypto_utils, session_context,
//  duress_manager, totp_generator, ble_keyboard_manager, serial_protocol,
//  secure_session, ap_mode_manager + all web_* security layers, qr_display,
//  portal_html (vault webapp).
//
//  Added: mp3_manager, mp3_player, hotspot_manager.
// ═══════════════════════════════════════════════════════════════════════════════
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_sleep.h>
#include <Preferences.h>
#include <math.h>
#include <esp_heap_caps.h>
#include "board_config.h"
#include "ui_theme.h"       // F16: color variables (extern uint16_t C_*)
#include "display_manager.h"
#include "rtc_manager.h"
#include "mpu_manager.h"
#include "eeprom_manager.h"
#include "button_manager.h"
#include "sd_manager.h"
#include "mp3_manager.h"
#include "mp3_player.h"
#include "hotspot_manager.h"
#include "usb_msc_manager.h"
#include "ui_screens.h"
#include "diagnostics.h"
#include "ina219_manager.h"
#include "init_orchestrator.h"
#include "gpio_config_manager.h"

// ── Global instances ────────────────────────────────────────────────────────
DisplayManager     disp;
RtcManager         rtc;
MpuManager         mpu;
EepromManager      eeprom;
ButtonManager      btn;
SdManager          sd;
// mp3Mgr and mp3Player are defined in their own .cpp files (mp3_manager.cpp,
// mp3_player.cpp) and declared extern in their headers. Do NOT redefine here.
Ina219Manager       ina219;
GpioConfigManager   gpioCfg;

// HotspotManager is a singleton (same pattern as SecureVault's APModeManager)
// Its begin() is called in the init orchestrator.

// UsbMscManager is a singleton (same pattern as HotspotManager). Its
// begin() is called in the init orchestrator; start()/stop() are driven
// from the UI's mode menu (USB DRIVE).
UiController ui(disp, mp3Mgr, mp3Player, rtc, mpu, btn,
                HotspotManager::getInstance(), ina219,
                UsbMscManager::getInstance());

// ═══════════════════════════════════════════════════════════════════════════════
//  Init function wrappers for the orchestrator
// ═══════════════════════════════════════════════════════════════════════════════
static bool initSerial()      { Serial.setRxBufferSize(8192); Serial.begin(115200); delay(200); return true; }
static bool initDisplay()     { return disp.begin(); }
static bool initRTC()         { return rtc.begin(); }
static bool initMPU()         { return mpu.begin(); }
static bool initEEPROM()      { return eeprom.begin(); }
static bool initButtons()     { btn.begin(); return true; }
static bool initSD()          { return sd.begin(); }
static bool initMP3Mgr()      { mp3Mgr.begin(); return true; }
static bool initMP3Player()   { return mp3Player.begin(); }
static bool initINA219()      { return ina219.begin(); }
static bool initHotspot()     { return HotspotManager::getInstance().begin(); }
static bool initUsbMsc()      { return UsbMscManager::getInstance().begin(); }
static bool initGpioCfg()     { return gpioCfg.begin(); }

// Nothing in this firmware ever sounds the passive buzzer -- AudioManager
// (the LEDC tone driver written for it) is never instantiated or begin()'d
// anywhere in the app. That means PIN_BUZZER_PIN was left completely
// floating (default ESP32 reset state: high-impedance input) for the
// device's entire runtime. A floating passive-buzzer element wired
// straight to a GPIO acts like a small antenna -- it picks up capacitive/
// inductive crosstalk from the I2S bus's BCLK/LRCLK/MCLK lines toggling at
// audio-rate frequencies right next to it, and audibly buzzes in time with
// playback. Actively driving the pin to a solid LOW (real low-impedance
// output, not a floating gate) stops it from acting as an antenna at all.
// Needs PIN_BUZZER_PIN, so must run after GpioCfg has applied any override.
static bool initBuzzerSilence() {
  pinMode(PIN_BUZZER_PIN, OUTPUT);
  digitalWrite(PIN_BUZZER_PIN, LOW);
  return true;
}

// ============================================================================
//  Boot splash -- animated equalizer-bar logo, SHADOWTUNE wordmark, loading bar
//  Pure black background with a warm red-orange -> amber accent (no blue).
//  To change the colour theme, edit SPLASH_ACCENT_LO / SPLASH_ACCENT_HI below.
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

static void drawBootSplash(Adafruit_ILI9341& tft) {
  const uint16_t BG     = 0x0000;
  const uint16_t WHITE  = 0xFFFF;
  const uint16_t GREY   = 0x7BEF;
  const uint16_t DKGREY = 0x2945;
  const uint16_t SPLASH_ACCENT_LO = splashRgb(255, 56, 0);    // red-orange (bar base)
  const uint16_t SPLASH_ACCENT_HI = splashRgb(255, 196, 0);   // amber (bar tips)

  const int cx = SCREEN_W / 2;

  // Equalizer bars: 9 bars, tallest in the middle (the final "logo" shape)
  const int   NBARS = 9;
  const int   BAR_W = 10, BAR_GAP = 6;
  const int   BASE_Y = 118;                       // bottom edge of the bars
  const int   MAX_H = 80;
  const int   x0 = cx - (NBARS * BAR_W + (NBARS - 1) * BAR_GAP) / 2;
  const int   profile[NBARS] = { 22, 36, 52, 66, 80, 66, 52, 36, 22 };

  // Wordmark + loading bar geometry
  const int TXT_Y   = 136;                        // size 3 text: 24 px tall
  const int TXT_X   = cx - (10 * 18) / 2;         // "SHADOWTUNE" = 10 chars * 18 px
  const int BAR_Y   = 196, BAR_X = 60, BAR_LEN = 200, BAR_H = 4;

  const int FRAMES = 40;
  const int FRAME_MS = 55;                        // ~2.2 s total

  tft.fillScreen(BG);

  // Static elements: baseline, tagline, loading-bar track
  tft.drawFastHLine(x0 - 8, BASE_Y + 3, NBARS * BAR_W + (NBARS - 1) * BAR_GAP + 16, DKGREY);
  tft.setTextSize(1);
  tft.setTextColor(GREY, BG);
  tft.setCursor(cx - (14 * 6) / 2, 172);
  tft.print("EDGEHAX S3-PRO");
  tft.fillRoundRect(BAR_X, BAR_Y, BAR_LEN, BAR_H, 2, DKGREY);

  for (int f = 0; f < FRAMES; f++) {
    // ---- bars: dance, then ease into the symmetric logo shape ----
    float settle = (f < 28) ? 0.0f : (float)(f - 28) / (FRAMES - 1 - 28);   // 0 -> 1
    for (int i = 0; i < NBARS; i++) {
      float wave = 0.40f + 0.60f * fabsf(sinf(f * 0.45f + i * 0.85f));
      float amp  = wave + (1.0f - wave) * settle;
      int   h    = (int)(profile[i] * amp);
      if (h < 4) h = 4;
      int x = x0 + i * (BAR_W + BAR_GAP);

      tft.fillRect(x, BASE_Y - MAX_H, BAR_W, MAX_H + 1, BG);                 // clear column
      for (int y = 0; y < h; y += 4) {                                       // gradient in 4 px slices
        int sh = (h - y < 4) ? (h - y) : 4;
        int mix = (y * 255) / MAX_H;
        tft.fillRect(x, BASE_Y - y - sh + 1, BAR_W, sh, splashMix(SPLASH_ACCENT_LO, SPLASH_ACCENT_HI, mix));
      }
    }

    // ---- wordmark fades in over frames 8..22 ----
    if (f >= 8 && f <= 22) {
      int t = ((f - 8) * 255) / 14;
      tft.setTextSize(3);
      tft.setCursor(TXT_X, TXT_Y);
      tft.setTextColor(splashMix(DKGREY, WHITE, t), BG);
      tft.print("SHADOW");
      tft.setTextColor(splashMix(DKGREY, SPLASH_ACCENT_HI, t), BG);
      tft.print("TUNE");
    }

    // ---- loading bar fills across the whole animation ----
    int fill = ((f + 1) * BAR_LEN) / FRAMES;
    tft.fillRoundRect(BAR_X, BAR_Y, fill, BAR_H, 2, splashMix(SPLASH_ACCENT_LO, SPLASH_ACCENT_HI, (fill * 255) / BAR_LEN));

    delay(FRAME_MS);
  }
  delay(150);
}

void setup() {
  // ═════════════════════════════════════════════════════════════════════════════
  //  Phase 0: Serial + diagnostics
  // ═════════════════════════════════════════════════════════════════════════════
  Serial.setRxBufferSize(8192);
  Serial.begin(115200);
  delay(200);

  // Check if waking from deep sleep
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  bool fromDeepSleep = (wakeup_reason != ESP_SLEEP_WAKEUP_UNDEFINED);
  if (fromDeepSleep) {
    Serial.println("[BOOT] Waking from deep sleep.");
  }

  if (digitalRead(PIN_TOUCH_PIN) == HIGH && !fromDeepSleep) {
    Serial.println("\n[BOOT] Wake sensor held at power-on — entering diagnostics mode.");
    Diagnostics::runAll();
    Serial.println("[BOOT] Diagnostics complete. Reset the device to boot normally.");
    while (true) delay(1000);
  }

  // ═════════════════════════════════════════════════════════════════════════════
  //  Phase 1: LittleFS mount
  // ═════════════════════════════════════════════════════════════════════════════
  bool fsOK = false;
  static const char* LFS_BASE_PATH = "/littlefs";
  static const char* LFS_PARTITION_LABEL = "littlefs";

  fsOK = LittleFS.begin(false, LFS_BASE_PATH, 10, LFS_PARTITION_LABEL);
  if (fsOK) {
    Serial.println("[LittleFS] Mounted on first attempt.");
  } else {
    Serial.println("[LittleFS] Plain mount failed, attempting format...");
    LittleFS.format();
    fsOK = LittleFS.begin(false, LFS_BASE_PATH, 10, LFS_PARTITION_LABEL);
    if (fsOK) {
      Serial.println("[LittleFS] Formatted and mounted successfully.");
    } else {
      Serial.println("[LittleFS] Format+mount failed, trying format-on-fail...");
      fsOK = LittleFS.begin(true, LFS_BASE_PATH, 10, LFS_PARTITION_LABEL);
      if (fsOK) {
        Serial.println("[LittleFS] Mounted via format-on-fail.");
      } else {
        Serial.println("[LittleFS] ERROR: All mount attempts failed!");
      }
    }
  }

  // ═════════════════════════════════════════════════════════════════════════════
  //  Phase 2: Initialization Orchestrator (dependency-aware boot)
  // ═════════════════════════════════════════════════════════════════════════════
  InitOrchestrator initOrch;

  // GPIO Config Manager — MUST be the first component to initialize.
  // It reads the gpio_cfg flash partition and overrides runtime pin
  // variables (PIN_TFT_CS, PIN_RTC_SDA, etc.) before any other manager
  // uses them. If no config partition exists, all pins keep their
  // compile-time defaults — "flash and go" behavior.
  initOrch.addComponent("GpioCfg",        initGpioCfg,      false, nullptr);

  // Hold the unused passive buzzer pin actively LOW instead of leaving it
  // floating (see initBuzzerSilence() above for why that was buzzing
  // during playback). Needs the real GPIO number, so depends on GpioCfg.
  initOrch.addComponent("BuzzerSilence",  initBuzzerSilence, false, "GpioCfg");

  // No-dependency components
  initOrch.addComponent("Serial",         nullptr,          true, nullptr);
  initOrch.addComponent("LittleFS",       nullptr,          false, nullptr);  // soft — already mounted above
  initOrch.addComponent("Display",        initDisplay,      true, "GpioCfg");  // needs PIN_TFT_CS etc.
  initOrch.addComponent("RTC",            initRTC,          true, "GpioCfg");  // needs PIN_RTC_SDA etc.
  initOrch.addComponent("Buttons",        initButtons,      true, "GpioCfg");  // needs PIN_LADDER_PIN etc.

  // I2C bus components (depend on RTC bringing up I2C)
  initOrch.addComponent("MPU",            initMPU,          false, "RTC");    // soft — screen rotation is optional
  initOrch.addComponent("EEPROM",         initEEPROM,       false, "RTC");    // soft — same I2C bus
  initOrch.addComponent("INA219",         initINA219,       false, "RTC");    // soft — battery % is optional

  // SD card + MP3 subsystem
  initOrch.addComponent("SD",             initSD,           false, "GpioCfg");  // needs PIN_SD_CS etc.
  initOrch.addComponent("MP3Mgr",         initMP3Mgr,       false, "SD");     // needs SD for file listing
  initOrch.addComponent("MP3Player",      initMP3Player,    false, nullptr);  // needs I2S, not SD

  // Hotspot manager (singleton, empty shell until start() is called)
  initOrch.addComponent("Hotspot",        initHotspot,      false, nullptr);

  // USB MSC manager (singleton, drive stays hidden from the host until
  // the user enters USB Drive mode from the UI). Soft-depends on SD only
  // for boot-log ordering — begin() itself doesn't touch the card.
  initOrch.addComponent("UsbMsc",         initUsbMsc,       false, "SD");

  // Run the orchestrated init sequence.
  initOrch.run();

  // Restore volume from NVS
  {
    int dummyIdx;
    uint32_t dummyPos;
    uint8_t savedVol;
    if (mp3Mgr.loadState(dummyIdx, dummyPos, savedVol)) {
      mp3Player.setVolume(savedVol);
      Serial.printf("[BOOT] Restored volume from NVS: %u%%\n", savedVol);
    } else if (savedVol != DEFAULT_VOLUME) {
      mp3Player.setVolume(savedVol);
      Serial.printf("[BOOT] Restored volume from NVS (no last_idx): %u%%\n", savedVol);
    }
  }

  // ═════════════════════════════════════════════════════════════════════════════
  //  Phase 3: Post-init — deep sleep resume / boot splash
  // ═════════════════════════════════════════════════════════════════════════════
  // Resume screen is shown after a power-off. Behind the dual-boot launcher
  // the hardware wake cause is lost (the launcher restarts the chip), so
  // ShadowTune also leaves itself a flag in NVS right before it sleeps.
  bool resumeBoot = fromDeepSleep;
  {
    Preferences pwr;
    if (pwr.begin("st_power", false)) {
      if (pwr.getBool("slept", false)) {
        resumeBoot = true;
        pwr.putBool("slept", false);   // consume the flag (written only when set -> no flash wear)
      }
      pwr.end();
    }
  }

  if (resumeBoot) {
    Serial.println("[BOOT] Showing resume screen...");
    disp.tft().fillScreen(0x0000);
    disp.tft().sendCommand(ILI9341_SLPOUT); delay(150);
    disp.tft().sendCommand(ILI9341_DISPON); delay(150);
  } else {
    // Boot splash (animated logo, see drawBootSplash above)
    drawBootSplash(disp.tft());
  }

  // ═════════════════════════════════════════════════════════════════════════════
  //  Phase 4: Theme + UI
  // ═════════════════════════════════════════════════════════════════════════════
  ui.loadTheme();

  if (resumeBoot) {
    ui.showResumeScreen();
  }

  ui.begin();

  // ═════════════════════════════════════════════════════════════════════════════
  //  Phase 5: Boot summary
  // ═════════════════════════════════════════════════════════════════════════════
  bool rtcOK    = initOrch.isAvailable("RTC");
  bool sdOK     = initOrch.isAvailable("SD");
  bool audioOK  = initOrch.isAvailable("MP3Player");
  bool ina219OK = initOrch.isAvailable("INA219");
  bool gpioCfgOK = initOrch.isAvailable("GpioCfg");
  bool usbMscOK  = initOrch.isAvailable("UsbMsc");

  Serial.println("═══════════════════════════════════════════");
  Serial.println("  ShadowTune v1.5.0 — ready");
  Serial.printf("  GPIO Cfg: %s\n", gpioCfgOK ? "OK (custom pins)" : "DEFAULTS (no gpio_cfg partition)");
  Serial.printf("  RTC:     %s\n", rtcOK   ? "OK" : "NOT FOUND");
  Serial.printf("  EEPROM:  %s\n", initOrch.isAvailable("EEPROM") ? "OK" : "NOT FOUND");
  Serial.printf("  SD:      %s\n", sdOK    ? "OK" : "NOT PRESENT");
  Serial.printf("  Audio:   %s\n", audioOK ? "OK" : "NOT FOUND");
  Serial.printf("  INA219:  %s\n", ina219OK ? "OK" : "NOT FOUND (battery % hidden)");
  Serial.printf("  MP3s:    %d files\n", mp3Mgr.count());
  Serial.printf("  USB MSC: %s (enter USB Drive mode from the mode menu)\n", usbMscOK ? "ready" : "NOT READY");
  Serial.printf("  LittleFS: %s\n", fsOK ? "OK" : "MOUNT FAILED");
  Serial.println("═══════════════════════════════════════════");
}

void loop() {
  // Serial time-set: send  T<unix_timestamp>  e.g. T1735689600
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() > 1 && line[0] == 'T') {
      uint32_t epoch = strtoul(line.c_str() + 1, NULL, 10);
      if (epoch >= SANE_EPOCH) {
        rtc.writeRTCFromEpoch(epoch);
        Serial.printf("[RTC] Set to epoch %lu\n", (unsigned long)epoch);
      } else {
        Serial.println("[RTC] Rejected — timestamp before 2024-01-01");
      }
    }
  }

  // Drive the UI tick (handles audio playback, display, buttons, etc.)
  ui.tick();

  // Throttled heap breakdown
  static uint32_t lastHeapLog = 0;
  if (millis() - lastHeapLog > 30000) {
    lastHeapLog = millis();
    Serial.printf(
      "[HeapMon] total_free=%lu internal_free=%lu psram_free=%lu largest_block=%lu\n",
      (unsigned long)ESP.getFreeHeap(),
      (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
      (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
      (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
  }

  delay(8);
}

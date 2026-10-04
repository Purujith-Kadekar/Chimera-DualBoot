// ============================================================================
//  EdgeHax Launcher -- boot menu for SecureVault + ShadowTune
//  Runs from the factory partition on EVERY boot. Pick an app with the
//  joystick (LEFT/RIGHT/UP/DOWN to move, OK to launch) or tap its card.
// ============================================================================
#include <Arduino.h>
#include <string.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include "board_pins.h"
#include "display.h"
#include "input.h"
#include "boot_manager.h"

// ---- Palette (RGB565, matches the apps' default "Air-Gapped" theme) -------
static const uint16_t C_BG       = 0x0000;
static const uint16_t C_PANEL    = 0x1082;
static const uint16_t C_HEADER   = 0x000B;
static const uint16_t C_ACCENT   = 0x07FF;
static const uint16_t C_GREEN    = 0x07E0;
static const uint16_t C_RED      = 0xF800;
static const uint16_t C_WHITE    = 0xFFFF;
static const uint16_t C_GREY     = 0x7BEF;
static const uint16_t C_DKGREY   = 0x39E7;

// ---- Layout ----------------------------------------------------------------
static const int CARD_Y = 40;
static const int CARD_W = 146;
static const int CARD_H = 150;
static const int CARD_X[2] = { 10, 164 };

static Display display;
static Input   input(display);
static Preferences prefs;

static AppSlot slots[2] = {
  { "SecureVault", "Password Manager", ESP_PARTITION_SUBTYPE_APP_OTA_0, 0x07FF },
  { "ShadowTune",  "MP3 Player",       ESP_PARTITION_SUBTYPE_APP_OTA_1, 0xB81F },
};

static int selected = 0;

// ---- Small drawing helpers ----------------------------------------------------
static void printCentered(const char* s, int cx, int y, int size, uint16_t col) {
  Adafruit_ILI9341& t = display.tft();
  int w = (int)strlen(s) * 6 * size;
  t.setTextSize(size);
  t.setTextColor(col);
  t.setCursor(cx - w / 2, y);
  t.print(s);
}

static void drawPadlock(int cx, int cy, uint16_t col, uint16_t bg) {
  Adafruit_ILI9341& t = display.tft();
  t.drawRoundRect(cx - 10, cy - 20, 20, 24, 9, col);    // shackle (2px)
  t.drawRoundRect(cx - 9,  cy - 19, 18, 22, 8, col);
  t.fillRoundRect(cx - 16, cy - 2, 32, 26, 4, col);     // body
  t.fillCircle(cx, cy + 8, 3, bg);                      // keyhole
  t.fillRect(cx - 1, cy + 8, 3, 8, bg);
}

static void drawMusicNote(int cx, int cy, uint16_t col) {
  Adafruit_ILI9341& t = display.tft();
  t.fillCircle(cx - 10, cy + 14, 7, col);               // note heads
  t.fillCircle(cx + 12, cy + 10, 7, col);
  t.fillRect(cx - 5,  cy - 18, 3, 32, col);             // stems
  t.fillRect(cx + 17, cy - 22, 3, 32, col);
  t.fillTriangle(cx - 5, cy - 18, cx + 19, cy - 22, cx + 19, cy - 14, col);  // beam
  t.fillTriangle(cx - 5, cy - 18, cx - 5,  cy - 10, cx + 19, cy - 14, col);
}

static void drawHeader() {
  Adafruit_ILI9341& t = display.tft();
  t.fillRect(0, 0, SCREEN_W, 32, C_HEADER);
  t.drawFastHLine(0, 32, SCREEN_W, C_ACCENT);
  t.setTextSize(2);
  t.setTextColor(C_ACCENT);
  t.setCursor(8, 9);
  t.print("EDGEHAX S3 PRO");
  t.setTextSize(1);
  t.setTextColor(C_GREY);
  t.setCursor(SCREEN_W - 6 * 9 - 8, 12);
  t.print("BOOT MENU");
}

static void drawFooter() {
  Adafruit_ILI9341& t = display.tft();
  t.fillRect(0, 196, SCREEN_W, SCREEN_H - 196, C_BG);
  t.setTextSize(1);
  t.setTextColor(C_GREY);
  t.setCursor(10, 201);
  t.print("Joystick: move to choose, OK to launch");
  t.setCursor(10, 213);
  t.print("Screen: tap a card to launch");
  t.setCursor(10, 225);
  t.print("Touch pad: triple-tap to power off");
  t.setTextColor(C_DKGREY);
  t.setCursor(SCREEN_W - 6 * 6 - 8, 225);
  t.print("v1.1.0");
}

static void drawCard(int i) {
  Adafruit_ILI9341& t = display.tft();
  const AppSlot& s = slots[i];
  const bool sel = (i == selected);
  const int x = CARD_X[i], y = CARD_Y, w = CARD_W, h = CARD_H;
  const int cx = x + w / 2;

  // Card body + border (selected = thick accent border)
  t.fillRoundRect(x, y, w, h, 8, sel ? C_PANEL : C_BG);
  t.drawRoundRect(x, y, w, h, 8, sel ? C_ACCENT : C_DKGREY);
  if (sel) t.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 7, C_ACCENT);

  // Icon (dimmed if the app isn't installed)
  uint16_t ic = s.valid ? s.color : C_DKGREY;
  if (i == 0) drawPadlock(cx, y + 42, ic, sel ? C_PANEL : C_BG);
  else        drawMusicNote(cx, y + 44, ic);

  // Name, subtitle, build date
  printCentered(s.name, cx, y + 82, 2, s.valid ? C_WHITE : C_GREY);
  printCentered(s.subtitle, cx, y + 102, 1, C_GREY);
  if (s.valid && s.built[0]) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Built %s", s.built);
    printCentered(buf, cx, y + 114, 1, C_DKGREY);
  }

  // Status pill
  const char* st = s.valid ? "READY" : "NOT INSTALLED";
  uint16_t sc = s.valid ? C_GREEN : C_RED;
  t.fillRoundRect(x + 14, y + 126, w - 28, 18, 4, sc == C_GREEN ? 0x0320 : 0x4000);
  t.drawRoundRect(x + 14, y + 126, w - 28, 18, 4, sc);
  printCentered(st, cx, y + 131, 1, sc);
}

static void drawMenu() {
  display.tft().fillScreen(C_BG);
  drawHeader();
  drawCard(0);
  drawCard(1);
  drawFooter();
}

// ---- Full-screen message (launching / errors) --------------------------------
static void drawMessage(const char* title, const char* line1, const char* line2,
                        uint16_t col) {
  Adafruit_ILI9341& t = display.tft();
  t.fillScreen(C_BG);
  t.fillRect(0, 0, SCREEN_W, 32, C_HEADER);
  t.drawFastHLine(0, 32, SCREEN_W, col);
  printCentered(title, SCREEN_W / 2, 9, 2, col);
  if (line1) printCentered(line1, SCREEN_W / 2, 100, 2, C_WHITE);
  if (line2) printCentered(line2, SCREEN_W / 2, 135, 1, C_GREY);
}

// Pure power-off: screen goes dark, chip enters deep sleep. No animation, no
// message. Clicking the joystick (OK) wakes it and the menu comes up directly.
static void powerOff() {
  Adafruit_ILI9341& t = display.tft();
  t.fillScreen(0x0000);
  t.sendCommand(ILI9341_DISPOFF);
  t.sendCommand(ILI9341_SLPIN);

  // OK pulls the ladder pin LOW (idles HIGH), same wake method as the apps.
  gpio_reset_pin((gpio_num_t)LADDER_PIN);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)LADDER_PIN, 0);
  Serial.println("[Launcher] power off");
  Serial.flush();
  esp_deep_sleep_start();
}

static void saveLast(int i) {
  if (prefs.begin("launcher", false)) {
    prefs.putUChar("last", (uint8_t)i);
    prefs.end();
  }
}

static int loadLast() {
  int v = 0;
  if (prefs.begin("launcher", true)) {
    v = prefs.getUChar("last", 0);
    prefs.end();
  }
  return (v == 1) ? 1 : 0;
}

static void select(int i) {
  if (i == selected) return;
  int old = selected;
  selected = i;
  drawCard(old);
  drawCard(selected);
}

static void launch(int i) {
  const AppSlot& s = slots[i];
  Serial.printf("[Launcher] launch request: %s\n", s.name);

  if (!s.valid) {
    drawMessage("NOT INSTALLED", s.name, "Nothing flashed in this slot (see README)", C_RED);
    delay(2500);
    drawMenu();
    return;
  }

  drawMessage("LAUNCHING", s.name, "Verifying firmware image...", s.color);
  esp_err_t err = BootManager::selectNextBoot(s);
  if (err != ESP_OK) {
    Serial.printf("[Launcher] select failed: %s\n", esp_err_to_name(err));
    drawMessage("LAUNCH FAILED", s.name, "Image invalid or damaged - reflash it.", C_RED);
    delay(3000);
    drawMenu();
    return;
  }

  saveLast(i);
  drawMessage("LAUNCHING", s.name, "Starting...", s.color);
  delay(300);
  esp_restart();
}

// ============================================================================
void setup() {
  Serial.begin(115200);

  display.begin();
  input.begin();

  for (int i = 0; i < 2; i++) {
    BootManager::inspect(slots[i]);
    Serial.printf("[Launcher] slot %d %-12s part=%s valid=%d built=%s\n", i, slots[i].name,
                  slots[i].part ? slots[i].part->label : "none", slots[i].valid, slots[i].built);
  }

  selected = loadLast();
  drawMenu();
}

void loop() {
  InputEvent ev = input.poll();

  if (ev.key == Key::LEFT || ev.key == Key::UP ||
      ev.key == Key::RIGHT || ev.key == Key::DOWN) {
    select((selected + 1) % 2);       // two items: previous == next
  } else if (ev.key == Key::OK) {
    launch(selected);
  }

  if (ev.tripleTap) powerOff();   // never returns

  if (ev.tap) {
    for (int i = 0; i < 2; i++) {
      if (ev.x >= CARD_X[i] && ev.x < CARD_X[i] + CARD_W &&
          ev.y >= CARD_Y && ev.y < CARD_Y + CARD_H) {
        select(i);
        launch(i);
        break;
      }
    }
  }

  delay(10);
}

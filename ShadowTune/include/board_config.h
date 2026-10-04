#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  board_config.h — EdgeHax ESP32-S3 S3-PRO (N16R8) pin map & UI constants
//  MP3 Player Firmware — forked from SecureVault
//  Developed by Purujith Kadekar
// ═══════════════════════════════════════════════════════════════════════════════
#include <Arduino.h>

// ---- Display (ILI9341, hardware SPI — FSPI bus, shared with touch) ----
#define TFT_CS      4
#define TFT_RST     5
#define TFT_DC      7
#define TFT_MOSI    15
#define TFT_CLK     16
#define TFT_MISO    39
#define TFT_SPI_HZ  40000000UL

// ---- Touch (XPT2046, same physical SPI bus as the display, own CS) ----
#define TOUCH_CS    14
#define T_DO        41

// ---- RTC (DS3231) + MPU6050 + EEPROM — shared I2C bus ----
#define RTC_SDA     1
#define RTC_SCL     2
#define RTC_I2C_ADDR 0x68
#define MPU_ADDR    0x69
#define EEPROM_I2C_ADDR 0x57
#define INA219_I2C_ADDR 0x40
// PIN_INA219_ADDR is now a runtime variable (gpio_config_manager.h), not a macro.
// Default value is INA219_I2C_ADDR (0x40) via INA219_I2C_ADDR_DEFAULT.

// ---- Input ----
#define LADDER_PIN  6    // 5-switch resistor ladder (analog)
#define TOUCH_PIN   40   // TTP223 capacitive wake sensor
#define BUZZER_PIN  22   // Passive buzzer (LEDC PWM) — UI feedback sounds
// NOTE: Moved from GPIO 38 to GPIO 22 to avoid conflict with I2S_SDIN (GPIO 38)
// which is used by the CS4344 DAC. In SecureVault, GPIO 38 was the buzzer
// because I2S was not used; here, I2S audio output takes priority.

// ═══════════════════════════════════════════════════════════════════════════════
//  GPIO Config Manager — compile-time defaults for runtime pin variables
// ═══════════════════════════════════════════════════════════════════════════════
// gpio_config_manager.cpp initializes runtime pin variables (PIN_TFT_CS, etc.)
// from these *_DEFAULT macros. If a valid gpio_cfg flash partition exists, the
// manager overrides them at boot. Without these defaults, the GPIO config
// manager won't compile. These MUST match the #define values above.
#define TFT_CS_DEFAULT        TFT_CS
#define TFT_RST_DEFAULT       TFT_RST
#define TFT_DC_DEFAULT        TFT_DC
#define TFT_MOSI_DEFAULT      TFT_MOSI
#define TFT_CLK_DEFAULT       TFT_CLK
#define TFT_MISO_DEFAULT      TFT_MISO
#define TOUCH_CS_DEFAULT      TOUCH_CS
#define T_DO_DEFAULT          T_DO
#define RTC_SDA_DEFAULT       RTC_SDA
#define RTC_SCL_DEFAULT       RTC_SCL
#define LADDER_PIN_DEFAULT    LADDER_PIN
#define TOUCH_PIN_DEFAULT     TOUCH_PIN
#define BUZZER_PIN_DEFAULT    BUZZER_PIN
#define SD_CS_DEFAULT         SD_CS
#define SD_MOSI_DEFAULT       SD_MOSI
#define SD_SCK_DEFAULT        SD_SCK
#define SD_MISO_DEFAULT       SD_MISO
#define I2S_MCLK_DEFAULT      I2S_MCLK
#define I2S_SDIN_DEFAULT      I2S_SDIN
#define I2S_LRCLK_DEFAULT     I2S_LRCLK
#define I2S_BCLK_DEFAULT      I2S_BCLK
#define RTC_I2C_ADDR_DEFAULT  RTC_I2C_ADDR
#define MPU_ADDR_DEFAULT      MPU_ADDR
#define EEPROM_I2C_ADDR_DEFAULT EEPROM_I2C_ADDR
#define INA219_I2C_ADDR_DEFAULT  INA219_I2C_ADDR

// Resistor ladder thresholds — from Buttons.txt's final calibration pass
#define TH_OK_MAX      350    // OK button (center press on 5D joystick)
#define TH_SPRING_MAX  0      // Disabled (physical power switch, not on ADC)
#define TH_RIGHT_MAX   489
#define TH_UP_MAX      935
#define TH_DOWN_MAX    1565
#define TH_LEFT_MAX    3009

// ---- microSD (dedicated SPI bus — HSPI, separate from the display bus) ----
#define SD_CS       10
#define SD_MOSI     11
#define SD_SCK      12
#define SD_MISO     13

// ---- I2S audio out (CS4344 24-bit/192kHz stereo DAC) ----
#define I2S_MCLK    9    // CS4344 Master Clock
#define I2S_SDIN    38   // CS4344 Serial Data Input (exclusive — do NOT share with buzzer)
#define I2S_LRCLK   21   // CS4344 LRCK/WS (Word Select)
#define I2S_BCLK    42   // CS4344 BCLK (Bit Clock)
// Note: GPIO 42 BCLK causes on-board Green LED to flicker during audio.
// This is a hardware constraint and cannot be avoided.

// ═══════════════════════════════════════════════════════════════════════════════
//  UI LAYOUT
// ═══════════════════════════════════════════════════════════════════════════════
#define SCREEN_W     320
#define SCREEN_H     240
#define SBAR_H       20

// Numpad layout (reused for Dashboard PIN entry)
#define NUM_COLS     3
#define NUM_ROWS     4
#define NUM_BW       96
#define NUM_BH       38
#define NUM_GAP      3
#define NUM_X0       4
#define NUM_Y0       66

// Playlist list layout (same as vault list)
#define LIST_ITEM_H    38
#define LIST_Y0        44
#define LIST_VISIBLE   5

// Play All / Shuffle buttons (playlist header row, y = SBAR_H+1)
#define PLAYALL_BTN_X    126
#define PLAYALL_BTN_Y    (SBAR_H + 1)
#define PLAYALL_BTN_W    52
#define PLAYALL_BTN_H    22
#define SHUFFLE_BTN_X    182
#define SHUFFLE_BTN_Y    (SBAR_H + 1)
#define SHUFFLE_BTN_W    42
#define SHUFFLE_BTN_H    22

// Mode badge + battery
#define MODE_BADGE_X  (SCREEN_W - 80)
#define MODE_BADGE_Y  0
#define MODE_BADGE_W  28
#define MODE_BADGE_H  20

// Battery percentage display
#define BATT_ICON_X  270
#define BATT_ICON_Y  5
#define BATT_ICON_W  16
#define BATT_ICON_H  9
#define BATT_NIB_W   2
#define BATT_NIB_H   4
#define BATT_TEXT_X  290
#define BATT_TEXT_Y  6

// Now Playing screen layout
#define NP_PROGRESS_Y  130
#define NP_PROGRESS_H  8
#define NP_BTN_Y       155
#define NP_BTN_H       36
#define NP_BTN_W       70
#define NP_VOL_Y       200

// ═══════════════════════════════════════════════════════════════════════════════
//  TIMING
// ═══════════════════════════════════════════════════════════════════════════════
#define AUTO_TIMEOUT_MS    60000UL   // Auto-timeout (replaces auto-lock)
#define MAX_PIN_LEN        8
#define TOUCH_DEBOUNCE_MS  120
#define SANE_EPOCH         1704067200UL   // 2024-01-01

// Button poll interval
#define BTN_POLL_MS       50

// ═══════════════════════════════════════════════════════════════════════════════
//  MP3 PLAYER
// ═══════════════════════════════════════════════════════════════════════════════
#define MP3_FOLDER        "/"
#define MAX_MP3_FILES     256
#define MAX_FILENAME_LEN  128
#define VOLUME_HOLD_RATE_MS  200    // 1% per 200ms when holding vol+/vol-
#define DEFAULT_VOLUME    70

// Trash folder — files "deleted" from Hotspot/Dashboard are moved here
// instead of being permanently erased, so an accidental delete is
// recoverable. It's a plain top-level folder under MP3_FOLDER (not a
// FAT-hidden entry), so it shows up normally when the device is mounted
// over USB MSC — that's where the user permanently empties it.
#define TRASH_FOLDER_NAME ".trash"

// Prefix used for in-progress uploads. The uploader writes to
// "<prefix><final name>" and only renames to the real filename once the
// upload has been fully received and verified — so a power loss or
// dropped connection mid-upload leaves an orphaned staging file instead
// of a truncated file that looks like a normal (but corrupt) track.
// Mp3Manager::cleanupStaleUploads() removes any of these left over from
// a previous session, at boot.
#define UPLOAD_STAGING_PREFIX ".uploading_"

// How many multipart chunks to buffer between forced flush()es of the
// upload file. Lower = safer against sudden power loss, more SD wear/
// latency. 8 chunks at Arduino WebServer's ~1.4KB TCP MSS is roughly
// an 11KB worst-case loss window per flush.
#define UPLOAD_FLUSH_EVERY_N_CHUNKS 8

// NVS namespace for MP3 player settings
#define MP3_NVS_NAMESPACE "mp3player"

// Hotspot mode (AP mode)
#define AP_SSID_STATIC    "SecureVault-MP3"
#define AP_MAX_CLIENTS    1
#define AP_TX_POWER_DBM   WIFI_POWER_8_5dBm
#define AP_IDLE_TIMEOUT_MS 300000UL   // 5-min idle auto-off

// ═══════════════════════════════════════════════════════════════════════════════
//  PALETTE (RGB565)
// ═══════════════════════════════════════════════════════════════════════════════
// F16 FIX: Removed all #define color macros. Color values are now defined
// in ui_theme.h. See SecureVault's ui_theme.h for the full explanation.
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  error_framework.h — F15: Formal Error Handling Framework
// ═══════════════════════════════════════════════════════════════════════════════
//  Purpose: Replaces ad-hoc error handling with a structured framework that
//  provides:
//
//    1. Error severity enum: INFO, WARN, ERROR, CRITICAL
//    2. Error code enum for each subsystem
//    3. logError(severity, code, message) function that logs to Serial
//    4. LastError struct that tracks the last error per subsystem
//    5. Optional ring buffer of recent errors for diagnostics
//    6. begin() functions that returned void now return bool
//    7. Return values of begin() calls checked in main.cpp setup()
//
//  Design choices:
//    - Lightweight: no dynamic allocation, fixed-size buffers.
//    - Thread-safe: each subsystem has its own LastError slot (no shared
//      mutex needed for per-subsystem reads).
//    - Log function is a simple Serial.printf wrapper — no complex
//      formatting infrastructure.
//    - Ring buffer is optional and small (16 entries) — enough for
//      diagnostics, not enough to waste memory.
//
//  Usage:
//    logError(ErrSeverity::ERROR, ErrCode::VAULT_SAVE_FAILED, "AES-GCM tag mismatch");
//    LastError err = getLastError(ErrSubsystem::ERR_VAULT);
//    if (err.code != ErrCode::NONE) { ... handle error ... }
// ═══════════════════════════════════════════════════════════════════════════════
#include <Arduino.h>

// ── Error severity ──────────────────────────────────────────────────────────
enum class ErrSeverity : uint8_t {
  INFO     = 0,   // informational, no action needed
  WARN     = 1,   // warning, may need attention
  ERROR    = 2,   // error, operation failed but system can continue
  CRITICAL = 3,   // critical, system cannot continue safely
};

// ── Error subsystems ────────────────────────────────────────────────────────
enum class ErrSubsystem : uint8_t {
  // All prefixed with ERR_ to avoid clashes with Arduino.h macros
  ERR_DISPLAY     = 0,
  ERR_RTC         = 1,
  ERR_MPU         = 2,
  ERR_EEPROM      = 3,
  ERR_BUTTONS     = 4,
  ERR_SD          = 5,
  ERR_AUDIO       = 6,
  ERR_INA219      = 13,
  ERR_LITTLEFS    = 14,
  ERR_SETTINGS    = 22,
  ERR_GENERAL     = 23,
  ERR_MAX_SUBSYSTEM = 24,
};

// ── Error codes (per subsystem, using 16-bit for namespace + code) ────────
// Format: high byte = subsystem, low byte = specific error code.
enum class ErrCode : uint16_t {
  // ── General (0x0000) ──────────────────
  NONE              = 0x0000,
  UNKNOWN           = 0x00FF,

  // ── Display (0x0000) ──────────────────
  DISPLAY_INIT_FAILED     = 0x0001,
  DISPLAY_SPI_FAILED      = 0x0002,

  // ── RTC (0x0100) ──────────────────────
  RTC_INIT_FAILED         = 0x0101,
  RTC_I2C_FAILED          = 0x0102,
  RTC_CLOCK_NOT_SET       = 0x0103,

  // ── MPU (0x0200) ──────────────────────
  MPU_INIT_FAILED         = 0x0201,
  MPU_I2C_FAILED          = 0x0202,

  // ── EEPROM (0x0300) ──────────────────
  EEPROM_INIT_FAILED      = 0x0301,
  EEPROM_I2C_FAILED       = 0x0302,

  // ── SD (0x0500) ──────────────────────
  SD_INIT_FAILED          = 0x0501,
  SD_MOUNT_FAILED         = 0x0502,
  SD_FILE_NOT_FOUND       = 0x0503,
  SD_WRITE_FAILED         = 0x0504,
  SD_READ_FAILED          = 0x0505,

  // ── Audio (0x0600) ──────────────────
  AUDIO_INIT_FAILED       = 0x0601,
  AUDIO_I2S_FAILED        = 0x0602,

  // ── INA219 (0x0D00) ─────────────────
  INA219_INIT_FAILED      = 0x0D01,
  INA219_I2C_FAILED       = 0x0D02,

  // ── LittleFS (0x0E00) ───────────────
  LITTLEFS_MOUNT_FAILED   = 0x0E01,
  LITTLEFS_FORMAT_FAILED  = 0x0E02,

  // ── Settings (0x1600) ──────────────
  SETTINGS_INIT_FAILED    = 0x1601,
  SETTINGS_NVS_FAILED     = 0x1602,
};

// ── LastError struct ────────────────────────────────────────────────────────
struct LastError {
  ErrSeverity severity;
  ErrCode code;
  unsigned long timestamp;   // millis() when the error was logged
  char message[64];          // brief human-readable description
};

// ── Ring buffer for recent errors ──────────────────────────────────────────
#define EF_RING_SIZE 16

struct ErrorRingEntry {
  ErrSeverity severity;
  ErrCode code;
  ErrSubsystem subsystem;
  unsigned long timestamp;
  char message[64];
};

// ── Error framework functions ───────────────────────────────────────────────

void logError(ErrSeverity severity, ErrCode code, const char* message);
void logError(ErrSeverity severity, ErrCode code, ErrSubsystem subsystem, const char* message);
LastError getLastError(ErrSubsystem subsystem);
void clearLastError(ErrSubsystem subsystem);
void printErrorRing();

const char* errSeverityStr(ErrSeverity s);
const char* errCodeStr(ErrCode c);

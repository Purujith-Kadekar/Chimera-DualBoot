#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  settings_manager.h — F11: Unified Settings/Persistence Layer (MP3 Player)
// ═══════════════════════════════════════════════════════════════════════════════
//  Purpose: A lightweight FACADE over the storage backends (NVS, LittleFS).
//  Provides a single `get(key, default)` and `set(key, value)` interface
//  for all settings. Each key has an associated storage backend declared
//  in a key→backend mapping table.
//
//  Simplified from SecureVault's settings_manager:
//    - NO PIN setting, NO factory reset
//    - MP3-specific keys: theme_id, auto_timeout_ms, volume, last_idx, last_pos
//    - Uses NVS namespace "mp3player"
//
//  Storage backends:
//    NVS     — small key-value pairs (theme ID, auto-timeout, volume,
//              last track index/position). Fast, survives reboot.
//    LittleFS — files on the "littlefs" flash partition (touch calibration,
//              config files). Medium speed, survives reboot.
// ═══════════════════════════════════════════════════════════════════════════════
#include <Arduino.h>
#include <Preferences.h>
#include <LittleFS.h>

// ── Storage backend enum ────────────────────────────────────────────────────
enum class SettingsBackend : uint8_t {
  NVS      = 0,
  LITTLEFS = 1,
};

// ── Key→backend mapping entry ──────────────────────────────────────────────
struct SettingsKeyMapping {
  const char* key;            // e.g. "mp3.volume"
  SettingsBackend backend;    // which backend stores this key
  const char* nvsNamespace;   // only meaningful for NVS keys
  const char* nvsKey;         // only meaningful for NVS keys
  const char* filePath;       // only meaningful for LittleFS keys
};

// ── Maximum settings keys ──────────────────────────────────────────────────
#define SM_MAX_KEYS 16

// ── Settings value type ────────────────────────────────────────────────────
// Settings can be strings or integers. We use a tagged union to handle both.
struct SettingsValue {
  enum Type : uint8_t { STRING, INTEGER, BOOL_VAL, NOT_FOUND };
  Type type = NOT_FOUND;
  String strVal;
  int32_t intVal = 0;
  bool boolVal = false;
};

class SettingsManager {
public:
  SettingsManager() = default;

  // ── Initialization ────────────────────────────────────────────────────
  // Must be called AFTER LittleFS is mounted (in main.cpp setup()
  // after those mount steps). Initializes the key mapping table.
  bool begin();

  // ── Key registration ─────────────────────────────────────────────────
  // Add a key→backend mapping. Called during begin() for all known keys,
  // and can be called later for new keys added by future features.
  bool registerKey(const char* key, SettingsBackend backend,
                   const char* nvsNamespace = nullptr,
                   const char* nvsKey = nullptr,
                   const char* filePath = nullptr);

  // ── Unified get/set interface ─────────────────────────────────────────
  SettingsValue get(const char* key);

  // Convenience getters that return a default if not found.
  String getString(const char* key, const String& defaultValue = "");
  int32_t getInt(const char* key, int32_t defaultValue = 0);
  bool getBool(const char* key, bool defaultValue = false);

  // set(): writes the value to the appropriate backend for the given key.
  // Returns true on success, false on failure.
  bool set(const char* key, const String& value);
  bool setInt(const char* key, int32_t value);
  bool setBool(const char* key, bool value);

  // ── Diagnostics ──────────────────────────────────────────────────────
  void printKeyMap() const;

  // Returns the backend for a given key, or NVS if the key isn't registered.
  SettingsBackend getBackendForKey(const char* key) const;

  // Returns the number of registered keys.
  int getKeyCount() const { return _keyCount; }

private:
  SettingsKeyMapping _keys[SM_MAX_KEYS];
  int _keyCount = 0;

  // NVS Preferences handle — opened/closed per operation (NVS is not
  // thread-safe with a persistent handle when multiple tasks access it).
  bool _nvsGet(const char* namespace_, const char* key, SettingsValue& out);
  bool _nvsSet(const char* namespace_, const char* key, const String& value);
  bool _nvsSetInt(const char* namespace_, const char* key, int32_t value);
  bool _nvsSetBool(const char* namespace_, const char* key, bool value);

  bool _littlefsGet(const char* filePath, SettingsValue& out);
  bool _littlefsSet(const char* filePath, const String& value);

  // Find a key mapping by key name. Returns nullptr if not found.
  const SettingsKeyMapping* _findKey(const char* key) const;
};

// ── Global settings manager instance ────────────────────────────────────────
extern SettingsManager settingsMgr;

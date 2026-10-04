// ═══════════════════════════════════════════════════════════════════════════════
//  settings_manager.cpp — F11: Unified Settings/Persistence Layer (MP3 Player)
// ═══════════════════════════════════════════════════════════════════════════════
//  Simplified from SecureVault — no PIN setting, no factory reset, no SD backend.
//  MP3-specific keys: theme_id, auto_timeout_ms, volume, last_idx, last_pos.
// ═══════════════════════════════════════════════════════════════════════════════
#include "settings_manager.h"
#include "board_config.h"

SettingsManager settingsMgr;

bool SettingsManager::begin() {
  Serial.println("[F11] SettingsManager::begin() — registering MP3 key mapping table");

  // ── NVS-backed settings (MP3 player namespace) ───────────────────────
  registerKey("mp3.theme_id",        SettingsBackend::NVS, MP3_NVS_NAMESPACE, "theme_id");
  registerKey("mp3.auto_timeout_ms",  SettingsBackend::NVS, MP3_NVS_NAMESPACE, "auto_timeout_ms");
  registerKey("mp3.volume",           SettingsBackend::NVS, MP3_NVS_NAMESPACE, "volume");
  registerKey("mp3.last_idx",         SettingsBackend::NVS, MP3_NVS_NAMESPACE, "last_idx");
  registerKey("mp3.last_pos",         SettingsBackend::NVS, MP3_NVS_NAMESPACE, "last_pos");
  registerKey("mp3.last_folder",      SettingsBackend::NVS, MP3_NVS_NAMESPACE, "last_folder");
  registerKey("mp3.last_song",        SettingsBackend::NVS, MP3_NVS_NAMESPACE, "last_song");
  registerKey("mp3.last_playall",     SettingsBackend::NVS, MP3_NVS_NAMESPACE, "last_playall");

  // ── LittleFS-backed settings ─────────────────────────────────────────
  registerKey("touch.calibration",    SettingsBackend::LITTLEFS, nullptr, nullptr, "/littlefs/touch_cal.ini");

  Serial.printf("[F11] Registered %d settings keys\n", _keyCount);
  return true;
}

bool SettingsManager::registerKey(const char* key, SettingsBackend backend,
                                   const char* nvsNamespace,
                                   const char* nvsKey,
                                   const char* filePath) {
  if (_keyCount >= SM_MAX_KEYS) return false;
  if (!key) return false;

  // Check for duplicate keys.
  for (int i = 0; i < _keyCount; i++) {
    if (strcmp(_keys[i].key, key) == 0) return false;  // already registered
  }

  _keys[_keyCount].key = key;
  _keys[_keyCount].backend = backend;
  _keys[_keyCount].nvsNamespace = nvsNamespace;
  _keys[_keyCount].nvsKey = nvsKey;
  _keys[_keyCount].filePath = filePath;
  _keyCount++;
  return true;
}

// ── Get/set implementation ──────────────────────────────────────────────

const SettingsKeyMapping* SettingsManager::_findKey(const char* key) const {
  for (int i = 0; i < _keyCount; i++) {
    if (strcmp(_keys[i].key, key) == 0) return &_keys[i];
  }
  return nullptr;
}

SettingsBackend SettingsManager::getBackendForKey(const char* key) const {
  const SettingsKeyMapping* m = _findKey(key);
  return m ? m->backend : SettingsBackend::NVS;  // default to NVS
}

SettingsValue SettingsManager::get(const char* key) {
  const SettingsKeyMapping* m = _findKey(key);
  if (!m) {
    SettingsValue v;
    v.type = SettingsValue::NOT_FOUND;
    return v;
  }

  switch (m->backend) {
    case SettingsBackend::NVS:
      {
        SettingsValue v;
        if (_nvsGet(m->nvsNamespace, m->nvsKey, v)) return v;
        v.type = SettingsValue::NOT_FOUND;
        return v;
      }
    case SettingsBackend::LITTLEFS:
      {
        SettingsValue v;
        if (_littlefsGet(m->filePath, v)) return v;
        v.type = SettingsValue::NOT_FOUND;
        return v;
      }
    default:
      {
        SettingsValue v;
        v.type = SettingsValue::NOT_FOUND;
        return v;
      }
  }
}

String SettingsManager::getString(const char* key, const String& defaultValue) {
  SettingsValue v = get(key);
  if (v.type == SettingsValue::NOT_FOUND) return defaultValue;
  if (v.type == SettingsValue::STRING) return v.strVal;
  if (v.type == SettingsValue::INTEGER) return String(v.intVal);
  if (v.type == SettingsValue::BOOL_VAL) return v.boolVal ? "true" : "false";
  return defaultValue;
}

int32_t SettingsManager::getInt(const char* key, int32_t defaultValue) {
  SettingsValue v = get(key);
  if (v.type == SettingsValue::INTEGER) return v.intVal;
  if (v.type == SettingsValue::STRING) return v.strVal.toInt();
  return defaultValue;
}

bool SettingsManager::getBool(const char* key, bool defaultValue) {
  SettingsValue v = get(key);
  if (v.type == SettingsValue::BOOL_VAL) return v.boolVal;
  if (v.type == SettingsValue::INTEGER) return v.intVal != 0;
  return defaultValue;
}

bool SettingsManager::set(const char* key, const String& value) {
  const SettingsKeyMapping* m = _findKey(key);
  if (!m) return false;

  switch (m->backend) {
    case SettingsBackend::NVS:
      return _nvsSet(m->nvsNamespace, m->nvsKey, value);
    case SettingsBackend::LITTLEFS:
      return _littlefsSet(m->filePath, value);
    default:
      return false;
  }
}

bool SettingsManager::setInt(const char* key, int32_t value) {
  const SettingsKeyMapping* m = _findKey(key);
  if (!m) return false;

  if (m->backend == SettingsBackend::NVS) {
    return _nvsSetInt(m->nvsNamespace, m->nvsKey, value);
  }
  // For non-NVS backends, store as string.
  return set(key, String(value));
}

bool SettingsManager::setBool(const char* key, bool value) {
  const SettingsKeyMapping* m = _findKey(key);
  if (!m) return false;

  if (m->backend == SettingsBackend::NVS) {
    return _nvsSetBool(m->nvsNamespace, m->nvsKey, value);
  }
  return set(key, value ? "true" : "false");
}

// ── NVS helpers ──────────────────────────────────────────────────────────

bool SettingsManager::_nvsGet(const char* namespace_, const char* key, SettingsValue& out) {
  if (!namespace_ || !key) return false;
  Preferences prefs;
  if (!prefs.begin(namespace_, false)) return false;

  // Try to read as string first (NVS stores most values as strings).
  String s = prefs.getString(key, "");
  if (s.length() > 0) {
    out.type = SettingsValue::STRING;
    out.strVal = s;
    // Check if it's actually an integer.
    if (s.length() <= 10) {  // int32 max is 10 digits
      bool allDigits = true;
      for (unsigned int i = 0; i < s.length(); i++) {
        if (i == 0 && s[0] == '-') continue;
        if (!isdigit(s[i])) { allDigits = false; break; }
      }
      if (allDigits) {
        out.type = SettingsValue::INTEGER;
        out.intVal = s.toInt();
      }
    }
    prefs.end();
    return true;
  }

  // Try as int.
  int32_t iv = prefs.getInt(key, -1);
  if (iv != -1) {
    out.type = SettingsValue::INTEGER;
    out.intVal = iv;
    prefs.end();
    return true;
  }

  // Try as bool.
  bool bv = prefs.getBool(key, false);
  // NVS getBool returns the default if not found, which is ambiguous.
  prefs.end();
  return false;
}

bool SettingsManager::_nvsSet(const char* namespace_, const char* key, const String& value) {
  if (!namespace_ || !key) return false;
  Preferences prefs;
  if (!prefs.begin(namespace_, false)) return false;
  bool ok = prefs.putString(key, value) > 0;
  prefs.end();
  return ok;
}

bool SettingsManager::_nvsSetInt(const char* namespace_, const char* key, int32_t value) {
  if (!namespace_ || !key) return false;
  Preferences prefs;
  if (!prefs.begin(namespace_, false)) return false;
  bool ok = prefs.putInt(key, value) > 0;
  prefs.end();
  return ok;
}

bool SettingsManager::_nvsSetBool(const char* namespace_, const char* key, bool value) {
  if (!namespace_ || !key) return false;
  Preferences prefs;
  if (!prefs.begin(namespace_, false)) return false;
  bool ok = prefs.putBool(key, value) > 0;
  prefs.end();
  return ok;
}

// ── LittleFS helpers ────────────────────────────────────────────────────

bool SettingsManager::_littlefsGet(const char* filePath, SettingsValue& out) {
  if (!filePath) return false;
  File f = LittleFS.open(filePath, "r");
  if (!f) return false;
  String content = f.readString();
  f.close();

  out.type = SettingsValue::STRING;
  out.strVal = content;
  return true;
}

bool SettingsManager::_littlefsSet(const char* filePath, const String& value) {
  if (!filePath) return false;
  File f = LittleFS.open(filePath, "w");
  if (!f) return false;
  size_t written = f.print(value);
  f.close();
  return written > 0;
}

// ── Diagnostics ──────────────────────────────────────────────────────────

void SettingsManager::printKeyMap() const {
  Serial.println("═══════════ F11: Settings Key Mapping Table ═══════════");
  for (int i = 0; i < _keyCount; i++) {
    const SettingsKeyMapping& m = _keys[i];
    Serial.printf("  %s → ", m.key);
    switch (m.backend) {
      case SettingsBackend::NVS:      Serial.printf("NVS(%s/%s)", m.nvsNamespace, m.nvsKey); break;
      case SettingsBackend::LITTLEFS: Serial.printf("LittleFS(%s)", m.filePath); break;
    }
    Serial.println();
  }
  Serial.println("═══════════════════════════════════════════════════════════");
}

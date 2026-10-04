#include "mp3_manager.h"
#include "board_config.h"
#include "gpio_config_manager.h"
#include "sd_manager.h"    // runtime PIN_SD_CS
#include <string.h>
#include <strings.h>      // strcasecmp
#include <mbedtls/sha256.h>

// ═══════════════════════════════════════════════════════════════════════════════
//  mp3_manager.cpp — MP3 file/folder listing + NVS-backed settings
// ═══════════════════════════════════════════════════════════════════════════════
//  Scans /MP3/ (and any subfolders) on the SD card. Folders appear as
//  first-class entries alongside .mp3 files. A "current folder path" is
//  maintained so the playlist always shows the contents of one folder.
//
//  NVS keys (namespace "mp3player"):
//    theme_id, auto_tout_ms, volume, last_idx, last_pos, pin_hash, first_boot,
//    hotspot_ssid, hotspot_pass
// ═══════════════════════════════════════════════════════════════════════════════

// ── NVS key names (all live in the MP3_NVS_NAMESPACE = "mp3player") ────────
static const char* KEY_THEME       = "theme_id";
static const char* KEY_TIMEOUT     = "auto_tout_ms";
static const char* KEY_VOLUME      = "volume";
static const char* KEY_LAST_IDX    = "last_idx";
static const char* KEY_LAST_POS    = "last_pos";
static const char* KEY_PIN_HASH    = "pin_hash";
static const char* KEY_FIRST_BOOT  = "first_boot";
static const char* KEY_HS_SSID     = "hotspot_ssid";
static const char* KEY_HS_PASS     = "hotspot_pass";
static const char* KEY_LAST_FOLDER = "last_folder";
static const char* KEY_LAST_SONG   = "last_song";
static const char* KEY_LAST_PLAYALL= "last_playall";

// Default hotspot credentials — used only if NVS has none stored yet.
static const char* DEFAULT_HS_SSID = "SecureVault-MP3";
static const char* DEFAULT_HS_PASS = "12345678";

// Sentinel value for "no PIN has been set yet".
static const char* PIN_UNSET_SENTINEL = "UNSET";

// ── CRC-16/CCITT for shuffle NVS key hashing ─────────────────────────────
// Used to create short (≤15 char) NVS keys from folder paths.
static uint16_t crc16(const char* data) {
  uint16_t crc = 0xFFFF;
  if (!data) return crc;
  while (*data) {
    crc ^= (uint16_t)((unsigned char)*data++) << 8;
    for (int i = 0; i < 8; i++) {
      if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
      else crc <<= 1;
    }
  }
  return crc;
}

// ── Global instance ────────────────────────────────────────────────────────
Mp3Manager mp3Mgr;

// ── Internal helper: compute SHA256 hex digest of a null-terminated string ─
static void sha256Hex(const char* input, char out[65]) {
  uint8_t digest[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);  // 0 = SHA-256 (not SHA-224)
  mbedtls_sha256_update(&ctx,
                        reinterpret_cast<const unsigned char*>(input),
                        strlen(input));
  mbedtls_sha256_finish(&ctx, digest);
  mbedtls_sha256_free(&ctx);

  static const char hex[] = "0123456789abcdef";
  for (int i = 0; i < 32; i++) {
    out[i * 2]     = hex[digest[i] >> 4];
    out[i * 2 + 1] = hex[digest[i] & 0x0F];
  }
  out[64] = '\0';
}

// ── Internal helper: case-insensitive check for music file extensions ──────
// Supports: .mp3, .wav, .aac, .ogg, .flac, .wma, .m4a, .opus, .mid, .midi
static bool isMusicExtension(const char* filename) {
  if (!filename) return false;
  const char* dot = strrchr(filename, '.');
  if (!dot) return false;
  return strcasecmp(dot, ".mp3")  == 0 ||
         strcasecmp(dot, ".wav")  == 0 ||
         strcasecmp(dot, ".aac")  == 0 ||
         strcasecmp(dot, ".ogg")  == 0 ||
         strcasecmp(dot, ".flac") == 0 ||
         strcasecmp(dot, ".wma")  == 0 ||
         strcasecmp(dot, ".m4a")  == 0 ||
         strcasecmp(dot, ".opus") == 0 ||
         strcasecmp(dot, ".mid")  == 0 ||
         strcasecmp(dot, ".midi") == 0;
}

// Legacy alias for backwards compatibility with other code
static bool isMp3Extension(const char* filename) {
  return isMusicExtension(filename);
}

// ── Internal helper: strip directory prefix from a path ───────────────────
//   e.g. "/MP3/Rock/song.mp3" → "song.mp3", "Rock" → "Rock"
static const char* baseNameOf(const char* path) {
  if (!path) return "";
  const char* slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  begin()
// ═══════════════════════════════════════════════════════════════════════════════
bool Mp3Manager::begin() {
  sd_switch_to_mp3();
  _count = 0;
  _sortedCount = 0;
  _sdOk = false;
  _depth = 0;
  _relPath[0] = '\0';
  strcpy(_absPath, MP3_FOLDER);

  // Verify the /MP3/ folder exists on the SD card.
  File mp3Dir = SD.open(MP3_FOLDER);
  if (!mp3Dir || !mp3Dir.isDirectory()) {
    Serial.println("[Mp3Manager] /MP3/ folder not found");
    if (mp3Dir) mp3Dir.close();
    return false;
  }
  mp3Dir.close();

  _sdOk = true;

  // Remove any staging files left behind by an upload that was cut off by
  // a power loss / crash / dropped connection last session — they're
  // incomplete and would otherwise sit there invisibly forever.
  cleanupStaleUploads();

  int found = scanCurrentFolder();
  buildSortedIndex();

  Serial.printf("[Mp3Manager] Initialized: %d entries in %s\n", found, _absPath);
  return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  rescan()
// ═══════════════════════════════════════════════════════════════════════════════
bool Mp3Manager::rescan() {
  sd_switch_to_mp3();
  if (!_sdOk) {
    return begin();
  }
  _count = 0;
  _sortedCount = 0;
  int found = scanCurrentFolder();
  buildSortedIndex();
  Serial.printf("[Mp3Manager] Rescan: %d entries in %s\n", found, _absPath);
  return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  scanCurrentFolder() — populate _entries[] from the folder at _absPath
// ═══════════════════════════════════════════════════════════════════════════════
//  IMPORTANT: This function ONLY reads filenames from the current folder.
//  It NEVER touches vault.db or any file outside /MP3/. The SD.open() call
//  is constrained to _absPath which always starts with MP3_FOLDER.
//
//  Folders ARE included in the listing (with isFolder=true). Files without
//  a .mp3 extension are skipped.
int Mp3Manager::scanCurrentFolder() {
  sd_switch_to_mp3();
  File dir = SD.open(_absPath);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return 0;
  }

  _count = 0;
  File entry;
  while ((entry = dir.openNextFile())) {
    const char* name = entry.name();
    if (!name || name[0] == '\0') {
      entry.close();
      continue;
    }

    // Strip any leading path prefix that SD.openNextFile() may include.
    // On some ESP32 SD implementations, entry.name() returns "/MP3/Rock/song.mp3";
    // on others just "song.mp3". We want just the basename.
    const char* baseName = baseNameOf(name);

    // Skip "." and ".." pseudo-entries (defensive — SD lib doesn't usually
    // emit these, but better safe than sorry).
    if (strcmp(baseName, ".") == 0 || strcmp(baseName, "..") == 0) {
      entry.close();
      continue;
    }

    // Skip the trash folder and any in-progress-upload staging file — both
    // are internal bookkeeping, never real tracks to browse/play. Anything
    // dot-prefixed follows the same "hidden" convention.
    if (baseName[0] == '.') {
      entry.close();
      continue;
    }

    bool isDir = entry.isDirectory();

    // Skip system folders that should never appear in the playlist
    if (isDir) {
      if (strcasecmp(baseName, "System Volume Information") == 0 ||
          strcasecmp(baseName, "$RECYCLE.BIN") == 0 ||
          strcasecmp(baseName, "lost+found") == 0 ||
          strncasecmp(baseName, ".Trashes", 8) == 0) {
        entry.close();
        continue;
      }
    }

    // If it's a file, only accept music formats (mp3/wav/aac/ogg/flac/wma/m4a/etc)
    // Skip everything else — documents, photos, system files, vault.db, etc.
    if (!isDir) {
      if (!isMusicExtension(baseName)) {
        entry.close();
        continue;
      }
      if (strcasecmp(baseName, "vault.db") == 0) {
        entry.close();
        continue;
      }
    }

    // Check capacity
    if (_count >= MAX_MP3_FILES) {
      Serial.printf("[Mp3Manager] Max entry count (%d) reached, skipping rest\n",
                    MAX_MP3_FILES);
      entry.close();
      break;
    }

    // Store the entry
    Mp3Entry& e = _entries[_count];
    strncpy(e.name, baseName, MAX_FILENAME_LEN - 1);
    e.name[MAX_FILENAME_LEN - 1] = '\0';
    e.fileSize = isDir ? 0 : entry.size();
    e.isFolder = isDir;
    _count++;

    entry.close();
  }
  dir.close();
  return _count;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  buildSortedIndex() — sort _entries via _sortedIndex
// ═══════════════════════════════════════════════════════════════════════════════
//  Folders are listed BEFORE files (typical file-browser convention).
//  Within each group, entries are sorted alphabetically (case-insensitive).
void Mp3Manager::buildSortedIndex() {
  _sortedCount = _count;
  for (int i = 0; i < _count; i++) {
    _sortedIndex[i] = i;
  }

  // Comparator-less insertion sort: folders first, then alphabetical.
  auto entryLess = [&](int aIdx, int bIdx) -> bool {
    const Mp3Entry& a = _entries[aIdx];
    const Mp3Entry& b = _entries[bIdx];
    if (a.isFolder != b.isFolder) return a.isFolder;  // folders first
    return strcasecmp(a.name, b.name) < 0;
  };

  for (int i = 1; i < _count; i++) {
    int keyIdx = _sortedIndex[i];
    int j = i - 1;
    while (j >= 0 && entryLess(keyIdx, _sortedIndex[j])) {
      _sortedIndex[j + 1] = _sortedIndex[j];
      j--;
    }
    _sortedIndex[j + 1] = keyIdx;
  }

  _sorted = true;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  rebuildAbsolutePath() — rebuild _absPath from MP3_FOLDER + _relPath
// ═══════════════════════════════════════════════════════════════════════════════
void Mp3Manager::rebuildAbsolutePath() {
  if (_relPath[0] == '\0') {
    strcpy(_absPath, MP3_FOLDER);
  } else {
    snprintf(_absPath, sizeof(_absPath), "%s/%s", MP3_FOLDER, _relPath);
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  enterFolder() — navigate into a sub-folder
// ═══════════════════════════════════════════════════════════════════════════════
bool Mp3Manager::enterFolder(int index) {
  sd_switch_to_mp3();
  if (index < 0 || index >= _sortedCount) return false;
  const Mp3Entry* e = &_entries[_sortedIndex[index]];
  if (!e->isFolder) return false;
  if (_depth >= MAX_FOLDER_DEPTH) {
    Serial.println("[Mp3Manager] Max folder depth reached");
    return false;
  }

  // Append "/folderName" to _relPath
  if (_relPath[0] != '\0') {
    size_t cur = strlen(_relPath);
    snprintf(_relPath + cur, sizeof(_relPath) - cur, "/%s", e->name);
  } else {
    strncpy(_relPath, e->name, sizeof(_relPath) - 1);
    _relPath[sizeof(_relPath) - 1] = '\0';
  }
  _depth++;
  rebuildAbsolutePath();

  _count = 0;
  _sortedCount = 0;
  scanCurrentFolder();
  buildSortedIndex();

  Serial.printf("[Mp3Manager] Entered folder: %s (depth=%d, %d entries)\n",
                _absPath, _depth, _count);
  return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  goUp() — navigate up one folder level
// ═══════════════════════════════════════════════════════════════════════════════
bool Mp3Manager::goUp() {
  sd_switch_to_mp3();
  if (_depth == 0) return false;

  // Strip the last "/name" segment from _relPath
  char* lastSlash = strrchr(_relPath, '/');
  if (lastSlash) {
    *lastSlash = '\0';
  } else {
    _relPath[0] = '\0';  // single-segment path — back to root
  }
  _depth--;
  rebuildAbsolutePath();

  _count = 0;
  _sortedCount = 0;
  scanCurrentFolder();
  buildSortedIndex();

  Serial.printf("[Mp3Manager] Went up to: %s (depth=%d, %d entries)\n",
                _absPath, _depth, _count);
  return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  entryAt()
// ═══════════════════════════════════════════════════════════════════════════════
const Mp3Entry* Mp3Manager::entryAt(int index) const {
  if (index < 0 || index >= _sortedCount) return nullptr;
  return &_entries[_sortedIndex[index]];
}

// ═══════════════════════════════════════════════════════════════════════════════
//  getFullPath() — return the full absolute path for an entry
// ═══════════════════════════════════════════════════════════════════════════════
void Mp3Manager::getFullPath(int index, char* buf, size_t bufLen) const {
  sd_switch_to_mp3();
  if (!buf || bufLen == 0) return;

  const Mp3Entry* e = entryAt(index);
  if (!e) {
    buf[0] = '\0';
    return;
  }

  // Build "<_absPath>/<name>"
  snprintf(buf, bufLen, "%s/%s", _absPath, e->name);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  searchAllMusic() — recursive search across ALL folders
// ═══════════════════════════════════════════════════════════════════════════════
//  When the user is at the root playlist level and opens search, this function
//  recursively walks ALL subfolders under /MP3/ (or MP3_FOLDER) and finds
//  every music file whose name matches the query. Only MUSIC files are
//  returned — folders, documents, photos, etc. are never included.
//
//  Results are two-pass: first "starts with" matches, then "word-prefix"
//  matches (any word in the name starts with the query), so the most
//  relevant results appear first. Mid-word substrings like "pin" in
//  "pumpkin" are NOT matched — only word-start matches qualify.
// ═══════════════════════════════════════════════════════════════════════════════
int Mp3Manager::searchAllMusic(const char* query, const char* rootPath) {
  _searchCount = 0;
  if (!query || query[0] == '\0') return 0;

  String qLower = String(query);
  qLower.toLowerCase();

  // ── Heap-allocated candidates to avoid stack overflow ─────────────────
  // Each Candidate is ~389 bytes; 64 × 389 = ~25KB which blows the ESP32
  // 8KB task stack. Use malloc instead (goes to PSRAM if available).
  struct Candidate {
    char fullPath[MAX_FILENAME_LEN * 2];
    char name[MAX_FILENAME_LEN];
    uint32_t fileSize;
    bool startsWith;
  };
  // We only need at most 16 results (display cap), use 16 not 64 to save RAM
  static const int MAX_CANDIDATES = 16;
  Candidate* candidates = (Candidate*) malloc(sizeof(Candidate) * MAX_CANDIDATES);
  if (!candidates) {
    Serial.println("[Mp3Manager] searchAllMusic: malloc failed!");
    return 0;
  }
  int candidateCount = 0;

  // ── Heap-allocated directory stack ────────────────────────────────────
  // Each DirStack entry is 256 bytes; 8 levels × 256 = 2KB (vs 8KB on stack)
  struct DirStack { char path[MAX_FILENAME_LEN * 2]; };
  static const int MAX_DEPTH = 8;
  DirStack* dirStack = (DirStack*) malloc(sizeof(DirStack) * MAX_DEPTH);
  if (!dirStack) {
    Serial.println("[Mp3Manager] searchAllMusic: dirStack malloc failed!");
    free(candidates);
    return 0;
  }
  int stackTop = 0;

  // Push the root folder (use rootPath if provided, otherwise MP3_FOLDER)
  const char* startDir = (rootPath && rootPath[0] != '\0') ? rootPath : MP3_FOLDER;
  strncpy(dirStack[0].path, startDir, sizeof(dirStack[0].path) - 1);
  dirStack[0].path[sizeof(dirStack[0].path) - 1] = '\0';
  stackTop = 1;

  while (stackTop > 0 && candidateCount < MAX_CANDIDATES) {
    stackTop--;
    char curDir[MAX_FILENAME_LEN * 2];
    strncpy(curDir, dirStack[stackTop].path, sizeof(curDir) - 1);
    curDir[sizeof(curDir) - 1] = '\0';

    sd_switch_to_mp3();
    File dir = SD.open(curDir);
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      continue;
    }

    File entry;
    while ((entry = dir.openNextFile()) && candidateCount < MAX_CANDIDATES) {
      const char* entryName = entry.name();
      if (!entryName || entryName[0] == '\0') { entry.close(); continue; }

      const char* baseName = baseNameOf(entryName);
      // Skip dotfiles, .trash, etc.
      if (baseName[0] == '.') { entry.close(); continue; }

      // Skip system folders that should never appear in results
      // (System Volume Information, $RECYCLE.BIN, .Trashes, lost+found, etc.)
      bool isDir = entry.isDirectory();
      if (isDir) {
        if (strcasecmp(baseName, "System Volume Information") == 0 ||
            strcasecmp(baseName, "$RECYCLE.BIN") == 0 ||
            strcasecmp(baseName, "lost+found") == 0 ||
            strncasecmp(baseName, ".Trashes", 8) == 0) {
          entry.close();
          continue;  // skip this directory entirely
        }
        // Push subdirectory onto the stack for later visiting
        if (stackTop < MAX_DEPTH) {
          snprintf(dirStack[stackTop].path, sizeof(dirStack[stackTop].path),
                   "%s/%s", curDir, baseName);
          stackTop++;
        }
        entry.close();
        continue;
      }

      // It's a file — only include music files
      if (!isMusicExtension(baseName)) { entry.close(); continue; }

      // Check if the name matches the query using word-prefix matching
      // Strip extension for matching
      int len = strlen(baseName);
      String nameNoExt = String(baseName);
      const char* dot = strrchr(baseName, '.');
      if (dot) {
        int extLen = strlen(dot);
        if (extLen > 0 && extLen < 6) nameNoExt = String(baseName).substring(0, len - extLen);
      }
      nameNoExt.toLowerCase();

      // Word-prefix match: query must match the START of a word in the name.
      // e.g. "pin" matches "Pin", "Pink Floyd", "Pinball Wizard"
      //      but NOT "pumpkin" or "spine" (mid-word, not word-start)
      bool startsWithMatch = nameNoExt.startsWith(qLower);
      bool wordMatch = false;
      if (!startsWithMatch) {
        // Check if any word in the name starts with the query
        // Words delimited by: space, hyphen, underscore, paren, dot, bracket
        int nameLen = nameNoExt.length();
        for (int ci = 1; ci < nameLen; ci++) {
          char c = nameNoExt.charAt(ci);
          if (c == ' ' || c == '-' || c == '_' || c == '(' || c == '.' || c == '[') {
            if (nameNoExt.substring(ci + 1).startsWith(qLower)) {
              wordMatch = true;
              break;
            }
          }
        }
      }

      if (startsWithMatch || wordMatch) {
        Candidate& c = candidates[candidateCount];
        snprintf(c.fullPath, sizeof(c.fullPath), "%s/%s", curDir, baseName);
        strncpy(c.name, baseName, sizeof(c.name) - 1);
        c.name[sizeof(c.name) - 1] = '\0';
        c.fileSize = entry.size();
        c.startsWith = startsWithMatch;
        candidateCount++;
      }

      entry.close();
    }
    dir.close();
  }

  // Sort: starts-with matches first, then word-prefix matches
  for (int i = 1; i < candidateCount; i++) {
    Candidate key = candidates[i];
    int j = i - 1;
    while (j >= 0 &&
           (!key.startsWith && candidates[j].startsWith)) {
      candidates[j + 1] = candidates[j];
      j--;
    }
    candidates[j + 1] = key;
  }

  // Copy to _searchEntries
  for (int i = 0; i < candidateCount && i < MAX_SEARCH_RESULTS; i++) {
    strncpy(_searchEntries[i].fullPath, candidates[i].fullPath,
            sizeof(_searchEntries[i].fullPath) - 1);
    _searchEntries[i].fullPath[sizeof(_searchEntries[i].fullPath) - 1] = '\0';
    strncpy(_searchEntries[i].name, candidates[i].name,
            sizeof(_searchEntries[i].name) - 1);
    _searchEntries[i].name[sizeof(_searchEntries[i].name) - 1] = '\0';
    _searchEntries[i].fileSize = candidates[i].fileSize;
  }
  _searchCount = candidateCount;

  // Free heap allocations
  free(dirStack);
  free(candidates);

  Serial.printf("[Mp3Manager] searchAllMusic('%s'): %d results\n", query, _searchCount);
  return _searchCount;
}

const Mp3Manager::SearchEntry* Mp3Manager::searchResultAt(int index) const {
  if (index < 0 || index >= _searchCount) return nullptr;
  return &_searchEntries[index];
}

// ═══════════════════════════════════════════════════════════════════════════════
//  File / folder management (used by Dashboard + Hotspot)
// ═══════════════════════════════════════════════════════════════════════════════

// SECURITY: All relPath arguments are sanitized to reject path traversal.
// We reject any path containing "..", absolute paths starting with '/', or
// backslashes (which some clients send).
static bool isPathSafe(const char* relPath) {
  if (!relPath || relPath[0] == '\0') return false;
  if (relPath[0] == '/') return false;          // no absolute paths
  if (strstr(relPath, "..")) return false;      // no parent traversal
  if (strchr(relPath, '\\')) return false;      // no backslashes
  return true;
}

bool Mp3Manager::createFolder(const char* relPath) {
  sd_switch_to_mp3();
  if (!isPathSafe(relPath)) return false;
  char full[MAX_FILENAME_LEN * (MAX_FOLDER_DEPTH + 2)];
  snprintf(full, sizeof(full), "%s/%s", MP3_FOLDER, relPath);
  if (SD.exists(full)) return false;
  return SD.mkdir(full);
}

bool Mp3Manager::deleteEntry(const char* relPath) {
  sd_switch_to_mp3();
  if (!isPathSafe(relPath)) return false;
  char full[MAX_FILENAME_LEN * (MAX_FOLDER_DEPTH + 2)];
  snprintf(full, sizeof(full), "%s/%s", MP3_FOLDER, relPath);
  if (!SD.exists(full)) return false;

  // Determine if it's a file or directory.
  File f = SD.open(full);
  if (!f) return false;
  bool isDir = f.isDirectory();
  f.close();

  if (isDir) return SD.rmdir(full);
  return SD.remove(full);
}

bool Mp3Manager::renameEntry(const char* oldRelPath, const char* newRelPath) {
  sd_switch_to_mp3();
  if (!isPathSafe(oldRelPath) || !isPathSafe(newRelPath)) return false;
  char oldFull[MAX_FILENAME_LEN * (MAX_FOLDER_DEPTH + 2)];
  char newFull[MAX_FILENAME_LEN * (MAX_FOLDER_DEPTH + 2)];
  snprintf(oldFull, sizeof(oldFull), "%s/%s", MP3_FOLDER, oldRelPath);
  snprintf(newFull, sizeof(newFull), "%s/%s", MP3_FOLDER, newRelPath);
  if (!SD.exists(oldFull)) return false;
  if (SD.exists(newFull)) return false;
  return SD.rename(oldFull, newFull);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  cleanupStaleUploads() — remove orphaned upload staging files
// ═══════════════════════════════════════════════════════════════════════════════
//  Called once at begin(). Any file at top level still named
//  "<UPLOAD_STAGING_PREFIX>...*" is the leftover of an upload that never
//  finished (power loss, crash, dropped connection) — it's incomplete and
//  was never renamed to its real name, so it's safe to just delete.
void Mp3Manager::cleanupStaleUploads() {
  sd_switch_to_mp3();
  File dir = SD.open(MP3_FOLDER);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  int removed = 0;
  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      const char* name = baseNameOf(entry.name());
      if (strncmp(name, UPLOAD_STAGING_PREFIX, strlen(UPLOAD_STAGING_PREFIX)) == 0) {
        char full[MAX_FILENAME_LEN * (MAX_FOLDER_DEPTH + 2)];
        snprintf(full, sizeof(full), "%s/%s", MP3_FOLDER, name);
        entry.close();
        if (SD.remove(full)) {
          Serial.printf("[Mp3Manager] Removed stale upload staging file: %s\n", full);
          removed++;
        } else {
          Serial.printf("[Mp3Manager] WARNING: could not remove stale staging file: %s\n", full);
        }
        entry = dir.openNextFile();
        continue;
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
  if (removed > 0) {
    Serial.printf("[Mp3Manager] Cleaned up %d stale upload(s)\n", removed);
  }
}

bool Mp3Manager::openUploadFile(const char* relPath) {
  sd_switch_to_mp3();
  if (!isPathSafe(relPath)) return false;
  if (_uploadOpen) closeUploadFile();

  char full[MAX_FILENAME_LEN * (MAX_FOLDER_DEPTH + 2)];
  snprintf(full, sizeof(full), "%s/%s", MP3_FOLDER, relPath);

  // NOTE: arduino-esp32's SD.open(path, FILE_WRITE) does NOT truncate an
  // existing file — it opens (or creates) the file positioned at EOF, so
  // writing after it would append onto whatever was already there. If a
  // file already exists at this path (e.g. the user re-uploads a file
  // with the same name), remove it first so the upload starts clean.
  if (SD.exists(full)) {
    File existing = SD.open(full);
    bool isDir = existing && existing.isDirectory();
    if (existing) existing.close();
    if (isDir) {
      Serial.printf("[Mp3Manager] Upload open FAILED: %s is a folder\n", full);
      return false;
    }
    SD.remove(full);
  }

  _uploadFile = SD.open(full, FILE_WRITE);
  if (!_uploadFile) {
    Serial.printf("[Mp3Manager] Upload open FAILED: %s\n", full);
    return false;
  }
  _uploadOpen = true;
  _uploadBytesWritten = 0;
  _uploadChunksSinceFlush = 0;
  Serial.printf("[Mp3Manager] Upload open: %s\n", full);
  return true;
}

size_t Mp3Manager::writeUploadChunk(const uint8_t* data, size_t len) {
  if (!_uploadOpen || !data || len == 0) return 0;
  size_t w = _uploadFile.write(data, len);
  _uploadBytesWritten += w;

  // Flush to the SD card every few chunks rather than every single one
  // (that would reintroduce per-chunk overhead) or only at the very end
  // (that would leave a large in-memory window unflushed if power drops
  // mid-upload) — this bounds how much of the file could be lost/corrupt
  // on a sudden power-off to roughly UPLOAD_FLUSH_EVERY_N_CHUNKS chunks.
  if (++_uploadChunksSinceFlush >= UPLOAD_FLUSH_EVERY_N_CHUNKS) {
    _uploadFile.flush();
    _uploadChunksSinceFlush = 0;
  }
  return w;
}

size_t Mp3Manager::uploadBytesWritten() const {
  return _uploadBytesWritten;
}

void Mp3Manager::closeUploadFile() {
  if (!_uploadOpen) return;
  _uploadFile.flush();
  _uploadFile.close();
  _uploadOpen = false;
  Serial.println("[Mp3Manager] Upload file closed");
}

void Mp3Manager::abortUploadFile(const char* relPath) {
  // Close (if open) and remove whatever partial data was written — used
  // when a client disconnects mid-upload so we don't leave a truncated
  // MP3 file behind.
  if (_uploadOpen) closeUploadFile();
  if (relPath && isPathSafe(relPath)) {
    sd_switch_to_mp3();
    char full[MAX_FILENAME_LEN * (MAX_FOLDER_DEPTH + 2)];
    snprintf(full, sizeof(full), "%s/%s", MP3_FOLDER, relPath);
    if (SD.exists(full)) SD.remove(full);
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Trash — "delete" moves a top-level file into TRASH_FOLDER_NAME instead
//  of erasing it. The trash folder is a plain (non-hidden) directory so it
//  shows up normally when the device is mounted over USB MSC — that's
//  where the user permanently deletes trashed files themselves.
// ═══════════════════════════════════════════════════════════════════════════════
bool Mp3Manager::ensureTrashFolder() {
  sd_switch_to_mp3();
  char full[MAX_FILENAME_LEN * (MAX_FOLDER_DEPTH + 2)];
  snprintf(full, sizeof(full), "%s/%s", MP3_FOLDER, TRASH_FOLDER_NAME);
  if (SD.exists(full)) return true;
  if (!SD.mkdir(full)) {
    Serial.printf("[Mp3Manager] ERROR: could not create trash folder %s\n", full);
    return false;
  }
  return true;
}

bool Mp3Manager::moveToTrash(const char* relPath) {
  sd_switch_to_mp3();
  if (!isPathSafe(relPath)) return false;
  // Don't allow trashing the trash folder itself or anything inside it.
  if (strncasecmp(relPath, TRASH_FOLDER_NAME, strlen(TRASH_FOLDER_NAME)) == 0) return false;
  if (!ensureTrashFolder()) return false;

  char srcFull[MAX_FILENAME_LEN * (MAX_FOLDER_DEPTH + 2)];
  snprintf(srcFull, sizeof(srcFull), "%s/%s", MP3_FOLDER, relPath);
  if (!SD.exists(srcFull)) return false;

  const char* baseName = baseNameOf(relPath);

  // If a file of the same name is already in the trash (e.g. deleted
  // before, or a same-named file re-uploaded and deleted again), find a
  // free name by appending a numeric suffix rather than clobbering it —
  // the previous trashed copy might still be wanted.
  char dstFull[MAX_FILENAME_LEN * (MAX_FOLDER_DEPTH + 2)];
  snprintf(dstFull, sizeof(dstFull), "%s/%s/%s", MP3_FOLDER, TRASH_FOLDER_NAME, baseName);

  if (SD.exists(dstFull)) {
    // Split "name" / ".ext" so the suffix lands before the extension.
    char stem[MAX_FILENAME_LEN];
    char ext[16] = {0};
    strncpy(stem, baseName, sizeof(stem) - 1);
    stem[sizeof(stem) - 1] = '\0';
    char* dot = strrchr(stem, '.');
    if (dot) {
      strncpy(ext, dot, sizeof(ext) - 1);
      *dot = '\0';
    }
    bool found = false;
    for (int i = 1; i <= 99; i++) {
      snprintf(dstFull, sizeof(dstFull), "%s/%s/%s_%d%s",
               MP3_FOLDER, TRASH_FOLDER_NAME, stem, i, ext);
      if (!SD.exists(dstFull)) { found = true; break; }
    }
    if (!found) {
      Serial.printf("[Mp3Manager] Trash full of collisions for %s\n", baseName);
      return false;
    }
  }

  if (!SD.rename(srcFull, dstFull)) {
    Serial.printf("[Mp3Manager] ERROR: could not move %s to trash\n", srcFull);
    return false;
  }
  Serial.printf("[Mp3Manager] Trashed: %s -> %s\n", srcFull, dstFull);
  return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  NVS state persistence — saveState / loadState
// ═══════════════════════════════════════════════════════════════════════════════
void Mp3Manager::saveState(int fileIndex, uint32_t bytePos, uint8_t volume) {
  _prefs.begin(MP3_NVS_NAMESPACE, false);  // read-write
  _prefs.putInt(KEY_LAST_IDX, fileIndex);
  _prefs.putUInt(KEY_LAST_POS, bytePos);
  _prefs.putUChar(KEY_VOLUME, volume);
  _prefs.end();
}

bool Mp3Manager::loadState(int& fileIndex, uint32_t& bytePos, uint8_t& volume) {
  _prefs.begin(MP3_NVS_NAMESPACE, true);  // read-only

  // If the key doesn't exist, no state has been saved yet
  if (!_prefs.isKey(KEY_LAST_IDX)) {
    // Still load the volume if it exists — even on first boot, the user
    // may have set a volume via Settings before any track was played.
    if (_prefs.isKey(KEY_VOLUME)) {
      volume = _prefs.getUChar(KEY_VOLUME, DEFAULT_VOLUME);
    } else {
      volume = DEFAULT_VOLUME;
    }
    _prefs.end();
    return false;
  }

  fileIndex = _prefs.getInt(KEY_LAST_IDX, -1);
  bytePos   = _prefs.getUInt(KEY_LAST_POS, 0);
  volume    = _prefs.getUChar(KEY_VOLUME, DEFAULT_VOLUME);
  _prefs.end();

  // Validate: file index must be within range
  if (fileIndex < 0 || fileIndex >= _count) {
    return false;
  }

  return true;
}

// ── saveVolume() — just persists the volume ────────────────────────────────
void Mp3Manager::saveVolume(uint8_t volume) {
  _prefs.begin(MP3_NVS_NAMESPACE, false);
  _prefs.putUChar(KEY_VOLUME, volume);
  _prefs.end();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Theme persistence
// ═══════════════════════════════════════════════════════════════════════════════
uint8_t Mp3Manager::getThemeId() const {
  _prefs.begin(MP3_NVS_NAMESPACE, true);
  uint8_t id = _prefs.getUChar(KEY_THEME, 0);
  _prefs.end();
  return id;
}

void Mp3Manager::setThemeId(uint8_t id) {
  _prefs.begin(MP3_NVS_NAMESPACE, false);
  _prefs.putUChar(KEY_THEME, id);
  _prefs.end();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Auto-timeout persistence
// ═══════════════════════════════════════════════════════════════════════════════
uint32_t Mp3Manager::getAutoTimeoutMs() const {
  _prefs.begin(MP3_NVS_NAMESPACE, true);
  uint32_t ms = _prefs.getUInt(KEY_TIMEOUT, AUTO_TIMEOUT_MS);
  _prefs.end();
  return ms;
}

void Mp3Manager::setAutoTimeoutMs(uint32_t ms) {
  _prefs.begin(MP3_NVS_NAMESPACE, false);
  _prefs.putUInt(KEY_TIMEOUT, ms);
  _prefs.end();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Hotspot credentials persistence (SSID + WPA2 password)
// ═══════════════════════════════════════════════════════════════════════════════
void Mp3Manager::getHotspotSSID(char* buf, size_t bufLen) const {
  _prefs.begin(MP3_NVS_NAMESPACE, true);
  String s = _prefs.getString(KEY_HS_SSID, DEFAULT_HS_SSID);
  _prefs.end();
  strncpy(buf, s.c_str(), bufLen - 1);
  buf[bufLen - 1] = '\0';
}

void Mp3Manager::setHotspotSSID(const char* ssid) {
  if (!ssid || ssid[0] == '\0') return;
  _prefs.begin(MP3_NVS_NAMESPACE, false);
  _prefs.putString(KEY_HS_SSID, String(ssid));
  _prefs.end();
}

void Mp3Manager::getHotspotPassword(char* buf, size_t bufLen) const {
  _prefs.begin(MP3_NVS_NAMESPACE, true);
  String s = _prefs.getString(KEY_HS_PASS, DEFAULT_HS_PASS);
  _prefs.end();
  strncpy(buf, s.c_str(), bufLen - 1);
  buf[bufLen - 1] = '\0';
}

void Mp3Manager::setHotspotPassword(const char* password) {
  if (!password || password[0] == '\0') return;
  // WPA2 requires at least 8 chars. Reject shorter.
  if (strlen(password) < 8) {
    Serial.printf("[Mp3Manager] Rejecting hotspot password: too short (%zu < 8)\n",
                  strlen(password));
    return;
  }
  _prefs.begin(MP3_NVS_NAMESPACE, false);
  _prefs.putString(KEY_HS_PASS, String(password));
  _prefs.end();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Dashboard PIN — SHA256 hashed, stored in NVS
// ═══════════════════════════════════════════════════════════════════════════════
bool Mp3Manager::isPinSet() const {
  _prefs.begin(MP3_NVS_NAMESPACE, true);
  bool set = false;
  if (_prefs.isKey(KEY_PIN_HASH)) {
    String hash = _prefs.getString(KEY_PIN_HASH, PIN_UNSET_SENTINEL);
    set = (hash != PIN_UNSET_SENTINEL);
  }
  _prefs.end();
  return set;
}

bool Mp3Manager::verifyPin(const char* pin) const {
  if (!pin || strlen(pin) < 4) return false;

  _prefs.begin(MP3_NVS_NAMESPACE, true);
  String storedHash;
  if (_prefs.isKey(KEY_PIN_HASH)) {
    storedHash = _prefs.getString(KEY_PIN_HASH, PIN_UNSET_SENTINEL);
  } else {
    storedHash = PIN_UNSET_SENTINEL;
  }
  _prefs.end();

  if (storedHash == PIN_UNSET_SENTINEL) return false;

  char computedHash[65];
  sha256Hex(pin, computedHash);

  // Constant-time comparison
  volatile uint8_t diff = 0;
  volatile uint8_t lenMatch = (storedHash.length() == 64);
  for (int i = 0; i < 64; i++) {
    diff |= (uint8_t)(storedHash[i] ^ computedHash[i]);
  }
  return (diff == 0) && (lenMatch != 0);
}

void Mp3Manager::setPin(const char* newPin) {
  if (!newPin || strlen(newPin) < 4) return;
  char hash[65];
  sha256Hex(newPin, hash);
  _prefs.begin(MP3_NVS_NAMESPACE, false);
  _prefs.putString(KEY_PIN_HASH, hash);
  _prefs.end();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  First-boot detection
// ═══════════════════════════════════════════════════════════════════════════════
bool Mp3Manager::isFirstBoot() const {
  _prefs.begin(MP3_NVS_NAMESPACE, true);
  bool firstBoot = true;
  if (_prefs.isKey(KEY_FIRST_BOOT)) {
    firstBoot = !_prefs.getBool(KEY_FIRST_BOOT, false);
  }
  if (!firstBoot) {
    String pinHash = _prefs.getString(KEY_PIN_HASH, PIN_UNSET_SENTINEL);
    if (pinHash == PIN_UNSET_SENTINEL) {
      firstBoot = true;
    }
  }
  _prefs.end();
  return firstBoot;
}

void Mp3Manager::completeFirstBoot(const char* pin) {
  if (!pin || strlen(pin) < 4) return;
  char hash[65];
  sha256Hex(pin, hash);
  _prefs.begin(MP3_NVS_NAMESPACE, false);
  _prefs.putString(KEY_PIN_HASH, hash);
  _prefs.putBool(KEY_FIRST_BOOT, true);
  _prefs.end();
  Serial.println("[Mp3Manager] First-boot PIN setup complete");
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Per-folder shuffle toggle (persisted in NVS)
// ═══════════════════════════════════════════════════════════════════════════════
// NVS key: "sh_XXXX" where XXXX is 4-hex-digit CRC16 of the folder's
// relative path. Value: uint8 1=shuffle on, 0=off.
bool Mp3Manager::getShuffleForFolder(const char* folderRelPath) const {
  uint16_t crc = crc16(folderRelPath ? folderRelPath : "");
  char key[8];
  snprintf(key, sizeof(key), "sh_%04x", crc);
  _prefs.begin(MP3_NVS_NAMESPACE, true);  // read-only
  bool val = _prefs.getUChar(key, 0) != 0;
  _prefs.end();
  return val;
}

void Mp3Manager::setShuffleForFolder(const char* folderRelPath, bool on) {
  uint16_t crc = crc16(folderRelPath ? folderRelPath : "");
  char key[8];
  snprintf(key, sizeof(key), "sh_%04x", crc);
  _prefs.begin(MP3_NVS_NAMESPACE, false);  // read-write
  _prefs.putUChar(key, on ? 1 : 0);
  _prefs.end();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Boot-time state restoration — last folder, last song, play-all
// ═══════════════════════════════════════════════════════════════════════════════
void Mp3Manager::saveLastFolder(const char* folderRelPath) {
  _prefs.begin(MP3_NVS_NAMESPACE, false);  // read-write
  _prefs.putString(KEY_LAST_FOLDER, String(folderRelPath ? folderRelPath : ""));
  _prefs.end();
}

void Mp3Manager::loadLastFolder(char* buf, size_t bufLen) const {
  _prefs.begin(MP3_NVS_NAMESPACE, true);  // read-only
  String s = _prefs.getString(KEY_LAST_FOLDER, "");
  strncpy(buf, s.c_str(), bufLen - 1);
  buf[bufLen - 1] = '\0';
  _prefs.end();
}

void Mp3Manager::saveLastSong(const char* songName) {
  _prefs.begin(MP3_NVS_NAMESPACE, false);  // read-write
  _prefs.putString(KEY_LAST_SONG, String(songName ? songName : ""));
  _prefs.end();
}

void Mp3Manager::loadLastSong(char* buf, size_t bufLen) const {
  _prefs.begin(MP3_NVS_NAMESPACE, true);  // read-only
  String s = _prefs.getString(KEY_LAST_SONG, "");
  strncpy(buf, s.c_str(), bufLen - 1);
  buf[bufLen - 1] = '\0';
  _prefs.end();
}

void Mp3Manager::saveLastPlayAll(bool active) {
  _prefs.begin(MP3_NVS_NAMESPACE, false);  // read-write
  _prefs.putUChar(KEY_LAST_PLAYALL, active ? 1 : 0);
  _prefs.end();
}

bool Mp3Manager::loadLastPlayAll() const {
  _prefs.begin(MP3_NVS_NAMESPACE, true);  // read-only
  bool val = _prefs.getUChar(KEY_LAST_PLAYALL, 0) != 0;
  _prefs.end();
  return val;
}

bool Mp3Manager::navigateToFolder(const char* folderRelPath) {
  // Navigate from root to the specified relative path by entering
  // each path segment one at a time.
  // e.g. "Rock/Live" → enterFolder("Rock") then enterFolder("Live")
  if (!folderRelPath || folderRelPath[0] == '\0') {
    // Empty path = root, already there after begin()
    return true;
  }

  // Make a mutable copy to tokenize
  char pathCopy[MAX_FILENAME_LEN * MAX_FOLDER_DEPTH];
  strncpy(pathCopy, folderRelPath, sizeof(pathCopy) - 1);
  pathCopy[sizeof(pathCopy) - 1] = '\0';

  // Walk each segment separated by '/'
  char* segment = strtok(pathCopy, "/");
  while (segment) {
    // Find this folder name in the current listing
    int foundIdx = -1;
    for (int i = 0; i < _count; i++) {
      const Mp3Entry* e = &_entries[i];
      if (e->isFolder && strcasecmp(e->name, segment) == 0) {
        foundIdx = i;
        break;
      }
    }
    if (foundIdx < 0) {
      // Folder not found — stop here and return false
      Serial.printf("[Mp3Manager] navigateToFolder: segment '%s' not found\n", segment);
      return false;
    }
    // Enter the folder using the sorted index (find sorted position for foundIdx)
    int sortedPos = -1;
    for (int i = 0; i < _sortedCount; i++) {
      if (_sortedIndex[i] == foundIdx) {
        sortedPos = i;
        break;
      }
    }
    if (sortedPos < 0 || !enterFolder(sortedPos)) {
      Serial.printf("[Mp3Manager] navigateToFolder: failed to enter '%s'\n", segment);
      return false;
    }
    segment = strtok(nullptr, "/");
  }
  return true;
}

#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  mp3_manager.h — MP3 file/folder listing + NVS-backed settings
// ═══════════════════════════════════════════════════════════════════════════════
//  Scans /MP3/ (and any subfolders) on the SD card. Folders appear as
//  first-class entries alongside .mp3 files — the UI lets the user navigate
//  into/out of them. A "current folder path" is maintained so the playlist
//  always shows the contents of one folder at a time.
//
//  NVS persistence (namespace "mp3player"):
//    - theme_id, auto_tout_ms, volume, last_idx, last_pos
//    - pin_hash (SHA256-hashed Dashboard PIN)
//    - first_boot flag
//    - hotspot_ssid, hotspot_pass  (WPA2 credentials for Hotspot Mode)
//    - sh_XXXX  per-folder shuffle toggle (CRC16 of folder path)
//    - last_folder, last_song, last_playall  (boot state restoration)
// ═══════════════════════════════════════════════════════════════════════════════
#include <Arduino.h>
#include <SD.h>
#include <Preferences.h>
#include "board_config.h"

// Maximum depth of nested folders under /MP3/ (e.g. /MP3/Rock/Live/ = depth 2)
#define MAX_FOLDER_DEPTH 8

struct Mp3Entry {
  char name[MAX_FILENAME_LEN];   // filename or folder name (no path)
  uint32_t fileSize;             // bytes (0 for folders)
  bool isFolder;                 // true if this is a directory entry
};

class Mp3Manager {
public:
  Mp3Manager() = default;

  // Initialize: scan the /MP3/ folder and build the file list.
  bool begin();

  // Re-scan the current folder (e.g. after Hotspot/Dashboard uploads).
  bool rescan();

  // ── Folder navigation ──────────────────────────────────────────────
  // Number of entries (files + folders) in the CURRENT folder.
  int count() const { return _count; }

  // Open a sub-folder by its index in the current listing. Returns true
  // if the folder was opened and rescanned.
  bool enterFolder(int index);

  // Go up one folder level. Returns false if already at /MP3/ root.
  bool goUp();

  // True if the current folder is the /MP3/ root.
  bool atRoot() const { return _depth == 0; }

  // Current folder path relative to /MP3/ (e.g. "" for root, "Rock" for /MP3/Rock/).
  // Always returns a pointer to an internal buffer.
  const char* currentRelativePath() const { return _relPath; }

  // Full absolute path of current folder (e.g. "/MP3" or "/MP3/Rock").
  const char* currentAbsolutePath() const { return _absPath; }

  // ── Entry access ───────────────────────────────────────────────────
  // Access entry by DISPLAY index (0..count-1). Returns nullptr if out of range.
  const Mp3Entry* entryAt(int index) const;

  // Get the full absolute path for an entry (e.g. "/MP3/Rock/song.mp3").
  // Buffer must be at least MAX_PATH_LEN bytes.
  void getFullPath(int index, char* buf, size_t bufLen) const;

  // ── Recursive search across ALL folders ───────────────────────────
  // Struct for search results with full paths (used by the search screen
  // when searching across ALL playlists/folders at once).
  struct SearchEntry {
    char fullPath[MAX_FILENAME_LEN * 2];  // e.g. "/MP3/Rock/song.mp3"
    char name[MAX_FILENAME_LEN];           // just "song.mp3"
    uint32_t fileSize;
  };

  // Perform a recursive search for music files matching `query` across
  // ALL subfolders of /MP3/. Results are stored in _searchEntries[].
  // Returns the number of matches found (max MAX_SEARCH_RESULTS).
  // query is case-insensitive. Matches on filename (starts-with priority,
  // then word-prefix). Only music files are included (no folders, no docs, etc).
  // rootPath: optional starting directory (defaults to MP3_FOLDER).
  //   Used to restrict search to a specific playlist and its subfolders.
  int searchAllMusic(const char* query, const char* rootPath = nullptr);

  // Access a search result by index.
  const SearchEntry* searchResultAt(int index) const;

  // Total number of search results from the last searchAllMusic() call.
  int searchResultCount() const { return _searchCount; }

  // Maximum number of search results
  static const int MAX_SEARCH_RESULTS = 64;

  // ── File / folder management (used by Dashboard + Hotspot) ─────────
  // All return true on success. Paths are relative to /MP3/.
  bool createFolder(const char* relPath);
  bool deleteEntry(const char* relPath);
  bool renameEntry(const char* oldRelPath, const char* newRelPath);
  // Open a file for writing (used by uploaders). Returns true on success.
  // Truncates/replaces any existing file at relPath.
  bool openUploadFile(const char* relPath);
  // Append a chunk to the currently-open upload file. Returns bytes written.
  size_t writeUploadChunk(const uint8_t* data, size_t len);
  // Total bytes written to the currently (or most recently) open upload file.
  size_t uploadBytesWritten() const;
  // Close the currently-open upload file.
  void closeUploadFile();
  // Close (if open) and delete relPath — used when an upload is aborted
  // mid-transfer so a truncated/partial file isn't left behind.
  void abortUploadFile(const char* relPath);
  // Remove any leftover ".uploading_*" staging files from an upload that
  // was cut off (power loss, crash, dropped connection) before it could
  // be renamed to its final name. Called once at begin().
  void cleanupStaleUploads();

  // ── Trash (soft delete) ─────────────────────────────────────────────
  // Moves relPath into TRASH_FOLDER_NAME instead of deleting it outright.
  // Renames around name collisions in the trash folder. The trash folder
  // itself and its contents are excluded from scanCurrentFolder().
  bool moveToTrash(const char* relPath);
  bool ensureTrashFolder();

  // ── NVS state persistence ──────────────────────────────────────────
  void saveState(int fileIndex, uint32_t bytePos, uint8_t volume);
  bool loadState(int& fileIndex, uint32_t& bytePos, uint8_t& volume);

  // Save just the volume (called on every volume change, debounced by caller).
  void saveVolume(uint8_t volume);

  // ── Theme persistence ──────────────────────────────────────────────
  uint8_t getThemeId() const;
  void setThemeId(uint8_t id);

  // ── Auto-timeout persistence ───────────────────────────────────────
  uint32_t getAutoTimeoutMs() const;
  void setAutoTimeoutMs(uint32_t ms);

  // ── Hotspot credentials (NVS-backed) ───────────────────────────────
  // Defaults: SSID = "SecureVault-MP3", password = "12345678"
  void getHotspotSSID(char* buf, size_t bufLen) const;
  void setHotspotSSID(const char* ssid);
  void getHotspotPassword(char* buf, size_t bufLen) const;
  void setHotspotPassword(const char* password);

  // ── Dashboard PIN (SHA256 hashed) ──────────────────────────────────
  bool isPinSet() const;
  bool verifyPin(const char* pin) const;
  void setPin(const char* newPin);
  bool isFirstBoot() const;
  void completeFirstBoot(const char* pin);

  // ── Per-folder shuffle toggle (NVS-backed) ────────────────────────
  // Persisted per-folder using CRC16 hash of the relative path.
  bool getShuffleForFolder(const char* folderRelPath) const;
  void setShuffleForFolder(const char* folderRelPath, bool on);

  // ── Boot-time state restoration (NVS-backed) ─────────────────────
  // Persists the last opened folder path, last played song name, and
  // whether play-all was active — so the device can restore its UI
  // state across reboots instead of always starting at the root folder
  // with no song selected.
  void saveLastFolder(const char* folderRelPath);
  void loadLastFolder(char* buf, size_t bufLen) const;
  void saveLastSong(const char* songName);
  void loadLastSong(char* buf, size_t bufLen) const;
  void saveLastPlayAll(bool active);
  bool loadLastPlayAll() const;
  // Navigate to a saved folder path (e.g. "Rock/Live") by entering
  // each segment. Returns true if the folder was reached successfully.
  bool navigateToFolder(const char* folderRelPath);

  // SD card status
  bool isSDOk() const { return _sdOk; }

private:
  Mp3Entry _entries[MAX_MP3_FILES];
  int _count = 0;
  bool _sdOk = false;
  bool _sorted = false;

  // Sorted display index — maps display position → actual entry index.
  int _sortedIndex[MAX_MP3_FILES];
  int _sortedCount = 0;
  void buildSortedIndex();

  // Scan the current folder (identified by _absPath) and populate _entries[].
  int scanCurrentFolder();

  // Rebuild _absPath from _relPath.
  void rebuildAbsolutePath();

  // Folder navigation state
  int _depth = 0;                              // 0 = at /MP3/ root
  char _relPath[MAX_FILENAME_LEN * MAX_FOLDER_DEPTH] = {0};  // relative to /MP3/
  char _absPath[MAX_FILENAME_LEN * (MAX_FOLDER_DEPTH + 1)] = MP3_FOLDER;  // full path

  // Currently-open upload file (for chunked writes from Hotspot/Dashboard)
  File _uploadFile;
  bool _uploadOpen = false;
  size_t _uploadBytesWritten = 0;
  uint8_t _uploadChunksSinceFlush = 0;

  // ── Recursive search state ───────────────────────────────────────
  SearchEntry _searchEntries[MAX_SEARCH_RESULTS];
  int _searchCount = 0;

  // Internal recursive helper for searchAllMusic().
  void searchRecursive(const char* dirPath, const String& qLower, int depth);

  // NVS handle (mutable so const getters can use it)
  mutable Preferences _prefs;
};

// ── Global MP3 manager instance ────────────────────────────────────────────
extern Mp3Manager mp3Mgr;

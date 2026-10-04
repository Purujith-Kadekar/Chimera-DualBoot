#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  hotspot_manager.h — AP Mode for MP3 file upload (Hotspot Mode)
// ═══════════════════════════════════════════════════════════════════════════════
//  SoftAP + DNS hijack + captive portal with a file upload page for adding
//  MP3 files to the /MP3/ folder.
//
//  Credentials: SSID + WPA2 password are stored in NVS (via Mp3Manager).
//  Defaults: SSID="SecureVault-MP3", password="12345678". The user can
//  change them from Settings → Hotspot → Change SSID / Change Password.
//
//  Lifecycle:
//    1. User selects "HOTSPOT" in the mode menu.
//    2. start() loads credentials from NVS, brings up WPA2 SoftAP + DNS
//       hijack + web server.
//    3. UI shows the HOTSPOT info screen (SSID, password, QR code, BACK).
//    4. User joins the AP, opens the captive portal, uploads MP3 files.
//    5. User taps BACK, or 5-min idle timeout → stop().
// ═══════════════════════════════════════════════════════════════════════════════
#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include "board_config.h"

class HotspotManager {
public:
  static HotspotManager& getInstance();

  bool begin();
  bool start();
  void stop();

  bool isActive() const { return _active; }
  void tick();
  void noteActivity();
  unsigned long lastActivity() const { return _lastActivity; }

  // ── SSID / password (loaded from NVS at start()) ────────────────────
  const char* ssid() const { return _ssid; }
  const char* password() const { return _password; }

  int connectedClients() const;

  int uploadedCount() const { return _uploadedCount; }
  void resetUploadCount() { _uploadedCount = 0; }

  // Shared by the free-function filename sanitizer in the .cpp — exposed
  // here rather than duplicated. lowerName must already be lowercased.
  static bool isReservedName(const String& lowerName);

private:
  HotspotManager() = default;
  HotspotManager(const HotspotManager&) = delete;
  HotspotManager& operator=(const HotspotManager&) = delete;

  bool _initialized = false;
  bool _active = false;

  // Loaded from NVS at start() — buffer sized for SSID (max 32) and
  // WPA2 password (min 8, max 63).
  char _ssid[32] = {0};
  char _password[64] = {0};

  DNSServer _dnsServer;
  WebServer* _webServer = nullptr;
  bool _dnsRunning = false;

  unsigned long _lastActivity = 0;
  int _uploadedCount = 0;

  // Name of the file currently being received via /upload (empty when no
  // upload is in progress, or the current one was rejected). Tracked here
  // because Mp3Manager's upload-file API only holds one file open at a
  // time and doesn't remember the name for us.
  String _uploadRelName;      // final (public) filename, e.g. "song.mp3"
  String _uploadStagingName;  // staging filename actually being written to

  // ── Web server route handlers ────────────────────────────────────────
  void handleRoot();
  void handleVersion();
  void handleUpload();
  void handleUploadPost();
  void handleDelete();
  void handleRename();
  void handleDownload();
  void handleStream();
  void handleStorageInfo();
  void handleList();
  void handleNotFound();
  void handleCaptivePortal();

  // ── File management helpers ──────────────────────────────────────────
  bool ensureMP3Folder();
  String formatFileSize(size_t bytes);
};

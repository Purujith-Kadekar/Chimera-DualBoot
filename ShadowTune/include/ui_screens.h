#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  ui_screens.h — screen state machine for the MP3 Player firmware
// ═══════════════════════════════════════════════════════════════════════════════
#include <Arduino.h>
#include "board_config.h"
#include "ui_theme.h"
#include "display_manager.h"
#include "mp3_manager.h"
#include "mp3_player.h"
#include "rtc_manager.h"
#include "mpu_manager.h"
#include "button_manager.h"
#include "hotspot_manager.h"
#include "usb_msc_manager.h"
#include "ina219_manager.h"

// ── Screen enum ─────────────────────────────────────────────────────────
enum class Screen {
  PLAYLIST,          // Main screen — scrollable list of MP3 files + folders
  NOW_PLAYING,       // Track info, progress bar, playback controls
  HOTSPOT_INFO,      // AP mode info screen (SSID, password, QR code, clients)
  HOTSPOT_CHANGE,    // Sub-menu: Change SSID / Change Password
  HOTSPOT_INPUT,     // Multi-tap text entry for SSID or password
  USB_INFO,          // USB Drive mode info screen ("connected, do not unplug")
  CLOCK,             // Lock/clock screen — big HH:MM + now-playing overlay
  SEARCH,            // QWERTY keyboard search screen
  SETTINGS,          // Theme, auto-timeout, volume, hotspot, about
  ABOUT,             // Device info
  SETTIME,           // Set RTC time
  POWER_OFF,         // Deep sleep countdown
  RESUME,            // Resume from deep sleep
  FIRST_BOOT_PIN,    // Mandatory first-boot PIN setup
  NONE
};

// ── Mode enum ────────────────────────────────────────────────────────────
enum class PlayerMode { NORMAL, HOTSPOT, USB_MSC };

// ── Settings menu indices (must match drawSettingsScreen order) ──────────
enum class SettingsRow {
  THEME = 0,
  AUTO_TIMEOUT = 1,
  VOLUME = 2,
  HOTSPOT = 3,        // NEW — opens HOTSPOT_CHANGE sub-menu
  ABOUT = 4,
  BACK = 5,
  COUNT
};

// ── Hotspot change sub-mode ──────────────────────────────────────────────
enum class HotspotInputMode { SSID, PASSWORD };

class UiController {
public:
  UiController(DisplayManager& disp, Mp3Manager& mp3Mgr, Mp3Player& player,
               RtcManager& rtc, MpuManager& mpu, ButtonManager& btn,
               HotspotManager& hotspot, Ina219Manager& ina219,
               UsbMscManager& usbMsc);

  void begin();
  void tick();

  // ── Public: called from main.cpp on deep-sleep wake ────────────────
  void showResumeScreen();
  void loadTheme();

private:
  DisplayManager& _disp;
  Mp3Manager& _mp3Mgr;
  Mp3Player& _player;
  RtcManager& _rtc;
  MpuManager& _mpu;
  ButtonManager& _btn;
  HotspotManager& _hotspot;
  Ina219Manager& _ina219;
  UsbMscManager& _usbMsc;

  Screen _screen = Screen::NONE;
  Screen _prevScreen = Screen::NONE;
  PlayerMode _mode = PlayerMode::NORMAL;

  // ── Playlist screen state ───────────────────────────────────────────
  int _selectedEntry = 0;
  int _listScroll = 0;
  enum : int { MAX_DISPLAY = MAX_MP3_FILES };
  int _sortedIndex[MAX_MP3_FILES];
  int _sortedCount = 0;
  void buildSortedIndex();
  // Finds the next/previous entry starting from fromIndex (exclusive) in
  // the given direction (+1 or -1) that is a playable track, skipping over
  // any folder entries in between. Returns -1 if none exists in that
  // direction (start/end of the list).
  int findAdjacentPlayable(int fromIndex, int direction) const;
  void refreshListing();        // rescan + rebuild + redraw

  // ── Play All / Shuffle state ──────────────────────────────────────
  bool    _playAllActive = false;       // true = auto-advance when song ends
  bool    _shuffleOn = false;           // shuffle toggle for current folder
  int     _playSeq[MAX_MP3_FILES];      // play-order: indices of playable entries
  int     _playSeqCount = 0;            // # of playable entries in _playSeq[]
  int     _playSeqPos = -1;             // current position in _playSeq[] (-1 = none)
  char    _playSeqFolder[MAX_FILENAME_LEN * MAX_FOLDER_DEPTH] = {0};  // folder this seq was built for
  PlaybackState _prevPlayerState = PlaybackState::STOPPED;  // detect PLAYING→STOPPED
  void buildPlaySequence();             // populate _playSeq[] from current folder
  void shufflePlaySequence();           // Fisher-Yates shuffle on _playSeq[]
  void advanceToNextTrack();            // auto-advance to next song in sequence
  void playAllFromCurrent();            // entry point for PLAY ALL button
  void loadFolderShuffleState();        // read shuffle from NVS for current folder
  void skipTrack(int direction);        // prev/next respecting play-all sequence
  void saveBootState();                 // persist folder+song+playall to NVS for next boot
  void restoreBootState();              // navigate to last folder & select last song on boot

  // ── Now Playing screen state ────────────────────────────────────────
  bool _npFirstDraw = true;
  uint8_t _lastProgress = 0;
  PlaybackState _lastState = PlaybackState::STOPPED;
  uint8_t _lastVolume = 0;
  unsigned long _volumeHoldStart = 0;
  bool _volumeHoldDir = false;
  unsigned long _lastVolumeTick = 0;
  // Currently playing track info — set whenever _player.play() is called,
  // so the NOW_PLAYING screen always shows the correct song name even when
  // the song was played from search (where _selectedEntry may be stale).
  char _playingName[MAX_FILENAME_LEN] = {0};
  uint32_t _playingSize = 0;

  // ── Volume persistence (debounced NVS save) ─────────────────────────
  unsigned long _lastVolumeChange = 0;
  bool _volumeDirty = false;
  void setVolumePersisted(uint8_t vol);   // set + mark dirty
  void checkVolumeSave();                  // called from tick()

  // ── Clock screen state ──────────────────────────────────────────────
  unsigned long _lastClockRedraw = 0;
  uint8_t _lastClockProgress = 255;

  // ── Settings screen state ───────────────────────────────────────────
  static const int SETTINGS_COUNT = (int)SettingsRow::COUNT;

  // ── Hotspot input (multi-tap text entry) state ──────────────────────
  HotspotInputMode _hsInputMode = HotspotInputMode::SSID;
  char _hsTextBuf[64] = {0};
  int _hsTextLen = 0;
  // Multi-tap: when user taps a key repeatedly, cycle through its letters.
  // _hsMultitapKey = -1 means no active multi-tap (next tap starts fresh).
  int _hsMultitapKey = -1;
  int _hsMultitapIdx = 0;          // which letter of the current key
  unsigned long _hsMultitapLast = 0;  // millis() of last tap
  static const unsigned long HS_MULTITAP_TIMEOUT = 800;  // commit after 800ms

    char _pinBuf[MAX_PIN_LEN + 1] = {0};
  int _pinLen = 0;
  int _failCount = 0;
  bool _isFirstBoot = false;
  int _firstBootPinStep = 0;
  char _firstBootPinBuf[MAX_PIN_LEN + 1] = {0};
  int _firstBootPinLen = 0;
  char _firstBootPinConfirmBuf[MAX_PIN_LEN + 1] = {0};
  int _firstBootPinConfirmLen = 0;
  char _firstBootError[40] = {0};
  unsigned long _firstBootErrorTime = 0;

  // ── Search screen state (QWERTY keyboard, live-filtered track search) ─
  char _searchQuery[32] = {0};
  int _searchQueryLen = 0;
  int _searchResultCount = 0;
  int _searchSelIdx = 0;         // selected result for scrolling
  bool _searchKeyboardMode = false; // false=letters, true=numbers/symbols
  int _searchKeyFlash = -1;
  unsigned long _searchKeyFlashTime = 0;
  bool _searchFromRoot = false;  // true = search ALL folders; false = current folder only

  // ── Mode menu overlay ───────────────────────────────────────────────
  bool _modeMenuOpen = false;
  Screen _modeMenuHost = Screen::PLAYLIST;

  // ── Deferred mode switching ─────────────────────────────────────────
  enum class ModeSwitchState { IDLE, TEARDOWN_OLD, INIT_NEW };
  ModeSwitchState _modeSwitchState = ModeSwitchState::IDLE;
  PlayerMode _modePendingSwitch = PlayerMode::NORMAL;
  uint32_t _modeSwitchStartTime = 0;

  // ── Timing ──────────────────────────────────────────────────────────
  unsigned long _lastActivity = 0;
  unsigned long _lastClockTick = 0;
  unsigned long _lastStatusBarTick = 0;
  unsigned long _lastBtnPoll = 0;
  unsigned long _lastMpuPoll = 0;
  unsigned long _lastBatteryRead = 0;
  static const unsigned long BATTERY_READ_MS = 5000;
  uint8_t _batteryPercent = 0;
  byte _curRot = 1, _newRot = 1, _rotStableCount = 0;

  // ── Theme ───────────────────────────────────────────────────────────
  uint8_t _themeId = 0;
  void applyTheme(uint8_t id);
  void cycleTheme();

  // ── Deep sleep (5 s touch pad hold = power off) ───────────────────────────────────
  void enterPowerOff();
  void drawPowerOffScreen(int countdownSeconds, const char* statusMsg);
  void drawResumeScreen(int phase);
  void checkTouchPowerOff();

  // ── Hold-to-return (5-second capacitive pad hold) ───────────────────
  unsigned long _touchHoldStart = 0;
  bool _lockHoldActive = false;
  void checkHoldToReturn();

  // ── Scroll repeat ───────────────────────────────────────────────────
  unsigned long _lastScrollTime = 0;
  bool _scrollHeld = false;
  BtnEvent _scrollDir = BtnEvent::IDLE;
  static const unsigned long SCROLL_REPEAT_MS = 150;

  // ── Transitions ─────────────────────────────────────────────────────
  void transitionTo(Screen s);

  // ── Screen draws ────────────────────────────────────────────────────
  void drawPlaylistScreen();
  void drawListRow(int row);
  void redrawList();

  void drawNowPlayingScreen();
  void updateNowPlayingScreen();

  void drawHotspotInfoScreen();
  void updateHotspotInfoScreen();

  void drawSearchScreen();
  void drawSearchKeyboard();
  void drawSearchResults();
  void handleSearchTouch(int tx, int ty);
  void handleSearchButtons();
  void updateSearchResults();
  void handleSearchKeyPress(char c);
  void handleSearchBackspace();

  void drawUsbInfoScreen();
  void updateUsbInfoScreen();

  void drawFirstBootPinScreen();

  void drawSettingsScreen();
  void drawAboutScreen();

  void drawHotspotChangeScreen();
  void drawHotspotInputScreen();
  void commitHotspotInput();

  void drawClockScreen();
  void updateClockScreen();

  void drawSetTimeScreen();
  void drawTimeDigits();

  void drawModeMenu();
  void closeModeMenu();

  void drawStatusBar();
  void drawBatteryIcon(int x, int y, uint8_t pct);
  void drawPinDot(int idx, bool filled);
  void drawNumpadBtn(int idx, bool flash);
  void drawFirstBootPinDot(int idx, bool filled);

  // ── Input handlers ──────────────────────────────────────────────────
  void handlePlaylistTouch(int tx, int ty);
  void handlePlaylistButtons();

  void handleNowPlayingTouch(int tx, int ty);
  void handleNowPlayingButtons();

  void handleHotspotInfoTouch(int tx, int ty);
  void handleUsbInfoTouch(int tx, int ty);

  void handleFirstBootPinTouch(int tx, int ty);
  void handleFirstBootPinButtons();
  void submitFirstBootPin();

  void handleSettingsTouch(int tx, int ty);
  void handleSettingsButtons();

  void handleHotspotChangeTouch(int tx, int ty);
  void handleHotspotInputTouch(int tx, int ty);
  void handleHotspotInputButtons();

  void handleClockTouch(int tx, int ty);
  void handleClockButtons();

  void handleAboutTouch(int tx, int ty);

  void handleSetTimeTouch(int tx, int ty);

  void handleModeMenuTouch(int tx, int ty);

  // Deferred mode switch state machine
  void processModeSwitch();

        // and returns to PLAYLIST.

    
  // ── Error feedback ──────────────────────────────────────────────────
  void showLockError(const char* msg);
  void clearLockError();
  unsigned long _lockErrorTime = 0;
  bool _shaking = false;
  unsigned long _shakeStart = 0;
  void triggerShake();
  void drawPinDotsShaking();

  // ── Color blending helpers ──────────────────────────────────────────
  uint16_t lerp565(uint16_t c1, uint16_t c2, float t);
  void drawBreathingHalo(int cx, int cy, int maxR, uint16_t color);
  void drawStatusBarPadlock(int cx, int cy, bool closed);
  void drawPadlockGlyph(int x, int y, int size, uint16_t color);

  // ── Set Time ────────────────────────────────────────────────────────
  char _timeBuf[11] = {0};
  int _timeLen = 0;
};
